#pragma once
// ============================================================================
//  EVDPeers.h - multi-server peer beacons + "servers decide" election (P3)
// ============================================================================
//
// Same wire protocol as enkelvoud/peers.py on the PC: every Enkelvoud server
// broadcasts a JSON beacon on UDP :4205 once a second:
//
//   {"e":"enkelvoud-peer/1","id":"esp-<mac>","name":"Enkelvoud",
//    "priority":2,"leader":true,"httpPort":80,"uptimeMs":1234,"version":"..."}
//
// Election is deterministic: rank every ALIVE server by (priority, id)
// ascending and the lowest is the LEADER. Only the leader answers node
// discovery, advertises mDNS <name>.local, accepts /api/player/register and
// sends audio. A standby returns 503, releases mDNS and tells its nodes to
// re-discover. The leader is gone after PEER_DEAD_MS -> failover. A
// higher-priority server joining demotes the current leader -> takeover.
//
// Priority (1 = highest) is persisted in NVS as peer_prio; default 2 so the
// PC server (default 1) is preferred when both are on.
//
// Include order: after EVDUDP.h (sendCtrlToSubscriber) and EVDHelpers.h
// (snapshotSubscribers/lockFor), before EVDRoutes.h (which registers the
// /api/servers* handlers).
// ============================================================================

#include "EVDHelpers.h"

#define PEER_PORT        4205
#define PEER_MAX         8
#define PEER_BEACON_MS   1000UL
#define PEER_DEAD_MS     5000UL
#define PEER_PRUNE_MS    60000UL
#define PEER_PROTOCOL    "enkelvoud-peer/1"
#define PEER_DEFAULT_PRIORITY 2   // PC defaults to 1, so the PC is preferred

struct PeerInfo {
  String        id;
  String        name;
  String        version;
  IPAddress     ip;
  int           priority;
  bool          leader;
  uint16_t      httpPort;
  unsigned long lastSeenMs;
};

static PeerInfo         g_peers[PEER_MAX];
static int              g_peerCount = 0;
static SemaphoreHandle_t g_peerMutex = nullptr;
static unsigned long    g_peerLastBeaconMs = 0;
static String           g_selfPeerId;
static bool             g_peerMdnsLeader = true;
static WiFiUDP          peerUdp;

// ---- NVS priority ----------------------------------------------------------

inline void peerLoadPriority() {
  PrefsGuard guard;
  if (!guard.ok()) return;
  if (!preferences.begin("enkelvoud", true)) return;
  int p = (int)preferences.getInt("peer_prio", PEER_DEFAULT_PRIORITY);
  preferences.end();
  g_peerPriority = constrain(p, 1, 99);
}

inline void peerSavePriority() {
  PrefsGuard guard;
  if (!guard.ok()) return;
  if (!preferences.begin("enkelvoud", false)) {
    LOGW("peers: preferences.begin failed; priority NOT saved");
    return;
  }
  preferences.putInt("peer_prio", g_peerPriority);
  preferences.end();
}

// ---- mDNS ownership --------------------------------------------------------
// Only the leader owns "<serverName>.local": that is what nodes resolve first.
inline void peerApplyMdnsRole(bool leader) {
  if (leader == g_peerMdnsLeader) return;
  g_peerMdnsLeader = leader;
  if (leader) {
    if (WiFi.status() == WL_CONNECTED) {
      MDNS.end();
      if (MDNS.begin(serverName.c_str())) {
        MDNS.addService("http",     "tcp", 80);
        MDNS.addService("enkelsrv", "tcp", 80);
        LOGI("peers: LEADER - mDNS %s.local registered", serverName.c_str());
      } else {
        LOGW("peers: LEADER - mDNS restart failed");
      }
    }
  } else {
    MDNS.end();
    LOGI("peers: STANDBY - mDNS released");
  }
}

// ---- Beacon ----------------------------------------------------------------

inline void peerBroadcastTargets(IPAddress out[2], int &n) {
  n = 0;
  out[n++] = IPAddress(255, 255, 255, 255);
  IPAddress ip   = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  if (ip[0] || ip[1] || ip[2] || ip[3]) {
    IPAddress bcast;
    for (int i = 0; i < 4; i++) {
      bcast[i] = (uint8_t)((ip[i] & mask[i]) | (uint8_t)(~mask[i]));
    }
    out[n++] = bcast;
  }
}

