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
// output sample per channel. Costs ~2x the multiplies of the previous
// linear interpolator (4 vs 2 per channel) and removes the two artifacts
// that made the old version sound "grainy":
//
//   * linear interpolation has roughly -30 dB worst-case error near
//     Nyquist, which shows up as broadband HF hash on cymbals and on
//     anything with consonants;
//   * the images the linear interpolator folds back into the passband
//     after the 44.1 -> 48 kHz step are not filterable downstream and
//     read as a fine "sand" texture on top of the music.
//
// Cubic does not eliminate the images entirely (a proper 32-tap polyphase
// FIR would), but it pushes them 20+ dB below where linear put them, which
// is the difference between audible and not audible on this pipeline.
class ResamplerToOpusRate {
public:
  void reset() {
    _pos = 0.0;
    _haveHistory = false;
    _prevL = _prevR = _lastL = _lastR = 0;
  }

  void setRatio(double r) {
    const double nominal = (double)SOURCE_SAMPLE_RATE / (double)OPUS_SAMPLE_RATE;
    if (r < nominal * 0.95) r = nominal * 0.95;
    if (r > nominal * 1.05) r = nominal * 1.05;
    _ratio = r;
  }
  double ratio() const        { return _ratio; }
  double nominalRatio() const { return (double)SOURCE_SAMPLE_RATE / (double)OPUS_SAMPLE_RATE; }

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

    while (outCount < outCapacityFrames) {
      double srcIndexF = _pos;
      long idx = (long)floor(srcIndexF);
      if (idx >= (long)inFrames - 1) break;

      double frac = srcIndexF - (double)idx;

      // Gather the 4-tap neighbourhood p[-1 .. 2] around the interpolation
      // point. The interpolation sits between p[0] (index idx) and p[1]
      // (index idx+1). Where the tap falls outside the available data we
      // clamp to the nearest valid sample rather than reading past the
      // buffer.
      int16_t pmL, pmR, p0L, p0R, p1L, p1R, p2L, p2R;

      if (idx < 0) {
        // _pos can be slightly negative at the very start of a chunk; the
        // two "history" samples carried over from the previous call stand
        // in for indices -2 and -1.
        pmL = _prevL; pmR = _prevR;
        p0L = _lastL; p0R = _lastR;
        p1L = in[0];  p1R = in[1];
        p2L = (inFrames >= 2) ? in[2] : in[0];
        p2R = (inFrames >= 2) ? in[3] : in[1];
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
        } else {
          p2L = p1L; p2R = p1R;
        }
      }

      // Catmull-Rom basis polynomials for t in [0, 1]:
      //   am = -0.5 t^3 +       t^2 - 0.5 t
      //   a0 =  1.5 t^3 - 2.5   t^2       + 1
      //   a1 = -1.5 t^3 + 2.0   t^2 + 0.5 t
      //   a2 =  0.5 t^3 - 0.5   t^2
      float t  = (float)frac;
      float t2 = t * t;
      float t3 = t2 * t;
      float am = -0.5f * t3 +        t2 - 0.5f * t;
      float a0 =  1.5f * t3 - 2.5f * t2              + 1.0f;
      float a1 = -1.5f * t3 +  2.0f * t2 + 0.5f * t;
      float a2 =  0.5f * t3 -  0.5f * t2;

      float outLf = am * (float)pmL + a0 * (float)p0L + a1 * (float)p1L + a2 * (float)p2L;
      float outRf = am * (float)pmR + a0 * (float)p0R + a1 * (float)p1R + a2 * (float)p2R;

      // Round-to-nearest and clamp. Cubic overshoots slightly on sharp
      // transients; the clamp is what stops that from wrapping on int16_t.
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
  double _ratio = (double)SOURCE_SAMPLE_RATE / (double)OPUS_SAMPLE_RATE;
  bool   _haveHistory = false;
  int16_t _lastL = 0, _lastR = 0;
  int16_t _prevL = 0, _prevR = 0;
};

inline ResamplerToOpusRate resampler;

