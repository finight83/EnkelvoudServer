// ============================================================================
//  EVDEq.h -- 10-band peaking equaliser, master stream, before Opus.
//
//  Ported from the PC server's enkelvoud/eq.py so the phone dashboard and the
//  PC dashboard run the same equaliser:
//    * the same ISO octave band centres (31.5 Hz .. 16 kHz),
//    * the same +/-12 dB range, 0.5 dB steps,
//    * the same RBJ peaking-biquad maths with Q = 1.4142 (one octave),
//    * bands whose centre is above 0.9 x Nyquist are skipped, so the 16 kHz
//      band simply does nothing at a 24 kHz Opus rate,
//    * a flat EQ (or a disabled EQ) is bypassed entirely: the frame passes
//      through untouched and no CPU is spent.
//
//  Placement: audioProcessingTask() (core 1) runs applyEqFilters() on the
//  assembled frame BEFORE quiet mode, exactly like the PC chain
//  (EQ -> band-pass -> limiter). One frame is the whole unit of work: the
//  coefficients are stable across a frame and the biquad history is carried
//  from frame to frame in eqState().
//
//  Biquad topology: transposed direct form II (the same one FcBiquadState in
//  EVDAudio.h uses). Ten sections of it on 960 frames x 2 channels is ~19 k
//  multiply-adds per 20 ms frame; one core of an ESP32-S3 does that in well
//  under a millisecond.
//
//  Cross-core rule: the coefficients are only ever WRITTEN by the web task
//  (handleApiEq, under eqConfigMux) and only ever READ by the audio task.
//  A section is written as "a1, a2, b0, b1, b2" in that order and read back
//  the same way, so a reader can never see a new b0 with an old a1.
// ============================================================================
#pragma once
#include <Arduino.h>

// ---- Bands -----------------------------------------------------------------
// ISO octave centres, identical to EQ_BANDS in the PC's constants.py.
#define EQ_BAND_COUNT 10
static const float EQ_BAND_HZ[EQ_BAND_COUNT] = {
  31.5f, 63.0f, 125.0f, 250.0f, 500.0f,
  1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};
#define EQ_MAX_GAIN_DB   12.0f
// Q for a one-octave peaking section: sqrt(2^1) / (2^1 - 1) = 1.4142, the
// same constant the PC uses (enkelvoud/eq.py BAND_Q).
#define EQ_BAND_Q        1.41421356237f
// Below this a band is flat and its section is dropped (PC: _EPS = 0.05).
#define EQ_FLAT_DB       0.05f

// ---- One biquad section ----------------------------------------------------
struct EqSection {
  volatile float a1, a2;   // denominator (a0 normalised to 1)
  volatile float b0, b1, b2;
  float z1, z2;            // per-channel history lives in EqChannel below
  bool  active;
};

// History is per section and per channel: 10 x 2 x 2 floats.
struct EqChannel {
  float z1[EQ_BAND_COUNT];
  float z2[EQ_BAND_COUNT];
};

// [eq-presets] The curve names the dashboard offers, in dropdown order. The
// numbers themselves live in the dashboards (both servers carry the identical
// table), so a preset is a name plus a gain vector, not firmware data.
enum EqPreset : uint8_t {
  EQ_PRESET_CUSTOM = 0, EQ_PRESET_FLAT, EQ_PRESET_POP, EQ_PRESET_ROCK,
  EQ_PRESET_JAZZ, EQ_PRESET_CLASSICAL, EQ_PRESET_EDM, EQ_PRESET_VOCAL,
  EQ_PRESET_COUNT
};
static const char *EQ_PRESET_NAMES[EQ_PRESET_COUNT] = {
  "custom", "flat", "pop", "rock", "jazz", "classical", "edm", "vocal"
};

struct EqRuntime {
  volatile bool  enabled;                       // master on/off
  volatile float gains[EQ_BAND_COUNT];          // dB, -12..+12
  volatile uint8_t preset;                      // EqPreset of the live curve
  volatile float custom[EQ_BAND_COUNT];         // last hand-made curve
  volatile bool  dirty;                         // coefficients need rebuild
  EqSection      sec[EQ_BAND_COUNT];            // only [0..activeCount)
  volatile uint8_t activeCount;
  EqChannel      ch[2];                         // left, right
  uint32_t       fsConfigured;                  // rate the coefficients use
};

// Deliberately a global, not a pointer: the audio task must never dereference
// anything that the web task could have swapped out from under it.
static EqRuntime gEq;

// ---- Coefficient maths (RBJ peaking, identical to the PC's _peaking()) -----
inline void eqPeakingCoeffs(float fc, float gainDb, float fs,
                            float &b0, float &b1, float &b2,
                            float &a1, float &a2) {
  float A  = powf(10.0f, gainDb / 40.0f);
  float w0 = 6.28318530718f * fc / fs;
  float cw = cosf(w0), sw = sinf(w0);
  float alpha = sw / (2.0f * EQ_BAND_Q);
  float a0 = 1.0f + alpha / A;
  b0 = (1.0f + alpha * A) / a0;
  b1 = (-2.0f * cw) / a0;
  b2 = (1.0f - alpha * A) / a0;
  a1 = (-2.0f * cw) / a0;
  a2 = (1.0f - alpha / A) / a0;
}

