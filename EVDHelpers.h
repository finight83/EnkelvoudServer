#pragma once
// ============================================================================
//  EVDHelpers.h - Locks, snapshots, subscriber bookkeeping, string helpers
// ============================================================================
#include "EVDState.h"

// ---- Sync primitives -------------------------------------------------------
inline void createSyncPrimitives() {
  if (!playersMutex) playersMutex = xSemaphoreCreateMutex();
  if (!uartMutex)    uartMutex    = xSemaphoreCreateMutex();
  if (!subsMutex)    subsMutex    = xSemaphoreCreateMutex();
  if (!zonesMutex)   zonesMutex   = xSemaphoreCreateMutex();
  if (!perfMutex)    perfMutex    = xSemaphoreCreateMutex();
  if (!prefsMutex)   prefsMutex   = xSemaphoreCreateMutex();
  if (!logTailMutex) logTailMutex = xSemaphoreCreateMutex();
}

// [v1.1.1-prefs-mutex]
// RAII-style guard for Preferences. Every preferences.begin()/end() pair in
// the tree should sit inside one of these. Named PrefsGuard to avoid
// colliding with any FreeRTOS symbol.
struct PrefsGuard {
  bool locked;
  explicit PrefsGuard(uint32_t ms = 200) {
    locked = (prefsMutex &&
              xSemaphoreTake(prefsMutex, pdMS_TO_TICKS(ms)) == pdTRUE);
  }
  ~PrefsGuard() { if (locked) xSemaphoreGive(prefsMutex); }
  bool ok() const { return locked; }
};

static inline bool lockFor(SemaphoreHandle_t m, uint32_t ms) {
  if (!m) return true;
  return xSemaphoreTake(m, pdMS_TO_TICKS(ms)) == pdTRUE;
}
static inline void unlockFor(SemaphoreHandle_t m) {
  if (m) xSemaphoreGive(m);
}

// ---- Player list management ------------------------------------------------
inline void pruneStalePlayersLocked(unsigned long nowMs) {
  playerNodes.erase(
      std::remove_if(playerNodes.begin(), playerNodes.end(),
                     [nowMs](const PlayerNode &p) {
                       return (nowMs - p.lastSeenMs) > PLAYER_STALE_TIMEOUT_MS;
                     }),
      playerNodes.end());
}

inline void pruneStalePlayers(unsigned long nowMs) {
  if (!lockFor(playersMutex, 50)) return;
  pruneStalePlayersLocked(nowMs);
  unlockFor(playersMutex);
}

inline std::vector<PlayerNode> snapshotPlayers(unsigned long nowMs) {
  std::vector<PlayerNode> copy;
  if (!lockFor(playersMutex, 50)) return copy;
  pruneStalePlayersLocked(nowMs);
  copy = playerNodes;
  unlockFor(playersMutex);
  return copy;
}

// ---- Subscriber table (replaces the WS client bookkeeping) -----------------
// Caller must hold subsMutex. Prunes entries whose lastSeenMs is older than
// the same stale timeout the player list uses. Also recomputes the visible
// counter so every reader sees a consistent value.
inline void pruneStaleSubscribersLocked(unsigned long nowMs) {
  int w = 0;
  for (int i = 0; i < subscriberTableCount; i++) {
    if ((nowMs - subscribers[i].lastSeenMs) <= PLAYER_STALE_TIMEOUT_MS) {
      if (w != i) subscribers[w] = subscribers[i];
      w++;
    }
  }
  subscriberTableCount = w;
  subscriberCount      = w;
}

inline int findSubscriberLocked(const String &ip) {
  for (int i = 0; i < subscriberTableCount; i++)
    if (subscribers[i].ip == ip) return i;
  return -1;
}