// ---------------------------------------------------------------------------
//  Adaptive input rate measurement
//
//  The source board is configured for SOURCE_SAMPLE_RATE (44,100 Hz) but a
//  classic ESP32 I2S master without APLL cannot hit that exactly; some boards
//  land near 43.8 kHz, others near 42.5 kHz. If we resample assuming 44,100
//  the encoder produces ~48.2 packets/sec while the node plays at 50/sec.
//  The node jitter buffer drains ~1.8 packets/sec and rebuffers on a ~5 s
//  cycle: perfect -> grainy -> perfect.
//
//  We measure the real input rate over a sliding window and retune the
//  resampler ratio to compensate. One measurement per window, one ratio
//  write per window, no per-chunk arithmetic beyond a counter.
// ---------------------------------------------------------------------------
// A 3 s window is short enough that the IIR converges to a stable rate in
// a handful of writes, and long enough that the per-window quantization
// error (millis() has 1 ms granularity; 3 s of 44.1 kHz audio is 132 300
// frames, so the rate is quantized to ~0.03 %) is well below the 0.05 %
// deadband the estimator uses before it rewrites the resampler ratio.
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

inline void updateInputRateEstimate(uint32_t framesJustArrived, uint32_t nowMs) {
    if (rateMeasureStartMs == 0) {
        rateMeasureStartMs = nowMs;
        rateMeasureFrames  = 0;
    }
    rateMeasureFrames += framesJustArrived;

    uint32_t elapsed = nowMs - rateMeasureStartMs;
    if (elapsed < RATE_MEASURE_WINDOW_MS) return;

    // Fold in any raw chunks that were dropped during this window. The
    // dropped frames represent time that elapsed but produced no samples
    // the estimator saw, so without this correction the measured rate is
    // biased low by exactly the drop fraction. Reading a volatile uint32_t
    // on a 32-bit aligned address is atomic on the ESP32.
    uint32_t lostNow    = gRateFramesLostTotal;
    uint32_t lostInWin  = lostNow - rateFramesLostSeen;
    rateFramesLostSeen  = lostNow;

    double rawRate = (double)(rateMeasureFrames + lostInWin) * 1000.0 / (double)elapsed;
    rateMeasureStartMs = nowMs;
    rateMeasureFrames  = 0;

    // Reject obviously bogus windows (boot burst, WiFi stall, GC pause).
    if (rawRate < 30000.0 || rawRate > 52000.0) return;

    if (smoothedInputRate == 0.0) {
        smoothedInputRate = rawRate;
    } else {
        smoothedInputRate += RATE_SMOOTH_ALPHA * (rawRate - smoothedInputRate);
    }

    statMeasuredInputHz = (float)smoothedInputRate;

      // [drift-ppm-source-ref]
    // Compare against the source rate the board is supposed to be producing,
    // not against OPUS_SAMPLE_RATE. The resampler ratio already handles the
    // 44100 -> 48000 conversion; the only thing this number is supposed to
    // answer is "how far off nominal is the source board". Subtracting 48000
    // makes the reported drift look like the whole conversion ratio
    // (-81251 ppm for a perfect source), which makes the field useless for
    // diagnostics.
      statDriftPpm = (int32_t)(((double)smoothedInputRate - (double)SOURCE_SAMPLE_RATE)
                             / (double)SOURCE_SAMPLE_RATE * 1000000.0);

    double targetRatio  = smoothedInputRate / (double)OPUS_SAMPLE_RATE;
    double currentRatio = resampler.ratio();

    // Write cadence and deadband. The previous 30 s / 0.3 % pair took
    // minutes to close a 3.6 % boot error, during which the node's jitter
    // buffer was drifting in one direction and audibly underflowing or
    // overflowing on a several-second cycle. With the dropped-frame
    // correction above, the burst artifact the 30 s gate was guarding
    // against cannot corrupt the window any more, so the write cadence can
    // be much tighter.
    static uint32_t lastRatioWriteMs = 0;
    if (nowMs - lastRatioWriteMs < 4000) return;                       // was 30000
    if (fabs(currentRatio - targetRatio) / targetRatio < 0.0005) return; // was 0.0020
    lastRatioWriteMs = nowMs;

    const double maxStepFrac = 0.0100;   // 1.0 % per write, was 0.3 %
    double maxStep = currentRatio * maxStepFrac;
    if (targetRatio > currentRatio + maxStep) targetRatio = currentRatio + maxStep;
    if (targetRatio < currentRatio - maxStep) targetRatio = currentRatio - maxStep;

    resampler.setRatio(targetRatio);
    LOGI("Input rate %.1f Hz (smoothed %.1f, lost %u); resampler ratio -> %.8f",
         rawRate, smoothedInputRate, (unsigned)lostInWin, targetRatio);
}

// ---- Pending (post-resample) buffer ---------------------------------------
EXT_RAM_BSS_ATTR int16_t  pendingBuf[PENDING_MAX_FRAMES * 2];
uint32_t pendingCount = 0;
uint32_t pendingOldestCapturedMs = 0;

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

