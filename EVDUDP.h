#pragma once
// ============================================================================
//  EVDUDP.h - UDP transport: audio multicast, ctrl push, clock sync
// ============================================================================
#include "EVDHelpers.h"
#include "EVDPacer.h"
#include <esp_timer.h>

// ---- ctrl-send mutex --------------------------------------------------------
// [v1.1.1-ctrl-mutex]
// Created once in setup_udp_transport() (single-threaded setup path).
// Serialises WiFiUDP beginPacket/write/endPacket on ctrlUdp because
// sendCtrlToSubscriber() is called from both Core 0 (registration/zone
// handlers) and Core 1 (loop-time path).
static SemaphoreHandle_t ctrlMutex = nullptr;

// ---- Socket setup ----------------------------------------------------------
inline void setup_udp_transport() {
  // Allocate outbound sockets. We never receive on these, so port 0 gives
  // us an ephemeral source port.
  audioUdp.begin(0);
  ctrlUdp.begin(0);

  // [v1.1.1-ctrl-mutex]
  // Create the ctrl-send mutex once, on the single-threaded setup path.
  // Previously it was lazily created inside sendCtrlToSubscriber(), which
  // runs on Core 0 (registration) and Core 1 (loop-time sync) concurrently;
  // two first-callers could each allocate and one mutex leaked, and the two
  // callers would then serialise on different mutexes.
  if (!ctrlMutex) ctrlMutex = xSemaphoreCreateMutex();  

  // clockSyncUdp is bound to a fixed, well-known port because players must
  // be able to *send* request datagrams to it (see CLOCK_SYNC_UDP_PORT docs
  // in EVDConfig.h). It is only ever touched from the Arduino loop() task
  // (Core 1), via handleClockSyncRequests(), so it needs no mutex.
  bool csOk = clockSyncUdp.begin(CLOCK_SYNC_UDP_PORT);
  LOGI("clockSyncUdp.begin(%d) = %d", CLOCK_SYNC_UDP_PORT, csOk ? 1 : 0);
  if (!csOk) {
    LOGE("clockSyncUdp.begin(%d) FAILED", CLOCK_SYNC_UDP_PORT);
  }

  LOGI("UDP transport ready: audio -> unicast:%d, ctrl -> unicast:%d, "
       "clock-sync -> unicast:%d",
       AUDIO_UDP_PORT, CTRL_UDP_PORT, CLOCK_SYNC_UDP_PORT);
}

// ---- Config push (unicast per subscriber) ----------------------------------
// Same reasoning as the audio path: WiFi multicast gets silently dropped by
// most APs, so we send one unicast packet to the target IP. The "target"
// field in the JSON still tells the node it is the intended recipient, which
// keeps the filtering logic in applyCtrlJson() working unchanged.
//
// This helper is called from two different cores:
//   - sendAuthoritativeTimeSync -> clockSyncTask (Core 0, every 30 s)
//   - broadcastNodeConfig      -> AsyncWebServer task (Core 0)
//   - sendZoneConfigToIp       -> AsyncWebServer task (Core 0)
//   - handlePlayerRegistration -> AsyncWebServer task (Core 0)
//
// WiFiUDP's internal packet-building state (write buffer, header, cursor) is
// not re-entrant. Two concurrent beginPacket/write/endPacket sequences on the
// same socket interleave their bytes and the receiver sees a corrupted
// datagram. A dedicated mutex serialises the writes so every packet goes out
// intact. Two lock attempts per second, held for microseconds each.

// [sub-dead-fast-skip] Was 45000. A dead node can block UDP sendto() for
// ARP timeout (~2s). 7s is > 3 missed sync_reqs, safely detecting a dead
// node before it stalls the pacer.
static constexpr uint32_t SUB_DEAD_LASTSEEN_MS   = 7000UL;
static constexpr uint8_t  SUB_FAILURE_SKIP_COUNT = 2;

// [sub-stall-cooldown] How long a subscriber that just failed a send is left
// alone before it is retried. This is the "drop the dead node in 3-4 s and
// leave everyone else alone" contract: 1 s, 2 s, 4 s, then 8 s while the
// failures keep coming. Kept well under SUB_DEAD_LASTSEEN_MS so a node that
// reboots inside the window is re-admitted by the very next retry.
static constexpr uint32_t SUB_STALL_COOLDOWN_MIN_MS = 1000UL;
static constexpr uint32_t SUB_STALL_COOLDOWN_MAX_MS = 8000UL;

// [wire-flag-unicast] AUDIO_HEADER_BYTES byte 3, bit 0. See udpSendTask().
static constexpr uint8_t  AUDIO_FLAG_UNICAST = 0x01;