inline void peerSendBeacon() {
  StaticJsonDocument<256> doc;
  doc["e"]        = PEER_PROTOCOL;
  doc["id"]       = g_selfPeerId;
  doc["name"]     = serverName;
  doc["priority"] = g_peerPriority;
  doc["leader"]   = g_peerIsLeader;
  doc["httpPort"] = 80;
  doc["uptimeMs"] = (unsigned long)millis();
  doc["version"]  = FW_VERSION;

  char buf[288];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n == 0) return;

  IPAddress targets[2];
  int tn = 0;
  peerBroadcastTargets(targets, tn);
  for (int i = 0; i < tn; i++) {
    peerUdp.beginPacket(targets[i], PEER_PORT);
    peerUdp.write((const uint8_t *)buf, n);
    peerUdp.endPacket();
  }
}

inline void peerHandlePacket(int sz) {
  char buf[384];
  int n = peerUdp.read(buf, sizeof(buf) - 1);
  if (n <= 0) return;
  buf[n] = '\0';
  if (n < sz) { /* short read: still parsable */ }

  StaticJsonDocument<320> doc;
  if (deserializeJson(doc, buf) != DeserializationError::Ok) return;
  const char *e = doc["e"] | "";
  if (strcmp(e, PEER_PROTOCOL) != 0) return;

  String pid = String((const char *)(doc["id"] | ""));
  if (pid.length() == 0 || pid == g_selfPeerId) return;
  int prio = doc["priority"] | PEER_DEFAULT_PRIORITY;
  prio = constrain(prio, 1, 99);
  uint16_t port = (uint16_t)(doc["httpPort"] | 80);
  if (port == 0) port = 80;
  const char *pname = doc["name"] | "";
  const char *pver  = doc["version"] | "";
  bool pledge = doc["leader"] | false;
  IPAddress src = peerUdp.remoteIP();
  unsigned long now = millis();

  if (!lockFor(g_peerMutex, 20)) return;
  int slot = -1;
  for (int i = 0; i < g_peerCount; i++) {
    if (g_peers[i].id == pid) { slot = i; break; }
  }
  if (slot < 0) {
    if (g_peerCount >= PEER_MAX) { unlockFor(g_peerMutex); return; }
    slot = g_peerCount++;
    g_peers[slot].id = pid;
  }
  g_peers[slot].name       = String(pname).substring(0, 32);
  g_peers[slot].version    = String(pver).substring(0, 16);
  g_peers[slot].ip         = src;
  g_peers[slot].priority   = prio;
  g_peers[slot].leader     = pledge;
  g_peers[slot].httpPort   = port;
  g_peers[slot].lastSeenMs = now;
  unlockFor(g_peerMutex);
}

// ---- Election --------------------------------------------------------------

inline void peerNotifyStandby() {
  // Old node firmware ignores the unknown ctrl type and falls back to its
  // 60 s stale-contact re-discovery; new firmware can drop the contact now.
  std::vector<SubscriberSnap> subs = snapshotSubscribers(millis());
  for (auto &s : subs) {
    sendCtrlToSubscriber(s.ip, String("{\"type\":\"server_standby\"}"));
  }
  if (!subs.empty()) {
    LOGI("peers: told %d node(s) to re-discover", (int)subs.size());
  }
}

inline void peerEval() {
  unsigned long now = millis();
  bool wasLeader = g_peerIsLeader;
  String bestId   = g_selfPeerId;
  int    bestPrio = g_peerPriority;

  if (lockFor(g_peerMutex, 20)) {
    // Prune peers that have been quiet for a full minute.
    for (int i = g_peerCount - 1; i >= 0; --i) {
      if ((unsigned long)(now - g_peers[i].lastSeenMs) > PEER_PRUNE_MS) {
        for (int k = i; k < g_peerCount - 1; k++) g_peers[k] = g_peers[k + 1];
        g_peerCount--;
      }
    }
    for (int i = 0; i < g_peerCount; i++) {
      if ((unsigned long)(now - g_peers[i].lastSeenMs) > PEER_DEAD_MS) continue;
      bool better = (g_peers[i].priority < bestPrio) ||
                    (g_peers[i].priority == bestPrio && g_peers[i].id < bestId);
      if (better) {
        bestPrio = g_peers[i].priority;
        bestId   = g_peers[i].id;
      }
    }
    unlockFor(g_peerMutex);
  }

  bool isLeader = (bestId == g_selfPeerId);
  if (isLeader == wasLeader) return;
  g_peerIsLeader = isLeader;
  peerApplyMdnsRole(isLeader);
  if (isLeader) {
    LOGI("peers: LEADER - answering nodes (priority %d)", g_peerPriority);
  } else {
    LOGW("peers: STANDBY - %s outranks this server (priority %d)",
         bestId.c_str(), g_peerPriority);
    peerNotifyStandby();
  }
}

