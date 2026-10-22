#pragma once
// ============================================================================
//  EVDRoutes.h - AsyncWebServer route table
// ============================================================================
#include "EVDUDP.h"
#include "EVDCTRL_GZ.h"     // EVDCTRL_HTML_GZ / EVDCTRL_HTML_GZ_LEN
                            // (generated from EVDCTRL.h by gen_gz.py;
                            //  EVDCTRL.h itself must NOT be #included here
                            //  or ~140 KB of raw HTML lands in .rodata)
#include "EVDOTA.h"         // handleOtaPlan / Status / Apply / Upload*
#include "EVDSTS.h"         // handleApiState / handleApiStats
#include "EVDLivePlayer.h"  // EVDLIVEPLAYER_HTML + setup_live_player()

inline void setup_http_server() {
  LOGI("Configuring Async HTTP server...");

  // ---- OTA ----------------------------------------------------------------
  // ---- OTA ----------------------------------------------------------------
  server.on("/api/ota/plan",   HTTP_GET,  handleOtaPlan);
  server.on("/api/ota/status", HTTP_GET,  handleOtaStatus);
  server.on("/api/ota/apply",  HTTP_POST, [](AsyncWebServerRequest *){},
            nullptr, handleOtaApply);

  // [ota-upload-route]
  // The dashboard's Update wizard POSTs the .bin to /api/ota/upload via XHR
  // so it can drive a real progress bar. handleOtaUploadFinal fires only when
  // the full body has been received; handleOtaUploadChunk writes each
  // multipart fragment to LittleFS and, at index == 0, registers a per-request
  // request->onDisconnect() handler so a browser abort cannot leave the audio
  // pipeline parked. 4-argument form: onRequest + onUpload.
  server.on("/api/ota/upload", HTTP_POST,
            handleOtaUploadFinal,
            handleOtaUploadChunk);

  server.on("/api/fw", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json",
                  String("{\"version\":\"") + FW_VERSION + "\"}");
  });

  // ---- Dashboard + settings ----------------------------------------------
  //
  // The dashboard page carries the entire Update-tab JavaScript. If the
  // browser caches it across a server firmware update, an old refreshPlan()
  // keeps polling /api/ota/plan with stale expectations and can report
  // "Offline" against a perfectly healthy backend. no-store is the only
  // header that guarantees a fresh fetch on every navigation.
  //
  // The page is stored on flash as a gzip-compressed byte array
  // (EVDCTRL_HTML_GZ, generated from EVDCTRL.h by gen_gz.py / gen_gz.bat).
  // Three things matter here and all three are load-bearing:
  //   1. The 4-argument beginResponse_P overload is used so the byte count
  //      is explicit. The 3-argument overload assumes a NUL-terminated C
  //      string; gzip data contains 0x00 bytes, so the response would be
  //      truncated at the first zero and the browser would refuse to inflate
  //      it. EVDCTRL_HTML_GZ_LEN is the exact length of the array.
  //   2. Content-Encoding: gzip tells the browser to inflate the body before
  //      parsing. Every modern browser sends Accept-Encoding: gzip on the
  //      initial navigation, so this is always correct to send unconditionally
  //      to a browser. (curl needs --compressed to do the same.)
  //   3. Content-Type is still text/html; it describes the *decoded* body,
  //      not the wire bytes. Setting it to application/gzip would make the
  //      browser download the file instead of rendering it.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *r =
        request->beginResponse_P(200, "text/html; charset=utf-8",
                                 EVDCTRL_HTML_GZ, EVDCTRL_HTML_GZ_LEN);
    r->addHeader("Content-Encoding", "gzip");
    r->addHeader("Cache-Control", "no-store, max-age=0");
    r->addHeader("Pragma", "no-cache");
    request->send(r);
  });
  // ---- Network settings (inline dashboard tab + standalone page) ----------
  // Four routes the dashboard Network tab depends on:
  //   GET  /settings            standalone HTML page (EVDNTW.h)
  //   POST /save                validate + kick off connection test
  //   GET  /api/network/state   JSON snapshot to populate the inline form
  //   GET  /api/network-test    poll connection-test progress
  // Omitting /save or /api/network/state silently breaks the Network tab:
  // loadNetworkState() gets a 404 and returns early, and the "Save &
  // Reconnect" button ends up throwing an HTTP error instead of applying
  // the new Wi-Fi config.
  server.on("/settings",          HTTP_GET,  handleNetworkSettings);
  server.on("/save",              HTTP_POST, handleSaveNetworkSettings);
  server.on("/api/network/state", HTTP_GET,  handleNetworkState);
  server.on("/api/network-test",  HTTP_GET,  handleNetworkTestStatus);
  server.on("/api/wifi/scan/start",  HTTP_GET, handleWifiScanStart);
  server.on("/api/wifi/scan/status", HTTP_GET, handleWifiScanStatus);
  server.on("/api/restart",       HTTP_POST, handleRestart);

  // ---- State / stats ------------------------------------------------------
  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *request) { handleApiState(request); });
  server.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest *request) { handleApiStats(request); });

  // ---- Serial-log verbosity (0..3). Dashboard log tabs use this to gate the
  // Serial.printf output while a tab is toggled off.
  server.on("/api/log/level", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json",
              String("{\"ok\":true,\"level\":") + String(gSerialLogLevel) + "}");
  });
  server.on("/api/log/level", HTTP_POST,
    [](AsyncWebServerRequest *){},
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
      StaticJsonDocument<64> in;
      if (deserializeJson(in, data, len) || !in["level"].is<int>()) {
        req->send(400, "application/json", "{\"ok\":false}");
        return;
      }
      int v = in["level"] | 1;
      if (v < 0) v = 0;
      if (v > 3) v = 3;
      gSerialLogLevel = (uint8_t)v;
      req->send(200, "application/json",
                String("{\"ok\":true,\"level\":") + String(v) + "}");
    });

  // ---- Serial-log tail ----------------------------------------------------
  //  Returns every log-ring entry whose seq is greater than the client's
  //  ?since= cursor, up to the ring's capacity, as a single JSON object:
  //
  //      {"ok":true,"lines":[{"seq":N,"ms":T,"level":"I|W|E","text":"..."}],
  //       "nextSeq":N}
  //
  //  The client advances its cursor to nextSeq and calls again. First call
  //  uses since=0 to pull the entire ring contents. Entries are snapshotted
  //  under the same critical section the writer uses, then formatted to
  //  JSON outside the lock so a slow String append never holds a spinlock.
  //
  //  logTailMutex guards the shared static snap[] buffer below. On the
  //  default single-worker AsyncTCP configuration the handler is already
  //  serialised and the mutex is a no-op; it exists so a multi-worker
  //  configuration cannot have two requests interleaving writes into the
  //  same static array. The busy path returns 503 rather than blocking,
  //  so a hung request can never stall the dashboard's 1 s poll cadence.
  server.on("/api/log/tail", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!lockFor(logTailMutex, 100)) {
      req->send(503, "application/json",
                "{\"ok\":false,\"error\":\"log tail busy, retry\"}");
      return;
    }

    uint32_t since = 0;
    if (req->hasParam("since")) since = (uint32_t)req->getParam("since")->value().toInt();

    // Static deliberately: 32 × ~208 B ≈ 6.6 KB is larger than the default
    // 4 KB AsyncTCP task stack, so a stack allocation would overflow the
    // first time this handler runs. logTailMutex above makes the sharing
    // safe across workers.
    static LogRingEntry snap[EVD_LOG_RING_LEN];
    uint32_t endSeq = 0, count = 0;
    portENTER_CRITICAL(&gLogRingMux);
      endSeq = gLogRingSeq;
      uint32_t oldestSeq = (endSeq >= EVD_LOG_RING_LEN)
                             ? (endSeq - EVD_LOG_RING_LEN + 1) : 1;
      uint32_t startSeq  = since + 1;
      if (startSeq < oldestSeq) startSeq = oldestSeq;
      for (uint32_t s = startSeq; s <= endSeq && count < EVD_LOG_RING_LEN; s++) {
        uint32_t idx = (s - 1) % EVD_LOG_RING_LEN;
        if (gLogRing[idx].seq != s) continue;
        snap[count++] = gLogRing[idx];
      }
    portEXIT_CRITICAL(&gLogRingMux);

    String body;
    body.reserve(256 + count * 220);
    body  = "{\"ok\":true,\"lines\":[";
    for (uint32_t i = 0; i < count; i++) {
      if (i) body += ",";
      body += "{\"seq\":";   body += String(snap[i].seq);
      body += ",\"ms\":";    body += String(snap[i].ms);
      body += ",\"level\":\""; body += snap[i].level; body += "\"";
      body += ",\"text\":\"";
      for (const char *p = snap[i].text; *p; ++p) {
        char c = *p;
        if (c == '"' || c == '\\') body += '\\';
        else if (c == '\n') { body += "\\n"; continue; }
        else if (c == '\r') continue;
        body += c;
      }
      body += "\"}";
    }
    body += "],\"nextSeq\":"; body += String(endSeq); body += "}";
    AsyncWebServerResponse *r =
        req->beginResponse(200, "application/json", body);
    r->addHeader("Cache-Control", "no-store, max-age=0");
    req->send(r);

    unlockFor(logTailMutex);
  });


  // ---- OGG test stream ----------------------------------------------------
  server.on("/stream.ogg", HTTP_GET, handleOggTestStream);

  // ---- Player + bridge ----------------------------------------------------
  server.on("/api/player/register", HTTP_POST, [](AsyncWebServerRequest *){}, nullptr, handlePlayerRegistration);
  server.on("/api/node_stats",      HTTP_POST, [](AsyncWebServerRequest *){}, nullptr, handleNodeStats);
  server.on("/api/player/ota_result", HTTP_POST, [](AsyncWebServerRequest *){}, nullptr, handleOtaResult);
  server.on("/api/source",          HTTP_POST, [](AsyncWebServerRequest *){}, nullptr, handleApiSourceBridge);

  server.on("/api/cmd",             HTTP_POST, [](AsyncWebServerRequest *){}, nullptr, handleApiCmdBridge);
  server.on("/api/config",          HTTP_POST, [](AsyncWebServerRequest *){}, nullptr, handleApiConfig);

  // ---- Zones --------------------------------------------------------------
  server.on("/api/zones/create", HTTP_POST, [](AsyncWebServerRequest *){}, nullptr, handleZonesCreate);
  server.on("/api/zones/delete", HTTP_POST, [](AsyncWebServerRequest *){}, nullptr, handleZonesDelete);
  server.on("/api/zones/move",   HTTP_POST, [](AsyncWebServerRequest *){}, nullptr, handleZonesMove);
  server.on("/api/zones/config", HTTP_POST, [](AsyncWebServerRequest *){}, nullptr, handleZonesConfig);

  // ---- Discovery + BT -----------------------------------------------------
  server.on("/api/discover", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"name\":\"" + String(serverName) + "\",\"ip\":\"" +
                  WiFi.localIP().toString() + "\",\"hostname\":\"" +
                  String(serverName) + ".local\"}";
    request->send(200, "application/json", json);
  });

  server.on("/api/bt/devices", HTTP_GET, [](AsyncWebServerRequest *request) {
    String cached = "{\"devices\":[]}";
    if (lockFor(uartMutex, 20)) { cached = receiverBtJson; unlockFor(uartMutex); }
    request->send(200, "application/json", cached);
  });
  server.on("/api/bt/kick", HTTP_POST, [](AsyncWebServerRequest *request) {
    sendReceiverCommand("KICK_BT");
    request->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/bt/remove", HTTP_POST, [](AsyncWebServerRequest *){}, nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t) {
      DynamicJsonDocument doc(256);
      if (!deserializeJson(doc, data, len) && doc.containsKey("mac")) {
        sendReceiverCommand("REMOVE_BT " + doc["mac"].as<String>());
        request->send(200, "application/json", "{\"ok\":true}");
      } else {
        request->send(400, "application/json", "{\"ok\":false}");
      }
    });

  server.on("/api/source/info", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Aggregates the most recent BT_JSON the source sent over UART plus a
    // small snapshot of the receiver-link state. Used by the dashboard's
    // Source tab.
    gLastSourceInfoRequestMs = millis();
    String cached = "{\"activeMac\":\"\",\"devices\":[]}";
    if (lockFor(uartMutex, 20)) { cached = receiverBtJson; unlockFor(uartMutex); }

    String body;
    // The base fields are ~600 B; the log ring can add up to
    // SOURCE_LOG_RING_LEN * ~80 B of escaped JSON. Reserve up front so
    // String growth doesn't fragment the heap every 2 s poll.
    body.reserve(768 + SOURCE_LOG_RING_LEN * 96);
    body  = "{\"ok\":true";
    body += ",\"link\":true";
    body += ",\"btConnected\":";
    body += (receiverState == "connected") ? "true" : "false";
    body += ",\"connectedMac\":\""; body += jsonEscape(currentConnectedMacCache); body += "\"";
    body += ",\"volume\":"; body += String((int)currentSourceBtVolume);
    body += ",\"activeSource\":\""; body += jsonEscape(receiverSource); body += "\"";
    body += ",\"a2dpConnecting\":";    body += String(diagA2dpConnecting);
    body += ",\"a2dpConnected\":";     body += String(diagA2dpConnected);
    body += ",\"a2dpDisconnected\":";  body += String(diagA2dpDisconnected);
    body += ",\"avrcConnected\":";     body += String(avrcConnectedCache ? "true" : "false");
    body += ",\"volumeEvents\":";      body += String(diagVolumeEvents);
    body += ",\"uartLines\":";         body += String(diagUartRxLines);
    body += ",\"heartbeatAgeMs\":";    body += String((unsigned long)millis() - lastHeartbeatMsCache);
    // cached is the source board's full BT_JSON payload:
    //   {"activeMac":"...","devices":[...]}
    // Lift its fields to the top level so the client's j.devices is the array
    // it expects, and j.activeMac matches the source board's active pairing.
    DynamicJsonDocument cdoc(768);
    String activeMacJson = "";
    if (!deserializeJson(cdoc, cached)) {
        JsonArray devs = cdoc["devices"].as<JsonArray>();
        const char* am = cdoc["activeMac"] | "";
        activeMacJson = am;
    }
    body += ",\"activeMac\":\""; body += jsonEscape(activeMacJson); body += "\"";
    body += ",\"devices\":";
    // Reuse the cached BT_JSON body verbatim; the client parses it.
    body += cached;

    // Source-board log ring: most recent N lines, oldest first. The client
    // (see fetchSourceInfo in EVDCTRL.h) renders this as the "Log" box.
    body += ",\"log\":[";
    if (lockFor(uartMutex, 20)) {
      int start = (sourceLogRingHead + SOURCE_LOG_RING_LEN - sourceLogRingCount)
                  % SOURCE_LOG_RING_LEN;
      for (int i = 0; i < sourceLogRingCount; ++i) {
        if (i) body += ",";
        body += "\"";
        body += jsonEscape(sourceLogRing[(start + i) % SOURCE_LOG_RING_LEN]);
        body += "\"";
      }
      unlockFor(uartMutex);
    }
    body += "]";

    body += "}";
    request->send(200, "application/json", body);
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
  });

  // ---- Live MSE player ----------------------------------------------------
  // The /live page contains the MediaSource JavaScript. The WebSocket at
  // /audio.ws is registered as a handler on the same AsyncWebServer so it
  // shares the server's port and TCP stack. Both must be set up before
  // server.begin() — the WS handler is added via addHandler(), which is
  // the mechanism ESPAsyncWebServer uses for persistent-connection routes.
  server.on("/live", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *r =
        request->beginResponse_P(200, "text/html; charset=utf-8",
                                 EVDLIVEPLAYER_HTML);
    r->addHeader("Cache-Control", "no-store, max-age=0");
    request->send(r);
  });
  setup_live_player();
  add_live_player_routes(server);

  server.begin();
  LOGI("HTTP server started on port 80");
}