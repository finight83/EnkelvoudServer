#pragma once
// ============================================================================
//  EVDAudio.h - I2S in/out, resampler, quiet filters, Opus, audio tasks
// ============================================================================
#include "EVDHelpers.h"
#include "EVDUsb.h"

// [avg-encode-us-single-tu]
// gAvgEncodeUs is defined in EVDState.h (visible through EVDHelpers.h).
// This file only WRITES it — audioProcessingTask() updates the EMA after
// each opus_encode() call.

// ---- Resampler -------------------------------------------------------------
// Catmull-Rom cubic Hermite (a = -0.5). Uses the two samples straddling the
// interpolation point plus one sample on each side, so four taps total per
// output sample per channel.
//
// [ratio-1-fast-path] The cubic basis at t == 0 reduces to identity
// (am=a1=a2=0, a0=1) — the output IS p0. So when the ratio is exactly 1.0
// the entire per-sample polynomial (and the double-precision position math,
// which is soft-float on the S3 because it only has a single-precision FPU)
// is wasted work. The `process()` method below short-circuits that case with
// a plain memcpy, which is exactly what makes the normal steady state (I2S
// clock ≈ SOURCE_SAMPLE_RATE) cheap again. The cubic math is only paid when
// the input clock is genuinely off, which is when the filter actually matters.
class ResamplerToOpusRate {
public:
  void reset() {
    _pos = 0.0;
    _haveHistory = false;
    _prevL = _prevR = _lastL = _lastR = 0;
  }

  void setRatio(double r) {
    // [ratio-clamp-widened-for-24k]
    // Two-stage pipeline shares this class:
    //   Stage 1 (resampler):            ratio = measured_src_hz / SOURCE_SAMPLE_RATE
    //                                   Bounded to 0.625..1.083 by the 30–52 kHz
    //                                   sanity gate in updateInputRateEstimate();
    //                                   the clamp below is only a hard safety net.
    //   Stage 2 (resamplerContentToOpus): ratio = SOURCE_SAMPLE_RATE / g_opusSampleRate
    //                                   = 1.0  (48 kHz opus)
    //                                   = 2.0  (24 kHz opus)  ← needs headroom.
    // Previous 1.20 ceiling silently clamped 2.0 → 1.20, feeding 40 kHz-equivalent
    // samples into a 24 kHz encoder and overrunning pendingBuf in ~128 ms.
    // Widened to 2.10 (≈ 22.8 kHz floor, ~8 % margin below 24 kHz) so the
    // 24 kHz path passes through unmodified while stage-1's own sanity gate
    // still prevents it from ever asking for anything outside 0.625..1.083.
    if (r < 0.40) r = 0.40;   // ~19.2 kHz source floor
    if (r > 2.10) r = 2.10;   // covers 48k→24k (2.0) with margin
    _ratio = r;
  }
  double ratio() const        { return _ratio; }
  // [opus-sample-rate] Diagnostic only — the actual ratio is set via
  // setRatio(). Uses the runtime global so it always reports the current
  // encoder-rate relationship.
  double nominalRatio() const { return (double)SOURCE_SAMPLE_RATE / (double)g_opusSampleRate; }

  uint32_t process(const int16_t *in, uint32_t inFrames,
                   int16_t *out, uint32_t outCapacityFrames) {
    const double ratio = _ratio;
    uint32_t outCount = 0;

    if (inFrames == 0) return 0;

    // On the very first call, prime both history slots from in[0] so the
    // cubic taps at the very start of the stream do not extrapolate from
    // a zeroed previous buffer.
    if (!_haveHistory) {
      _prevL = _lastL = in[0];
      _prevR = _lastR = in[1];
      _haveHistory = true;
    }

    // Copy only for exact identity at an exact sample boundary. A fractional
    // carried position must still be interpolated even when the ratio returns
    // to 1.0, or re-anchoring here would discard timeline phase.
    if (ratio == 1.0 &&
        _pos == 0.0 &&
        inFrames <= outCapacityFrames) {
      memcpy(out, in, inFrames * 2 * sizeof(int16_t));
      _pos = 0.0;   // explicit re-anchor; the guard above accepted |_pos|<1
      if (inFrames >= 2) {
        _prevL = in[(inFrames - 2) * 2 + 0];
        _prevR = in[(inFrames - 2) * 2 + 1];
      } else {
        _prevL = _lastL; _prevR = _lastR;
      }
      _lastL = in[(inFrames - 1) * 2 + 0];
      _lastR = in[(inFrames - 1) * 2 + 1];
      // _pos already 0.0; nothing to advance.
      return inFrames;
    }

    while (outCount < outCapacityFrames) {
      double srcIndexF = _pos;
      long idx = (long)floor(srcIndexF);
      if (idx >= (long)inFrames - 1) break;

      double frac = srcIndexF - (double)idx;

      int16_t pmL, pmR, p0L, p0R, p1L, p1R, p2L, p2R;
      bool    haveP2 = false;

      if (idx < 0) {
        pmL = _prevL; pmR = _prevR;
        p0L = _lastL; p0R = _lastR;
        p1L = in[0];  p1R = in[1];
        if (inFrames >= 2) { p2L = in[2]; p2R = in[3]; haveP2 = true; }
        else               { p2L = p1L;  p2R = p1R; }
      } else {
        pmL = (idx == 0) ? _lastL : in[(idx - 1) * 2 + 0];
        pmR = (idx == 0) ? _lastR : in[(idx - 1) * 2 + 1];
        p0L = in[ idx      * 2 + 0];
        p0R = in[ idx      * 2 + 1];
        p1L = in[(idx + 1) * 2 + 0];
        p1R = in[(idx + 1) * 2 + 1];
        if ((uint32_t)(idx + 2) < inFrames) {
          p2L = in[(idx + 2) * 2 + 0];
          p2R = in[(idx + 2) * 2 + 1];
          haveP2 = true;
        } else {
          p2L = p1L; p2R = p1R;
        }
      }

      float t  = (float)frac;
      float t2 = t * t;

      float outLf, outRf;
      if (haveP2) {
        // Full Catmull-Rom cubic (4 taps, all data available)
        float t3 = t2 * t;
        float am = -0.5f * t3 +        t2 - 0.5f * t;
        float a0 =  1.5f * t3 - 2.5f * t2              + 1.0f;
        float a1 = -1.5f * t3 +  2.0f * t2 + 0.5f * t;
        float a2 =  0.5f * t3 -  0.5f * t2;
        outLf = am * (float)pmL + a0 * (float)p0L + a1 * (float)p1L + a2 * (float)p2L;
        outRf = am * (float)pmR + a0 * (float)p0R + a1 * (float)p1R + a2 * (float)p2R;
      } else {
        // [quadratic-tail] Last 1-2 samples of this chunk: the true p2 lives
        // in the NEXT chunk which has not arrived yet. Use a 3-point quadratic
        // through (pm, p0, p1) instead of clamping p2 = p1 in the cubic:
        //   q(t) = p0 + t*(p1-pm)/2 + t^2*(p1-2p0+pm)/2
        // It matches the cubic exactly at t=0, t=1, and at t=0's slope, so
        // there is no discontinuity at the chunk boundary. Overshoot is
        // slightly larger than the cubic case; the int16 clamp below bounds it.
        float qa1L = ((float)p1L - (float)pmL) * 0.5f;
        float qa2L = ((float)p1L - 2.0f*(float)p0L + (float)pmL) * 0.5f;
        float qa1R = ((float)p1R - (float)pmR) * 0.5f;
        float qa2R = ((float)p1R - 2.0f*(float)p0R + (float)pmR) * 0.5f;
        outLf = (float)p0L + qa1L * t + qa2L * t2;
        outRf = (float)p0R + qa1R * t + qa2R * t2;
      }

      int32_t outLi = (int32_t)(outLf + (outLf >= 0.0f ? 0.5f : -0.5f));
      int32_t outRi = (int32_t)(outRf + (outRf >= 0.0f ? 0.5f : -0.5f));
      if (outLi >  32767) outLi =  32767;
      if (outLi < -32768) outLi = -32768;
      if (outRi >  32767) outRi =  32767;
      if (outRi < -32768) outRi = -32768;

      out[outCount * 2 + 0] = (int16_t)outLi;
      out[outCount * 2 + 1] = (int16_t)outRi;
      outCount++;
      _pos += ratio;
    }

    // Carry the last two frames into the next call so the cubic taps at
    // the start of the next chunk have real data behind them.
    if (inFrames >= 2) {
      _prevL = in[(inFrames - 2) * 2 + 0];
      _prevR = in[(inFrames - 2) * 2 + 1];
    } else {
      _prevL = _lastL; _prevR = _lastR;
    }
    _lastL = in[(inFrames - 1) * 2 + 0];
    _lastR = in[(inFrames - 1) * 2 + 1];

    _pos -= (double)inFrames;
    return outCount;
  }

private:
  double _pos = 0.0;
  // Initial value only. audioProcessingTask() calls setRatio() at task start
  // with SOURCE_SAMPLE_RATE / g_opusSampleRate, so this is never used at
  // runtime after the first chunk.
  double _ratio = (double)SOURCE_SAMPLE_RATE / (double)OPUS_SAMPLE_RATE_DEFAULT;
  bool   _haveHistory = false;
  int16_t _lastL = 0, _lastR = 0;
  int16_t _prevL = 0, _prevR = 0;
};

inline ResamplerToOpusRate resampler;