// ---- Public API ------------------------------------------------------------
inline void eqInit() {
  for (int i = 0; i < EQ_BAND_COUNT; ++i) {
    gEq.gains[i]  = 0.0f;
    gEq.custom[i] = 0.0f;
    gEq.sec[i].active = false;
    gEq.sec[i].a1 = gEq.sec[i].a2 = 0.0f;
    gEq.sec[i].b0 = 1.0f; gEq.sec[i].b1 = gEq.sec[i].b2 = 0.0f;
  }
  for (int c = 0; c < 2; ++c)
    for (int i = 0; i < EQ_BAND_COUNT; ++i) gEq.ch[c].z1[i] = gEq.ch[c].z2[i] = 0.0f;
  gEq.enabled     = false;
  gEq.preset      = EQ_PRESET_CUSTOM;
  gEq.activeCount = 0;
  gEq.fsConfigured = 0;
  gEq.dirty       = true;
}

inline bool eqIsFlat() {
  for (int i = 0; i < EQ_BAND_COUNT; ++i)
    if (fabsf(gEq.gains[i]) >= EQ_FLAT_DB) return false;
  return true;
}

// Rebuilds the sections from gEq.gains at the given rate. Web task only.
// Called on a gain change, on an enable, and whenever the Opus rate changes.
inline void eqRebuild(uint32_t fs) {
  if (fs == 0) return;
  uint8_t n = 0;
  const float nyquist = (float)fs * 0.5f;
  for (int i = 0; i < EQ_BAND_COUNT; ++i) {
    float g = gEq.gains[i];
    if (g >  EQ_MAX_GAIN_DB) g =  EQ_MAX_GAIN_DB;
    if (g < -EQ_MAX_GAIN_DB) g = -EQ_MAX_GAIN_DB;
    gEq.gains[i] = g;
    // Same skip rule as the PC: a flat band, or a centre at/above 0.9 x
    // Nyquist (the 16 kHz band at 24 kHz), contributes nothing.
    if (fabsf(g) < EQ_FLAT_DB || EQ_BAND_HZ[i] >= nyquist * 0.9f) continue;
    float b0, b1, b2, a1, a2;
    eqPeakingCoeffs(EQ_BAND_HZ[i], g, (float)fs, b0, b1, b2, a1, a2);
    // Publish a1/a2 first: a reader that sees the new b0 has the new a1/a2.
    gEq.sec[n].a1 = a1;
    gEq.sec[n].a2 = a2;
    gEq.sec[n].b0 = b0;
    gEq.sec[n].b1 = b1;
    gEq.sec[n].b2 = b2;
    gEq.sec[n].active = true;
    n++;
  }
  for (int i = n; i < EQ_BAND_COUNT; ++i) gEq.sec[i].active = false;
  gEq.activeCount = n;
  // New coefficients make the old history meaningless - the PC's configure()
  // does exactly this reset for the same reason.
  for (int c = 0; c < 2; ++c)
    for (int i = 0; i < EQ_BAND_COUNT; ++i) gEq.ch[c].z1[i] = gEq.ch[c].z2[i] = 0.0f;
  gEq.fsConfigured = fs;
  gEq.dirty = false;
}

// Called from the audio task once per frame: picks up a rate change without
// the web task having to know about it.
inline void eqServiceRate(uint32_t fs) {
  if (fs != gEq.fsConfigured) gEq.dirty = true;
  if (gEq.dirty) eqRebuild(fs);
}

// Applies the equaliser to one interleaved stereo int16 frame, in place.
// Bypasses completely when disabled, flat, or with no active section, so a
// flat EQ costs nothing and leaves the samples bit-exact.
inline void applyEqFilters(int16_t *buf, uint32_t frames) {
  if (!gEq.enabled) return;
  if (gEq.dirty) eqServiceRate(g_opusSampleRate);
  const uint8_t n = gEq.activeCount;
  if (n == 0) return;

  for (uint32_t i = 0; i < frames; ++i) {
    float l = (float)buf[i * 2];
    float r = (float)buf[i * 2 + 1];
    // One section at a time, each section's history read from and written
    // back to gEq.ch - no per-frame register copy of ten sections.
    for (uint8_t s = 0; s < n; ++s) {
      const EqSection &sec = gEq.sec[s];
      const float b0 = sec.b0, b1 = sec.b1, b2 = sec.b2;
      const float a1 = sec.a1, a2 = sec.a2;
      // Left
      float z1 = gEq.ch[0].z1[s], z2 = gEq.ch[0].z2[s];
      float y = b0 * l + z1;
      z1 = b1 * l - a1 * y + z2;
      z2 = b2 * l - a2 * y;
      gEq.ch[0].z1[s] = z1; gEq.ch[0].z2[s] = z2;
      l = y;
      // Right
      z1 = gEq.ch[1].z1[s]; z2 = gEq.ch[1].z2[s];
      y = b0 * r + z1;
      z1 = b1 * r - a1 * y + z2;
      z2 = b2 * r - a2 * y;
      gEq.ch[1].z1[s] = z1; gEq.ch[1].z2[s] = z2;
      r = y;
    }
    // A boosted band can push past full scale; clamp instead of wrapping.
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