// Monotonically increasing audio frame sequence number (see AUDIO_HEADER_BYTES
// docs in EVDConfig.h). Assigned once per successfully-encoded Opus frame,
// before the frame is queued for sending, so a frame dropped later (full
// opusQueue, UDP send failure) still leaves a visible gap in the sequence
// seen by the player - that gap is the loss signal; the server does not
// retransmit real-time audio.
static uint32_t gAudioFrameSeq = 0;

// Peak limiter state. Attack is fast (a transient is caught within one
// frame), release is slow (~2 s) so quiet passages are not lifted back up.
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
  quietHpL.setHighpass(400.0f, (float)OPUS_SAMPLE_RATE);
  quietHpR.setHighpass(400.0f, (float)OPUS_SAMPLE_RATE);
  quietLpL.setLowpass (9500.0f, (float)OPUS_SAMPLE_RATE);
  quietLpR.setLowpass (9500.0f, (float)OPUS_SAMPLE_RATE);
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
    buf[i * 2]     = (int16_t)l;
    buf[i * 2 + 1] = (int16_t)r;
  }
}

// Peak limiter with slow release. This is what actually keeps audio from
// carrying through walls: spikes in a quiet passage are what the neighbour
// hears, not the average level. Ceiling is -4 dBFS, hard limit at 18 dB.
inline void applyQuietDynamics(int16_t *buf, uint32_t frames) {
  const float releaseCoef = 0.0007f;    // envelope release per sample
  const float ceiling     = 0.63f;      // ~-4 dBFS
  const float minGain     = 0.125f;     // -18 dB, hard limit
  const float attackSlew  = 0.5f;       // gain closes ~50% per 20 ms frame
  const float releaseSlew = 0.015f;     // gain opens ~1.5% per 20 ms frame

  int32_t peak = 0;
  for (uint32_t i = 0; i < frames * 2; ++i) {
    int32_t s = buf[i] < 0 ? -buf[i] : buf[i];
    if (s > peak) peak = s;
  }

  float p = (float)peak / 32768.0f;
  if (p > quietDyn.envelope) quietDyn.envelope = p;   // instant attack
  else quietDyn.envelope = quietDyn.envelope * (1.0f - releaseCoef) + p * releaseCoef;

  float targetGain = 1.0f;
  if (quietDyn.envelope > ceiling) targetGain = ceiling / quietDyn.envelope;
  if (targetGain < minGain) targetGain = minGain;

  if (targetGain < quietDyn.gain) {
    quietDyn.gain = quietDyn.gain * (1.0f - attackSlew) + targetGain * attackSlew;
  } else {
    quietDyn.gain = quietDyn.gain * (1.0f - releaseSlew) + targetGain * releaseSlew;
  }

  for (uint32_t i = 0; i < frames * 2; ++i) {
    int32_t s = (int32_t)((float)buf[i] * quietDyn.gain);
    if (s >  32767) s =  32767;
    if (s < -32768) s = -32768;
    buf[i] = (int16_t)s;
  }
}