inline void sendCtrlToSubscriber(const String &ip, const String &json) {
  if (WiFi.status() != WL_CONNECTED && !accessPointActive) return;

  // Respect the same back-off the audio path uses. If the audio send to
  // this subscriber has already declared it unreachable, don't keep
  // poking it with ctrl packets either — that would refill the same ARP
  // queue that the audio path is trying to drain.
  if (lockFor(subsMutex, 10)) {
    int slot = findSubscriberLocked(ip);
    if (slot >= 0 &&
        subscribers[slot].consecutiveSendFailures >= SUB_FAILURE_SKIP_COUNT) {
      unlockFor(subsMutex);
      return;
    }
    unlockFor(subsMutex);
  }

  IPAddress dst;
  if (!dst.fromString(ip)) { statUdpErrors++; return; }

  if (!ctrlMutex) { statUdpErrors++; return; }   // [v1.1.1-ctrl-mutex] created in setup
  if (xSemaphoreTake(ctrlMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    statUdpErrors++;
    return;
  }

  if (!ctrlUdp.beginPacket(dst, CTRL_UDP_PORT)) {
    statUdpErrors++;
    xSemaphoreGive(ctrlMutex);
    return;
  }
  ctrlUdp.write((const uint8_t *)json.c_str(), json.length());
  if (ctrlUdp.endPacket() != 1) statUdpErrors++;

  xSemaphoreGive(ctrlMutex);
}

// ---- Authoritative time broadcast -----------------------------------------
// Reinstates the periodic server-authoritative reference that the previous
// "Time sync heartbeat (REMOVED)" block in EVDPlayers.h deleted. That
// removal was justified on the theory that every node's RTT-derived offset
// would be fine forever after the first exchange. In practice the RTT
// estimate on a jittery WiFi link is systematically biased (upstream and
// downstream delays are not symmetric, and the median filter does not
// remove the mean of that asymmetry), so different nodes settle on
// different offsets and stay that way. This packet is the only thing in
// the system that can force them back to a common timeline.
//
// The node-side handler in EVDPNet.h applies this DIRECTLY to
// server_clock_offset_ms, bypassing its own filter, and forces a rebuffer
// if the correction is large enough to be audible. Between broadcasts the
// node's own RTT samples keep fine-tuning.
inline void sendAuthoritativeTimeSync() {
  if (WiFi.status() != WL_CONNECTED && !accessPointActive) return;
  if (!subsMutex) return;

  char buf[176];
  int n = snprintf(buf, sizeof(buf),
      "{\"type\":\"time_sync\",\"serverMs\":%lu,\"epoch\":%lu,"
      "\"protoVersion\":%d,\"authoritative\":true}",
      (unsigned long)millis(), (unsigned long)serverEpoch,
      AUDIO_PROTO_VERSION);
  if (n <= 0 || n >= (int)sizeof(buf)) return;
  String body(buf, (size_t)n);

  std::vector<SubscriberSnap> subs = snapshotSubscribers(millis());
  for (auto &s : subs) sendCtrlToSubscriber(s.ip, body);
}

// ---- Audio send task (per-subscriber unicast) ------------------------------
// [udp-disconnect-isolation]
// A subscriber that goes silent on the LAN still sits in the subscriber
// table until pruneStaleSubscribersLocked() removes it (PLAYER_STALE_
// TIMEOUT_MS = 70 s). Sending a UDP datagram to its IP blocks udpSendTask
// inside the lwIP ARP lookup for 1-2 s PER ATTEMPT, serially, so a single
// dead node starves every other node for the length of the retry window.
// With the previous MAX_SEND_FAILURES_BEFORE_SKIP == 10 that was 10-20 s
// of stutter per disconnect. Two pre-send filters prevent the stalls:
//   1. Skip a subscriber whose last heartbeat is older than 45 s.
//   2. Skip after 3 consecutive send failures (down from 10).
// Both are checked in the snapshot loop below, BEFORE any endPacket() is
// issued, so no ARP lookup is attempted for a node we already know is gone.
inline void udpSendTask(void *param) {
  (void)param;
  LOGI("udpSendTask started on core %d", xPortGetCoreID());
  OpusPacket pkt;
  uint32_t lastCleanupMs = 0;
  static uint8_t sendBuf[AUDIO_HEADER_BYTES + OPUS_MAX_PACKET_BYTES];

  struct SubEntry {
    char ip[16];
    bool compatible;
    uint8_t failures;
    bool synced;
    bool provisional;
    // [sub-stall-cooldown] True when this slot's cooldown has expired and the
    // flag still needs clearing under subsMutex. See the write block below.
    bool cooldownDue;
  };
  static SubEntry snapBuf[MAX_UDP_SUBSCRIBERS];
  bool sendOk[MAX_UDP_SUBSCRIBERS];
  static AudioFramePacer pacer;
  static bool hasLastPacerPacket = false;
  static uint32_t lastPacerPacketMs = 0;

  for (;;) {
    g_udpSendLoopCount++;

    if (otaPauseAudio) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    // [servers-decide] A standby must not stream: its nodes belong to the
    // elected leader. Drain the queue so the encoder's output never backs up
    // (a backed-up opusQueue is what stalls the encoder), but send nothing.
    if (!g_peerIsLeader) {
      if (xQueueReceive(opusQueue, &pkt, pdMS_TO_TICKS(20)) == pdTRUE) {
        statOpusDropped++;
      }
      continue;
    }
    if (xQueueReceive(opusQueue, &pkt, portMAX_DELAY) != pdTRUE) continue;
    uint32_t packetArrivalMs = millis();

    // [udp-stage-afterRecv-pre-mute] Move afterRecv above the mute gate so
    // the counter keeps climbing during a master mute — this proves the
    // task is running and only skipping sends, not blocked. afterRecv
    // climbing while afterStore freezes is the unambiguous "muted, not
    // stalled" fingerprint.
    g_udpStage_afterRecv++;

    if (masterMuted) continue;

    g_udpStage_afterStore++;
    uint32_t nowMs = millis();
    if (nowMs - lastCleanupMs >= 1000) {
      lastCleanupMs = nowMs;
      if (lockFor(subsMutex, 50)) {
        pruneStaleSubscribersLocked(nowMs);
        unlockFor(subsMutex);
      }
    }

    nowMs = packetArrivalMs;
    bool packetGap = hasLastPacerPacket &&
        (uint32_t)(nowMs - lastPacerPacketMs) > AUDIO_PACER_MAX_LAG_MS;
    lastPacerPacketMs = nowMs;
    hasLastPacerPacket = true;
    if (!pacer.initialized) pacer.start(nowMs);

    int32_t untilMs = pacer.untilMs(nowMs);
    if (untilMs > 0) {
      uint32_t waitMs = (uint32_t)untilMs;
      if (waitMs > 100) waitMs = 100;
      vTaskDelay(pdMS_TO_TICKS(waitMs));
    }

    // Keep an absolute 20 ms frame schedule. Normal send jitter never shifts
    // its phase. When queued work is stale, drain it until the schedule is
    // current rather than transmitting a catch-up burst.
    uint32_t postDelayMs = millis();
    int32_t driftMs = pacer.latenessMs(postDelayMs);
    uint32_t queuedFrames = opusQueue
        ? (uint32_t)uxQueueMessagesWaiting(opusQueue) : 0;
    if (!pacer.prepareFrame(postDelayMs, OPUS_FRAME_MS,
                            queuedFrames, packetGap)) {
      static uint32_t lastStallLogMs = 0;
      if (millis() - lastStallLogMs > 5000) {
        lastStallLogMs = millis();
        LOGW("pacer dropping stale frame: drift=%ld ms  opusQ=%u/%d",
             (long)driftMs, (unsigned)queuedFrames, (int)OPUS_QUEUE_LEN);
      }
      g_udpSendStallResets++;
      g_udpPacerDriftReset++;
      g_udpPacerBehindReset++;
      statOpusDropped++;
      continue;
    }

    uint16_t magic = AUDIO_PROTO_MAGIC;
    uint32_t seq   = pkt.seq;
    uint32_t ts    = pkt.captured_ms;
    sendBuf[0]  = (uint8_t)( magic       & 0xFF);
    sendBuf[1]  = (uint8_t)((magic >> 8) & 0xFF);
    sendBuf[2]  = (uint8_t)AUDIO_PROTO_VERSION;
    // [wire-flag-unicast]
    // Audio is delivered by unicast (see [udp-disconnect-isolation]), so every
    // node on the LAN receives every frame addressed to any node. The flag
    // byte has been 0 since v2; bit 0 now marks a unicast delivery so the
    // player can drop frames that were not addressed to it instead of
    // decoding a second copy and desynchronising its own timeline. The
    // server gates every send on the subscriber table, so this is always set.
    sendBuf[3]  = AUDIO_FLAG_UNICAST;
    sendBuf[4]  = (uint8_t)( seq        & 0xFF);
    sendBuf[5]  = (uint8_t)((seq >>  8) & 0xFF);
    sendBuf[6]  = (uint8_t)((seq >> 16) & 0xFF);
    sendBuf[7]  = (uint8_t)((seq >> 24) & 0xFF);
    sendBuf[8]  = (uint8_t)( ts        & 0xFF);
    sendBuf[9]  = (uint8_t)((ts >>  8) & 0xFF);
    sendBuf[10] = (uint8_t)((ts >> 16) & 0xFF);
    sendBuf[11] = (uint8_t)((ts >> 24) & 0xFF);
    memcpy(sendBuf + AUDIO_HEADER_BYTES, pkt.data, pkt.len);
    size_t frameLen = AUDIO_HEADER_BYTES + (size_t)pkt.len;

    uint32_t latency = millis() - pkt.captured_ms;

    if (WiFi.status() != WL_CONNECTED && !accessPointActive) continue;

    int nCompatible   = 0;
    int nIncompatible = 0;
    int nSubs         = 0;

    // [subs-lock-timeout-fallback]
    // A failed 30 ms subsMutex acquisition leaves nSubs == 0 and drops the
    // whole frame for every subscriber — a clean 20 ms click. subsMutex is
    // held briefly by AsyncTCP registration/heartbeat/broadcastNodeConfig
    // (Core 0) and by clockSyncTask (Core 0, pri 3, EQUAL to this task's
    // pri 3, so it blocks us from preempting). A single String assign inside
    // findOrCreateSubscriberLocked() while AsyncTCP happens to be mid-alloc
    // is enough to exceed 30 ms. Fix: cache the last good snapshot and use
    // it when the lock fails; the alternative is to drop the frame
    // unconditionally, which is strictly worse.
    static SubEntry lastGoodSnap[MAX_UDP_SUBSCRIBERS];
    static int      lastGoodN  = 0;
    static uint32_t lastGoodMs = 0;
    static uint32_t subsLockTimeouts = 0;

    bool lockOk = lockFor(subsMutex, 30);
    if (lockOk) {
      uint32_t nowSnap = millis();
      for (int i = 0; i < subscriberTableCount && nSubs < MAX_UDP_SUBSCRIBERS; i++) {
        if ((nowSnap - subscribers[i].lastSeenMs) > SUB_DEAD_LASTSEEN_MS) {
          continue;
        }
        if (subscribers[i].consecutiveSendFailures >= SUB_FAILURE_SKIP_COUNT) {
          continue;
        }
        // [sub-stall-cooldown] A node that has been skipped because its sends
        // stalled is retried once per cooldown, NOT on every frame. Without
        // this the retry itself is the stall: a node that was switched off at
        // the wall sits in the ARP table as "incomplete", every sendto()
        // blocks the pacer for the full ARP timeout, and the surviving nodes
        // stutter for the whole detection window. Skipping it until the
        // cooldown expires costs nothing (it gets no audio either way) and
        // keeps the pacer's deadline for everyone else.
        if (subscribers[i].cooldownUntilMs != 0 &&
            (int32_t)(nowSnap - subscribers[i].cooldownUntilMs) < 0) {
          statAudioCooldownSkips++;
          continue;
        }
        strncpy(snapBuf[nSubs].ip, subscribers[i].ip.c_str(),
                sizeof(snapBuf[nSubs].ip) - 1);
        snapBuf[nSubs].ip[sizeof(snapBuf[nSubs].ip) - 1] = '\0';
        snapBuf[nSubs].compatible = subscribers[i].audioCompatible;
        snapBuf[nSubs].failures   = subscribers[i].consecutiveSendFailures;
        snapBuf[nSubs].cooldownDue =
            (subscribers[i].cooldownUntilMs != 0 &&
             (int32_t)(nowSnap - subscribers[i].cooldownUntilMs) >= 0);
        snapBuf[nSubs].synced     = subscribers[i].hasCompletedClockSync;
        snapBuf[nSubs].provisional = subscribers[i].provisional;
        if (subscribers[i].audioCompatible) nCompatible++;
        else                                nIncompatible++;
        nSubs++;
      }
      unlockFor(subsMutex);
      lastGoodN  = nSubs;
      lastGoodMs = millis();
      if (nSubs > 0) memcpy(lastGoodSnap, snapBuf, sizeof(SubEntry) * nSubs);
    } else {
      subsLockTimeouts++;
      // Rate-limited diagnostic: LOGW on this task blocks the pacer for
      // ~9 ms at 115200 baud, which is worse than the frame drop we are
      // fixing. Only fire once per 5 s, and only at verbose level.
      static uint32_t lastTimeoutLogMs = 0;
      if (millis() - lastTimeoutLogMs > 5000) {
        lastTimeoutLogMs = millis();
        LOGW("udpSendTask: subsMutex timeout #%u; using cached snapshot "
             "(n=%d, age=%lu ms)",
             (unsigned)subsLockTimeouts, lastGoodN,
             (unsigned long)(millis() - lastGoodMs));
      }
      // Fall back to cached snapshot if fresh enough. 5 s is longer than
      // any legitimate lock hold, shorter than PLAYER_STALE_TIMEOUT_MS.
      if (lastGoodN > 0 && (millis() - lastGoodMs) < 5000) {
        memcpy(snapBuf, lastGoodSnap, sizeof(SubEntry) * lastGoodN);
        nSubs = lastGoodN;
        for (int si = 0; si < nSubs; si++) {
          if (snapBuf[si].compatible) nCompatible++;
          else                        nIncompatible++;
        }
      }
    }
    if (nSubs == 0) continue;

    // ---- All-compatible path -------------------------------------------
    if (nIncompatible == 0) {
      bool anyOk = false;
      for (int si = 0; si < nSubs; si++) {
        sendOk[si] = false;
        if (!snapBuf[si].synced) {
          statAudioSyncPending++;
          continue;
        }
        if (snapBuf[si].failures >= MAX_SEND_FAILURES_BEFORE_SKIP) continue;
        IPAddress dst;
        if (!dst.fromString(snapBuf[si].ip)) continue;
        bool ok = false;
        if (audioUdp.beginPacket(dst, AUDIO_UDP_PORT)) {
          audioUdp.write(sendBuf, frameLen);
          ok = (audioUdp.endPacket() == 1);
        }
        sendOk[si] = ok;
        if (ok) anyOk = true;
        else    statUdpErrors++;
      }
      if (!anyOk) vTaskDelay(pdMS_TO_TICKS(1));
      bool attempted[MAX_UDP_SUBSCRIBERS];
      for (int si = 0; si < nSubs; si++) {
        attempted[si] = snapBuf[si].synced &&
                        (snapBuf[si].failures < MAX_SEND_FAILURES_BEFORE_SKIP);
      }
      bool onlyAttempts[MAX_UDP_SUBSCRIBERS];
      int  onlyIdx[MAX_UDP_SUBSCRIBERS];
      int  onlyN = 0;
      for (int si = 0; si < nSubs; si++) {
        if (!attempted[si]) continue;
        onlyAttempts[onlyN] = sendOk[si];
        onlyIdx[onlyN]      = si;
        onlyN++;
      }
      // [udpsend-skip-second-lock-on-clean-frame]
      // The second subsMutex acquisition fires once per frame just to
      // reset failure counters. On a healthy fleet (all sends succeed,
      // no prior failures) there is nothing to write, so the lock is
      // pure overhead. Field measurement: 50 frames/s × 3 subscribers,
      // ~150 sendto()/s, 12% core-0 CPU — roughly 2-3% of that is the
      // second lock. Skip it when there is genuinely nothing to update.
      //
      // This is a micro-optimisation, not the root cause of the 12%.
      // The real CPU consumer is the 150 sendto() syscalls and the
      // per-frame snapshot memcpy, neither of which can be avoided
      // without restructuring the pacer. See blocker 1.3 for the
      // encoder CPU issue that actually matters.
      bool anyFailure   = false;
      bool anyNeedReset = false;
      for (int k = 0; k < onlyN; k++) {
        int si = onlyIdx[k];
        if (!onlyAttempts[k])                anyFailure   = true;
        else if (snapBuf[si].failures > 0)   anyNeedReset = true;
      }
      // [sub-stall-cooldown] A cooldown is a MUTATION of subscriber state, so
      // it has to be written under subsMutex — the gate at the top of this
      // function reads it unlocked on the snapshot, which is only safe if the
      // write is never lost. The block below used to run only on a failure or
      // a reset, so a cooldown that had just EXPIRED was never cleared: the
      // subscriber sat out one extra send after every cooldown, and the field
      // stayed set while the node looked healthy. Detect it in the snapshot.
      bool anyCooldownDue = false;
      for (int k = 0; k < onlyN; k++) {
        if (onlyAttempts[k] && snapBuf[onlyIdx[k]].cooldownDue) {
          anyCooldownDue = true;
          break;
        }
      }
      if (onlyN > 0 && (anyFailure || anyNeedReset || anyCooldownDue)) {
        if (lockFor(subsMutex, 20)) {
          for (int k = 0; k < onlyN; k++) {
            int si = onlyIdx[k];
            int slot = findSubscriberLocked(snapBuf[si].ip);
            if (slot < 0) continue;
            if (onlyAttempts[k]) {
              subscribers[slot].consecutiveSendFailures = 0;
              subscribers[slot].cooldownUntilMs = 0;
            } else if (snapBuf[si].cooldownDue) {
              // Cooldown expired, this send still failed: keep the count so the
              // back-off keeps growing. [sub-stall-cooldown]
              subscribers[slot].cooldownUntilMs = 0;
            } else {
              if (subscribers[slot].consecutiveSendFailures < 255)
                subscribers[slot].consecutiveSendFailures++;
              // [sub-stall-cooldown] Exponential-ish back-off, capped. The
              // first skip costs one ARP timeout (~2 s); every later one costs
              // a few, so a node that comes back is picked up within a second
              // or two while a node that is gone stops costing anything.
              uint32_t cd = SUB_STALL_COOLDOWN_MIN_MS;
              for (uint8_t f = 1; f < subscribers[slot].consecutiveSendFailures && f < 4; f++)
                cd *= 2;
              if (cd > SUB_STALL_COOLDOWN_MAX_MS) cd = SUB_STALL_COOLDOWN_MAX_MS;
              subscribers[slot].cooldownUntilMs = millis() + cd;
            }
          }
          unlockFor(subsMutex);
        }
      }
      // [udp-stats-per-sub] Count one datagram per successful subscriber,
      // not one per frame. With 3 nodes the old code reported 50/s instead
      // of 150/s. Latency stats stay per-frame (one sample per frame).
      int nOkSubs = 0;
      for (int si = 0; si < nSubs; si++) if (sendOk[si]) nOkSubs++;
      if (nOkSubs > 0) {
        portENTER_CRITICAL(&gStatsMux);
          statUdpPacketsSent += (uint32_t)nOkSubs;
          statUdpBytesSent   += (uint32_t)frameLen * (uint32_t)nOkSubs;
          statLatencySumMs   += latency;
          statLatencyCount++;
        portEXIT_CRITICAL(&gStatsMux);
      }
      continue;
    }

    // ---- Mixed-environment fallback: per-subscriber unicast -------------
    bool anyOk = false;
    bool attempted[MAX_UDP_SUBSCRIBERS];
    for (int si = 0; si < nSubs; si++) {
      sendOk[si]    = false;
      attempted[si] = false;
      if (!snapBuf[si].compatible) {
        if (snapBuf[si].provisional) statAudioProvisionalSkips++;
        else                         statAudioIncompatibleSkips++;
        continue;
      }
      if (!snapBuf[si].synced) {
        statAudioSyncPending++;
        continue;
      }
      if (snapBuf[si].failures >= MAX_SEND_FAILURES_BEFORE_SKIP) continue;
      IPAddress dst;
      if (!dst.fromString(snapBuf[si].ip)) continue;
      attempted[si] = true;
      bool ok = false;
      if (audioUdp.beginPacket(dst, AUDIO_UDP_PORT)) {
        audioUdp.write(sendBuf, frameLen);
        ok = (audioUdp.endPacket() == 1);
      }
      sendOk[si] = ok;
      if (ok) anyOk = true;
      else    statUdpErrors++;
    }
    {
      bool anyAttempt = false;
      for (int si = 0; si < nSubs; si++) if (attempted[si]) { anyAttempt = true; break; }
      if (anyAttempt && lockFor(subsMutex, 20)) {
        for (int si = 0; si < nSubs; si++) {
          if (!attempted[si]) continue;
          int slot = findSubscriberLocked(snapBuf[si].ip);
          if (slot < 0) continue;
          if (sendOk[si]) {
            subscribers[slot].consecutiveSendFailures = 0;
            subscribers[slot].cooldownUntilMs = 0;
          } else {
            if (subscribers[slot].consecutiveSendFailures < 255)
              subscribers[slot].consecutiveSendFailures++;
            uint32_t cd = SUB_STALL_COOLDOWN_MIN_MS;
            for (uint8_t f = 1; f < subscribers[slot].consecutiveSendFailures && f < 4; f++)
              cd *= 2;
            if (cd > SUB_STALL_COOLDOWN_MAX_MS) cd = SUB_STALL_COOLDOWN_MAX_MS;
            subscribers[slot].cooldownUntilMs = millis() + cd;
          }
        }
        unlockFor(subsMutex);
      }
    }
    {
      int nOkSubs = 0;
      for (int si = 0; si < nSubs; si++) if (sendOk[si]) nOkSubs++;
      if (nOkSubs > 0) {
        portENTER_CRITICAL(&gStatsMux);
          statUdpPacketsSent += (uint32_t)nOkSubs;
          statUdpBytesSent   += (uint32_t)frameLen * (uint32_t)nOkSubs;
          statLatencySumMs   += latency;
          statLatencyCount++;
        portEXIT_CRITICAL(&gStatsMux);
      } else {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }
  }
}

// ---- Clock sync (request/response, UDP, CLOCK_SYNC_UDP_PORT) ---------------
// See the wire-format doc block in EVDConfig.h for the JSON request/response
// shape and the RTT/offset math the player performs with it. This handler
// runs only from clockSyncTask() (Core 0, priority 3, single-threaded with
// itself), so it needs no mutex around clockSyncUdp - unlike ctrlUdp,
// nothing else ever touches this socket. clockSyncTask() polls it every
// ~1 ms (vTaskDelay(1)); that is the dominant source of server-side jitter
// in serverRxMs/serverTxMs, which is far tighter than the one-way time_sync
// heartbeat it supersedes as the authoritative exchange.
// Small fixed-size per-source cooldown table so a misbehaving/flooding client
#define SYNC_RATE_TABLE_SIZE 16
#define SYNC_MIN_INTERVAL_MS 20
struct SyncRateEntry { bool occupied = false; uint32_t ip = 0; uint32_t lastMs = 0; };
static SyncRateEntry gSyncRateTable[SYNC_RATE_TABLE_SIZE];

// Returns true if a request from ipKey should be served now (and records it).
// Eviction is least-recently-used by `lastMs`: unoccupied slots default to
// lastMs == 0, so they are always picked first (before evicting any slot
// that has actually been used), which is the intended "prefer a free slot,
// otherwise evict whoever has been quietest the longest" behavior.
inline bool syncRateAllow(uint32_t ipKey, uint32_t nowMs) {
  int freeSlot = -1;
  int oldestSlot = 0;
  for (int i = 0; i < SYNC_RATE_TABLE_SIZE; i++) {
    if (gSyncRateTable[i].occupied && gSyncRateTable[i].ip == ipKey) {
      if ((uint32_t)(nowMs - gSyncRateTable[i].lastMs) < SYNC_MIN_INTERVAL_MS) return false;
      gSyncRateTable[i].lastMs = nowMs;
      return true;
    }
    if (!gSyncRateTable[i].occupied && freeSlot < 0) freeSlot = i;
    if (gSyncRateTable[i].lastMs < gSyncRateTable[oldestSlot].lastMs) oldestSlot = i;
  }
  int slot = (freeSlot >= 0) ? freeSlot : oldestSlot;
  gSyncRateTable[slot].occupied = true;
  gSyncRateTable[slot].ip       = ipKey;
  gSyncRateTable[slot].lastMs   = nowMs;
  return true;
}

inline void handleClockSyncRequests() {
  int sz = clockSyncUdp.parsePacket();
  if (sz <= 0) return;
  // Timestamp receipt before reading/deserializing the payload. Recording it
  // after JSON parsing makes processing time part of the server-side clock
  // offset and biases the RTT exchange asymmetrically.
  const uint32_t serverRxMs =
      (uint32_t)(esp_timer_get_time() / 1000ULL);

  uint8_t buf[160];
  int n = clockSyncUdp.read(buf, sizeof(buf) - 1);
  if (n <= 0) { statSyncRequestErrors++; return; }
  buf[n] = '\0';

  IPAddress remote     = clockSyncUdp.remoteIP();
  uint16_t  remotePort = clockSyncUdp.remotePort();

  if (!syncRateAllow((uint32_t)remote, millis())) {
    statSyncRequestsThrottled++;
    return;
  }

  // Stack-allocated (not Dynamic) so repeated requests never touch the heap;
  // 192 bytes comfortably covers the small fixed sync_req/sync_resp shape.
  StaticJsonDocument<192> in;
  const char *reqType = nullptr;
  if (deserializeJson(in, buf, (size_t)n) == DeserializationError::Ok) {
    reqType = in["type"] | (const char *)nullptr;
  }
  if (!reqType || strcmp(reqType, "sync_req") != 0) {
    statSyncRequestErrors++;
    return;
  }

  uint32_t clientTxMs = (uint32_t)(in["clientTxMs"] | 0UL);
  char out[160];
  // Stamp transmit as late as possible before handing the datagram to lwIP.
  const uint32_t serverTxMs =
      (uint32_t)(esp_timer_get_time() / 1000ULL);
  int len = snprintf(out, sizeof(out),
      "{\"type\":\"sync_resp\",\"protoVersion\":%d,\"epoch\":%lu,"
      "\"clientTxMs\":%lu,\"serverRxMs\":%lu,\"serverTxMs\":%lu}",
      AUDIO_PROTO_VERSION, (unsigned long)serverEpoch,
      (unsigned long)clientTxMs, (unsigned long)serverRxMs,
      (unsigned long)serverTxMs);
  if (len <= 0 || len >= (int)sizeof(out)) { statSyncRequestErrors++; return; }

  if (clockSyncUdp.beginPacket(remote, remotePort)) {
    clockSyncUdp.write((const uint8_t *)out, (size_t)len);
    if (clockSyncUdp.endPacket() == 1) {
      statSyncRequestsServed++;
      // [sync-promote-after-send]
      // Promote to audio-eligible only AFTER the sync_resp actually left
      // the socket. A failed send must not mark the node as synced — that
      // would start audio flow on the coarse one-shot seed, which is exactly
      // what the hasCompletedClockSync gate exists to prevent.
      String remoteStr = remote.toString();
      bool promoted = false;
      if (lockFor(subsMutex, 20)) {
        int slot = findSubscriberLocked(remoteStr);
        if (slot < 0) {
          // [sync-before-heartbeat-provisional-slot]
          // A node's sync_req can arrive BEFORE its heartbeat (boot race,
          // post-prune, etc.). Previously we created the slot with
          // audioCompatible=false, so udpSendTask SKIPPED every frame to
          // that node until its first /api/player/register arrived
          // (typically 700 ms–2 s per boot). Each skipped 20 ms frame
          // starved the node's playback buffer, forcing it to rebuffer —
          // and each rebuffer accumulates ~50 ms of permanent playout
          // delay, which is exactly the "audio slowly drifts behind"
          // symptom operators report after a few boot cycles.
          //
          // A node that speaks the sync_req protocol already speaks
          // AUDIO_PROTO_VERSION 2+. Assume compatible; if its next
          // heartbeat declares an OLD version, the update path in
          // findOrCreateSubscriberLocked() overwrites audioCompatible to
          // false on the SAME heartbeat tick, so the assumption is
          // self-correcting and the risk window is bounded to one
          // heartbeat interval.
          slot = findOrCreateSubscriberLocked(remoteStr, remoteStr,
                                              AUDIO_PROTO_VERSION,
                                              AUDIO_PROTO_VERSION, true);
          // [provisional-classification] Mark so stats can still
          // distinguish "assumed-compatible" from "heartbeat-confirmed".
          // Cleared by handlePlayerRegistration() on first real heartbeat.
          if (slot >= 0) subscribers[slot].provisional = true;
        }
        if (slot >= 0) {
          // [sync-refresh-lastseen]
          // Refresh lastSeenMs on every sync_req. Previously the subscriber
          // could be pruned at PLAYER_STALE_TIMEOUT_MS (70 s) if the HTTP
          // heartbeat path hiccupped even though the UDP sync path kept
          // working — after which the next sync_req recreated a provisional
          // slot and started a fresh audio-skip window. Every
          // prune/recreate cycle starved the node's buffer once more,
          // adding another ~50 ms of permanent playout delay. Refreshing
          // here keeps the slot alive as long as the node is really
          // reachable, independent of which transport happens to be
          // healthy.
          subscribers[slot].lastSeenMs = millis();
          // A sync_req proves the node is reachable right now; without this
          // two transient send failures mute it until the next 30 s heartbeat.
          subscribers[slot].consecutiveSendFailures = 0;
          if (!subscribers[slot].hasCompletedClockSync) {
            subscribers[slot].hasCompletedClockSync = true;
            promoted = true;
          }
        }
        unlockFor(subsMutex);
      }
      if (promoted) {
        LOGI("Clock sync: %s promoted to audio-eligible (first RTT sync)",
            remoteStr.c_str());
      }
    } else {
      statSyncRequestErrors++;
    }
  } else {
    statSyncRequestErrors++;
  }
}

// ---- Dedicated clock-sync task --------------------------------------------
// Previously handleClockSyncRequests() was called from loop() on Core 1.
// loop() also does discovery, NVS writes (the settingsDirty block can block
// on a flash erase for ~100 ms), Opus complexity
// tuning, receiver UART parsing and stats dumps. Any of those blocking
// operations delays the parsePacket()/send() sequence that builds
// serverRxMs/serverTxMs, and that delay is directly observable on the
// player as RTT jitter of 40-80 ms. The player's clock offset estimate
// comes from that RTT, and the playout deadline comes from the offset,
// which is why nearly every frame ends up 20-100 ms late.
//
// Priority 4 is above the Arduino loop (1) and above udpSendTask (2), so a
// sync response is never starved by a slow send. It is below the WiFi task,
// so radio servicing still wins. The 1 ms vTaskDelay is essentially free:
// the player sends one sync_req every 2 s, so 99.95 % of polls return
// immediately from parsePacket()==0.
inline void clockSyncTask(void *param) {
    (void)param;
    LOGI("clockSyncTask started on core %d", xPortGetCoreID());
    uint32_t lastAuthSyncMs = 0;
    for (;;) {
        // [ota-pause-clock] Completely halt clock sync during OTA.
        if (otaPauseAudio) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        handleClockSyncRequests();
        uint32_t now = millis();
        if (now - lastAuthSyncMs >= 30000) {
            lastAuthSyncMs = now;
            sendAuthoritativeTimeSync();
        }
        // [clocksync-poll-5ms]
        // vTaskDelay(1) = 1000 wakeups/sec, but sync requests arrive at
        // ~1.58/sec (62485 in 39551s of server uptime). That is 630x
        // oversampling and was measured at 5.7% of core 0 in the field.
        //
        // 5 ms poll = 200 wakeups/sec = 5x CPU reduction. RTT accuracy
        // is unaffected: serverRxMs and serverTxMs are both captured
        // AFTER this delay, so the delay cancels out of the
        // (serverTxMs - serverRxMs) term. The only effect is that the
        // node's RTT measurement includes up to 5 ms extra round-trip,
        // which the min-RTT-of-8 filter absorbs.
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}