#pragma once
// ============================================================================
//  EVDUDP.h - UDP transport: audio multicast, ctrl push, OGG test stream
// ============================================================================
#include "EVDHelpers.h"
#include <esp_timer.h>

// ---- Forward declarations --------------------------------------------------
// udpSendTask references storeOggTestPacket, which is defined further down.
// Declare it here so the task compiles.
inline void storeOggTestPacket(const OpusPacket &packet);
inline void writeOggPage(AsyncResponseStream *response, const uint8_t *packet,
                         size_t packetLength, uint32_t serial, uint32_t sequence,
                         uint64_t granulePosition, uint8_t headerType);
inline void handleOggTestStream(AsyncWebServerRequest *request);

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
//   - sendAuthoritativeTimeSync -> clockSyncTask (Core 1, every 30 s)
//   - broadcastNodeConfig      -> AsyncWebServer task (Core 0)
//   - sendZoneConfigToIp       -> AsyncWebServer task (Core 0)
//   - handlePlayerRegistration -> AsyncWebServer task (Core 0)
//
// WiFiUDP's internal packet-building state (write buffer, header, cursor) is
// not re-entrant. Two concurrent beginPacket/write/endPacket sequences on the
// same socket interleave their bytes and the receiver sees a corrupted
// datagram. A dedicated mutex serialises the writes so every packet goes out
// intact. Two lock attempts per second, held for microseconds each.