// ---- Opus ------------------------------------------------------------------
inline void setup_opus() {
  LOGI("Initializing Opus encoder (%d Hz, %d ch, %d bps)...",
       OPUS_SAMPLE_RATE, CHANNELS, OPUS_BITRATE);
  int err = 0;
  opusEncoder = opus_encoder_create(OPUS_SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &err);
  if (err != OPUS_OK || opusEncoder == nullptr) {
    LOGE("opus_encoder_create() FAILED, error %d", err);
    return;
  }
  opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(OPUS_BITRATE));
  opus_encoder_ctl(opusEncoder, OPUS_SET_VBR(1));
  opus_encoder_ctl(opusEncoder, OPUS_SET_VBR_CONSTRAINT(1));
  opus_encoder_ctl(opusEncoder, OPUS_SET_COMPLEXITY(currentOpusComplexity));
  opus_encoder_ctl(opusEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
  LOGI("Opus encoder ready (frame = %d samples / %d ms)",
       OPUS_FRAME_SAMPLES, (OPUS_FRAME_SAMPLES * 1000) / OPUS_SAMPLE_RATE);
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
// Single task that feeds rawQueue from whichever input is active. When the
// dashboard's source is BT or AUX, this reads from I2S (the source board is
// the I2S master and provides clocks regardless). When the source is USB,
// this reads from the UAC playback stream instead. Both branches push the
// same RawChunk struct onto rawQueue, so everything downstream - rate
// measurement, resampler, quiet filter, master volume, Opus - is unaware
// which input produced the samples.
//
// The i2s_read() branch uses portMAX_DELAY because the DMA ring always
// eventually fills (the source board clocks every sample). The USB branch
// uses a short sleep when the host isn't streaming so we don't spin.
inline void audioReadTask(void *param) {
  (void)param;
  LOGI("audioReadTask started on core %d", xPortGetCoreID());
  static uint8_t readBuf[I2S_READ_CHUNK_BYTES];

  for (;;) {
    if (otaPauseAudio) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

    size_t bytesRead = 0;

    if (serverInputSource == SRC_FROM_USB) {
      bytesRead = readServerUsbAudio(readBuf, sizeof(readBuf));
      if (bytesRead == 0) {
        // No data: either the host is not playing, or the device is
        // unplugged. Sleep briefly and try again. 5 ms keeps the CPU cost
        // negligible while still reacting to a resume within one frame.
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      statUsbBytesIn += bytesRead;
    } else {
      esp_err_t res = i2s_read(I2S_PORT_IN, readBuf, sizeof(readBuf),
                               &bytesRead, portMAX_DELAY);
      if (res != ESP_OK) {
        statI2sReadErrors++;
        LOGW("i2s_read() error %d", (int)res);
        continue;
      }
      if (bytesRead == 0) continue;
      statI2sBytesIn    += bytesRead;
      statI2sBytesTotal += bytesRead;
    }

    RawChunk chunk;
    memcpy(chunk.data, readBuf, bytesRead);
    chunk.len         = bytesRead;
    chunk.captured_ms = millis();
    if (xQueueSend(rawQueue, &chunk, 0) != pdTRUE) {
      statRawDropped++;
      // Feed the exact frame count of the lost chunk back into the rate
      // estimator. Without this the estimator sees the elapsed window
      // stretch while the frame count stays put, so a burst of drops
      // during a scheduling stall biases the measured rate low - which is
      // what the old 30 s write cadence and 0.3 % clamp were guarding
      // against. With the correction, the clamp can be relaxed and the
      // resampler converges in seconds instead of minutes.
      gRateFramesLostTotal += bytesRead / (2 * sizeof(int16_t));
    }
  }
}

// ---- Audio processing task -------------------------------------------------
// Pulls RawChunks from rawQueue, resamples to Opus rate, applies the quiet
// filter and peak limiter if applicable, applies master volume, encodes to
// Opus, and pushes OpusPackets to opusQueue. Unchanged from the I2S-only
// version - it does not know or care whether the samples came from I2S or
// from the USB UAC device, because both go through rawQueue as RawChunks.
inline void audioProcessingTask(void *param) {
  (void)param;
  LOGI("audioProcessingTask started on core %d", xPortGetCoreID());
  static EXT_RAM_BSS_ATTR int16_t resampledScratch[8192];
  RawChunk chunk;

  // Start from nominal. updateInputRateEstimate() corrects it within the
  // first window once it has a stable measurement.
  resampler.reset();
  resampler.setRatio(resampler.nominalRatio());
  statDriftPpm = 0;

  for (;;) {
    if (otaPauseAudio) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    if (xQueueReceive(rawQueue, &chunk, portMAX_DELAY) != pdTRUE) continue;

    // setup_opus() can fail (opus_encoder_create returns nullptr). In that
    // case there is no encoder to feed, so drain the queue quietly instead
    // of burning CPU on resample/filter/volume for a packet that cannot be
    // produced. Log once per second so the operator sees it, not 50 Hz.
    if (opusEncoder == nullptr) {
      static unsigned long lastWarnMs = 0;
      unsigned long nowMs = millis();
      if (nowMs - lastWarnMs > 1000) {
        lastWarnMs = nowMs;
        LOGE("audioProcessingTask: opusEncoder is null; dropping chunk "
             "(check setup_opus() log for opus_encoder_create failure)");
      }
      continue;
    }

    // ---- Apply any queued Opus complexity change --------------------
    // g_pendingComplexity is written by the tuner (loop, Core 1) and by
    // applyOpusProfileForMode() (AsyncWebServer, Core 0). opus_encoder_ctl()
    // is only called from here, on the same task that calls opus_encode(),
    // so the two can never overlap. libopus is not re-entrant per encoder
    // instance: a concurrent ctl+encode corrupts the SILK/CELT predictor
    // state and produces audio that decodes but sounds wrong (grain).
    if (g_pendingComplexity >= 0) {
      int c = g_pendingComplexity;
      g_pendingComplexity = -1;
      if (c != currentOpusComplexity) {
        opus_encoder_ctl(opusEncoder, OPUS_SET_COMPLEXITY(c));
        currentOpusComplexity = c;
        LOGI("Opus complexity applied: %d", c);
      }
    }

    uint32_t inFrames = chunk.len / (2 * sizeof(int16_t));
    const int16_t *inSamples = (const int16_t *)chunk.data;

    // Measure the real input rate before resampling. The ratio used for
    // this chunk reflects the data actually arriving.
    updateInputRateEstimate(inFrames, chunk.captured_ms);

    // Re-anchor the capture stamp of the oldest pending sample to real wall
    // clock on every chunk. Without this, pendingOldestCapturedMs is a
    // synthetic clock that only re-syncs when pendingCount hits zero, and
    // any systematic rate error accumulates into the reported latency
    // (which is how a 3.6 % rate mismatch showed up as a 5200 ms reading).
    if (pendingCount > 0) {
      uint32_t pendingAgeMs =
          (uint32_t)(((uint64_t)pendingCount * 1000ULL) / OPUS_SAMPLE_RATE);
      pendingOldestCapturedMs = chunk.captured_ms - pendingAgeMs;
    } else {
      pendingOldestCapturedMs = chunk.captured_ms;
    }

    uint32_t produced = resampler.process(
        inSamples, inFrames, resampledScratch,
        sizeof(resampledScratch) / (2 * sizeof(int16_t)));

    for (uint32_t i = 0; i < produced && pendingCount < PENDING_MAX_FRAMES; i++) {
      pendingBuf[pendingCount * 2 + 0] = resampledScratch[i * 2 + 0];
      pendingBuf[pendingCount * 2 + 1] = resampledScratch[i * 2 + 1];
      pendingCount++;
    }

    while (pendingCount >= OPUS_FRAME_SAMPLES) {
      OpusPacket pkt;
      pkt.captured_ms = pendingOldestCapturedMs;

      if (playbackMode == "quiet") {
        applyQuietFilter(pendingBuf, OPUS_FRAME_SAMPLES);
        applyQuietDynamics(pendingBuf, OPUS_FRAME_SAMPLES);
      }

      // Combined slider + hardware-pot gain. Both contribute; neither
      // overrides the other. Product is 0..10000, fits uint16_t; /100 -> 0..100.
      uint8_t effectiveVolume =
          (uint8_t)(((uint16_t)masterVolume * potVolume) / 100);
      if (effectiveVolume < 100) {
        for (uint32_t i = 0; i < OPUS_FRAME_SAMPLES * CHANNELS; ++i)
          pendingBuf[i] = (int16_t)((int32_t)pendingBuf[i] * effectiveVolume / 100);
      }

      uint32_t t0 = micros();
      int nbytes = opus_encode(opusEncoder, pendingBuf, OPUS_FRAME_SAMPLES,
                               pkt.data, OPUS_MAX_PACKET_BYTES);
      uint32_t encodeUs = micros() - t0;

      gAvgEncodeUs = (gAvgEncodeUs * 63 + encodeUs) >> 6;

      if (nbytes < 0) {
        LOGE("opus_encode() failed, error code %d", nbytes);
      } else {
        pkt.len = nbytes;
        pkt.seq = gAudioFrameSeq++;
        if (xQueueSend(opusQueue, &pkt, 0) != pdTRUE) statOpusDropped++;
        else                                         statOpusEncoded++;
      }

      uint32_t remaining = pendingCount - OPUS_FRAME_SAMPLES;
      memmove(pendingBuf, pendingBuf + OPUS_FRAME_SAMPLES * 2,
              remaining * 2 * sizeof(int16_t));
      pendingCount = remaining;
      pendingOldestCapturedMs += 20;
    }
  }
}

inline void setup_queues_and_tasks() {
  LOGI("Creating queues and background tasks...");
  rawQueue  = xQueueCreate(RAW_QUEUE_LEN,  sizeof(RawChunk));
  opusQueue = xQueueCreate(OPUS_QUEUE_LEN, sizeof(OpusPacket));
  if (!rawQueue || !opusQueue) { LOGE("Failed to create one or more queues!"); return; }

  xTaskCreatePinnedToCore(audioReadTask,       "audioRead", 4096, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(audioProcessingTask, "audioProc", 8192, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(udpSendTask,         "udpSend",   8192, nullptr, 2, nullptr, 0);

  LOGI("Tasks created: audioReadTask, audioProcessingTask, udpSendTask");
}