// [two-stage-resample-compiled-out]
// Stage 2 (source rate -> Opus rate) is only needed when the two rates
// differ. In the default configuration SOURCE_SAMPLE_RATE == OPUS_SAMPLE_RATE
// (both 48000), so stage 2 would run cubic interpolation at ratio 1.0 —
// mathematically identity, but it still pays the full soft-float double-
// precision cost per sample. That doubles the resampler CPU with zero
// audio benefit, and was the primary regression this patch reverts.
//
// The code path remains available for a future build that needs a real
// rate conversion (e.g. 44.1 kHz source -> 48 kHz Opus). To re-enable it,
// change SOURCE_SAMPLE_RATE to 44100; the preprocessor will select the
// two-stage branch automatically. See also the [ratio-1-fast-path]
// comment on ResamplerToOpusRate::process above — that fast path also
// covers the "stage 1 lands at ratio 1.0" case when the input is properly
// clocked.
// [opus-sample-rate] Stage 2 must always exist now: the runtime opus rate
// may differ from SOURCE_SAMPLE_RATE at any time. When rates match, the
// [ratio-1-fast-path] in process() makes it a memcpy.
inline ResamplerToOpusRate resamplerContentToOpus;

#define RATE_MEASURE_WINDOW_MS 3000
#define RATE_SMOOTH_ALPHA      0.35      // was 0.25; faster convergence

// Counter incremented by audioReadTask() whenever a raw chunk is dropped
// because rawQueue was full. updateInputRateEstimate() folds this back into
// the measured frame rate so a burst of dropped chunks does not bias the
// window low (which was the original reason the write cadence was set to
// 30 s and the per-write clamp to 0.3 %).
static volatile uint32_t gRateFramesLostTotal = 0;

static uint32_t rateMeasureFrames  = 0;
static uint32_t rateMeasureStartMs = 0;
static double   smoothedInputRate  = 0.0;
static uint32_t rateFramesLostSeen = 0;

// ---- Pending (post-resample) buffer ---------------------------------------
// Was EXT_RAM_BSS_ATTR (PSRAM). 16 KB of internal DRAM is a trivial
// cost on the S3, and Opus's per-frame access pattern into this buffer
// (FFT twiddle reads, predictor coefficient writes) hit PSRAM cache
// misses that showed up as 7-8 ms of preemption-inclusive encode time
// where 2-3 ms is the expected floor.
//
// NOTE: this move is a NET CPU WIN — PSRAM cache misses stall the CPU
// pipeline but do not show up in the FreeRTOS runtime stats, so moving
// the buffer to DRAM *increases* the reported CPU% while *decreasing*
// the actual wall-clock encode time. The resampler fast path above is
// what actually reduces the CPU% figure; this buffer's placement is
// orthogonal and correct as-is.
int16_t  pendingBuf[PENDING_MAX_FRAMES * 2];
uint32_t pendingCount = 0;
uint32_t pendingOldestCapturedMs = 0;


// [rate-estimator-reset-on-resume]
// Set by audioProcessingTask() when it leaves otaPauseAudio. Consumed at
// the top of updateInputRateEstimate(). Without this the first post-pause
// window measures elapsed = <entire pause duration>, produces a rawRate
// far below the 30 kHz sanity band, and stores garbage in candidateRate /
// byteFastCandidate for two subsequent windows. Self-healing, but noisy;
// clearing it here makes resume determinstically silent.
static volatile bool g_rateEstimatorNeedsReset = false;


// [ratio-snap-only-to-standard-rates]
// A real BT codec switch lands within ~1 % of a standard audio rate
// (32 kHz, 44.1 kHz, 48 kHz, etc.). A single bad 3-second window (WiFi
// preemption, BT codec renegotiation transient) lands anywhere.
// updateInputRateEstimate() snaps the Stage-1 ratio in one step when
// |errFrac| > 3 %, which is correct for a real switch but causes a brief
// pitch jump on a transient. Gate the snap on the target being near a
// standard rate; use gradual stepping otherwise.
inline bool isNearStandardAudioRate(double rate) {
    static const double kStandard[] = {
        8000.0, 16000.0, 22050.0, 24000.0, 32000.0, 44100.0, 48000.0,
        88200.0, 96000.0
    };
    for (double c : kStandard) {
        if (fabs(rate - c) / c < 0.01) return true;
    }
    return false;
}