inline void sendCtrlToSubscriber(const String &ip, const String &json) {
  if (WiFi.status() != WL_CONNECTED && !accessPointActive) return;

  // Respect the same back-off the audio path uses. If the audio send to
  // this subscriber has already declared it unreachable, don't keep
  // poking it with ctrl packets either — that would refill the same ARP
  // queue that the audio path is trying to drain.
  if (lockFor(subsMutex, 10)) {
    int slot = findSubscriberLocked(ip);
    if (slot >= 0 &&
        subscribers[slot].consecutiveSendFailures >= MAX_SEND_FAILURES_BEFORE_SKIP) {
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
// One datagram per audio frame per audio-compatible subscriber. Unicast is
// the shipped transport, not a fallback: multicast over 802.11 is sent at
// the lowest basic rate (1 Mbps) and buffered by the AP until the next
// DTIM beacon, which on consumer routers is typically 100 ms. That is the
// historical cause of the "packets arrive in ~100 ms batches, ~34/s
// instead of 50/s" symptom this pipeline was rewritten to eliminate.
// Unicast does not go through the DTIM buffer, transmits at the negotiated
// MCS, and is acknowledged at Layer 2.
//
// With MAX_UDP_SUBSCRIBERS capped at 32 the per-frame wire cost is a few
// hundred bytes times the compatible-subscriber count. Each frame issues
// one beginPacket/write/endPacket per subscriber and the protocol is
// stateless, so there is no longer a separate "multicast primary" path —
// every compatible subscriber gets exactly the same v2 header bytes.
//
// Fail-closed policy (EVDProtocolCompat.h): a subscriber that has not
// declared a compatible protocol version is skipped entirely. The gate
// lives inside the send loop below, not in a separate fallback branch.
//
// [new-node-sync-gate]: additionally, a subscriber that has not completed
// its first RTT clock-sync exchange is skipped (the counter statAudioSync-
// Pending tracks those skips separately from statAudioIncompatibleSkips so
// the two conditions are distinguishable in /api/stats).
//
// Per-subscriber delivery counters (attempted / delivered / skipped) were
// removed: they required a second subsMutex acquisition per frame and only
// ever reported whether the local WiFi stack accepted the datagram (UDP
// has no acknowledgement). The node's own /api/node_stats packet counters
// report the same information from the receiving end, which is the only
// end that can observe a loss.

inline void udpSendTask(void *param) {
  (void)param;
  LOGI("udpSendTask started on core %d", xPortGetCoreID());
  OpusPacket pkt;
  uint32_t lastCleanupMs = 0;
  static uint8_t sendBuf[AUDIO_HEADER_BYTES + OPUS_MAX_PACKET_BYTES];

  // Fixed-capacity snapshot. Used only on the mixed-environment unicast
  // fallback path below; the pure-compatible path never touches it.
  struct SubEntry { char ip[16]; bool compatible; uint8_t failures; bool synced; };
  static SubEntry snapBuf[MAX_UDP_SUBSCRIBERS];

  // Per-frame failure accounting is inlined in each send path below
  // (all-compatible and mixed-environment). A successful send resets
  // consecutiveSendFailures to 0; a failed send increments it (saturating
  // at 255). Skipped subscribers (unsynced, or already backed off) are
  // excluded so their counters do not drift from what the send loop
  // actually observed.
  bool sendOk[MAX_UDP_SUBSCRIBERS];

  for (;;) {
    if (otaPauseAudio) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    if (xQueueReceive(opusQueue, &pkt, portMAX_DELAY) != pdTRUE) continue;

    // [v1.1.1-mute-gate]
    // Master mute must gate *every* consumer of the pipeline, not just the
    // UDP audio path. Previously storeOggTestPacket/evdLiveBroadcastPacket
    // ran before this check, so /live and /stream.ogg kept playing audio at
    // full level while the dashboard showed "muted".
    if (masterMuted) continue;

    storeOggTestPacket(pkt);
    evdLiveBroadcastPacket(pkt);

    uint32_t nowMs = millis();
    if (nowMs - lastCleanupMs >= 1000) {
      lastCleanupMs = nowMs;
      if (lockFor(subsMutex, 50)) {
        pruneStaleSubscribersLocked(nowMs);
        unlockFor(subsMutex);
      }
    }

    uint32_t targetDelayMs = (uint32_t)latencyAdjustmentMs;
    if (targetDelayMs > 0) {
      int32_t remainingMs = (int32_t)((pkt.captured_ms + targetDelayMs) - millis());
      if (remainingMs > 0) vTaskDelay(pdMS_TO_TICKS((uint32_t)remainingMs));
    }

    // v2 wire header: magic(2) + version(1) + flags(1) + seq(4) + captured_ms(4).
    uint16_t magic = AUDIO_PROTO_MAGIC;
    uint32_t seq   = pkt.seq;
    uint32_t ts    = pkt.captured_ms;
    sendBuf[0]  = (uint8_t)( magic       & 0xFF);
    sendBuf[1]  = (uint8_t)((magic >> 8) & 0xFF);
    sendBuf[2]  = (uint8_t)AUDIO_PROTO_VERSION;
    sendBuf[3]  = 0;   // flags, reserved
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

    // ---- Count subscriber categories ------------------------------------
    int nCompatible   = 0;
    int nIncompatible = 0;
    int nSubs         = 0;
    if (lockFor(subsMutex, 30)) {
      for (int i = 0; i < subscriberTableCount && nSubs < MAX_UDP_SUBSCRIBERS; i++) {
        strncpy(snapBuf[nSubs].ip, subscribers[i].ip.c_str(),
                sizeof(snapBuf[nSubs].ip) - 1);
        snapBuf[nSubs].ip[sizeof(snapBuf[nSubs].ip) - 1] = '\0';
        snapBuf[nSubs].compatible = subscribers[i].audioCompatible;
        snapBuf[nSubs].failures   = subscribers[i].consecutiveSendFailures;
        snapBuf[nSubs].synced     = subscribers[i].hasCompletedClockSync;
        if (subscribers[i].audioCompatible) nCompatible++;
        else                                nIncompatible++;
        nSubs++;
      }
      unlockFor(subsMutex);
    }
    if (nSubs == 0) continue;

    // ---- All-compatible path -------------------------------------------
    if (nIncompatible == 0) {
      bool anyOk = false;
      for (int si = 0; si < nSubs; si++) {
        sendOk[si] = false;                       // default: not sent
        if (!snapBuf[si].synced) continue;        // [new-node-sync-gate]
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
      // Update persistent failure counters once, under a single lock,
      // only for subscribers we actually attempted (those we skipped due
      // to synced=false or failure back-off are excluded from this batch
      // by leaving sendOk[si]==false with a separate "attempted" mask; we
      // simply do not touch their counters).
      bool attempted[MAX_UDP_SUBSCRIBERS];
      for (int si = 0; si < nSubs; si++) {
        attempted[si] = snapBuf[si].synced &&
                        (snapBuf[si].failures < MAX_SEND_FAILURES_BEFORE_SKIP);
      }
      // Convert "not attempted" back into "no change": applySendResults
      // only touches attempts, so make a parallel array of only attempts.
      bool onlyAttempts[MAX_UDP_SUBSCRIBERS];
      int  onlyIdx[MAX_UDP_SUBSCRIBERS];
      int  onlyN = 0;
      for (int si = 0; si < nSubs; si++) {
        if (!attempted[si]) continue;
        onlyAttempts[onlyN] = sendOk[si];
        onlyIdx[onlyN]      = si;
        onlyN++;
      }
      if (onlyN > 0) {
        // Temporarily swap snapBuf slots to the attempted ones so the
        // helper's findSubscriberLocked(ip) lookup matches.
        // (Simpler: inline the update here under one lock.)
        if (lockFor(subsMutex, 20)) {
          for (int k = 0; k < onlyN; k++) {
            int si = onlyIdx[k];
            int slot = findSubscriberLocked(snapBuf[si].ip);
            if (slot < 0) continue;
            if (onlyAttempts[k]) subscribers[slot].consecutiveSendFailures = 0;
            else if (subscribers[slot].consecutiveSendFailures < 255)
              subscribers[slot].consecutiveSendFailures++;
          }
          unlockFor(subsMutex);
        }
      }
      if (anyOk) {
        portENTER_CRITICAL(&gStatsMux);
          statUdpPacketsSent++;
          statUdpBytesSent  += frameLen;
          statLatencySumMs  += latency;
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
        statAudioIncompatibleSkips++;
        continue;
      }
      if (!snapBuf[si].synced) {                // [new-node-sync-gate]
        statAudioSyncPending++;                 // [stat-audio-sync-pending]
        continue;
      }
      if (snapBuf[si].failures >= MAX_SEND_FAILURES_BEFORE_SKIP) {
        continue;
      }
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
          if (sendOk[si]) subscribers[slot].consecutiveSendFailures = 0;
          else if (subscribers[slot].consecutiveSendFailures < 255)
            subscribers[slot].consecutiveSendFailures++;
        }
        unlockFor(subsMutex);
      }
    }
    if (anyOk) {
      portENTER_CRITICAL(&gStatsMux);
        statUdpPacketsSent++;
        statUdpBytesSent  += frameLen;
        statLatencySumMs  += latency;
        statLatencyCount++;
      portEXIT_CRITICAL(&gStatsMux);
    }
  }
}

// ---- Clock sync (request/response, UDP, CLOCK_SYNC_UDP_PORT) ---------------
// See the wire-format doc block in EVDConfig.h for the JSON request/response
// shape and the RTT/offset math the player performs with it. This handler
// runs only from clockSyncTask() (Core 1, priority 4, single-threaded with
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

  // [new-node-sync-gate]
  // First successful sync_req from this IP promotes the subscriber from
  // "registered but not yet aligned" to "audio-eligible". udpSendTask
  // withholds audio while the flag is false, so a new node never starts
  // playing on the coarse one-shot time_sync seed and drifts out of phase
  // with the nodes that already converged.
  {
    String remoteStr = remote.toString();
    if (lockFor(subsMutex, 20)) {
      int slot = findSubscriberLocked(remoteStr);
      if (slot >= 0 && !subscribers[slot].hasCompletedClockSync) {
        subscribers[slot].hasCompletedClockSync = true;
        LOGI("Clock sync: %s promoted to audio-eligible (first RTT sync)",
             remoteStr.c_str());
      }
      unlockFor(subsMutex);
    }
  }

  uint32_t clientTxMs = (uint32_t)(in["clientTxMs"] | 0UL);
  // millis() has 1 ms tick granularity and is read from an arbitrary point
  // in loop(), which was already the single largest contributor to RTT
  // jitter on the server side. esp_timer_get_time() is sub-microsecond and
  // works on both cores without a lock.
  uint32_t serverRxMs = (uint32_t)(esp_timer_get_time() / 1000ULL);

  char out[160];
  int len = snprintf(out, sizeof(out),
      "{\"type\":\"sync_resp\",\"protoVersion\":%d,\"epoch\":%lu,"
      "\"clientTxMs\":%lu,\"serverRxMs\":%lu,\"serverTxMs\":%lu}",
      AUDIO_PROTO_VERSION, (unsigned long)serverEpoch,
      (unsigned long)clientTxMs, (unsigned long)serverRxMs,
      (unsigned long)(esp_timer_get_time() / 1000ULL));
  if (len <= 0 || len >= (int)sizeof(out)) { statSyncRequestErrors++; return; }

  if (clockSyncUdp.beginPacket(remote, remotePort)) {
    clockSyncUdp.write((const uint8_t *)out, (size_t)len);
    if (clockSyncUdp.endPacket() == 1) statSyncRequestsServed++;
    else                               statSyncRequestErrors++;
  } else {
    statSyncRequestErrors++;
  }
}

// ---- Dedicated clock-sync task --------------------------------------------
// Previously handleClockSyncRequests() was called from loop() on Core 1.
// loop() also does discovery, NVS writes (the settingsDirty block can block
// on a flash erase for ~100 ms), adaptive buffer updates, Opus complexity
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
        handleClockSyncRequests();
        uint32_t now = millis();
        if (now - lastAuthSyncMs >= 30000) {
            lastAuthSyncMs = now;
            sendAuthoritativeTimeSync();
        }
        vTaskDelay(1);
    }
}