// ---- Setup / tick ----------------------------------------------------------

inline void peerSetup() {
  if (!g_peerMutex) g_peerMutex = xSemaphoreCreateMutex();
  char idbuf[24];
  uint64_t mac = ESP.getEfuseMac();
  snprintf(idbuf, sizeof(idbuf), "esp-%012llx",
           (unsigned long long)(mac & 0xFFFFFFFFFFFFULL));
  g_selfPeerId = String(idbuf);
  peerLoadPriority();
  g_peerIsLeader = true;
  g_peerMdnsLeader = true;
  if (peerUdp.begin(PEER_PORT)) {
    LOGI("peers: UDP %d, id %s, priority %d",
         PEER_PORT, g_selfPeerId.c_str(), g_peerPriority);
  } else {
    LOGW("peers: UDP %d bind failed; running as a single server", PEER_PORT);
  }
  g_peerLastBeaconMs = 0;    // send a beacon on the first tick
}

inline void peerTick() {
  if (!g_selfPeerId.length()) return;
  for (int i = 0; i < 8; i++) {
    int sz = peerUdp.parsePacket();
    if (sz <= 0) break;
    peerHandlePacket(sz);
  }
  unsigned long now = millis();
  if ((unsigned long)(now - g_peerLastBeaconMs) >= PEER_BEACON_MS) {
    g_peerLastBeaconMs = now;
    peerSendBeacon();
    peerEval();
  }
}

// ---- JSON + HTTP API -------------------------------------------------------

inline String peerBuildJson(bool wrap) {
  unsigned long now = millis();
  PeerInfo copy[PEER_MAX];
  int n = 0;
  if (lockFor(g_peerMutex, 20)) {
    n = g_peerCount;
    for (int i = 0; i < n; i++) copy[i] = g_peers[i];
    unlockFor(g_peerMutex);
  }

  String bestId   = g_selfPeerId;
  int    bestPrio = g_peerPriority;
  for (int i = 0; i < n; i++) {
    if ((unsigned long)(now - copy[i].lastSeenMs) > PEER_DEAD_MS) continue;
    bool better = (copy[i].priority < bestPrio) ||
                  (copy[i].priority == bestPrio && copy[i].id < bestId);
    if (better) {
      bestPrio = copy[i].priority;
      bestId   = copy[i].id;
    }
  }

  struct Row {
    String id, name, ip, version;
    int    prio;
    bool   self, alive, leader;
  };
  Row rows[PEER_MAX + 1];
  int rn = 0;
  rows[rn++] = { g_selfPeerId, String(serverName),
                 WiFi.localIP().toString(), String(FW_VERSION),
                 g_peerPriority, true, true, (bestId == g_selfPeerId) };
  for (int i = 0; i < n; i++) {
    bool alive = (unsigned long)(now - copy[i].lastSeenMs) <= PEER_DEAD_MS;
    rows[rn++] = { copy[i].id, copy[i].name, copy[i].ip.toString(),
                   copy[i].version, copy[i].priority, false, alive,
                   (bestId == copy[i].id) };
  }
  // Insertion sort by (priority, id) -- matches the dashboard order.
  for (int i = 1; i < rn; i++) {
    Row key = rows[i];
    int j = i - 1;
    while (j >= 0 &&
           (rows[j].prio > key.prio ||
            (rows[j].prio == key.prio && rows[j].id > key.id))) {
      rows[j + 1] = rows[j];
      j--;
    }
    rows[j + 1] = key;
  }

  const char *role = g_peerIsLeader ? "leader" : "standby";
  String f = "\"role\":\"";
  f += role;
  f += "\",\"priority\":";
  f += String(g_peerPriority);
  f += ",\"leaderId\":\"";
  f += jsonEscape(bestId);
  f += "\",\"protocol\":\"" PEER_PROTOCOL "\",";
  f += "\"self\":{\"id\":\"";
  f += jsonEscape(g_selfPeerId);
  f += "\",\"name\":\"";
  f += jsonEscape(String(serverName));
  f += "\",\"ip\":\"";
  f += WiFi.localIP().toString();
  f += "\",\"priority\":";
  f += String(g_peerPriority);
  f += ",\"role\":\"";
  f += role;
  f += "\",\"self\":true,\"alive\":true,\"version\":\"" FW_VERSION "\"},";
  f += "\"servers\":[";
  for (int i = 0; i < rn; i++) {
    if (i) f += ",";
    f += "{\"id\":\"";
    f += jsonEscape(rows[i].id);
    f += "\",\"name\":\"";
    f += jsonEscape(rows[i].name);
    f += "\",\"ip\":\"";
    f += rows[i].ip;
    f += "\",\"version\":\"";
    f += jsonEscape(rows[i].version);
    f += "\",\"priority\":";
    f += String(rows[i].prio);
    f += ",\"self\":";
    f += rows[i].self ? "true" : "false";
    f += ",\"alive\":";
    f += rows[i].alive ? "true" : "false";
    f += ",\"leader\":";
    f += rows[i].leader ? "true" : "false";
    f += "}";
  }
  f += "]";
  return wrap ? ("{" + f + "}") : f;
}