// Caller must hold subsMutex. Returns slot index or -1 if the table is full.
// declaredProtoVer/negotiatedProtoVer/compatible come from
// evaluateAudioCompatibility() (EVDProtocolCompat.h), recomputed by the
// caller on every registration/heartbeat so a firmware downgrade or reboot
// is reflected here immediately rather than sticking with a stale decision.
inline int findOrCreateSubscriberLocked(const String &ip, const String &name,
                                        int declaredProtoVer, int negotiatedProtoVer,
                                        bool compatible) {
  int slot = findSubscriberLocked(ip);
  if (slot >= 0) {
    subscribers[slot].name       = name;
    subscribers[slot].lastSeenMs = millis();
    // A heartbeat means the node is alive and reachable again. Clear any
    // accumulated send failures so udpSendTask resumes sending to it on
    // the very next frame instead of staying backed-off until the stale
    // timeout would otherwise prune it.
    subscribers[slot].consecutiveSendFailures = 0;
    subscribers[slot].declaredAudioProtocolVersion   = declaredProtoVer;
    subscribers[slot].negotiatedAudioProtocolVersion = negotiatedProtoVer;
    subscribers[slot].audioCompatible                = compatible;
    // [heartbeat-must-not-rearm-sync-gate]
    // DO NOT reset hasCompletedClockSync here. This branch runs on EVERY
    // /api/player/register heartbeat (every 30 s per node), not only on a
    // genuine re-connect, and closing the gate on every heartbeat makes
    // udpSendTask() withhold audio from the subscriber until the player's
    // next sync_req arrives. When that request is lost, or when the
    // player's own g_sync_req_in_flight back-off swallows the
    // g_force_sync_now retrigger, the gate stays closed for the full
    // 2 s re-request cadence. That is the periodic "plays fine, stutters
    // heavily, plays fine" cycle this build was exhibiting.
    //
    // The gate is only ever needed for a *brand-new* subscriber, which
    // is already handled by the insert-slot path below (default value
    // is false). A rebooting player is caught by its own
    // !g_have_rtt_sync flag in EVDPNet.h, which forces g_force_resync on
    // the first post-reboot RTT sample and re-establishes the offset
    // locally — the server does not need to re-arm anything for that.
    return slot;
  }
  if (subscriberTableCount >= MAX_UDP_SUBSCRIBERS) return -1;
  slot = subscriberTableCount++;
  subscribers[slot].ip           = ip;
  subscribers[slot].name         = name;
  subscribers[slot].lastSeenMs   = millis();
  subscribers[slot].consecutiveSendFailures        = 0;
  subscribers[slot].declaredAudioProtocolVersion   = declaredProtoVer;
  subscribers[slot].negotiatedAudioProtocolVersion = negotiatedProtoVer;
  subscribers[slot].audioCompatible                = compatible;
  subscribers[slot].hasCompletedClockSync          = false;   // new slots only
  subscriberCount = subscriberTableCount;
  return slot;
}

// Snapshot for readers that must not hold subsMutex while iterating.
struct SubscriberSnap {
  String   ip;
  String   name;
  unsigned long lastSeenMs;
  int  declaredAudioProtocolVersion;
  int  negotiatedAudioProtocolVersion;
  bool audioCompatible;
  bool hasCompletedClockSync;   // [snap-sync-flag]
};

inline std::vector<SubscriberSnap> snapshotSubscribers(unsigned long nowMs) {
  std::vector<SubscriberSnap> out;
  if (!lockFor(subsMutex, 50)) return out;
  pruneStaleSubscribersLocked(nowMs);
  out.reserve(subscriberTableCount);
  for (int i = 0; i < subscriberTableCount; i++) {
    out.push_back({ subscribers[i].ip, subscribers[i].name,
                    subscribers[i].lastSeenMs,
                    subscribers[i].declaredAudioProtocolVersion,
                    subscribers[i].negotiatedAudioProtocolVersion,
                    subscribers[i].audioCompatible,
                    subscribers[i].hasCompletedClockSync });
  }
  unlockFor(subsMutex);
  return out;
}

