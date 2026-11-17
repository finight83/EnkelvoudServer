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

// [empty-body-post-reject]
// Routes registered with the 5-arg on() form (uri, method, onRequest,
// onUpload, onBody) MUST answer Content-Length: 0 requests. Without a
// response here, the onBody handler never fires (no chunks arrive) and
// the empty onRequest lambda returns without touching the socket — the
// client blocks until TCP keepalive expires (~2 min). Any curl / fetch /
// proxy probe without a body triggers this. This function is the onRequest
// argument for every 5-arg route; it is a no-op when a body is present
// (the onBody handler owns the response in that case).
inline void rejectEmptyBodyPost(AsyncWebServerRequest *request) {
  if (request->contentLength() == 0) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"empty body\"}");
  }
}

inline void setup_http_server() {
  LOGI("Configuring Async HTTP server...");

  // ---- OTA ----------------------------------------------------------------
  // ---- OTA ----------------------------------------------------------------
  server.on("/api/ota/plan",   HTTP_GET,  handleOtaPlan);
  server.on("/api/ota/status", HTTP_GET,  handleOtaStatus);
  // [ota-delete-route] Handler uses the 3-arg onBody form (see handleOtaDelete).
  // [ota-delete-empty-body-guard] The 5-arg form only fires the body handler
  // once a chunk arrives. A client POSTing with Content-Length: 0 (curl -X
  // POST, a fetch() without a body, an HTTP proxy probe) never triggers
  // handleOtaDelete, so no response is sent and the connection stalls until
  // TCP keepalive. Reject empty bodies in the request handler, exactly like
  // /api/ota/apply does. Non-empty bodies are handled entirely in the body
  // callback; this lambda sends nothing in that case.
  server.on("/api/ota/delete", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (request->contentLength() == 0) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"empty body\"}");
              }
            },
            nullptr,
            handleOtaDelete);
  // [ota-apply-empty-body-guard]
  // The 5-arg form only fires handleOtaApply once a body chunk arrives.
  // A client that POSTs with Content-Length: 0 (curl -X POST, browser
  // fetch() without a body, or an HTTP proxy probe) never triggers the
  // body handler, so no response is ever sent and the client blocks
  // until TCP keepalive reaps the socket. Reject empty-body requests in
  // the request handler before the body path is expected.
  server.on("/api/ota/apply", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (request->contentLength() == 0) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"empty body\"}");
              }
              // Non-empty body: handleOtaApply() sends the response on
              // the final chunk; this handler sends nothing.
            },
            nullptr, handleOtaApply);

  // [ota-upload-route]
  // The dashboard's Update wizard POSTs the .bin to /api/ota/upload via XHR
  // so it can drive a real progress bar. handleOtaUploadFinal fires only when
  // the full body has been received; handleOtaUploadChunk writes each
  // multipart fragment to LittleFS and, at index == 0, registers a per-request
  // request->onDisconnect() handler so a browser abort cannot leave the audio
  // pipeline parked. 4-argument form: onRequest + onUpload.
  // [ota-upload-empty-body-guard] An empty-body POST (curl -X POST, a
  // browser preflight, a fetch() without body) never fires
  // handleOtaUploadFinal, holds the socket until TCP keepalive, and
  // leaves the OTA wizard's connection hanging. Reject empty bodies in
  // the request handler, mirroring /api/ota/apply and /api/ota/delete.
  // Non-empty bodies are handled entirely in handleOtaUploadChunk.
  server.on("/api/ota/upload", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (request->contentLength() == 0) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"empty body\"}");
                return;
              }
              // [ota-upload-final-handler] This is the ONLY path that sends
              // the success/failure body to the client. It must be called
              // after every upload — otherwise xhr.onload never fires and
              // the browser reports "network error" on a successful upload.
              handleOtaUploadFinal(request);
            },
            handleOtaUploadChunk);

  server.on("/api/fw", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json",
                  String("{\"version\":\"") + FW_VERSION + "\"}");
  });

  // ---- DLNA / UPnP MediaRenderer -----------------------------------------
  // All handlers and helper functions live in EVDDLNA.h. The .ino includes
  // that header before this file, so no new include is needed here.
  server.on("/description.xml",         HTTP_GET, dlnaHandleDescription);
  server.on("/upnp/scpd/avt.xml",       HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/xml; charset=utf-8", dlnaScpdAvt());
  });
  server.on("/upnp/scpd/rcs.xml",       HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/xml; charset=utf-8", dlnaScpdRcs());
  });
  server.on("/upnp/scpd/cms.xml",       HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/xml; charset=utf-8", dlnaScpdCms());
  });
  server.on("/upnp/control/avt",        HTTP_POST,
            rejectEmptyBodyPost, nullptr, dlnaHandleControlAvt);
  server.on("/upnp/control/rcs",        HTTP_POST,
            rejectEmptyBodyPost, nullptr, dlnaHandleControlRcs);
  server.on("/upnp/control/cms",        HTTP_POST,
            rejectEmptyBodyPost, nullptr, dlnaHandleControlCms);
  // GENA event subscriptions — minimal NO-OP.
  //
  // [upnp-event-http-any] ESPAsyncWebServer's WebRequestMethod enum does
  // not have HTTP_SUBSCRIBE. Unknown verbs (SUBSCRIBE / NOTIFY / anything
  // else) are stored as HTTP_ANY inside the request, and the router
  // matches a handler whose method is HTTP_ANY. Registering HTTP_ANY here
  // therefore catches SUBSCRIBE from a real DLNA controller and returns a
  // minimal 200 — which is enough for every controller I have tested.
  // Eventing itself is not implemented; a controller that polls
  // GetTransportInfo (the vast majority) works identically.
  server.on("/upnp/event/avt",          HTTP_ANY, [](AsyncWebServerRequest *r) {
    r->send(200, "text/xml",
            "<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\"/>");
  });
  server.on("/upnp/event/rcs",          HTTP_ANY, [](AsyncWebServerRequest *r) {
    r->send(200, "text/xml",
            "<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\"/>");
  });
  server.on("/upnp/event/cms",          HTTP_ANY, [](AsyncWebServerRequest *r) {
    r->send(200, "text/xml",
            "<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\"/>");
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
    // [ota-block-dashboard] Prevent the 50 KB gzip response from
    // allocating and crashing the server while OTA is pushing firmware.
    if (otaUploadInProgress || otaInProgress) {
      request->send(503, "text/plain", "OTA in progress. Dashboard paused.");
      return;
    }
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
    rejectEmptyBodyPost,
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len,
       size_t index, size_t total) {
      // [body-single-chunk] Level body is ~15 B and never fragments in
      // practice. Reject fragments rather than introduce a shared static
      // accumulator, which would race two concurrent POSTs.
      if (index != 0 || len != total) {
        req->send(413, "application/json",
                  "{\"ok\":false,\"error\":\"body fragmented; retry\"}");
        return;
      }
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

  // ---- DLNA on/off (persisted). The work can block for seconds (SSDP task
  // exit, fetch stop), so it runs in a short-lived task, not the async handler.
  server.on("/api/dlna", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json",
              String("{\"ok\":true,\"enabled\":") + (dlnaEnabled ? "true" : "false") + "}");
  });
  server.on("/api/dlna", HTTP_POST,
    rejectEmptyBodyPost,
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len,
       size_t index, size_t total) {
      if (index != 0 || len != total) {
        req->send(413, "application/json",
                  "{\"ok\":false,\"error\":\"body fragmented; retry\"}");
        return;
      }
      StaticJsonDocument<64> in;
      if (deserializeJson(in, data, len) || !in["enabled"].is<bool>()) {
        req->send(400, "application/json", "{\"ok\":false}");
        return;
      }
      static volatile bool pending = false;
      if (pending) {
        req->send(409, "application/json", "{\"ok\":false,\"error\":\"busy\"}");
        return;
      }
      bool en = in["enabled"].as<bool>();
      pending = true;
      static bool target;
      target = en;
      BaseType_t rc = xTaskCreate([](void *) {
        dlnaSetEnabled(target);
        pending = false;
        vTaskDelete(nullptr);
      }, "dlnaToggle", 4096, nullptr, 1, nullptr);
      if (rc != pdPASS) {
        pending = false;
        req->send(500, "application/json", "{\"ok\":false}");
        return;
      }
      req->send(200, "application/json",
                String("{\"ok\":true,\"enabled\":") + (en ? "true" : "false") + "}");
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
    // [ota-gate-log-tail] Even after the earlier 24 KB cap, this handler
    // reserves up to 24 KB of heap per call. During OTA that competes
    // directly with the OTA chunk writer for free blocks. The Status
    // page's log card will show stale data during an update — correct
    // behaviour; the OTA wizard is what matters.
    if (otaUploadInProgress || otaInProgress) {
      req->send(503, "application/json",
                "{\"ok\":false,\"error\":\"OTA in progress\"}");
      return;
    }
    if (!lockFor(logTailMutex, 100)) {
      req->send(503, "application/json",
                "{\"ok\":false,\"error\":\"log tail busy, retry\"}");
      return;
    }

    uint32_t since = 0;
    if (req->hasParam("since")) since = (uint32_t)req->getParam("since")->value().toInt();

    // [log-tail-psram]
    // Was `static LogRingEntry snap[EVD_LOG_RING_LEN]` in BSS = 6.6 KB
    // of internal DRAM permanently reserved for a transient snapshot.
    // The heap is at 92 %; PSRAM is at 1 %. Move the snapshot to PSRAM
    // via heap_caps_malloc, freed on every exit path (RAII guard).
    // AsyncTCP worker is single-threaded (same assumption as
    // logTailMutex) so no additional lock is needed.
    //
    // Falls back to the existing 503 path on PSRAM allocation failure
    // (which cannot happen at 1 % used, but is the correct defensive
    // default for a handler that must never block the log poller).
    struct PSRAMLogSnap {
      LogRingEntry *p = nullptr;
      PSRAMLogSnap() {
        p = (LogRingEntry*)heap_caps_malloc(
              sizeof(LogRingEntry) * EVD_LOG_RING_LEN,
              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      }
      ~PSRAMLogSnap() { if (p) heap_caps_free(p); }
      PSRAMLogSnap(const PSRAMLogSnap&) = delete;
      PSRAMLogSnap& operator=(const PSRAMLogSnap&) = delete;
    } snapHolder;
    if (!snapHolder.p) {
      unlockFor(logTailMutex);
      req->send(503, "application/json",
                "{\"ok\":false,\"error\":\"psram alloc failed\"}");
      return;
    }
    LogRingEntry *snap = snapHolder.p;
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
      // [log-json-escape] Use jsonEscape() — the inline version passed
      // tabs and other <0x20 control bytes through raw, so a single
      // LOGx("\t…") broke the whole tail response for strict parsers.
      body += jsonEscape(String(snap[i].text));
      body += "\"}";
    }
    body += "],\"nextSeq\":"; body += String(endSeq); body += "}";
    AsyncWebServerResponse *r =
        req->beginResponse(200, "application/json", body);
    r->addHeader("Cache-Control", "no-store, max-age=0");
    req->send(r);

    unlockFor(logTailMutex);
  });


  server.on("/api/player/register", HTTP_POST, rejectEmptyBodyPost, nullptr, handlePlayerRegistration);
  server.on("/api/audio/encoding",           HTTP_GET,  handleGetAudioEncoding);
  server.on("/api/audio/encoding",           HTTP_POST, rejectEmptyBodyPost, nullptr, handlePostAudioEncoding);
  server.on("/api/audio/encoding/revert",    HTTP_POST, handlePostAudioEncodingRevert);
  server.on("/api/audio/encoding/defaults",  HTTP_POST, handlePostAudioEncodingDefaults);
  server.on("/api/node_stats",      HTTP_POST, rejectEmptyBodyPost, nullptr, handleNodeStats);
  server.on("/api/player/ota_result", HTTP_POST, rejectEmptyBodyPost, nullptr, handleOtaResult);
  server.on("/api/source",          HTTP_POST, rejectEmptyBodyPost, nullptr, handleApiSourceBridge);

  server.on("/api/cmd",             HTTP_POST, rejectEmptyBodyPost, nullptr, handleApiCmdBridge);
  server.on("/api/config",          HTTP_POST, rejectEmptyBodyPost, nullptr, handleApiConfig);

  // ---- Zones --------------------------------------------------------------
  server.on("/api/zones/create", HTTP_POST, rejectEmptyBodyPost, nullptr, handleZonesCreate);
  server.on("/api/zones/delete", HTTP_POST, rejectEmptyBodyPost, nullptr, handleZonesDelete);
  server.on("/api/zones/move",   HTTP_POST, rejectEmptyBodyPost, nullptr, handleZonesMove);
  server.on("/api/zones/config", HTTP_POST, rejectEmptyBodyPost, nullptr, handleZonesConfig);
  server.on("/api/zones/remove-node", HTTP_POST, rejectEmptyBodyPost, nullptr, handleZonesRemoveNode);
  
  // ---- Discovery + BT -----------------------------------------------------
  server.on("/api/discover", HTTP_GET, [](AsyncWebServerRequest *request) {
    // [discover-escape] Escape serverName even though the current rule is
    // alphanumeric-only — future rule changes must not become a JSON hole.
    String safeName = jsonEscape(serverName);
    String json = "{\"name\":\"" + safeName + "\",\"ip\":\"" +
                  WiFi.localIP().toString() + "\",\"hostname\":\"" +
                  safeName + ".local\"}";
    request->send(200, "application/json", json);
  });

  server.on("/api/bt/devices", HTTP_GET, [](AsyncWebServerRequest *request) {
    String cached = "{\"devices\":[]}";
    if (lockFor(uartMutex, 20)) { cached = receiverBtJson; unlockFor(uartMutex); }
    request->send(200, "application/json", cached);
  });
  server.on("/api/bt/kick", HTTP_POST, [](AsyncWebServerRequest *request) {
    // [ota-gate-bt] UART writes are fast, but during OTA every AsyncTCP
    // scheduling slot matters. A stray button click on the dashboard (or
    // a cached tab) can otherwise emit "CMD:KICK_BT" mid-flash and cost
    // the OTA push one TCP ACK window.
    if (otaUploadInProgress || otaInProgress || otaPauseAudio) {
      request->send(503, "application/json",
                    "{\"ok\":false,\"error\":\"OTA in progress\"}");
      return;
    }
    sendReceiverCommand("KICK_BT");
    request->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/bt/remove", HTTP_POST, rejectEmptyBodyPost, nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      // [bt-remove-single-chunk] Same fragmented-body guard as every other
      // 5-arg route: a partial parse would silently drop the MAC and the
      // source board would keep the pairing alive.
      if (index != 0 || len != total) {
        if (index != 0) return;
        request->send(413, "application/json",
                      "{\"ok\":false,\"error\":\"body fragmented; retry\"}");
        return;
      }
      if (otaUploadInProgress || otaInProgress || otaPauseAudio) {
        request->send(503, "application/json",
                      "{\"ok\":false,\"error\":\"OTA in progress\"}");
        return;
      }
      DynamicJsonDocument doc(256);
      if (!deserializeJson(doc, data, len) && doc["mac"].is<const char*>()) {
        String mac = doc["mac"].as<String>();
        bool macOk = mac.length() == 17;
        for (size_t i = 0; macOk && i < 17; ++i) {
          char c = mac[i];
          macOk = isxdigit((unsigned char)c) || c == ':';
        }
        if (!macOk) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad mac\"}");
          return;
        }
        sendReceiverCommand("REMOVE_BT " + mac);
        request->send(200, "application/json", "{\"ok\":true}");
      } else {
        request->send(400, "application/json", "{\"ok\":false}");
      }
    });

  server.on("/api/source/info", HTTP_GET, [](AsyncWebServerRequest *request) {
    // [ota-gate-src-info] This handler parses the cached BT_JSON, walks
    // the source log ring, escapes every entry, and reserves 768 + 20×96
    // = ~2.7 KB of String. On an already-fragmented heap during OTA that
    // reserve can fail, or succeed and consume the last free block the
    // OTA chunk writer needed.
    if (otaUploadInProgress || otaInProgress) {
      request->send(503, "application/json",
                    "{\"ok\":false,\"error\":\"OTA in progress\"}");
      return;
    }
    // Aggregates the most recent BT_JSON the source sent over UART plus a
    // small snapshot of the receiver-link state. Used by the dashboard's
    // Source tab.
    gLastSourceInfoRequestMs = millis();
    // [srcstat-string-snapshot] Same reasoning as EVDSTS.h: take ONE lock,
    // copy everything String-shaped that this handler reads, then build
    // the JSON from the locals.
    String cached = "{\"activeMac\":\"\",\"devices\":[]}";
    String snapMac, snapSrc, snapState;
    if (lockFor(uartMutex, 20)) {
      cached    = receiverBtJson;
      snapMac   = currentConnectedMacCache;
      snapSrc   = receiverSource;
      snapState = receiverState;
      unlockFor(uartMutex);
    }

    String body;
    // The base fields are ~600 B; the log ring can add up to
    // SOURCE_LOG_RING_LEN * ~80 B of escaped JSON. Reserve up front so
    // String growth doesn't fragment the heap every 2 s poll.
    body.reserve(768 + SOURCE_LOG_RING_LEN * 96);
    // [srcinfo-link-real] link = UART heartbeat freshness, not hardcoded true.
    const unsigned long srcInfoNow = millis();
    bool srcLinkFresh = (lastHeartbeatMsCache != 0) &&
                        ((srcInfoNow - lastHeartbeatMsCache) < 30000UL);
    body  = "{\"ok\":true";
    body += ",\"link\":";
    body += srcLinkFresh ? "true" : "false";
    body += ",\"btConnected\":";
    body += (snapState == "connected") ? "true" : "false";
    body += ",\"connectedMac\":\""; body += jsonEscape(snapMac); body += "\"";
    body += ",\"volume\":"; body += String((int)currentSourceBtVolume);
    body += ",\"activeSource\":\""; body += jsonEscape(snapSrc); body += "\"";
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
    DynamicJsonDocument cdoc(2048);
    String activeMacJson = "";
    if (!deserializeJson(cdoc, cached)) {
        JsonArray devs = cdoc["devices"].as<JsonArray>();
        const char* am = cdoc["activeMac"] | "";
        activeMacJson = am;
    }
    // [srcinfo-devices-array] Serialize ONLY cdoc["devices"] as an array.
    // The old code appended the whole {"activeMac":...,"devices":[...]}
    // object, so j.devices was an object and every client that did
    // j.devices.length / .map got undefined/0.
    body += ",\"activeMac\":\""; body += jsonEscape(activeMacJson); body += "\"";
    body += ",\"devices\":[";
    {
      bool devFirst = true;
      for (JsonVariant v : cdoc["devices"].as<JsonArray>()) {
        if (!devFirst) body += ",";
        body += v.as<String>();
        devFirst = false;
      }
    }
    body += "]";

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

  // [http-begin] Bind the TCP listener. .on() only registers routes;
  // the socket is not opened until begin(). Without this every request
  // gets a TCP RST (browser: ERR_CONNECTION_REFUSED). Must be the last
  // call in setup_http_server() so no route registration races accept().
  server.begin();

  LOGI("HTTP server started on port 80");
}