inline void handleApiServers(AsyncWebServerRequest *request) {
  request->send(200, "application/json", peerBuildJson(true));
}

inline void handleApiServersPriority(AsyncWebServerRequest *request, uint8_t *data,
                                     size_t len, size_t index, size_t total) {
  if (index != 0 || len != total) {
    if (index != 0) return;
    request->send(413, "application/json",
                  "{\"ok\":false,\"error\":\"body fragmented; retry\"}");
    return;
  }
  DynamicJsonDocument in(128);
  if (deserializeJson(in, data, len) != DeserializationError::Ok ||
      !in["priority"].is<int>()) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"priority int required\"}");
    return;
  }
  int p = constrain(in["priority"].as<int>(), 1, 99);
  g_peerPriority = p;
  peerSavePriority();
  peerEval();
  request->send(200, "application/json",
                String("{\"ok\":true,\"priority\":") + String(p) +
                ",\"role\":\"" +
                String(g_peerIsLeader ? "leader" : "standby") + "\"}");
}

inline void handleApiServersOrder(AsyncWebServerRequest *request, uint8_t *data,
                                  size_t len, size_t index, size_t total) {
  if (index != 0 || len != total) {
    if (index != 0) return;
    request->send(413, "application/json",
                  "{\"ok\":false,\"error\":\"body fragmented; retry\"}");
    return;
  }
  DynamicJsonDocument in(512);
  if (deserializeJson(in, data, len) != DeserializationError::Ok ||
      !in["order"].is<JsonArray>()) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"order list required\"}");
    return;
  }
  JsonArray order = in["order"].as<JsonArray>();
  int idx = 0;
  for (JsonVariant v : order) {
    String sid = v.as<String>();
    if (!sid.length()) { idx++; continue; }
    int pr = constrain(idx + 1, 1, 99);
    idx++;
    if (sid == g_selfPeerId) {
      g_peerPriority = pr;
      peerSavePriority();
      continue;
    }
    IPAddress pip;
    uint16_t pport = 80;
    bool found = false;
    if (lockFor(g_peerMutex, 20)) {
      for (int i = 0; i < g_peerCount; i++) {
        if (g_peers[i].id == sid) {
          pip = g_peers[i].ip;
          pport = g_peers[i].httpPort;
          found = true;
          break;
        }
      }
      unlockFor(g_peerMutex);
    }
    if (!found) continue;
    HTTPClient http;
    http.setConnectTimeout(700);
    http.setTimeout(700);
    String url = String("http://") + pip.toString() + ":" +
                 String((unsigned)pport) + "/api/servers/priority";
    if (http.begin(url)) {
      http.addHeader("Content-Type", "application/json");
      http.POST(String("{\"priority\":") + String(pr) + "}");
      http.end();
    }
  }
  peerEval();
  request->send(200, "application/json", peerBuildJson(true));
}