// ---- OGG test stream (moved verbatim from EVDWebSocket.h) ------------------
inline void storeOggTestPacket(const OpusPacket &packet) {
  if (packet.len <= 0 || packet.len > OGG_TEST_MAX_PACKET_BYTES) {
    LOGW("Ogg test packet skipped: %d bytes exceeds %d-byte history slot",
         packet.len, OGG_TEST_MAX_PACKET_BYTES);
    return;
  }
  portENTER_CRITICAL(&oggTestMux);
  memcpy(oggTestPackets[oggTestWriteIndex].data, packet.data, packet.len);
  oggTestPackets[oggTestWriteIndex].len = packet.len;
  oggTestWriteIndex = (oggTestWriteIndex + 1) % OGG_TEST_PACKET_COUNT;
  if (oggTestPacketCount < OGG_TEST_PACKET_COUNT) ++oggTestPacketCount;
  portEXIT_CRITICAL(&oggTestMux);
}

inline void writeOggPage(AsyncResponseStream *response, const uint8_t *packet,
                         size_t packetLength, uint32_t serial, uint32_t sequence,
                         uint64_t granulePosition, uint8_t headerType) {
  const size_t segmentCount = (packetLength / 255) + 1;
  uint8_t page[27 + (OPUS_MAX_PACKET_BYTES / 255) + 2 + OPUS_MAX_PACKET_BYTES];
  memcpy(page, "OggS", 4);
  page[4] = 0;
  page[5] = headerType;
  for (uint8_t i = 0; i < 8; ++i) page[6  + i] = (granulePosition >> (i * 8)) & 0xFF;
  for (uint8_t i = 0; i < 4; ++i) page[14 + i] = (serial          >> (i * 8)) & 0xFF;
  for (uint8_t i = 0; i < 4; ++i) page[18 + i] = (sequence        >> (i * 8)) & 0xFF;
  memset(page + 22, 0, 4);
  page[26] = segmentCount;
  size_t remaining = packetLength;
  for (size_t i = 0; i < segmentCount; ++i) {
    page[27 + i] = remaining >= 255 ? 255 : remaining;
    remaining -= page[27 + i];
  }
  memcpy(page + 27 + segmentCount, packet, packetLength);
  size_t pageLength = 27 + segmentCount + packetLength;
  uint32_t crc = oggCrc(page, pageLength);
  for (uint8_t i = 0; i < 4; ++i) page[22 + i] = (crc >> (i * 8)) & 0xFF;
  response->write(page, pageLength);
}

