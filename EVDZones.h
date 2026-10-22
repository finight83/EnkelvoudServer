#pragma once
// ============================================================================
//  EVDZones.h - Zone CRUD, NVS persistence, HTTP handlers
// ============================================================================
#include "EVDHelpers.h"

// ---- Serialize / persist ---------------------------------------------------
inline String serializeZones() {
  String out;
  for (auto &z : zones) {
    if (z.name.indexOf('|') != -1 || z.name.indexOf('\n') != -1) continue;
    out += z.id + "|" + z.name + "|" + String(z.volume) + "|" + (z.muted ? "1" : "0") + "|";
    for (size_t i = 0; i < z.nodeIps.size(); ++i) {
      if (i) out += ",";
      out += z.nodeIps[i];
    }
    out += "\n";
  }
  return out;
}

inline void loadZonesFromNVS() {
  zones.clear();
  String raw;
  {
    PrefsGuard pg;                        // [v1.1.1-prefs-mutex]
    if (pg.ok()) {
      preferences.begin("enkelvoud", false);
      raw = preferences.getString("zones_v1", "");
      preferences.end();
    }
  }

  int start = 0;
  while (start < (int)raw.length()) {
    int nl = raw.indexOf('\n', start);
    String line = (nl == -1) ? raw.substring(start) : raw.substring(start, nl);
    line.trim();
    if (line.length() > 0) {
      int p1 = line.indexOf('|');
      int p2 = line.indexOf('|', p1 + 1);
      int p3 = line.indexOf('|', p2 + 1);
      int p4 = line.indexOf('|', p3 + 1);
      if (p1 > 0 && p2 > p1 && p3 > p2) {
        Zone z;
        z.id     = line.substring(0, p1);
        z.name   = line.substring(p1 + 1, p2);
        z.volume = line.substring(p2 + 1, p3).toInt();
        z.muted  = (p4 > p3) ? (line.substring(p3 + 1, p4).toInt() == 1) : false;
        if (p4 > p3) {
          String ips = line.substring(p4 + 1);
          int s = 0;
          while (s < (int)ips.length()) {
            int c = ips.indexOf(',', s);
            String ip = (c == -1) ? ips.substring(s) : ips.substring(s, c);
            ip.trim();
            if (ip.length() > 0) z.nodeIps.push_back(ip);
            if (c == -1) break;
            s = c + 1;
          }
        }
        bool hasMain = false;
        for (auto &ex : zones) if (ex.id == MAIN_ZONE_ID) hasMain = true;
        if (!hasMain || z.id != MAIN_ZONE_ID) zones.push_back(z);
      }
    }
    if (nl == -1) break;
    start = nl + 1;
  }
  bool hasMain = false;
  for (auto &ex : zones) if (ex.id == MAIN_ZONE_ID) hasMain = true;
  if (!hasMain) zones.push_back(Zone{MAIN_ZONE_ID, "Main Room", 100, false, {}});
}

inline void saveZonesToNVS() {
  // [defer-nvs-zones] Do not touch flash here.  All four zone HTTP
  // handlers run on Core 0 (AsyncWebServer); a synchronous Preferences
  // write from handleZonesConfig on every slider release can stall the
  // flash cache while audioReadTask is mid-I2S-read on Core 1.  The
  // in-RAM `zones` vector is authoritative; the deferred writer in
  // loop() (EVDMain.h) snapshots it under zonesMutex and writes
  // zones_v1 on the next >1 s I2S-quiet window, alongside every other
  // deferred setting.  Matches the pattern already used by
  // handlePlayerRegistration()'s auto-assign-to-Main path.
  settingsDirty      = true;
  settingsDirtySince = millis();
}

// ---- HTTP handlers ---------------------------------------------------------
inline void handleZonesCreate(AsyncWebServerRequest *request, uint8_t *data,
                              size_t len, size_t, size_t) {
  DynamicJsonDocument in(256);
  if (deserializeJson(in, data, len)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}"); return;
  }
  String name = in["name"] | "";
  if (name.length() == 0 || name.length() > 24) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"zone name must be 1-24 alphanumeric characters\"}");
    return;
  }
  for (size_t i = 0; i < name.length(); ++i) {
    char c = name[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
      request->send(400, "application/json",
                    "{\"ok\":false,\"error\":\"zone name must contain only letters and digits\"}");
      return;
    }
  }

  Zone z; z.id = "z" + String((uint32_t)millis(), HEX);
  z.name = name; z.volume = 100; z.muted = false;

  if (!lockFor(zonesMutex, 80)) {
    request->send(503, "application/json", "{\"ok\":false,\"error\":\"busy\"}"); return;
  }
  zones.push_back(z);
  saveZonesToNVS();
  unlockFor(zonesMutex);
  broadcastNodeConfig();
  request->send(200, "application/json", String("{\"ok\":true,\"id\":\"") + z.id + "\"}");
}

