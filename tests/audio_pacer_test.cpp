#include "../EVDPacer.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

static void testRepeatedLateWakeupsDoNotDropFrames() {
  AudioFramePacer pacer;
  uint32_t now = 1000;
  pacer.start(now);
  std::vector<uint32_t> sends;
  uint32_t drops = 0;

  for (uint32_t i = 0; i < 10000; ++i) {
    now += 4;  // simulated work after the prior frame
    int32_t wait = pacer.untilMs(now);
    if (wait > 0) now += (uint32_t)wait;
    now += 1 + (i % 5);  // every wakeup is 1-5 ms late
    bool accepted = pacer.prepareFrame(now, 20, 1, false);
    if (!accepted) {
      ++drops;
      continue;
    }
    sends.push_back(now);
  }

  assert(sends.size() == 10000);
  assert(drops == 0);
  assert(pacer.deadlineMs == 1000U + 10000U * 20U);
}

static void testStaleBacklogDrainDoesNotBurst() {
  AudioFramePacer pacer;
  uint32_t now = 0;
  pacer.start(now);
  uint32_t queued = 200;
  uint32_t drops = 0;
  std::vector<uint32_t> sends;
  int32_t maximumSentLate = 0;

  while (queued > 0) {
    int32_t wait = pacer.untilMs(now);
    if (wait > 0) now += (uint32_t)wait;
    int32_t late = pacer.latenessMs(now);
    if (!pacer.prepareFrame(now, 20, queued - 1, false)) {
      ++drops;
      --queued;
      continue;
    }

    if (late > maximumSentLate) maximumSentLate = late;
    sends.push_back(now);
    --queued;
    now += 24;  // simulated sender overload
  }

  assert(drops > 0);
  assert(maximumSentLate <= (int32_t)AUDIO_PACER_MAX_LAG_MS);
  for (size_t i = 1; i < sends.size(); ++i) {
    assert((uint32_t)(sends[i] - sends[i - 1]) >= 20U);
  }
}

static void testIdleReanchorRequiresAnEmptyQueue() {
  AudioFramePacer pacer;
  pacer.start(0);
  assert(pacer.prepareFrame(0, 20, 0, false));
  assert(pacer.deadlineMs == 20);
  assert(pacer.prepareFrame(1000, 20, 0, true));
  assert(pacer.deadlineMs == 1020);

  pacer.start(0);
  assert(pacer.prepareFrame(0, 20, 1, false));
  assert(!pacer.prepareFrame(1000, 20, 1, true));
  assert(pacer.deadlineMs == 40);
}

static void testRecoveryWhenStaleScheduleHasNoBacklog() {
  AudioFramePacer pacer;
  pacer.start(0);
  assert(pacer.prepareFrame(0, 20, 0, false));

  // A stall leaves the schedule behind, but new packets then arrive at the
  // normal frame rate with no backlog available to drain.
  uint32_t now = 1000;
  for (int i = 0; i < 500; ++i, now += 20) {
    assert(pacer.prepareFrame(now, 20, 0, false));
  }
  assert(pacer.deadlineMs == now);
}

static void testDrainReanchorsOnLastQueuedFrame() {
  AudioFramePacer pacer;
  pacer.start(0);
  assert(pacer.prepareFrame(0, 20, 0, false));
  assert(!pacer.prepareFrame(1000, 20, 2, false));
  assert(!pacer.prepareFrame(1000, 20, 1, false));
  assert(pacer.prepareFrame(1000, 20, 0, false));
  assert(pacer.deadlineMs == 1020);
}

static void testSampleTimelineDoesNotAccumulateRounding() {
  const uint32_t start = 0xfffffff0U;
  uint64_t frames = 0;
  uint32_t previousMs = audioSampleTimestampMs(start, frames, 48000);
  while (frames < 48000) {
    uint32_t chunk = (48000 - frames > 256) ? 256 : (uint32_t)(48000 - frames);
    frames += chunk;
    uint32_t stamp = audioSampleTimestampMs(start, frames, 48000);
    assert((int32_t)(stamp - previousMs) >= 0);
    previousMs = stamp;
  }
  assert(previousMs == start + 1000U);
}

static void testWrapSafeDeadlineArithmetic() {
  AudioFramePacer pacer;
  pacer.start(0xfffffff0U);
  assert(pacer.untilMs(0xffffffebU) == 5);
  assert(pacer.latenessMs(0xfffffff5U) == 5);
  pacer.advance(20);
  assert(pacer.deadlineMs == 4U);
  assert(pacer.latenessMs(8U) == 4);
}

int main() {
  testRepeatedLateWakeupsDoNotDropFrames();
  testStaleBacklogDrainDoesNotBurst();
  testIdleReanchorRequiresAnEmptyQueue();
  testRecoveryWhenStaleScheduleHasNoBacklog();
  testDrainReanchorsOnLastQueuedFrame();
  testSampleTimelineDoesNotAccumulateRounding();
  testWrapSafeDeadlineArithmetic();
  puts("PASS: integrated pacer jitter/backlog policy, DLNA sample timeline, and wraparound");
  return 0;
}