inline void handleOggTestStream(AsyncWebServerRequest *request) {
  uint8_t count, start;
  portENTER_CRITICAL(&oggTestMux);
  count = oggTestPacketCount;
  start = (oggTestWriteIndex + OGG_TEST_PACKET_COUNT - count) % OGG_TEST_PACKET_COUNT;
  portEXIT_CRITICAL(&oggTestMux);

  if (count == 0) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"No encoded audio is available yet\"}");
    return;
  }

  AsyncResponseStream *response = request->beginResponseStream("audio/ogg");
  response->addHeader("Cache-Control", "no-store");
  const uint8_t opusHead[] = {'O','p','u','s','H','e','a','d',1,2,0,0,0x80,0xBB,0,0,0,0,0};
  const uint8_t opusTags[] = {'O','p','u','s','T','a','g','s',0,0,0,0,0,0,0,0};
  const uint32_t serial = 0x454E4B4C;
  writeOggPage(response, opusHead, sizeof(opusHead), serial, 0, 0, 0x02);
  writeOggPage(response, opusTags, sizeof(opusTags), serial, 1, 0, 0);
  for (uint8_t i = 0; i < count; ++i) {
    OggTestPacket packet;
    portENTER_CRITICAL(&oggTestMux);
    packet = oggTestPackets[(start + i) % OGG_TEST_PACKET_COUNT];
    portEXIT_CRITICAL(&oggTestMux);
    writeOggPage(response, packet.data, packet.len, serial, i + 2,
                 (uint64_t)(i + 1) * OPUS_FRAME_SAMPLES, i + 1 == count ? 0x04 : 0);
  }
  request->send(response);
}