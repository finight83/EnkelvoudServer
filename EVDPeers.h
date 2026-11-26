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
  uint32_t      uptimeMs;
  unsigned long lastSeenMs;
};

// Tie-break shared by the election and the dashboard: an equal priority goes
// to the server that has been up LONGEST (the incumbent keeps leadership),
// then to the lower id. Without the uptime term a newcomer whose priority was
// left tied at 1 could displace a healthy running leader by machine id alone.
inline bool peerOutranks(int prio, uint32_t uptime, const String &id,
                         int bestPrio, uint32_t bestUptime,
                         const String &bestId) {
  if (prio != bestPrio) return prio < bestPrio;
  if (uptime != bestUptime) return uptime > bestUptime;
  return id < bestId;
}

static PeerInfo         g_peers[PEER_MAX];
static int              g_peerCount = 0;
static SemaphoreHandle_t g_peerMutex = nullptr;
static unsigned long    g_peerLastBeaconMs = 0;
static String           g_selfPeerId;
static bool             g_peerMdnsLeader = false;
static WiFiUDP          peerUdp;

// [node-sharing] The leader broadcasts its registered fleet in every beacon.
// A standby keeps the last leader's list: its dashboard shows those nodes
// (instead of "Offline") and, when it takes over, it tells them to connect.
struct PeerNodeInfo {
  String ip;
  String name;
  String ver;
};
static PeerNodeInfo     g_leaderNodes[16];
static int              g_leaderNodeCount = 0;
static unsigned long    g_readyNotifyUntilMs = 0;

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
  StaticJsonDocument<1024> doc;
  doc["e"]        = PEER_PROTOCOL;
  doc["id"]       = g_selfPeerId;
  doc["name"]     = serverName;
  doc["priority"] = g_peerPriority;
  doc["leader"]   = g_peerIsLeader;
  doc["httpPort"] = 80;
  doc["uptimeMs"] = (unsigned long)millis();
  doc["version"]  = FW_VERSION;
  // [node-sharing] The fleet this server serves (the leader's list is what
  // standbys display and use to guide nodes over after a failover).
  {
    std::vector<PlayerNode> ps = snapshotPlayers(millis());
    JsonArray na = doc.createNestedArray("nodes");
    int cnt = 0;
    for (auto &p : ps) {
      if (cnt >= 16) break;
      JsonObject o = na.createNestedObject();
      o["ip"]   = p.ip;
      o["name"] = p.name;
      o["ver"]  = p.version;
      cnt++;
    }
  }

  char buf[1024];
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
  uint32_t uptime = (uint32_t)(doc["uptimeMs"] | 0UL);
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
  g_peers[slot].uptimeMs   = uptime;
  g_peers[slot].lastSeenMs = now;
  // [node-sharing] A leader's beacon carries its fleet; keep the last one.
  if (pledge) {
    g_leaderNodeCount = 0;
    JsonArray arr = doc["nodes"].as<JsonArray>();
    for (JsonObject n : arr) {
      if (g_leaderNodeCount >= 16) break;
      String nip = String((const char *)(n["ip"] | ""));
      nip.trim();
      if (!nip.length()) continue;
      g_leaderNodes[g_leaderNodeCount].ip   = nip;
      g_leaderNodes[g_leaderNodeCount].name = String((const char *)(n["name"] | ""));
      g_leaderNodes[g_leaderNodeCount].ver  = String((const char *)(n["ver"] | ""));
      g_leaderNodeCount++;
    }
  }
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

inline void peerNotifyReady() {
  // [node-sharing] Announce to the previous leader's fleet (and our own
  // subscribers) that this server is now the one to connect to. The nodes
  // accept it only after their old server has gone quiet.
  int sent = 0;
  if (lockFor(g_peerMutex, 20)) {
    for (int i = 0; i < g_leaderNodeCount; i++) {
      if (!g_leaderNodes[i].ip.length()) continue;
      sendCtrlToSubscriber(g_leaderNodes[i].ip,
                           String("{\"type\":\"server_ready\"}"));
      sent++;
    }
    unlockFor(g_peerMutex);
  }
  std::vector<SubscriberSnap> subs = snapshotSubscribers(millis());
  for (auto &s : subs) {
    sendCtrlToSubscriber(s.ip, String("{\"type\":\"server_ready\"}"));
    sent++;
  }
  if (sent) LOGI("peers: announced 'connect here' to %d node(s)", sent);
}

// [node-sharing] Name/version of a fleet node learned from the leader.
inline bool peerNodeInfo(const String &ip, String &name, String &ver) {
  bool found = false;
  if (lockFor(g_peerMutex, 20)) {
    for (int i = 0; i < g_leaderNodeCount; i++) {
      if (g_leaderNodes[i].ip == ip) {
        name  = g_leaderNodes[i].name;
        ver   = g_leaderNodes[i].ver;
        found = true;
        break;
      }
    }
    unlockFor(g_peerMutex);
  }
  return found;
}

inline void peerEval() {
  unsigned long now = millis();
  bool wasLeader = g_peerIsLeader;
  String bestId   = g_selfPeerId;
  int    bestPrio = g_peerPriority;
  uint32_t bestUptime = millis();

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
      if (peerOutranks(g_peers[i].priority, g_peers[i].uptimeMs,
                       g_peers[i].id, bestPrio, bestUptime, bestId)) {
        bestPrio   = g_peers[i].priority;
        bestUptime = g_peers[i].uptimeMs;
        bestId     = g_peers[i].id;
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
    // Announce to the fleet we learned from the old leader, repeatedly for
    // ~4 s so a lost UDP datagram cannot strand a node.
    g_readyNotifyUntilMs = millis() + 4000;
    peerNotifyReady();
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
  // [servers-decide] Start as a STANDBY: the first election tick (<= 1 s)
  // promotes this board if it is alone. Claiming leadership before that made
  // a booting standby answer node discovery and fight the PC for mDNS.
  g_peerIsLeader   = false;
  g_peerMdnsLeader = false;
  g_leaderNodeCount = 0;
  g_readyNotifyUntilMs = 0;
  if (peerUdp.begin(PEER_PORT)) {
    LOGI("peers: UDP %d, id %s, priority %d",
         PEER_PORT, g_selfPeerId.c_str(), g_peerPriority);
  } else {
    LOGW("peers: UDP %d bind failed; running as a single server", PEER_PORT);
    // No peer socket: single-server mode, take leadership immediately.
    g_peerIsLeader = true;
    peerApplyMdnsRole(true);
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
    // [node-sharing] Repeat the takeover announcement inside the window.
    if (g_peerIsLeader && g_readyNotifyUntilMs != 0 &&
        (long)(now - g_readyNotifyUntilMs) < 0) {
      peerNotifyReady();
    } else if (g_readyNotifyUntilMs != 0) {
      g_readyNotifyUntilMs = 0;
    }
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
  uint32_t bestUptime = millis();
  for (int i = 0; i < n; i++) {
    if ((unsigned long)(now - copy[i].lastSeenMs) > PEER_DEAD_MS) continue;
    if (peerOutranks(copy[i].priority, copy[i].uptimeMs, copy[i].id,
                     bestPrio, bestUptime, bestId)) {
      bestPrio   = copy[i].priority;
      bestUptime = copy[i].uptimeMs;
      bestId     = copy[i].id;
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
  String f;
  f.reserve(768);   // built on every /api/state poll; avoid heap churn
  f = "\"role\":\"";
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