inline void updateInputRateEstimate(uint32_t framesJustArrived, uint32_t nowMs) {
    if (rateMeasureStartMs == 0) {
        rateMeasureStartMs = nowMs;
        rateMeasureFrames  = 0;
    }
    rateMeasureFrames += framesJustArrived;

    uint32_t elapsed = nowMs - rateMeasureStartMs;
    if (elapsed < RATE_MEASURE_WINDOW_MS) return;

if (g_rateEstimatorNeedsReset) {
  g_rateEstimatorNeedsReset = false;
  rateMeasureStartMs = 0;
  rateMeasureFrames  = 0;
  rateFramesLostSeen = gRateFramesLostTotal;
  // Leave smoothedInputRate alone. Its reuse after pause is exactly what
  // we want: the source clock did not change during the pause, so the
  // pre-pause smoothed value is still a valid starting point. Resetting
  // it to 0 would force a re-seed from a possibly-transient first window.
}

    // [lostInWin-compute]
    // MUST run BEFORE rawRate: audioReadTask() accumulates dropped-chunk
    // frames into gRateFramesLostTotal so a rawQueue-full burst does not
    // bias the window low. Order matters — the delta is snapshot here,
    // then folded into the measured frame count below.
    uint32_t lostNow   = gRateFramesLostTotal;
    uint32_t lostInWin = lostNow - rateFramesLostSeen;
    rateFramesLostSeen = lostNow;

    double rawRate = (double)(rateMeasureFrames + lostInWin) * 1000.0 / (double)elapsed;
    // The USB host clocks the stream at exactly the nominal rate; a reading
    // far from it means our own reads stalled, not that the source changed.
    // Trusting it made the resampler warp pitch and hunt (audible jitter).
    if (serverInputSource == SRC_FROM_USB &&
        fabs(rawRate - (double)SOURCE_SAMPLE_RATE) > (double)SOURCE_SAMPLE_RATE * 0.005) {
        rawRate = (double)SOURCE_SAMPLE_RATE;
    }
    rateMeasureStartMs = nowMs;
    rateMeasureFrames  = 0;

    // [rate-debounce-state-hoisted]
    // Hoisted above the byte-counter block so the fast-commit path below
    // can reset the debounce state. Previously these were statics local
    // to the debounce block; the byte counter could not touch them.
    static double   candidateRate = 0.0;
    static uint8_t  candidateHits = 0;
    const uint8_t   RATE_DEBOUNCE_HITS      = 2;
    const double    RATE_DEBOUNCE_TOLERANCE = 0.04;

    bool rawRateValid = (rawRate >= 30000.0 && rawRate <= 52000.0);

    // ---- Byte counter cross-check + fast-commit -----------------------
    // [rate-change-fast-commit]
    // The 2-hit debounce below was added to reject garbage during BT
    // codec renegotiation / PLL relock. But it only gates updates to
    // smoothedInputRate; it does NOT stop the close branch from pulling
    // smoothed TOWARD transitional garbage that happens to fall within
    // 4% of the current smoothed value.
    //
    // [chipmunk-fix] The previous version fired the fast-commit on a
    // SINGLE 1-second byteRate sample that disagreed with smoothed by
    // >= 8 %. A transient I2S read stall on Core 1 (AsyncTCP preemption,
    // source-board DMA hiccup, chunk queue purge during buffering) makes
    // the 1-second byte counter read low (e.g. 36 145 Hz) while the
    // 3-second rawRate estimator — which folds gRateFramesLostTotal
    // back in — stays at 48 kHz. The single-sample fast-commit snapped
    // smoothedInputRate to 36 145 Hz, which then snapped the resampler
    // ratio to 0.753 (36 145/48 000 = −25 %), producing a 33 % pitch-up
    // for the entire 3 s window. That is the audible chipmunk every
    // node on the LAN hears simultaneously.
    //
    // This version makes two changes:
    //   1. It NO LONGER overwrites rawRate with byteRate when they
    //      disagree. rawRate accounts for dropped chunks (via
    //      gRateFramesLostTotal) and is immune to the transient byte-
    //      counter dip. Byte-rate disagreements are logged only.
    //   2. The fast-commit requires TWO CONSECUTIVE byteRate samples
    //      to agree on the same new rate before committing. A real
    //      BT codec switch sustains the new rate across many windows
    //      (so this costs at most one extra 1 s window of adaptation),
    //      while a single-window transient dip never fires it at all.
    //
    // Real switches (44.1k -> 48k = 8.84 %, 48k -> 44.1k = 8.13 %,
    // 32k -> 48k = 50 %) are all still well above the threshold; the
    // only behaviour change is that a single glitched window can no
    // longer drag smoothedInputRate down to a wrong value.
    {
        static uint32_t lastByteLow    = 0;
        static uint32_t lastByteMs     = 0;
        static double   byteFastCandidate = 0.0;
        static uint8_t  byteFastHits      = 0;
        const uint8_t   BYTE_FAST_HITS      = 2;
        const double    BYTE_FAST_MATCH_TOL = 0.02;

        uint32_t byteLow = (uint32_t)statI2sBytesTotal;
        uint32_t nowSnap = millis();
        if (lastByteMs == 0) {
            lastByteLow = byteLow;
            lastByteMs  = nowSnap;
        } else if ((uint32_t)(nowSnap - lastByteMs) >= 1000) {
            uint32_t dBytes = byteLow - lastByteLow;
            uint32_t dMs    = nowSnap - lastByteMs;
            if (dMs < 60000) {
                double byteRate = ((double)dBytes / 4.0) * 1000.0 / (double)dMs;
                if (byteRate > 30000.0 && byteRate < 52000.0) {
                    // [byte-counter-rescues-invalid-rawRate]
                    // If rawRate was rejected by the band check above,
                    // trust the byte counter — but only after the 2-hit
                    // confirmation below, so a single bad window cannot
                    // promote an invalid rawRate to valid.
                    if (!rawRateValid) {
                        // Confirmation handled by the fast-commit block
                        // below; we only seed rawRate from byteRate when
                        // byteRate is confirmed.
                    } else {
                        // [rate-mismatch-log-only] Log the disagreement
                        // but do NOT overwrite rawRate. Overwriting
                        // rawRate with a glitched byteRate was the exact
                        // cause of the chipmunk window described above.
                        double diff = fabs(byteRate - rawRate) / rawRate;
                        if (diff > 0.03) {
                            static uint32_t lastMismatchLogMs = 0;
                            if (millis() - lastMismatchLogMs > 5000) {
                                lastMismatchLogMs = millis();
                                LOGW("Input-rate mismatch: byte=%.0f Hz, "
                                     "chunk-ts=%.0f Hz (%.1f%%); trusting chunk-ts",
                                     byteRate, rawRate, diff * 100.0);
                            }
                        }
                    }

                    // [rate-change-fast-commit-confirmed]
                    // Fast-commit only when TWO consecutive windows
                    // agree on the same new byteRate. A transient dip
                    // lasting one window never advances byteFastHits
                    // past 1, so no commit fires.
                    if (smoothedInputRate > 0.0 &&
                        fabs(byteRate - smoothedInputRate) / smoothedInputRate >=
                            2.0 * RATE_DEBOUNCE_TOLERANCE) {
                        if (byteFastCandidate > 0.0 &&
                            fabs(byteRate - byteFastCandidate) / byteFastCandidate
                                <= BYTE_FAST_MATCH_TOL) {
                            byteFastHits++;
                            byteFastCandidate =
                                (byteFastCandidate * (byteFastHits - 1) + byteRate)
                                / byteFastHits;
                        } else {
                            byteFastCandidate = byteRate;
                            byteFastHits      = 1;
                        }
                        if (byteFastHits >= BYTE_FAST_HITS) {
                            if (!rawRateValid) {
                                rawRate      = byteFastCandidate;
                                rawRateValid = true;
                            }
                            LOGE("Input rate fast-commit: %.0f -> %.0f Hz "
                                 "(byte counter confirmed over %u windows)",
                                 smoothedInputRate, byteFastCandidate,
                                 (unsigned)byteFastHits);
                            smoothedInputRate = byteFastCandidate;
                            candidateRate     = byteFastCandidate;
                            candidateHits     = 0;
                            byteFastCandidate = 0.0;
                            byteFastHits      = 0;
                        }
                    } else {
                        // Within tolerance — reset the confirmation ladder.
                        byteFastCandidate = 0.0;
                        byteFastHits      = 0;
                    }
                } else {
                    // Out-of-band byteRate — reset the ladder.
                    byteFastCandidate = 0.0;
                    byteFastHits      = 0;
                }
            }
            lastByteLow = byteLow;
            lastByteMs  = nowSnap;
        }
    }

    // [rate-estimator-debounce]
    // [invalid-rate-never-commits]
    // A rawRate outside the 30-52 kHz sanity band is almost always a
    // source-starvation artifact (BT pause, A2DP renegotiation, USB
    // disconnect) — the window counter measured low because the source
    // genuinely stopped providing frames during part of the window, NOT
    // because the source rate changed. Committing it forces the resampler
    // to a wrong ratio, floods pendingBuf, and corrupts every downstream
    // captured_ms timestamp, producing ~20-40 s of stutter on every node
    // before the next valid window can rescue the estimator.
    //
    // Only the byte-counter fast-commit is allowed to override an invalid
    // rawRate (via the rawRate = byteFastCandidate assignment at the end
    // of that block). If even the byte counter cannot confirm a valid
    // rate, leave smoothedInputRate at its last good value and try again
    // next window. Recovery is bounded by the pause duration + 1 window
    // (~3 s), not by the 30-40 s stutter an unwanted commit would cause.
    if (!rawRateValid) {
        candidateRate = 0.0;
        candidateHits = 0;
    } else if (smoothedInputRate == 0.0) {
        smoothedInputRate = rawRate;
        candidateRate     = rawRate;
        candidateHits     = 1;
    } else {
        double relDiff = fabs(rawRate - smoothedInputRate) / smoothedInputRate;
        if (relDiff <= RATE_DEBOUNCE_TOLERANCE) {
            smoothedInputRate += RATE_SMOOTH_ALPHA * (rawRate - smoothedInputRate);
            candidateRate     = smoothedInputRate;
            candidateHits     = 0;
        } else {
            if (candidateRate > 0.0 &&
                fabs(rawRate - candidateRate) / candidateRate <= RATE_DEBOUNCE_TOLERANCE) {
                candidateHits++;
                candidateRate = (candidateRate * (candidateHits - 1) + rawRate) / candidateHits;
            } else {
                candidateRate = rawRate;
                candidateHits = 1;
            }
            if (candidateHits >= RATE_DEBOUNCE_HITS) {
                LOGE("Input rate confirmed change: %.0f -> %.0f Hz "
                     "(after %u windows)",
                     smoothedInputRate, candidateRate, (unsigned)candidateHits);
                smoothedInputRate = candidateRate;
                candidateHits     = 0;
            }
        }
    }

    // [stat-diagnostics] Restored from the original function. These must
    // live BETWEEN the debounce block and the broadcast block: the values
    // are the post-debounce smoothed rate, so they must not be reported
    // until debounce has had its say.
    statMeasuredInputHz = (float)smoothedInputRate;
    // [drift-ref-matches-source]
    // Reference the drift to the actual expected source rate. SOURCE_SAMPLE_RATE
    // is 48000 (EVDConfig.h). The old hardcoded 44100.0 reported +88435 ppm for
    // a *perfect* 48 kHz source, which made the dashboard's "Clock drift" pill
    // meaningless. Use the macro so drift stays correct if SOURCE_SAMPLE_RATE
    // is ever changed.
    const double DRIFT_REF_HZ = (double)SOURCE_SAMPLE_RATE;
    statDriftPpm = (int32_t)((smoothedInputRate - DRIFT_REF_HZ)
                             / DRIFT_REF_HZ * 1000000.0);

    // [rate-change-broadcast-flag]
    // Only broadcast when we have a valid measurement. smoothedInputRate
    // can be 0 at boot (or for the duration of a pause that started
    // before the first valid window), and broadcasting 0 would make the
    // nodes reject the source rate (their sanity gate is 8k-96k) and
    // leave g_sourceSampleRate = 0 here, which then causes div-by-zero
    // in audioProcessingTask's chunkDurMs calculation.
    if (smoothedInputRate > 0.0) {
        uint32_t newSourceHz = (uint32_t)(smoothedInputRate + 0.5);
        {
            static uint32_t lastFlaggedHz = 0;
            if (lastFlaggedHz == 0 ||
                fabs((double)newSourceHz - lastFlaggedHz) / (double)lastFlaggedHz > 0.03) {
                lastFlaggedHz = newSourceHz;
                g_rateChangeBroadcastHz = newSourceHz;
            }
        }
        g_sourceSampleRate = newSourceHz;
    }

    // [ratio-fast-track-within-10pct]
    // Track the ratio off rawRate whenever rawRate is within 10 % of
    // the smoothed value (that's a genuine source change: 44.1 <-> 48
    // is 8.4 %). Outside 10 %, wait for debounce confirmation - those
    // readings are PLL relock transients, not real rate changes.
    // [ratio-track-any-valid-rate]
    // Was: only track the ratio against rawRate if it was within 10 %
    // of smoothed. That left 48→32 kHz transitions (>25 % change)
    // trusting the STALE smoothed value for 3-6 s, draining pendingBuf
    // at 16 kHz and stuttering the client every time the BT codec
    // renegotiated to SBC fallback. 44.1↔48 kHz (8 %) worked; larger
    // changes did not.
    //
    // Now: if rawRate is a valid in-band measurement (30-52 kHz, already
    // sanity-gated above via rawRateValid), drive the ratio from it
    // immediately. The debounce below still smooths smoothedInputRate
    // for broadcast/reporting, but the resampler response is instant.
    // [ratio-source-boot-fallback]
    // If smoothedInputRate is still 0 (nothing valid received yet) AND
    // rawRate is invalid (boot happened during a BT pause), fall back to
    // the nominal SOURCE_SAMPLE_RATE for the ratio. Without this, refRate
    // becomes rawRate (invalid), ratioSource becomes rawRate, and the
    // resampler runs at the wrong ratio (0.58) for the first 3 seconds
    // of the boot, flooding pendingBuf. If rawRate is valid, use it
    // (faster cold-start convergence).
    double refRate;
    if (smoothedInputRate > 0.0) {
        refRate = smoothedInputRate;
    } else if (rawRateValid) {
        refRate = rawRate;
    } else {
        refRate = (double)SOURCE_SAMPLE_RATE;
    }
    double ratioSource = rawRateValid ? rawRate : refRate;
    double targetRatio  = ratioSource / (double)SOURCE_SAMPLE_RATE;

    double currentRatio = resampler.ratio();

    static uint32_t lastRatioWriteMs = 0;
    if (nowMs - lastRatioWriteMs < 1000) return;

    // [ratio-deadband-tightened]
    // Was 500 ppm. That is 10× the noise floor of the 3 s measurement
    // window, and it meant realistic 20-50 ppm crystal drift never
    // updated the ratio at all. Over hours, pendingBuf slowly grew,
    // captured_ms lagged wall clock, and a client tracking captured_ms
    // saw its buffer creep upward (audible as increasing latency, then
    // periodic head-drops). 20 ppm is above noise but tight enough to
    // catch real drift before it accumulates.
    double errFrac = fabs(currentRatio - targetRatio) / targetRatio;
    // [rate-estimator-hysteresis] 20 ppm was too tight. Source jitter
    // caused the ratio to constantly hunt. 50 ppm gives the PI controller
    // on pendingBuf time to absorb long-term drift without changing pitch.
    if (errFrac < 0.000050) return;   // 50 ppm
    lastRatioWriteMs = nowMs;

    // [ratio-no-destructive-flush]
    // The old code called pendingCount = 0, reset pendingOldestCapturedMs,
    // and trimmed opusQueue on every ratio JUMP. That is what made the
    // receiving node rebuffer on every source change - the captured_ms
    // baseline on the next Opus frame jumped forward by however much
    // pending was thrown away. Now the ratio change is the ONLY side
    // effect. pendingBuf and opusQueue are left intact; the resampler
    // absorbs the transition over a few frames (a pitch glide of a few
    // cents) with no dropped packets and no timeline discontinuity.
    const bool targetIsStandard = isNearStandardAudioRate(ratioSource);
    if (errFrac > 0.03 && targetIsStandard) {
        resampler.setRatio(targetRatio);
        LOGI("Stage-1 ratio snap: %.6f -> %.6f (err %.1f%%, target %.0f Hz "
             "standard, no flush)",
             currentRatio, targetRatio, errFrac * 100.0, ratioSource);
    } else {
        double stepFrac = (errFrac > 0.01) ? 0.30 : 0.10;
        targetRatio = currentRatio + (targetRatio - currentRatio) * stepFrac;
        resampler.setRatio(targetRatio);
        LOGI("Input clock %.1f Hz; stage1 ratio -> %.6f (smoothed %.1f, "
             "target %.0f %s, lost %u)",
             smoothedInputRate, targetRatio, smoothedInputRate, ratioSource,
             targetIsStandard ? "std" : "non-std", (unsigned)lostInWin);
    }
}