inline void handleZonesDelete(AsyncWebServerRequest *request, uint8_t *data,
                              size_t len, size_t, size_t) {
  DynamicJsonDocument in(256);
  if (deserializeJson(in, data, len)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
  String id = in["zoneId"] | "";
  if (id.length() == 0 || id == MAIN_ZONE_ID) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"cannot delete main\"}"); return;
  }
  if (!lockFor(zonesMutex, 80)) {
    request->send(503, "application/json", "{\"ok\":false,\"error\":\"busy\"}"); return;
  }
  // Nodes that were in the deleted zone get moved to Main Room. Capture
  // their IPs plus Main's current zone state, and push the new state to
  // them after unlocking.
  std::vector<String> movedIps;
  int  mainVol  = 100;
  bool mainMute = false;
  for (auto it = zones.begin(); it != zones.end(); ++it) {
    if (it->id == id) {
      for (auto &n : it->nodeIps) {
        movedIps.push_back(n);
        for (auto &m : zones) {
          if (m.id == MAIN_ZONE_ID) {
            // [v1.1.1-zone-dedup] skip if already present
            bool dup = false;
            for (auto &existing : m.nodeIps) if (existing == n) { dup = true; break; }
            if (!dup) m.nodeIps.push_back(n);
            break;
          }
        }
      }
      zones.erase(it); break;
    }
  }
  for (auto &m : zones)
    if (m.id == MAIN_ZONE_ID) { mainVol = m.volume; mainMute = m.muted; break; }

  saveZonesToNVS();
  unlockFor(zonesMutex);

  for (auto &ip : movedIps) sendZoneConfigToIp(ip, mainVol, mainMute);

  // A deleted zone is an explicit "these nodes are no longer part of the
  // active topology" signal. Do NOT wait for the perf TTL to expire before
  // the Status page reflects it -- the operator just acted, and the page
  // should look different the moment the page refreshes. Nodes that were
  // moved to Main Room keep their perf entries only if they keep reporting;
  // otherwise they are removed here, on the spot.
  for (auto &ip : movedIps) removeNodePerf(ip);

  broadcastNodeConfig();
  request->send(200, "application/json", "{\"ok\":true}");
}

inline void handleZonesMove(AsyncWebServerRequest *request, uint8_t *data,
                            size_t len, size_t, size_t) {
  DynamicJsonDocument in(256);
  if (deserializeJson(in, data, len)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
  String ip  = in["ip"]     | "";
  String zid = in["zoneId"] | "";
  ip.trim();
  if (ip.length() == 0) { request->send(400, "application/json", "{\"ok\":false}"); return; }

  if (!lockFor(zonesMutex, 80)) {
    request->send(503, "application/json", "{\"ok\":false,\"error\":\"busy\"}"); return;
  }
  bool targetOk = false;
  for (auto &z : zones) if (z.id == zid) targetOk = true;
  if (!targetOk) { unlockFor(zonesMutex); request->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown zone\"}"); return; }

  for (auto &z : zones)
    for (auto it = z.nodeIps.begin(); it != z.nodeIps.end(); ++it)
      if (*it == ip) { z.nodeIps.erase(it); break; }
  int  newZvol  = 100;
  bool newZmute = false;
  for (auto &z : zones)
    if (z.id == zid) {
      z.nodeIps.push_back(ip);
      newZvol  = z.volume;
      newZmute = z.muted;
      break;
    }

  saveZonesToNVS();
  unlockFor(zonesMutex);

  // Send the destination zone's volume/mute to the node immediately so it
  // does not keep playing at the previous zone's level until the next
  // broadcastNodeConfig tick.
  sendZoneConfigToIp(ip, newZvol, newZmute);

  broadcastNodeConfig();
  request->send(200, "application/json", "{\"ok\":true}");
}

inline void handleZonesConfig(AsyncWebServerRequest *request, uint8_t *data,
                              size_t len, size_t, size_t) {
  DynamicJsonDocument in(256);
  if (deserializeJson(in, data, len)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
  String id = in["zoneId"] | "";
  if (id.length() == 0) { request->send(400, "application/json", "{\"ok\":false}"); return; }

  // Captured under the zone lock, sent after unlocking. Sending UDP while
  // holding zonesMutex would stall every other zone read behind a network
  // syscall, which is exactly the kind of thing that made zone changes feel
  // laggy in the previous build.
  std::vector<String> targetIps;
  int  newZvol  = 100;
  bool newZmute = false;

  if (!lockFor(zonesMutex, 80)) {
    request->send(503, "application/json", "{\"ok\":false,\"error\":\"busy\"}"); return;
  }
  bool ok = false;
  for (auto &z : zones) {
    if (z.id == id) {
      if (!in["name"].isNull()) {
        String n = in["name"] | "";
        if (n.length() == 0 || n.length() > 24) {
          unlockFor(zonesMutex);
          request->send(400, "application/json",
                        "{\"ok\":false,\"error\":\"zone name must be 1-24 alphanumeric characters\"}");
          return;
        }
        for (size_t i = 0; i < n.length(); ++i) {
          char c = n[i];
          if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            unlockFor(zonesMutex);
            request->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"zone name must contain only letters and digits\"}");
            return;
          }
        }
        z.name = n;
      }
      if (!in["volume"].isNull()) {
        int v = in["volume"] | 100;
        z.volume = v < 0 ? 0 : (v > 100 ? 100 : v);
      }
      if (!in["muted"].isNull()) z.muted = in["muted"] | false;
      targetIps = z.nodeIps;
      newZvol   = z.volume;
      newZmute  = z.muted;
      ok = true; break;
    }
  }
  if (!ok) { unlockFor(zonesMutex); request->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown zone\"}"); return; }
  saveZonesToNVS();
  unlockFor(zonesMutex);

  // Direct unicast to every node in the zone, whether or not it is in the
  // subscriber table yet. broadcastNodeConfig() below only reaches
  // subscribers, which is why a freshly-booted node used to miss the
  // change entirely.
  for (auto &ip : targetIps) sendZoneConfigToIp(ip, newZvol, newZmute);

  // Still refresh master volume / mode on all subscribers.
  broadcastNodeConfig();
  request->send(200, "application/json", "{\"ok\":true}");
}