// ---- Zone / perf snapshots -------------------------------------------------
inline std::vector<Zone> snapshotZones() {
  std::vector<Zone> copy;
  if (!lockFor(zonesMutex, 60)) return copy;
  copy = zones;
  unlockFor(zonesMutex);
  return copy;
}

// ---- Node perf list management ---------------------------------------------
// The perf table only ever grows: updateNodePerf() (EVDPlayers.h) appends a
// new entry for every IP it has never seen and nothing ever removes one. A
// node that was powered off, unplugged, kicked off Wi-Fi, or removed from
// every zone would therefore keep its last-reported latency on the Status
// page indefinitely. /api/stats renders the whole vector with no age check,
// so the operator sees a stale entry that the adaptive engine has already
// stopped acting on (updateAdaptiveBuffers() in EVDPlayers.h has its own
// 20 s freshness gate). The two views have to agree: an entry visible in
// the Node Latencies list must be an entry the buffer engine can still act
// on. Prune on every snapshot at exactly that window so they stay in sync.
//
// 20 s is chosen to match updateAdaptiveBuffers(), not the 45 s
// PLAYER_STALE_TIMEOUT_MS used for the player/subscriber tables: the perf
// table is a real-time signal that feeds control decisions, so it has to
// go stale before the coarser identity tables do.
#define NODE_PERF_STALE_TIMEOUT_MS 20000UL

inline void pruneStalePerfLocked(unsigned long nowMs) {
  nodePerf.erase(
      std::remove_if(nodePerf.begin(), nodePerf.end(),
                     [nowMs](const NodePerf &p) {
                       // updatedMs == 0 means a placeholder that was never
                       // filled by a /api/node_stats POST - treat it as
                       // stale so it can never influence the display.
                       return p.updatedMs == 0 ||
                              (nowMs - p.updatedMs) > NODE_PERF_STALE_TIMEOUT_MS;
                     }),
      nodePerf.end());
}

// Explicit removal, for the one place we know a node is gone without
// waiting out the TTL (the zone-delete path in EVDZones.h). Safe to call
// from any task; takes the same lock updateNodePerf() does.
inline void removeNodePerf(const String &ip) {
  if (!lockFor(perfMutex, 40)) return;
  nodePerf.erase(
      std::remove_if(nodePerf.begin(), nodePerf.end(),
                     [&ip](const NodePerf &p) { return p.ip == ip; }),
      nodePerf.end());
  unlockFor(perfMutex);
}

inline std::vector<NodePerf> snapshotPerf() {
  std::vector<NodePerf> copy;
  if (!lockFor(perfMutex, 40)) return copy;
  pruneStalePerfLocked(millis());
  copy = nodePerf;
  unlockFor(perfMutex);
  return copy;
}

// ---- String helpers --------------------------------------------------------
inline String sanitizeServerName(const String &value) {
  String result;
  result.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i)
    if (isAlphaNumeric(value[i])) result += value[i];
  return result.length() ? result : "Enkelvoud";
}

inline String htmlEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&': escaped += F("&amp;");  break;
      case '"': escaped += F("&quot;"); break;
      case '<': escaped += F("&lt;");   break;
      case '>': escaped += F("&gt;");   break;
      default:  escaped += value[i];    break;
    }
  }
  return escaped;
}

inline String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\' || c == '\"') out += '\\';
    out += c;
  }
  return out;
}

// ---- Network identity ------------------------------------------------------
inline IPAddress activeIpAddress() {
  return WiFi.status() == WL_CONNECTED
             ? WiFi.localIP()
             : (accessPointActive ? WiFi.softAPIP() : IPAddress(0, 0, 0, 0));
}

// ---- OGG CRC ---------------------------------------------------------------
inline uint32_t oggCrc(const uint8_t *data, size_t length) {
  uint32_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc ^= (uint32_t)data[i] << 24;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80000000) ? (crc << 1) ^ 0x04C11DB7 : crc << 1;
    }
  }
  return crc;
}