// ---- Quiet mode: band-pass (400 Hz - 9.5 kHz) + peak limiter --------------
struct FcBiquadState {
  float b0, b1, b2, a1, a2;
  float z1, z2;
  void reset() { b0 = b1 = b2 = a1 = a2 = 0; z1 = z2 = 0; }
  void setLowpass(float fc, float fs) {
    float w0 = 6.28318530718f * fc / fs;
    float cw = cosf(w0), sw = sinf(w0), al = sw / 1.41421356237f;
    float a0 = 1.0f + al;
    b0 = (1.0f - cw) / (2.0f * a0);
    b1 = (1.0f - cw) / a0;
    b2 = b0;
    a1 = (-2.0f * cw) / a0;
    a2 = (1.0f - al) / a0;
  }
  void setHighpass(float fc, float fs) {
    float w0 = 6.28318530718f * fc / fs;
    float cw = cosf(w0), sw = sinf(w0), al = sw / 1.41421356237f;
    float a0 = 1.0f + al;
    b0 = (1.0f + cw) / (2.0f * a0);
    b1 = -(1.0f + cw) / a0;
    b2 = b0;
    a1 = (-2.0f * cw) / a0;
    a2 = (1.0f - al) / a0;
  }
  float process(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

static FcBiquadState quietHpL, quietHpR, quietLpL, quietLpR;
static bool          quietFilterPrimed = false;

static uint32_t gAudioFrameSeq = 0;

struct QuietDynamics {
  float envelope = 0.0f;
  float gain     = 1.0f;
};
static QuietDynamics quietDyn;

inline void resetQuietDynamics() {
  quietDyn.envelope = 0.0f;
  quietDyn.gain     = 1.0f;
}

inline void configureQuietFilters() {
  // [opus-sample-rate] Use the current runtime rate so filter coefficients
  // are correct at both 48 kHz and 24 kHz. At 24 kHz the 9.5 kHz low-pass
  // sits below Nyquist (12 kHz) so the band-pass still works.
  float fs = (float)g_opusSampleRate;
  quietHpL.setHighpass(400.0f, fs);
  quietHpR.setHighpass(400.0f, fs);
  quietLpL.setLowpass (9500.0f, fs);
  quietLpR.setLowpass (9500.0f, fs);
  quietFilterPrimed = false;
  resetQuietDynamics();
}

inline void applyQuietFilter(int16_t *buf, uint32_t frames) {
  if (!quietFilterPrimed) {
    quietHpL.z1 = quietHpL.z2 = quietHpR.z1 = quietHpR.z2 = 0;
    quietLpL.z1 = quietLpL.z2 = quietLpR.z1 = quietLpR.z2 = 0;
    quietFilterPrimed = true;
  }
  for (uint32_t i = 0; i < frames; ++i) {
    float l = quietLpL.process(quietHpL.process((float)buf[i * 2]));
    float r = quietLpR.process(quietHpR.process((float)buf[i * 2 + 1]));
    int32_t li = (int32_t)(l + (l >= 0.0f ? 0.5f : -0.5f));
    int32_t ri = (int32_t)(r + (r >= 0.0f ? 0.5f : -0.5f));
    if (li >  32767) li =  32767;
    if (li < -32768) li = -32768;
    if (ri >  32767) ri =  32767;
    if (ri < -32768) ri = -32768;
    buf[i * 2]     = (int16_t)li;
    buf[i * 2 + 1] = (int16_t)ri;
  }
}

// ---- Quiet mode: peak limiter (uses quietDyn from resetQuietDynamics) ------
// Applied AFTER applyQuietFilter() in audioProcessingTask(). The 4th-order
// band-pass at 400 Hz / 9.5 kHz has a few dB of pass-band ripple and its
// group delay produces transient overshoot; this 1 ms / 100 ms peak limiter
// catches the resulting peaks before the int16→Opus path and before the
// master×pot volume multiplier (which also multiplies the peak).
//
// Structure:
//   * peak envelope follower — instantaneous attack, 100 ms release
//   * target gain = min(1, thresh/env)  — 1.0 below threshold
//   * gain smooth with the same attack/release as the envelope so a
//     transient is attenuated on the first sample past threshold, not
//     slewed into. Since we have no look-ahead, a 1 ms attack against
//     a 20 ms frame is fine: the very first overshoot sample is capped
//     by the int16 clamp below, and the gain is already down to
//     thresh/env by the second sample.
//
// Threshold 0.85 FS (≈ -1.4 dBFS) leaves 1.4 dB for downstream headroom
// (the master×pot multiplier can add up to +0 dB here — both are ≤100%).
//
// State lives in `quietDyn` (EVDState.h-visible, zeroed by
// resetQuietDynamics()). Rate uses the current g_opusSampleRate so the
// same coefficients are correct at both 48 kHz and 24 kHz.
//
// Cost: ~6 float ops + 1 fmaxf + 2 fabsf per frame. No expf on the hot
// path — coefficients are hoisted out of the loop.
inline void applyQuietDynamics(int16_t *buf, uint32_t frames) {
  if (!buf || frames == 0) return;

  const float fs         = (float)g_opusSampleRate;
  const float threshLin  = 0.85f;                                // -1.4 dBFS
  // 1 ms attack: a full-scale step is tracked to 1/e in 1 ms.
  // 100 ms release: gain recovers to ~99 % in 460 ms — inaudible pumping.
  const float attackCoef  = 1.0f - expf(-1.0f / (0.001f * fs));
  const float releaseCoef = 1.0f - expf(-1.0f / (0.100f * fs));

  for (uint32_t i = 0; i < frames; ++i) {
    const float l = (float)buf[i * 2 + 0] * (1.0f / 32768.0f);
    const float r = (float)buf[i * 2 + 1] * (1.0f / 32768.0f);
    const float peak = fmaxf(fabsf(l), fabsf(r));

    // Envelope: fast attack, slow release.
    const float envCoef = (peak > quietDyn.envelope) ? attackCoef
                                                     : releaseCoef;
    quietDyn.envelope += envCoef * (peak - quietDyn.envelope);

    // Gain target: 1.0 below threshold, thresh/env above (peak limiter).
    float targetGain = 1.0f;
    if (quietDyn.envelope > threshLin) {
      targetGain = threshLin / quietDyn.envelope;
    }

    // Smooth gain with the same time constants (fast attack prevents the
    // first over-threshold sample from escaping; slow release prevents
    // audible gain pumping on musical material).
    const float gainCoef = (targetGain < quietDyn.gain) ? attackCoef
                                                         : releaseCoef;
    quietDyn.gain += gainCoef * (targetGain - quietDyn.gain);

    int32_t li = (int32_t)(l * quietDyn.gain * 32768.0f);
    int32_t ri = (int32_t)(r * quietDyn.gain * 32768.0f);
    if (li >  32767) li =  32767;
    if (li < -32768) li = -32768;
    if (ri >  32767) ri =  32767;
    if (ri < -32768) ri = -32768;
    buf[i * 2 + 0] = (int16_t)li;
    buf[i * 2 + 1] = (int16_t)ri;
  }
}

// [anti-alias-24k]
// When the Opus sample rate is 24 kHz, Stage 2 decimates 48 kHz -> 24 kHz by
// Catmull-Rom at ratio 2.0. Catmull-Rom is an interpolator, not a decimator
// with an anti-alias filter. Source content above 12 kHz in the 48 kHz stream
// folds back into 0-12 kHz as aliasing. The audible component is worst from
// source 18-22 kHz folding into 2-6 kHz (mid-range where the ear is most
// sensitive).
//
// Add a 4th-order low-pass at 10 kHz before Stage 2 when Opus is at 24 kHz.
// Bypassed when g_opusSampleRate == 48000: no extra
// latency, no extra CPU, no passband change.
static FcBiquadState aa24kLpL1, aa24kLpR1, aa24kLpL2, aa24kLpR2;
static uint32_t     aa24kLastRate = 0;   // 0 forces configure on first call

inline void serviceAntiAliasFilter() {
  if (aa24kLastRate == g_opusSampleRate) return;
  aa24kLastRate = g_opusSampleRate;
  if (g_opusSampleRate == OPUS_SAMPLE_RATE_24K) {
    aa24kLpL1.setLowpass(10000.0f, 48000.0f);
    aa24kLpR1.setLowpass(10000.0f, 48000.0f);
    aa24kLpL2.setLowpass(10000.0f, 48000.0f);
    aa24kLpR2.setLowpass(10000.0f, 48000.0f);
    aa24kLpL1.z1 = aa24kLpL1.z2 = aa24kLpR1.z1 = aa24kLpR1.z2 = 0;
    aa24kLpL2.z1 = aa24kLpL2.z2 = aa24kLpR2.z1 = aa24kLpR2.z2 = 0;
    LOGI("Anti-alias filter enabled (10 kHz) for 24 kHz Opus");
  } else {
    LOGI("Anti-alias filter disabled (Opus rate %u Hz)",
         (unsigned)g_opusSampleRate);
  }
}

inline void applyAntiAlias24k(int16_t *buf, uint32_t frames) {
  if (aa24kLastRate != OPUS_SAMPLE_RATE_24K) return;
  for (uint32_t i = 0; i < frames; ++i) {
    float l = aa24kLpL1.process((float)buf[i * 2]);
    l       = aa24kLpL2.process(l);
    float r = aa24kLpR1.process((float)buf[i * 2 + 1]);
    r       = aa24kLpR2.process(r);
    int32_t li = (int32_t)(l + (l >= 0.0f ? 0.5f : -0.5f));
    int32_t ri = (int32_t)(r + (r >= 0.0f ? 0.5f : -0.5f));
    if (li >  32767) li =  32767;
    if (li < -32768) li = -32768;
    if (ri >  32767) ri =  32767;
    if (ri < -32768) ri = -32768;
    buf[i * 2]     = (int16_t)li;
    buf[i * 2 + 1] = (int16_t)ri;
  }
}

// ---- Opus ------------------------------------------------------------------
// [opus-sample-rate] Now takes the sample rate as a parameter so it can be
// called from setup() (boot, single-threaded) and from
// handleEncoderRecreate() (audio task, serialized with opus_encode).
// Updates g_opusSampleRate / g_opusFrameSamples / clears
// g_pendingOpusSampleRate on success; leaves them untouched on failure so
// the caller can recover at the old rate.
inline void setup_opus(uint32_t sampleRate) {
  int opusApp = g_newApplication;
  if (opusApp != OPUS_APPLICATION_AUDIO &&
      opusApp != OPUS_APPLICATION_VOIP &&
      opusApp != OPUS_APPLICATION_RESTRICTED_LOWDELAY) {
    LOGW("setup_opus: g_newApplication=%d out of range, falling back to AUDIO",
         opusApp);
    opusApp = OPUS_APPLICATION_AUDIO;
    // [infinite-recreate-fix] If we leave g_newApplication at the bad
    // value, audioEncodeTask sees g_newApplication != g_currentApplication
    // (which setup_opus sets to AUDIO below) on EVERY iteration and forces
    // a recreate every 20 ms — destroying audio. Sync both sides.
    g_newApplication = OPUS_APPLICATION_AUDIO;
  }
  LOGI("Initializing Opus encoder (%u Hz, %d ch, %d bps, app=%d)...",
       sampleRate, CHANNELS, OPUS_BITRATE, opusApp);
  int err = 0;
  opusEncoder = opus_encoder_create(sampleRate, CHANNELS, opusApp, &err);
  if (err != OPUS_OK || opusEncoder == nullptr) {
    LOGE("opus_encoder_create() FAILED, error %d", err);
    return;
  }
  g_currentApplication = opusApp;

  opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(g_newBitrate));
  opus_encoder_ctl(opusEncoder, OPUS_SET_VBR(g_newVbr));
  opus_encoder_ctl(opusEncoder, OPUS_SET_VBR_CONSTRAINT(g_newVbrConstraint));
  int bootComplexity = (g_pendingComplexity >= 0)
                       ? g_pendingComplexity : currentOpusComplexity;
  opus_encoder_ctl(opusEncoder, OPUS_SET_COMPLEXITY(bootComplexity));
  currentOpusComplexity = bootComplexity;
  g_pendingComplexity   = -1;
  opus_encoder_ctl(opusEncoder, OPUS_SET_SIGNAL(g_newSignal));

  // Mirror all applied settings so the audio task's dirty-block does not
  // re-apply them on the next frame. (Previously only app and signal were
  // mirrored; that worked at boot because g_new* == g_current* by default,
  // but not after a re-create when the user had a pending change.)
  g_currentSignal        = g_newSignal;
  g_currentBitrate       = g_newBitrate;
  g_currentVbr           = g_newVbr;
  g_currentVbrConstraint = g_newVbrConstraint;

  // Commit the new sample rate to the runtime globals only after a
  // successful create.
  g_opusSampleRate        = sampleRate;
  g_opusFrameSamples      = (uint16_t)(sampleRate / (1000 / OPUS_FRAME_MS));
  g_pendingOpusSampleRate = 0;

  LOGI("Opus encoder ready (%u Hz, frame = %u samples / %d ms)",
       sampleRate, (unsigned)g_opusFrameSamples, OPUS_FRAME_MS);
}

/// [core-split] Encoder-only re-create. Runs on core 0 (audioEncodeTask).
// Never touches pendingBuf / resamplerContentToOpus / quiet filters directly:
// those belong to audioProcTask (core 1). Instead it raises
// g_rateChangeFlushPending + g_quietFiltersNeedReconfig so audioProc can
// re-derive its stage-2 ratio and filter coefficients on its own core.
inline void handleEncoderRecreate() {
  uint32_t newRate = g_pendingOpusSampleRate;
  if (newRate == 0) return;

  // [app-change-recreate-fix]
  // 0xFFFFFFFF signals an application mode change (e.g., Audio to VoIP).
  // We must recreate the encoder at the CURRENT sample rate.
  bool forceRecreate = (newRate == 0xFFFFFFFF);
  if (forceRecreate) {
    newRate = g_opusSampleRate;
  } else {
    if (newRate != OPUS_SAMPLE_RATE_48K && newRate != OPUS_SAMPLE_RATE_24K) {
      LOGW("Encoder re-create: invalid rate %u, ignoring", newRate);
      g_pendingOpusSampleRate = 0;
      return;
    }
    if (newRate == g_opusSampleRate) {
      g_pendingOpusSampleRate = 0;
      return;
    }
  }

  LOGI("Encoder re-create: %u Hz -> %u Hz", g_opusSampleRate, newRate);

  // [core-split-drain] Drop any frames queued at the OLD sample count.
  // opus_encode() rejects a frame whose sample count does not match the
  // encoder's Fs, so leaving them in the queue would spam errors until the
  // pipeline caught up. Losing 20-80 ms of audio on a rate change is
  // exactly what the old code did (it flushed pendingBuf); same effect.
  if (encodeQueue) {
    EncodeFrame drain;
    while (xQueueReceive(encodeQueue, &drain, 0) == pdTRUE) { }
  }

  uint32_t oldRate = g_opusSampleRate;

  if (opusEncoder) {
    opus_encoder_destroy(opusEncoder);
    opusEncoder = nullptr;
  }

  setup_opus(newRate);

  if (opusEncoder == nullptr) {
    LOGE("Encoder re-create FAILED at %u Hz; recovering at %u Hz",
         newRate, oldRate);
    setup_opus(oldRate);
    if (opusEncoder == nullptr) {
      LOGE("Encoder recovery FAILED - audio will be silent until reboot");
    }
    g_pendingOpusSampleRate = 0;
    return;
  }

  // g_opusSampleRate / g_opusFrameSamples are now newRate / newSamples.
  // Hand the shared-state side-effects over to core 1.
  g_rateChangeFlushPending = true;
  if (quietModeEnabled) g_quietFiltersNeedReconfig = true;
  g_opusRateChangeNeedsBroadcast = true;

  LOGI("Encoder re-created at %u Hz (frame = %u samples)",
       g_opusSampleRate, (unsigned)g_opusFrameSamples);
}

// ---- I2S in / out ----------------------------------------------------------
inline void setup_i2s_in() {
  LOGI("Configuring I2S input...");
  i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_SLAVE | I2S_MODE_RX),
      .sample_rate = SOURCE_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format  = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8, .dma_buf_len = 256,
      .use_apll = false, .tx_desc_auto_clear = false, .fixed_mclk = 0
  };
  if (i2s_driver_install(I2S_PORT_IN, &cfg, 0, NULL) != ESP_OK) {
    LOGE("i2s_driver_install() FAILED for input"); return;
  }
  i2s_pin_config_t pins = {
      .mck_io_num = I2S_PIN_NO_CHANGE,
      .bck_io_num = pinBtBclk, .ws_io_num = pinBtLrc,
      .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = pinBtDin
  };
  if (i2s_set_pin(I2S_PORT_IN, &pins) != ESP_OK) {
    LOGE("i2s_set_pin() FAILED for input"); return;
  }
  LOGI("  I2S in -> BCLK:%d LRC:%d DIN:%d", pinBtBclk, pinBtLrc, pinBtDin);
}

