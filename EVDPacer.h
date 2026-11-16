#pragma once
#include <stdint.h>

static const uint32_t AUDIO_PACER_MAX_LAG_MS = 100;

// Wrap-safe absolute frame schedule. The caller advances one frame slot for
// each sent or explicitly discarded packet; ordinary lateness never moves the
// schedule origin.
struct AudioFramePacer {
  bool initialized = false;
  uint32_t deadlineMs = 0;
  bool drainingStaleBacklog = false;

  void start(uint32_t nowMs) {
    deadlineMs = nowMs;
    initialized = true;
    drainingStaleBacklog = false;
  }

  int32_t untilMs(uint32_t nowMs) const {
    return (int32_t)(deadlineMs - nowMs);
  }

  int32_t latenessMs(uint32_t nowMs) const {
    return (int32_t)(nowMs - deadlineMs);
  }

  bool shouldReanchorAfterIdle(bool packetGap, uint32_t queuedFrames) const {
    return packetGap && queuedFrames == 0;
  }

  bool prepareFrame(uint32_t nowMs, uint32_t frameMs,
                    uint32_t queuedFrames, bool packetGap) {
    if (shouldReanchorAfterIdle(packetGap, queuedFrames)) start(nowMs);

    int32_t late = latenessMs(nowMs);
    if (late > (int32_t)AUDIO_PACER_MAX_LAG_MS) {
      drainingStaleBacklog = true;
    }
    if (drainingStaleBacklog && late > 0) {
      advance(frameMs);
      return false;
    }

    drainingStaleBacklog = false;
    advance(frameMs);
    return true;
  }

  void advance(uint32_t frameMs) {
    deadlineMs += frameMs;
  }
};

static inline uint32_t audioSampleTimestampMs(uint32_t startMs,
                                             uint64_t frames,
                                             uint32_t sampleRate) {
  return startMs + (uint32_t)(frames * 1000ULL / sampleRate);
}