// ---- Audio read task -------------------------------------------------------
inline void audioReadTask(void *param) {
  (void)param;
  LOGI("audioReadTask started on core %d", xPortGetCoreID());
  alignas(4) static uint8_t readBuf[I2S_READ_CHUNK_BYTES];

  for (;;) {
    if (otaPauseAudio) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

    // [dlna-net-source] In network mode, dlnaFetchTask (EVDDLNA.h) is the
    // sole producer for rawQueue. This task must NOT touch I2S or USB —
    // both would race the fetch task's xQueueSend and corrupt the stream.
    if (serverInputSource == SRC_FROM_NETWORK) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    size_t bytesRead = 0;

    if (serverInputSource == SRC_FROM_USB) {
      bytesRead = readServerUsbAudio(readBuf, sizeof(readBuf));
      if (bytesRead == 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      portENTER_CRITICAL(&gStatsMux);
        statUsbBytesIn    += bytesRead;
        statI2sBytesTotal += bytesRead;   // keep the rate-estimator
                                          // byte-counter cross-check live
      portEXIT_CRITICAL(&gStatsMux);
    } else {
      esp_err_t res = i2s_read(I2S_PORT_IN, readBuf, sizeof(readBuf),
                               &bytesRead, portMAX_DELAY);
      if (res != ESP_OK) {
        statI2sReadErrors++;
        LOGW("i2s_read() error %d", (int)res);
        continue;
      }
      if (bytesRead == 0) continue;
      portENTER_CRITICAL(&gStatsMux);
        statI2sBytesIn    += bytesRead;
        statI2sBytesTotal += bytesRead;
      portEXIT_CRITICAL(&gStatsMux);
    }

    RawChunk chunk;
    memcpy(chunk.data, readBuf, bytesRead);
    chunk.len         = bytesRead;
    chunk.captured_ms = millis();

    // [raw-queue-block-instead-of-drop]
    // Was: drop any chunk when queue depth > 6 (32 ms of headroom).
    // That aggressive threshold made ANY transient Core-1 stall
    // (WiFi preemption, udpSendTask 16-subscriber burst, AsyncTCP
    // HTTP poll) cause a hard 5.33 ms audio gap AND inflate the rate
    // estimator via gRateFramesLostTotal — pushing the resampler ratio
    // in the wrong direction afterwards, draining pendingBuf.
    //
    // 20 ms block timeout: gives audioProc enough time to drain one
    // slot even under mild contention, while still leaving ~22 ms of
    // slack in the 42.6 ms DMA ring before overflow. If the queue is
    // still full after 20 ms, we drop and count it (rare pathological
    // case only).
    if (xQueueSend(rawQueue, &chunk, pdMS_TO_TICKS(20)) != pdTRUE) {
      statRawDropped++;
      gRateFramesLostTotal += bytesRead / (2 * sizeof(int16_t));
    }
  }
}


// ---- Audio processing task (Core 1) ---------------------------------------
// Runs pre-processing: rate estimation, stage-1/stage-2 resampling, quiet
// filters, master+pot volume. Produces a fully ready PCM frame every
// g_opusFrameSamples and hands it to audioEncodeTask via encodeQueue.
// Does NOT touch opusEncoder.
inline void audioProcessingTask(void *param) {
  (void)param;
  LOGI("audioProcessingTask started on core %d", xPortGetCoreID());
  static int16_t resampledScratch[2048];
  static int16_t contentScratch[2048];
  RawChunk chunk;

  resampler.reset();
  resamplerContentToOpus.reset();
  resamplerContentToOpus.setRatio((double)SOURCE_SAMPLE_RATE /
                                  (double)g_opusSampleRate);
  statDriftPpm = 0;

  // [rate-estimator-reset-on-resume] Set once on pause-entry so the next
  // rate window after resume re-seeds cleanly instead of measuring one
  // giant elapsed interval covering the entire pause.
  static bool wasPaused = false;

  for (;;) {
    if (otaPauseAudio) {
      if (!wasPaused) {
        wasPaused = true;
        g_rateEstimatorNeedsReset = true;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    wasPaused = false;

    // [core-split-flush] Drain rate-change side-effects here, on this core.
    // handleEncoderRecreate() set these after it re-created the encoder on
    // core 0; g_opusSampleRate has already been updated to the new value,
    // so the ratio derived below is correct for the newly configured encoder.
    if (g_rateChangeFlushPending) {
      g_rateChangeFlushPending = false;
      pendingCount            = 0;
      pendingOldestCapturedMs = 0;
      resamplerContentToOpus.reset();
      resamplerContentToOpus.setRatio((double)SOURCE_SAMPLE_RATE /
                                      (double)g_opusSampleRate);
    }
    if (g_quietFiltersNeedReconfig) {
      g_quietFiltersNeedReconfig = false;
      configureQuietFilters();
    }

    if (xQueueReceive(rawQueue, &chunk, portMAX_DELAY) != pdTRUE) continue;

    uint32_t inFrames = chunk.len / (2 * sizeof(int16_t));
    const int16_t *inSamples = (const int16_t *)chunk.data;

    updateInputRateEstimate(inFrames, chunk.captured_ms);

    // [captured-ms-no-per-chunk-jitter]
    //
    // chunkDurMs and chunkStartMs are still computed because they are
    // the anchor for the very first frame after the buffer empties.
    // What changes is: pendingOldestCapturedMs is NOT recomputed from
    // chunkStartMs on every chunk arrival. That recomputation was the
    // root cause of the micro-stutter / pace-drift symptom:
    //
    //   i2s_read() returns when the DMA buffer has one chunk ready.
    //   `chunk.captured_ms = millis()` is sampled AFTER the read
    //   returns, so when audioReadTask is preempted by WiFi (pri 23)
    //   or lwIP (pri 18) for 2-10 ms, that millis() is 2-10 ms late.
    //   chunkStartMs then jitters by the same amount, and
    //   pendingOldestCapturedMs inherited that jitter. Every frame
    //   timestamp handed to the encoder (ef.captured_ms) and every
    //   udpSendTask packet (pkt.captured_ms) carried ±2-10 ms of
    //   arrival-time noise into the node's playout scheduler, which
    //   had to insert/remove samples to absorb it. That is the
    //   audible "pitch drifts then re-syncs" and the queue bouncing
    //   between 5 and 8 at 50 Hz.
    //
    // With this change, pendingOldestCapturedMs becomes a pure
    // sample-count timeline:
    //   - Each emitted frame advances it by exactly OPUS_FRAME_MS (20).
    //   - Each new chunk appends to the TAIL of pendingBuf, which
    //     does not move the oldest-sample timestamp.
    //   - Only a true buffer-empty (first boot, or a starve event)
    //     re-anchors to wall clock.
    //
    // Long-term drift between the sample-count timeline and millis()
    // is ~25 ppm at this source rate (≈10 ms over 400 s) and is
    // absorbed by the node's 200 ms playout delay. No per-second
    // re-anchoring is needed.
    const uint32_t chunkDurMs =
        (uint32_t)(((uint64_t)inFrames * 1000ULL) / g_sourceSampleRate);
    uint32_t chunkEndMs   = chunk.captured_ms;
    uint32_t chunkStartMs = (chunkEndMs > chunkDurMs)
                            ? (chunkEndMs - chunkDurMs) : 0;
    if (pendingCount == 0) {
      // [captured-ms-stable-anchor]
      // Previous code re-anchored pendingOldestCapturedMs to chunkStartMs
      // EVERY time the pending buffer emptied. On a healthy stream that
      // happens ~3x/second (4 chunks × 256 samples = 1024, one frame = 960,
      // pendingCount cycles 0→960→0 every ~320 ms). chunkStartMs is
      // derived from chunk.captured_ms, which audioReadTask samples AFTER
      // i2s_read() returns — so any WiFi/lwIP preemption of that task
      // (2-30 ms) injects a 2-30 ms jump into the node's captured_ms
      // timeline on every re-anchor. The recordings show exactly this:
      // capturedMsGapGt20Total=105-142 and capturedMsGapGt40Total=25-28
      // on three nodes that all simultaneously sit at nearDeadline and
      // playout=300 ms, while the server never dropped a single frame
      // (pendingBufDroppedTotal=0, opusDroppedLastWin=0). The "gap" is
      // phantom — it is the server rewriting its own capture timeline.
      //
      // Fix: re-anchor ONLY on boot (pendingOldestCapturedMs == 0) or on
      // a genuine interruption > 100 ms (real starve, OTA pause, codec
      // renegotiation). Otherwise preserve the pure sample-count timeline,
      // which advances by exactly OPUS_FRAME_MS per emitted frame and is
      // immune to per-chunk arrival jitter. The slow rate-estimator drift
      // (a few ppm) is absorbed by the node's normal corrector, which is
      // exactly what the corrector exists for.
      if (pendingOldestCapturedMs == 0) {
        pendingOldestCapturedMs = chunkStartMs;
      } else {
        int32_t drift = (int32_t)(chunkStartMs - pendingOldestCapturedMs);
        if (drift > 100 || drift < -100) {
          LOGW("captured_ms re-anchor: drift %ld ms "
               "(tracked=%lu, wall=%lu)",
               (long)drift,
               (unsigned long)pendingOldestCapturedMs,
               (unsigned long)chunkStartMs);
          pendingOldestCapturedMs = chunkStartMs;
        }
      }
    }
    // else: keep pendingOldestCapturedMs unchanged — it already points
    //       to the oldest sample in pendingBuf (advanced by ±exact
    //       sample-count deltas only, never by chunk-arrival jitter).

    // [captured-ms-drift-diagnostic]
    // Rate-limited log when the tracked timeline has drifted > 50 ms
    // from the millis() estimate. 50 ms is far above the per-chunk
    // jitter band (2-10 ms) and far below anything audible once
    // playout absorbs it, so this only fires on a genuine rate
    // mismatch or a starve-reanchor — never on normal WiFi preemption.
    if (pendingCount > 0) {
      uint32_t pendingAgeMs =
          (uint32_t)(((uint64_t)pendingCount * 1000ULL) / g_opusSampleRate);
      uint32_t computed = (chunkStartMs > pendingAgeMs)
                          ? (chunkStartMs - pendingAgeMs) : 0;
      int32_t drift = (int32_t)(computed - pendingOldestCapturedMs);
      if (drift > 50 || drift < -50) {
        static uint32_t lastDriftLogMs = 0;
        if (millis() - lastDriftLogMs > 5000) {
          lastDriftLogMs = millis();
          LOGW("captured_ms drift %ld ms (tracked=%lu, wall=%lu, "
               "pendingCount=%u); sample-clock is authoritative",
               (long)drift,
               (unsigned long)pendingOldestCapturedMs,
               (unsigned long)computed, (unsigned)pendingCount);
        }
      }
    }

    uint32_t contentFrames = resampler.process(
        inSamples, inFrames, contentScratch,
        sizeof(contentScratch) / (2 * sizeof(int16_t)));
    // [anti-alias-24k] Low-pass at 10 kHz before Stage-2 decimation when the
    // current encoder is at 24 kHz. The filter is bypassed entirely when
    // g_opusSampleRate == 48000. serviceAntiAliasFilter() is cheap (two
    // integer compares per audio iteration) and only reconfigures the biquad
    // coefficients on an actual rate change.
    serviceAntiAliasFilter();
    if (g_opusSampleRate == OPUS_SAMPLE_RATE_24K) {
        applyAntiAlias24k(contentScratch, contentFrames);
    }
    uint32_t produced = resamplerContentToOpus.process(
        contentScratch, contentFrames, resampledScratch,
        sizeof(resampledScratch) / (2 * sizeof(int16_t)));

    // [pending-head-drop] When the buffer would overrun its cap, drop the
    // OLDEST samples to make room for fresh audio. Keeps encoder and every
    // downstream node on the freshest samples, at the cost of a brief
    // timeline discontinuity (which the node's captured_ms gap detector
    // reports explicitly).
    if (produced > (PENDING_MAX_FRAMES - pendingCount)) {
      uint32_t drop = produced - (PENDING_MAX_FRAMES - pendingCount);
      if (drop >= pendingCount) {
        drop            = pendingCount;
        pendingCount    = 0;
        pendingOldestCapturedMs = chunkStartMs;
      } else {
        uint32_t keep = pendingCount - drop;
        memmove(pendingBuf, pendingBuf + drop * 2, keep * 2 * sizeof(int16_t));
        pendingCount = keep;
        pendingOldestCapturedMs +=
            (uint32_t)((uint64_t)drop * 1000ULL / g_opusSampleRate);
      }
      // [stutter-visible-counters] Unconditional — the sample stream just
      // lost `drop` samples BEFORE encode; node drop/conceal/rebuffer
      // counters cannot see this. Every dropped sample here is heard as
      // "the pace is a hair slow" with no other symptom.
      //
      // [pending-counters-mux] statPendingBufDroppedThisSec is reset by
      // audioEncodeTask in a read-then-clear sequence. An increment that
      // lands between the consumer's read and write is silently dropped
      // from the per-second snapshot and lands in the next second's
      // LastSec — or, worse, is lost entirely if the counter is reset to
      // zero after the add. Diagnostic-only, but if we expose a counter
      // it must be honest.
      portENTER_CRITICAL(&gStatsMux);
        statPendingBufDroppedTotal   += drop;
        statPendingBufDroppedThisSec += drop;
      portEXIT_CRITICAL(&gStatsMux);
      static uint32_t lastOverflowLogMs = 0;
      if (millis() - lastOverflowLogMs > 1000) {
        lastOverflowLogMs = millis();
        LOGW("pendingBuf cap reached; dropped %u oldest samples "
             "(produced=%u, cap=%u) — source rate mismatch likely",
             (unsigned)drop, (unsigned)produced, (unsigned)PENDING_MAX_FRAMES);
      }
    }
    for (uint32_t i = 0; i < produced && pendingCount < PENDING_MAX_FRAMES; i++) {
      pendingBuf[pendingCount * 2 + 0] = resampledScratch[i * 2 + 0];
      pendingBuf[pendingCount * 2 + 1] = resampledScratch[i * 2 + 1];
      pendingCount++;
    }

    // [stutter-visible-counters] Peak fill per second. A single 80 ms
    // burst followed by 920 ms empty is invisible to any single snapshot
    // — the peak catches it. Values near PENDING_MAX_FRAMES confirm
    // overrun is imminent in the same second the drop counter fires.
    // [pending-counters-mux] Same reasoning as the drop counters above.
    portENTER_CRITICAL(&gStatsMux);
      if (pendingCount > statPendingBufPeakDepth) {
        statPendingBufPeakDepth = pendingCount;
      }
    portEXIT_CRITICAL(&gStatsMux);

    // Emit ready frames. All pre-processing happens here, not on core 0.
    while (pendingCount >= g_opusFrameSamples) {
      if (quietModeEnabled) {
        applyQuietFilter(pendingBuf, g_opusFrameSamples);
        applyQuietDynamics(pendingBuf, g_opusFrameSamples);
      }

      uint8_t effectiveVolume =
          (uint8_t)(((uint16_t)masterVolume * potVolume) / 100);
      if (effectiveVolume < 100) {
        for (uint32_t i = 0; i < g_opusFrameSamples * CHANNELS; ++i)
          pendingBuf[i] = (int16_t)((int32_t)pendingBuf[i] * effectiveVolume / 100);
      }

      EncodeFrame ef;
      ef.captured_ms = pendingOldestCapturedMs;
      // Reserve the wire sequence before enqueueing/encoding so frame drops
      // in either stage are visible as gaps to players running PLC.
      ef.seq         = gAudioFrameSeq++;
      ef.samples     = g_opusFrameSamples;
      memcpy(ef.pcm, pendingBuf, g_opusFrameSamples * CHANNELS * sizeof(int16_t));

      if (xQueueSend(encodeQueue, &ef, 0) != pdTRUE) statOpusDropped++;

      uint32_t remaining = pendingCount - g_opusFrameSamples;
      memmove(pendingBuf, pendingBuf + g_opusFrameSamples * 2,
              remaining * 2 * sizeof(int16_t));
      pendingCount = remaining;
      pendingOldestCapturedMs += OPUS_FRAME_MS;
    }
  }
}

// ---- Audio encode task (Core 0) -------------------------------------------
// Runs ONLY opus_encode() and the encoder-related controls (complexity,
// bitrate/VBR/signal, sample-rate re-create). Everything else lives on
// core 1. On this core the encoder competes only with the IDF WiFi/lwIP
// tasks (priority 18+) — when WiFi is quiet, encode gets a full core.
inline void audioEncodeTask(void *param) {
  (void)param;
  LOGI("audioEncodeTask started on core %d", xPortGetCoreID());
  // [core-split-encode-stack] ef is 3846 B, pkt is 1036 B. Both live in BSS
  // via `static` so the task stack only pays for opus_encode's internals
  // (~2–3 KB) plus this function's small locals. Non-static put ~4.8 KB of
  // frame buffers on the 8 KB stack, leaving almost no margin for a
  // complex-frame encode. audioProcTask has used the same `static` pattern
  // for its scratch buffers since the two-stage resampler landed.
  static EncodeFrame ef;
  static OpusPacket pkt;

  for (;;) {
    if (otaPauseAudio) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

    // [stutter-visible-counters] 1-second rollover. Runs here because
    // audioEncodeTask is the only place the *ThisSec counters are
    // incremented (the encode ones directly; the pendingBuf ones from
    // audioProcessingTask, which pushes into encodeQueue which we drain
    // at 50 Hz — close enough that a 1-second cadence here matches the
    // 1-second window in real time). It runs unconditionally at the top
    // of every iteration, so even if a frame is delayed the rollover is
    // not skipped by more than one frame (20 ms).
    //
    // Read-then-reset is not atomic across cores for the pendingBuf
    // counters (audioProcessingTask writes them on core 1). A drop that
    // lands between read and reset is lost from the snapshot — benign,
    // it lands in the next second's LastSec instead. Diagnostics, not
    // control path.
    {
      static uint32_t lastRolloverMs = 0;
      uint32_t nowRoll = millis();
      if (nowRoll - lastRolloverMs >= 1000) {
        lastRolloverMs = nowRoll;
        // [pending-counters-mux] The two pendingBuf counters are written
        // by audioProcessingTask; take the same mux the producers use so
        // the read-then-reset is atomic with respect to an increment
        // landing mid-window. statEncode* are only touched by this task.
        portENTER_CRITICAL(&gStatsMux);
          statPendingBufDroppedLastSec   = statPendingBufDroppedThisSec;
          statPendingBufDroppedThisSec   = 0;
          statPendingBufPeakDepthLastSec = statPendingBufPeakDepth;
          statPendingBufPeakDepth        = 0;
        portEXIT_CRITICAL(&gStatsMux);
        statEncodeOverrunsLastSec      = statEncodeOverrunsThisSec;
        statEncodeOverrunsThisSec      = 0;
        statEncodePeakUsLastSec        = statEncodePeakUsThisSec;
        statEncodePeakUsThisSec        = 0;
      }
    }

    // Drain any pending rate change BEFORE receiving the next frame.
    if (g_pendingOpusSampleRate != 0) handleEncoderRecreate();

    if (xQueueReceive(encodeQueue, &ef, portMAX_DELAY) != pdTRUE) continue;

    if (opusEncoder == nullptr) {
      static unsigned long lastWarnMs = 0;
      unsigned long nowMs = millis();
      if (nowMs - lastWarnMs > 1000) {
        lastWarnMs = nowMs;
        LOGE("audioEncodeTask: opusEncoder is null; dropping frame "
             "(check setup_opus() log for opus_encoder_create failure)");
      }
      continue;
    }

    // [core-split-samples-guard] A frame that was queued at the OLD rate
    // just before handleEncoderRecreate() ran will have the wrong sample
    // count for the new encoder. Drop it (equivalent to the old code's
    // pendingBuf flush on re-create).
    if (ef.samples != g_opusFrameSamples) continue;

    if (g_pendingComplexity >= 0) {
      int c = g_pendingComplexity;
      g_pendingComplexity = -1;
      if (c != currentOpusComplexity) {
        opus_encoder_ctl(opusEncoder, OPUS_SET_COMPLEXITY(c));
        currentOpusComplexity = c;
        LOGI("Opus complexity applied: %d", c);
      }
    }

    if (g_encoderSettingsDirty) {
      g_encoderSettingsDirty = false;
      
      // [app-change-recreate]
      // Libopus application mode (Audio/Voip/LowDelay) CANNOT be changed
      // via opus_encoder_ctl. It must be set at creation. If the dashboard
      // changes it, we must re-create the encoder.
      if (g_newApplication != g_currentApplication) {
        g_currentApplication = g_newApplication; // update current
        g_pendingOpusSampleRate = 0xFFFFFFFF;    // use a magic number to force recreate
      }

      if (g_newBitrate != g_currentBitrate) {
        opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(g_newBitrate));
        g_currentBitrate = g_newBitrate;
        LOGI("Opus bitrate applied: %d bps", g_currentBitrate);
      }
      if (g_newVbr != g_currentVbr) {
        opus_encoder_ctl(opusEncoder, OPUS_SET_VBR(g_newVbr));
        g_currentVbr = g_newVbr;
        LOGI("Opus VBR applied: %d", g_currentVbr);
      }
      if (g_newVbrConstraint != g_currentVbrConstraint) {
        opus_encoder_ctl(opusEncoder, OPUS_SET_VBR_CONSTRAINT(g_newVbrConstraint));
        g_currentVbrConstraint = g_newVbrConstraint;
        LOGI("Opus VBR constraint applied: %d", g_currentVbrConstraint);
      }
      if (g_newSignal != g_currentSignal) {
        opus_encoder_ctl(opusEncoder, OPUS_SET_SIGNAL(g_newSignal));
        g_currentSignal = g_newSignal;
        LOGI("Opus signal applied: %d", g_currentSignal);
      }
      // Removed g_currentApplication = g_newApplication; 
      // It is updated inside handleEncoderRecreate() only when the encoder
      // is actually re-created.
    }

    pkt.captured_ms = ef.captured_ms;
    pkt.seq         = ef.seq;

    uint32_t t0 = micros();
    int nbytes = opus_encode(opusEncoder, ef.pcm, ef.samples,
                             pkt.data, OPUS_MAX_PACKET_BYTES);
    uint32_t encodeUs = micros() - t0;
    gAvgEncodeUs = (gAvgEncodeUs * 63 + encodeUs) >> 6;

    // [stutter-visible-counters] Encode overruns and per-second peak.
    // An overrun is any encode that took longer than the 20 ms frame
    // period. The average (gAvgEncodeUs) always looks fine on a healthy
    // build (~3-5 ms), but if the peak is 30-50 ms the encoder is being
    // preempted by WiFi (pri 23 > our pri 10) for entire frame periods.
    // That is exactly what produces a late packet once every ~60 frames.
    if (encodeUs > 20000) {
      statEncodeOverrunsTotal++;
      statEncodeOverrunsThisSec++;
      
      // [encoder-dynamic-throttle] Auto-reduce complexity if CPU starved.
      static uint32_t lastThrottleMs = 0;
      if (millis() - lastThrottleMs > 10000) {
        lastThrottleMs = millis();
        if (g_pendingComplexity < 0 && currentOpusComplexity > 0) {
          g_pendingComplexity = currentOpusComplexity - 1;
          LOGW("Encoder overrun detected! Dynamically dropping complexity to %d", g_pendingComplexity);
        }
      }
    }
    if (encodeUs > statEncodePeakUsThisSec) {
      statEncodePeakUsThisSec = encodeUs;
    }

    if (nbytes < 0) {
      // [encode-error-rate-limited]
      // Two compounding bugs in the old code:
      //   (1) No statOpusDropped++ → dashboard was blind to the loss.
      //   (2) No rate limiting on LOGE. At 115200 baud, one formatted
      //       line ≈ 4.5 ms of blocked UART. If VBR peaks routinely
      //       exceed OPUS_MAX_PACKET_BYTES (common at high bitrate with
      //       vbrConstraint off), this fires at 50 Hz = 225 ms/s of
      //       guaranteed UART stall, cascading into more encode errors.
      //       A self-reinforcing collapse.
      //
      // Rate-limit to 1 log/s and always count the drop.
      static unsigned long lastEncodeErrLogMs = 0;
      unsigned long nowMs = millis();
      if (nowMs - lastEncodeErrLogMs > 1000) {
        lastEncodeErrLogMs = nowMs;
        LOGE("opus_encode() failed, error code %d "
             "(further encode errors suppressed for 1s)",
             nbytes);
      }
      statOpusDropped++;
    } else {
      pkt.len = nbytes;
      if (xQueueSend(opusQueue, &pkt, 0) != pdTRUE) statOpusDropped++;
      else                                         statOpusEncoded++;
    }
  }
}

inline void setup_queues_and_tasks() {
  LOGI("Creating queues and background tasks...");
  rawQueue    = xQueueCreate(RAW_QUEUE_LEN,    sizeof(RawChunk));
  opusQueue   = xQueueCreate(OPUS_QUEUE_LEN,   sizeof(OpusPacket));
  encodeQueue = xQueueCreate(ENCODE_QUEUE_LEN, sizeof(EncodeFrame));
  if (!rawQueue || !opusQueue || !encodeQueue) {
    LOGE("Failed to create one or more queues!");
    return;
  }

  BaseType_t rc;

  // ---- Core 0: audio pipeline + clock sync + loop() + WiFi + AsyncTCP ---
  // ---- Core 1: Opus encoder ONLY --------------------------------------
  // [encoder-sole-occupant]
  // Every task except audioEncodeTask now runs on Core 0:
  //   audioRead    pri 4   (I2S DMA drain)
  //   clockSync    pri 4   (UDP sync responder, see EVDMain.h)
  //   udpSend      pri 3   (UDP pacer)
  //   audioProc    pri 2   (resample + quiet + volume)
  //   loop()       pri 1   (Arduino default — discovery, NVS flush, pot)
  //   AsyncTCP     pri 1   (per build_opt.h)
  //   WiFi/lwIP    pri 18-23 (system — highest in the system)
  //
  // Core 1 has exactly ONE task:
  //   audioEncode  pri 5
  //
  // RATIONALE: only the encoder is latency-critical. Once opus_encode()
  // is called it must return within ~20 ms (one frame period) or the
  // frame misses its slot and the UDP pacer has to catch up. That does
  // not tolerate preemption. The other tasks all have explicit slack:
  //   - audioRead: 42.6 ms DMA ring (8 × 256 samples at 48 kHz stereo)
  //   - udpSend:   opusQueue (16 slots = 320 ms of buffering)
  //   - audioProc: pendingBuf (16 KB = ~85 ms at 48 kHz stereo)
  //   - clockSync: polls every 1 ms, only has to answer one packet every
  //                2 s per node
  //   - loop():    inherently tolerant (discovery, NVS flush on quiet,
  //                pot poll)
  // All of those can run on a core shared with WiFi and still deliver
  // on time.
  //
  // The encoder now has a whole core to itself — no WiFi, no lwIP, no
  // flash-write stalls from loop()'s NVS flush, no audioRead, no udpSend,
  // no clockSync. This is the cleanest possible isolation for
  // opus_encode().
  //
  // [encoder-crosscore-queue] audioProc produces one frame per 20 ms on
  // Core 0 and xQueueSend()s it into encodeQueue. audioEncode on Core 1
  // drains it. Cross-core wakeup adds a few microseconds (ESP32 IPC
  // interrupt); negligible against the 20 ms frame period. Because the
  // encoder has zero competition on its core, it drains the queue as
  // fast as audioProc fills it — no encodeQueue backlog in the steady
  // state, same net outcome as the previous "pri 5 above pri 2" design
  // but with strictly better isolation for the encoder.
  //
  // TRADEOFF: audioRead now shares Core 0 with WiFi (pri 23) and lwIP
  // (pri 18). A heavy WiFi burst CAN preempt it for 10-20 ms. The 42.6 ms
  // DMA ring absorbs normal preemption; a sustained burst over 40 ms
  // would overflow the ring and lose I2S samples. If i2sReadErrors climbs
  // in the field, the targeted fix is to move ONLY audioRead back to
  // Core 1 (both the encoder and the reader on the dedicated core,
  // everything else on Core 0). Do not move the rest without measuring —
  // pendingBuf and opusQueue are sized for exactly this jitter and absorb
  // it silently.
  //
  // Priority order on Core 0 (highest → lowest):
  //   WiFi(23) → lwIP(18) → audioRead(4) = clockSync(4) → udpSend(3) →
  //   audioProc(2) → loop(1) = AsyncTCP(1)
  // On Core 1: audioEncode(5) only.
  rc = xTaskCreatePinnedToCore(audioReadTask, "audioRead", 4096, nullptr, 4, nullptr, 0);
  if (rc != pdPASS) LOGE("FAILED to create audioReadTask (rc=%d)", (int)rc);

  rc = xTaskCreatePinnedToCore(udpSendTask, "udpSend", 8192, nullptr, 3, nullptr, 0);
  if (rc != pdPASS) LOGE("FAILED to create udpSendTask (rc=%d)", (int)rc);

  rc = xTaskCreatePinnedToCore(audioProcessingTask, "audioProc", 8192, nullptr, 2, nullptr, 0);
  if (rc != pdPASS) LOGE("FAILED to create audioProcessingTask (rc=%d)", (int)rc);

  // [encoder-sole-occupant] Core 1: audioEncode only.
  rc = xTaskCreatePinnedToCore(audioEncodeTask, "audioEncode", 8192, nullptr, 5, nullptr, 1);
  if (rc != pdPASS) LOGE("FAILED to create audioEncodeTask (rc=%d)", (int)rc);

  LOGI("Tasks created: audioRead(c0,p4) udpSend(c0,p3) audioProc(c0,p2) "
       "clockSync(c0,p4) loop(c0) | audioEncode(c1,p5) ALONE on core 1 | "
       "WiFi+lwIP+AsyncTCP on c0");
}