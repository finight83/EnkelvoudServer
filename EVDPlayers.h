#pragma once
// ============================================================================
//  EVDPlayers.h - Player registration, receiver link, per-node perf,
//                 adaptive buffer engine, /api/config|source|cmd
// ============================================================================
#include "EVDHelpers.h"
#include "EVDUsb.h"

// ---- Receiver (upstream BT/aux module over UART) ---------------------------
inline void setupReceiverLink() {
  receiverUart.begin(115200, SERIAL_8N1, pinSrcUartRx, pinSrcUartTx);
}
inline void sendReceiverSourceCommand(const String &source) {
  if (receiverUart) receiverUart.printf("SRC:%s\n", source.c_str());
}
inline void sendReceiverCommand(const String &cmd) {
  if (receiverUart) receiverUart.printf("CMD:%s\n", cmd.c_str());
}

inline void handleReceiverLink() {
  // Fixed-size char buffer, no String heap churn. The previous version
  // appended every UART byte into a static String, which reallocated on
  // roughly every power-of-two boundary and freed at each newline. With
  // a chatty source board (BT_JSON replies on GET_BT_DEVICES, LOG ring,
  // SRCSTAT heartbeats) that produced a steady drip of malloc/free on
  // the same core as the audio pipeline. A chatty burst could stall the
  // allocator long enough to cost the encoder a scheduling slot.
  static char   rxLine[2048];
  static size_t rxLen = 0;

  while (receiverUart.available()) {
    char c = (char)receiverUart.read();
    if (c == '\n' || c == '\r') {
      if (rxLen == 0) continue;
      rxLine[rxLen] = '\0';
      while (rxLen > 0 &&
             (rxLine[rxLen - 1] == ' ' || rxLine[rxLen - 1] == '\t')) {
        rxLine[--rxLen] = '\0';
      }
      if (rxLen == 0) continue;

      if (strncmp(rxLine, "BT_JSON|", 8) == 0) {
        if (lockFor(uartMutex, 20)) {
          receiverBtJson = String(rxLine + 8);
          unlockFor(uartMutex);
        }
      } else if (strncmp(rxLine, "LOG|", 4) == 0) {
        const char *line = rxLine + 4;
        if (line[0] != '\0') {
          if (lockFor(uartMutex, 20)) {
            sourceLogRing[sourceLogRingHead] = String(line);
            sourceLogRingHead = (sourceLogRingHead + 1) % SOURCE_LOG_RING_LEN;
            if (sourceLogRingCount < SOURCE_LOG_RING_LEN) sourceLogRingCount++;
            unlockFor(uartMutex);
          }
        }
      } else if (strncmp(rxLine, "SRCSTAT|", 8) == 0) {
        StaticJsonDocument<256> d;
        if (!deserializeJson(d, rxLine + 8)) {
          if (d.containsKey("mac"))    currentConnectedMacCache = d["mac"].as<String>();
          if (d.containsKey("vol"))    currentSourceBtVolume    = (uint8_t)(d["vol"] | 0);
          if (d.containsKey("avrc"))   avrcConnectedCache       = (d["avrc"] | 0) != 0;
          if (d.containsKey("a2dpC"))  diagA2dpConnected        = (uint32_t)(d["a2dpC"] | 0UL);
          if (d.containsKey("a2dpD"))  diagA2dpDisconnected     = (uint32_t)(d["a2dpD"] | 0UL);
          if (d.containsKey("volEv"))  diagVolumeEvents         = (uint32_t)(d["volEv"] | 0UL);
          if (d.containsKey("uartL"))  diagUartRxLines          = (uint32_t)(d["uartL"] | 0UL);
          if (d.containsKey("src"))    receiverSource           = d["src"].as<String>();
          receiverState        = (d["connected"] | 0) ? "connected" : "idle";
          lastHeartbeatMsCache = millis();
        }
      } else {
        LOGI("UART RX: %s", rxLine);
      }
      rxLen = 0;
    } else {
      if (rxLen < sizeof(rxLine) - 1) {
        rxLine[rxLen++] = c;
      } else {
        // Overlong line (no newline in 511 chars). Drop rather than
        // corrupt state; a well-behaved source never emits one.
        rxLen = 0;
      }
    }
  }
}

inline bool setReceiverSource(const String &source, String &respBody, uint16_t &httpCode) {
  sendReceiverSourceCommand(source);
  receiverSource = source;
  lastReceiverSource = source;
  httpCode = 200;
  respBody = String("{\"ok\":true,\"source\":\"") + source + "\",\"transport\":\"uart\"}";
  return true;
}
inline bool bridgeSendReceiverCmd(const String &cmd, String &respBody, uint16_t &httpCode) {
  sendReceiverCommand(cmd);
  httpCode = 200;
  respBody = String("{\"ok\":true,\"cmd\":\"") + cmd + "\",\"transport\":\"uart\"}";
  return true;
}

// ---- Player registration (also the heartbeat endpoint) ---------------------
// Nodes POST here on boot and then periodically. The handler refreshes the
// subscriber table, and on first sight pushes the current config so the node
// gets masterVolume / zone / mode without a separate handshake.
inline void handlePlayerRegistration(AsyncWebServerRequest *request, uint8_t *data,
                                     size_t len, size_t, size_t) {
  String ip    = request->client()->remoteIP().toString();
  String pName = "Player Node";
  String pVer  = "";                    // NEW
  int    declaredProtoVer = -1;         // NEW: -1 = not declared (legacy/unknown firmware)

  if (len > 0 && data) {
    DynamicJsonDocument in(384);        // was 256; name+version fits either way
    if (!deserializeJson(in, data, len)) {
      if (in.containsKey("name"))    pName = in["name"].as<String>();
      if (in.containsKey("version")) pVer  = in["version"].as<String>();   // NEW
      // Declared audio-protocol capability, see EVDProtocolCompat.h. Only
      // trust an explicit JSON integer here; anything else (missing field,
      // wrong type) stays -1 and is treated as "not declared" below.
      if (in.containsKey("audioProtocolVersion") && in["audioProtocolVersion"].is<int>())
        declaredProtoVer = in["audioProtocolVersion"].as<int>();
    }
  }

  AudioCompatDecision decision = evaluateAudioCompatibility(declaredProtoVer, AUDIO_PROTO_VERSION);

  bool isNew = false;
  if (lockFor(playersMutex, 50)) {
    pruneStalePlayersLocked(millis());
    bool known = false;
    for (auto &player : playerNodes) {
      if (player.ip == ip) {
        player.name       = pName;
        player.version    = pVer;       // NEW
        player.lastSeenMs = millis();
        // Recomputed every heartbeat so a firmware downgrade or reboot is
        // picked up on the player's very next check-in.
        player.declaredAudioProtocolVersion   = declaredProtoVer;
        player.negotiatedAudioProtocolVersion = decision.negotiatedProtocolVersion;
        player.audioCompatible                = decision.compatible;
        player.audioCompatReason              = decision.reason;
        known = true; break;
      }
    }
    if (!known && playerNodes.size() < MAX_UDP_SUBSCRIBERS * 2) {
      playerNodes.push_back({pName, ip, pVer, millis(),
                              declaredProtoVer, decision.negotiatedProtocolVersion,
                              decision.compatible, String(decision.reason)});
      isNew = true;
    }
    unlockFor(playersMutex);
  }

  if (lockFor(subsMutex, 50)) {
    int slot = findOrCreateSubscriberLocked(ip, pName, declaredProtoVer,
                                            decision.negotiatedProtocolVersion,
                                            decision.compatible);
    if (slot >= 0 && isNew) subscribers[slot].lastSeenMs = millis();
    unlockFor(subsMutex);
  }

  // ---------- BEGIN zone-assignment on first sight -------------------------
  // A newly-registered node is not in any zone's nodeIps list yet, so
  // handleZonesConfig() finds an empty targetIps for Main Room and
  // broadcastNodeConfig() silently defaults to zoneVolume=1.0 / zoneMuted=false
  // for every unassigned node. That is why master volume/mute work but
  // per-zone volume/mute never reach the node. Assign it to Main here, once,
  // the first time we see it.
  if (isNew) {
    if (lockFor(zonesMutex, 80)) {
      bool inAny = false;
      for (auto &z : zones) {
        for (auto &zip : z.nodeIps) {
          if (zip == ip) { inAny = true; break; }
        }
        if (inAny) break;
      }
      if (!inAny) {
        for (auto &z : zones) {
          if (z.id == MAIN_ZONE_ID) {
            z.nodeIps.push_back(ip);
            // Do NOT write NVS from this handler. saveZonesToNVS() can
            // trigger a 4 KB sector erase that stalls the flash cache for
            // up to ~100 ms on the ESP32-S3, and this handler runs on the
            // AsyncWebServer task while audioReadTask and the encoder are
            // running on Core 1. Three or four nodes registering in quick
            // succession after a server reboot was three or four of those
            // stalls inside a few seconds - audible on the stream that had
            // no mechanism for recovering lost I2S samples. The in-RAM
            // `zones` vector is authoritative; the settings-dirty timer in
            // EVDMain.h will flush it during the next quiet window (same
            // path every other persistent setting already uses).
            settingsDirty      = true;
            settingsDirtySince = millis();
            LOGI("Zone: auto-assigned new player %s to Main Room (NVS deferred)",
                 ip.c_str());
            break;
          }
        }
      }
      unlockFor(zonesMutex);
    }
  }
  // ---------- END zone-assignment on first sight ---------------------------

  if (isNew) {
    LOGI("Player registered: %s v%s from %s (audioProtocolVersion=%d compatible=%s reason=%s)",
         pName.c_str(), pVer.length() ? pVer.c_str() : "?", ip.c_str(),
         declaredProtoVer, decision.compatible ? "true" : "false", decision.reason);
    String json = String("{\"target\":\"") + ip +
                  "\",\"type\":\"host_audio_sync\",\"volume\":" +
                  String((float)masterVolume / 100.0f, 3) +
                  ",\"muted\":" + String(masterMuted ? "true" : "false") +
                  ",\"latencyMs\":" + String(latencyAdjustmentMs) +
                  ",\"bufferMs\":" + String(audioBufferMs) +
                  ",\"mode\":\"" + playbackMode +
                  "\",\"audioProtocolVersion\":" + String(AUDIO_PROTO_VERSION) +
                  ",\"negotiatedAudioProtocolVersion\":" + String(decision.negotiatedProtocolVersion) +
                  ",\"audioCompatible\":" + String(decision.compatible ? "true" : "false") +
                  ",\"audioCompatReason\":\"" + decision.reason + "\"" +
                  ",\"epoch\":" + String(serverEpoch) +
                  ",\"clockSyncPort\":" + String(CLOCK_SYNC_UDP_PORT) + "}";
    sendCtrlToSubscriber(ip, json);
    
    char tsBuf[128];
    int n = snprintf(tsBuf, sizeof(tsBuf),
        "{\"type\":\"time_sync\",\"serverMs\":%lu,\"epoch\":%lu,\"protoVersion\":%d}",
        (unsigned long)millis(), (unsigned long)serverEpoch, AUDIO_PROTO_VERSION);
    if (n > 0 && n < (int)sizeof(tsBuf)) {
        sendCtrlToSubscriber(ip, String(tsBuf, (size_t)n));
    }

  } else if (!decision.compatible) {
    // Not new, but still fail-closed every heartbeat: log once per
    // registration cadence (~25 s) rather than only once at first sight, so
    // the reason stays visible in the log if a node is left running
    // incompatible firmware for a long time.
    LOGW("Player %s (%s) heartbeat: audio DISABLED, reason=%s",
         pName.c_str(), ip.c_str(), decision.reason);
  }

  // Every registration/heartbeat response (not just the first) carries the
  // current negotiated state, so a player can detect a server-side policy
  // change (e.g. it was compatible, now isn't) without waiting on the ctrl
  // push above, which only fires on first sight.
  // Re-send the current config in the response body on EVERY registration.
  String respJson = String("{\"ok\":true,\"audioProtocolVersion\":") + String(AUDIO_PROTO_VERSION) +
                    ",\"negotiatedAudioProtocolVersion\":" + String(decision.negotiatedProtocolVersion) +
                    ",\"audioCompatible\":" + String(decision.compatible ? "true" : "false") +
                    ",\"audioCompatReason\":\"" + decision.reason + "\"" +
                    ",\"epoch\":" + String(serverEpoch) +
                    ",\"clockSyncPort\":" + String(CLOCK_SYNC_UDP_PORT) +
                    ",\"bufferMs\":" + String(audioBufferMs) +
                    ",\"latencyMs\":" + String(latencyAdjustmentMs) +
                    ",\"mode\":\"" + playbackMode + "\"" +
                    ",\"masterVolume\":" + String((float)masterVolume / 100.0f, 3) +
                    ",\"masterMuted\":" + String(masterMuted ? "true" : "false") +
                    "}";
  request->send(200, "application/json", respJson);
}

// ---- Node stats (replaces the WS node_stats message) -----------------------
inline void handleNodeStats(AsyncWebServerRequest *request, uint8_t *data,
                            size_t len, size_t, size_t) {
  if (len > 0 && data) {
    DynamicJsonDocument in(384);
    if (!deserializeJson(in, data, len)) {
      // node_stats payload has the same shape it used to over WS.
      String ip = request->client()->remoteIP().toString();
      updateNodePerf(ip,
                     (uint32_t)(in["latencyMs"] | 0),
                     (uint32_t)(in["queue"]     | 0),
                     (uint32_t)(in["target"]    | 2),
                     (uint16_t)(in["rebuffers"] | 0),
                     millis());
    }
  }
  request->send(200, "application/json", "{\"ok\":true}");
}


// ---- OTA result notification -----------------------------------------------
// POSTed by the node's handleNodeOtaDone() (EVDPWeb.h on the node) after the
// complete firmware has been written into the node's inactive OTA partition
// but BEFORE Update.end() commits the boot pointer. So a "success:true" here
// means "bytes are safely in NVM, reboot imminent"; it does NOT mean "the
// node is already running the new firmware". A subsequent /api/player/register
// heartbeat carrying the new version confirms the reboot succeeded.
//
// Fire-and-forget from the node's side (1.5 s HTTPClient timeout). If this
// handler is missing or slow, the node still commits and reboots; the only
// loss is the immediate confirmation. /api/ota/status already reports the
// push as complete because otaPushToNode() saw the "ok":true body on the
// upload response, so nothing here is load-bearing for correctness.
inline void handleOtaResult(AsyncWebServerRequest *request, uint8_t *data,
                            size_t len, size_t, size_t) {
  String ip  = request->client()->remoteIP().toString();
  String who = ip;
  bool   success = false;
  if (len > 0 && data) {
    DynamicJsonDocument in(192);
    if (!deserializeJson(in, data, len)) {
      if (in["name"].is<const char*>()) who = in["name"].as<String>();
      if (in["success"].is<bool>())     success = in["success"].as<bool>();
    }
  }
  if (success)
    LOGI("OTA result: %s (%s) firmware written to NVM, awaiting reboot",
         who.c_str(), ip.c_str());
  else
    LOGW("OTA result: %s (%s) reported FAILURE (local Update.end() failing?)",
         who.c_str(), ip.c_str());
  request->send(200, "application/json", "{\"ok\":true}");
}

// ---- Config push -----------------------------------------------------------

// ---- Config push -----------------------------------------------------------
// One UDP packet per subscriber, targeted at that subscriber's IP. Nodes
// filter incoming ctrl packets by the "target" field.
// ---- Direct zone config push ----------------------------------------------
// Sends ONLY zoneVolume/zoneMuted to a specific IP, regardless of whether
// that IP is currently in the subscriber table. Used by the zone handlers
// (EVDZones.h) so a node that just booted, was pruned, or hasn't had its
// first heartbeat yet still receives the zone state the moment the zone is
// changed on the dashboard.
//
// applyCtrlJson() on the node only overwrites fields that are present in
// the packet, so this partial payload leaves masterVolume / muted / mode
// untouched on the node.
inline void sendZoneConfigToIp(const String &ip, int zvol, bool zmute) {
  String json = String("{\"target\":\"") + ip +
                "\",\"type\":\"host_audio_sync\"" +
                ",\"zoneVolume\":" + String((float)zvol / 100.0f, 3) +
                ",\"zoneMuted\":"  + String(zmute ? "true" : "false") +
                "}";
  sendCtrlToSubscriber(ip, json);
}

// ---- Config push -----------------------------------------------------------
// One UDP packet per subscriber, targeted at that subscriber's IP. Nodes
// filter incoming ctrl packets by the "target" field.
inline void broadcastNodeConfig() {
  if (!subsMutex) return;
  std::vector<SubscriberSnap> subs = snapshotSubscribers(millis());
  std::vector<Zone> zcopy = snapshotZones();

  for (auto &s : subs) {
    // Start from Main Room's current state, not a hardcoded 100/false.
    // That way an unassigned node inherits whatever the operator set on
    // Main's slider instead of always getting a silent default.
    int  zvol  = 100;
    bool zmute = false;
    for (auto &z : zcopy) {
      if (z.id == MAIN_ZONE_ID) { zvol = z.volume; zmute = z.muted; break; }
    }

    // Then override with the node's actual zone, if it has one.
    for (auto &z : zcopy) {
      bool member = false;
      for (auto &n : z.nodeIps) {
        if (n == s.ip) { member = true; break; }
      }
      if (member) { zvol = z.volume; zmute = z.muted; break; }
    }

    String json = String("{\"target\":\"") + s.ip +
                  "\",\"type\":\"host_audio_sync\",\"volume\":" +
                  String((float)masterVolume / 100.0f, 3) +
                  ",\"muted\":"     + String(masterMuted ? "true" : "false") +
                  ",\"zoneVolume\":" + String((float)zvol / 100.0f, 3) +
                  ",\"zoneMuted\":"  + String(zmute ? "true" : "false") +
                  ",\"bufferMs\":"   + String(audioBufferMs) +
                  ",\"latencyMs\":"  + String(latencyAdjustmentMs) +
                  ",\"mode\":\"" + playbackMode +
                  "\",\"audioProtocolVersion\":" + String(AUDIO_PROTO_VERSION) +
                  ",\"negotiatedAudioProtocolVersion\":" + String(s.negotiatedAudioProtocolVersion) +
                  ",\"audioCompatible\":" + String(s.audioCompatible ? "true" : "false") +
                  ",\"epoch\":" + String(serverEpoch) +
                  ",\"clockSyncPort\":" + String(CLOCK_SYNC_UDP_PORT) + "}";
    sendCtrlToSubscriber(s.ip, json);
  }
}

// ---- Time sync heartbeat (REMOVED) -----------------------------------------
// This function used to fan out a one-way "time_sync" UDP packet to every
// subscriber every 5 s, as a passive fallback for the RTT clock-sync
// protocol on CLOCK_SYNC_UDP_PORT. It is deleted because the node already
// discards it: EVDPNet.h's applyCtrlJson() early-returns on
//   if (serverMs == 0 || g_have_rtt_sync) return;
// as soon as one RTT sample has been collected, which happens within ~2 s
// of the node's first sync_req. The one-shot time_sync that
// handlePlayerRegistration() sends on first sight is retained (it seeds the
// node before its first RTT exchange completes). Deleting the periodic
// push removes one subsMutex acquisition, one per-subscriber JSON build,
// and one UDP send from every 5 s tick of loop().

// ---- Node perf + adaptive buffers ------------------------------------------
inline void updateNodePerf(const String &ip, uint32_t latencyMs, uint32_t queueDepth,
                           uint32_t target, uint16_t rebuffers, unsigned long now) {
  if (!lockFor(perfMutex, 40)) return;
  bool found = false;
  for (auto &p : nodePerf) {
    if (p.ip == ip) {
      p.latencyMs = latencyMs; 
      p.queueDepth = queueDepth;
      p.target = target;
      p.rebuffers = rebuffers; 
      p.updatedMs  = now; 
      p.hasData = true;
      found = true; break;
    }
  }
  if (!found) nodePerf.push_back({ip, latencyMs, queueDepth, target, rebuffers, now, true});
  unlockFor(perfMutex);
}

// ============================================================================
//  Opus complexity auto-tuner
// ============================================================================
// Each playback mode has a target latency for the server-side capture->send
// leg and a complexity range. The tuner steps complexity down if measured
// latency is over target, and back up if it has been comfortably under
// target for several consecutive checks. Hysteresis on both sides keeps it
// from oscillating.
//
// Since the encoder is not thread-safe, this tuner and applyOpusProfileForMode()
// do NOT call opus_encoder_ctl() directly: they set g_pendingComplexity
// (EVDState.h) and audioProcessingTask() applies it on Core 1, serialised
// with opus_encode().
struct ModeProfile {
  int      minComplexity;
  int      maxComplexity;
  int      startComplexity;
};

inline ModeProfile profileForMode(const String& m) {
  if (m == "movie") return { 4, 8, 4 };
  if (m == "quiet") return { 4, 8, 4 };
  if (m == "music") return { 4, 8, 4 };
  return                  { 4, 8, 4 };
}

inline void applyOpusProfileForMode(const String& mode, bool resetToStart) {
  ModeProfile p = profileForMode(mode);
  int target = resetToStart ? p.startComplexity : currentOpusComplexity;
  if (target < p.minComplexity) target = p.minComplexity;
  if (target > p.maxComplexity) target = p.maxComplexity;
  // Do NOT touch opus_encoder_ctl() here. Queue the change; the audio task
  // applies it on Core 1. currentOpusComplexity is written only by that task.
  g_pendingComplexity = target;
  LOGI("Opus profile queued: mode=%s c=%d range=%d..%d",
       mode.c_str(), target, p.minComplexity, p.maxComplexity);
}

inline void autoTuneOpusComplexity(unsigned long now) {
  static unsigned long lastCheck  = 0;
  static int           stableRuns = 0;
  // 4 s between tuning decisions. Each change is a full encoder-state
  // rebuild inside libopus; a wider interval means fewer audible steps and
  // fewer chances to preempt the audio task on Core 1 mid-encode.
  if (now - lastCheck < 4000) return;
  lastCheck = now;
  if (!opusEncoder || gAvgEncodeUs == 0) return;
  // NOTE: the previous "off-mode early-return" is removed. The server does
  // not gate encoding on playbackMode: in off mode the encoder still runs,
  // still produces packets, and still needs to keep up. Skipping the tuner
  // in off mode locked complexity at whatever applyOpusProfileForMode()
  // installed at boot - which is exactly the "no audio until you tap the
  // mode select" symptom this fix removes.

  ModeProfile p = profileForMode(playbackMode);
  int c         = currentOpusComplexity;

  // 12 ms of a 20 ms frame is a 60 % encoder duty cycle. That leaves 8 ms
  // for the I2S DMA drain, the resampler, the quiet filters, WiFi servicing
  // and the FreeRTOS scheduler. The previous 16 ms budget treated a 14 ms
  // encode as "fine", which left less than 6 ms for everything else and
  // pushed frames into the node's 20-100 ms late bucket under any jitter.
  const uint32_t budgetUs = 8000;

  if (gAvgEncodeUs > budgetUs && c > p.minComplexity) {
    g_pendingComplexity = c - 1;
    LOGI("Opus c %d -> %d queued (encode %u us > %u us budget)",
         c, c - 1, (unsigned)gAvgEncodeUs, (unsigned)budgetUs);
    stableRuns = 0;
    return;
  }

  // Climb back up only when there is real headroom: under 60 % of the
  // budget. Tighter than the previous budgetUs/2 gate, so the tuner does
  // not oscillate around the new lower threshold.
  if (gAvgEncodeUs < (budgetUs * 3) / 5 && c < p.maxComplexity) {
    if (++stableRuns >= 3) {
      g_pendingComplexity = c + 1;
      LOGI("Opus c %d -> %d queued (encode %u us, headroom)",
           c, c + 1, (unsigned)gAvgEncodeUs);
      stableRuns = 0;
    }
  } else {
    stableRuns = 0;
  }
}

  // ---- HTTP: /api/config, /api/source, /api/cmd ------------------------------
inline void handleApiConfig(AsyncWebServerRequest *request, uint8_t *data,
                            size_t len, size_t index, size_t total) {
  // Multi-chunk body assembly. The previous `if (len != total) reject`
  // test fired on the FIRST chunk of any body larger than one TCP segment,
  // sending a 400 and then re-entering this handler for every subsequent
  // chunk. Current clients post < 200 B so it never tripped, but any
  // future payload (zone arrays, per-node overrides) would be silently
  // discarded. Accumulate across chunks into a static buffer sized to
  // the largest legitimate /api/config payload and deserialize once when
  // index + len == total.
  static char     cfgBody[1024];
  static size_t   cfgLen = 0;

  if (index == 0) {
    cfgLen = 0;
    if (total > sizeof(cfgBody)) {
      // Reject up front so we do not send one 400 per chunk as
      // ESPAsyncWebServer keeps delivering the remaining body.
      request->send(400, "application/json",
                    "{\"ok\":false,\"error\":\"Config payload too large\"}");
      return;
    }
  }
  if (index + len > sizeof(cfgBody)) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"Config payload too large\"}");
    cfgLen = 0;
    return;
  }
  if (data && len > 0) {
    memcpy(cfgBody + cfgLen, data, len);
    cfgLen += len;
  }
  if (index + len != total) return;   // not yet complete

  if (cfgLen == 0) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Empty request body\"}");
    return;
  }

  DynamicJsonDocument input(256);
  if (deserializeJson(input, cfgBody, cfgLen)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}"); return;
  }

  bool changed = false;
  if (input.containsKey("masterMuted") && input["masterMuted"].is<bool>()) {
    masterMuted = input["masterMuted"].as<bool>();
    changed = true;
  }
  if (input.containsKey("mode") && input["mode"].is<const char*>()) {
    String m = String((const char*)input["mode"]); m.toLowerCase();
    if (!(m == "off" || m == "music" || m == "movie" || m == "quiet")) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"mode must be off, music, movie or quiet\"}"); return;
    }
    bool wasQuiet = (playbackMode == "quiet");
    playbackMode = m;
    if (m == "quiet" && !wasQuiet) configureQuietFilters();
    if (m == "movie") audioBufferMs = 50;
    if (m == "music") audioBufferMs = 150;
    if (m == "off")   audioBufferMs = 120;
    if (m == "quiet") audioBufferMs = 150;
    applyOpusProfileForMode(playbackMode, true);
    changed = true;
  }
  if (input.containsKey("masterVolume") && input["masterVolume"].is<int>()) {
    int v = input["masterVolume"];
    if (v < 0 || v > 100) { request->send(400, "application/json", "{\"ok\":false,\"error\":\"Volume must be between 0 and 100\"}"); return; }
    masterVolume = (uint8_t)v;
    masterMuted = false;
    changed = true;
  }
  if (input.containsKey("latencyAdjustmentMs") && input["latencyAdjustmentMs"].is<int>()) {
    int v = input["latencyAdjustmentMs"];
    if (v < 0 || v > 200) { request->send(400, "application/json", "{\"ok\":false,\"error\":\"Latency must be between 0 and 200 ms\"}"); return; }
    latencyAdjustmentMs = v; changed = true;
  }
  if (input.containsKey("audioBufferMs") && input["audioBufferMs"].is<int>()) {
    int v = input["audioBufferMs"];
    if (v < 0 || v > 200) { request->send(400, "application/json", "{\"ok\":false,\"error\":\"Buffer must be between 0 and 200 ms\"}"); return; }
    audioBufferMs = v; changed = true;
  }
  if (input.containsKey("serverName") && input["serverName"].is<const char*>()) {
    String v = input["serverName"];
    if (v.length() == 0 || v.length() > 32 || v != sanitizeServerName(v)) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Server name must use 1-32 letters or numbers\"}"); return;
    }
    serverName = v; changed = true;
  }
  if (input.containsKey("defaultSource") && input["defaultSource"].is<const char*>()) {
    String v = input["defaultSource"]; v.toUpperCase();
    if (!(v == "BT" || v == "AUX" || v == "USB")) {
      request->send(400, "application/json",
        "{\"ok\":false,\"error\":\"defaultSource must be BT, AUX or USB\"}");
      return;
    }
    defaultBootSource = v;
    // [defer-nvs-boot-source] Hand this to loop()'s quiet-window flush
    // instead of writing NVS inline.  AsyncWebServer runs on Core 0; a
    // 4 KB sector erase can stall the flash cache for ~100 ms and cost
    // I2S samples on Core 1.  loop() writes boot_source together with
    // every other deferred setting once the stream has been quiet >1 s.
    LOGI("Boot default source queued for NVS flush: %s",
         defaultBootSource.c_str());
    changed = true;
  }
  if (!changed) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"No valid settings were supplied\"}"); return;
  }

  settingsDirty = true; settingsDirtySince = millis();
  broadcastNodeConfig();
  request->send(200, "application/json", "{\"ok\":true}");
}

// ---- Source bridge --------------------------------------------------------
// Handles POST /api/source {"source":"BT"|"AUX"|"USB"}.
//
// BT / AUX: the source board keeps feeding us PCM over I2S. We forward the
// source name over UART so the source board's own log shows the change and
// its routing logic can act on it (currently a no-op for AUX; BT is live).
//
// USB: the server itself becomes the audio source. We do NOT need the
// source board for this - the USB host streams PCM directly into the S3
// via the UAC device. So we take the input path locally: stop the UART
// bridge from being the authority, switch the read task to USB, and start
// the UAC device. The source board is still notified (for logging) via
// the same UART line, and it logs "handled on server, no-op here".
inline void handleApiSourceBridge(AsyncWebServerRequest *request, uint8_t *data,
                                  size_t len, size_t, size_t) {
  String source = "";
  if (len > 0 && data) {
    DynamicJsonDocument in(256);
    if (!deserializeJson(in, data, len) && in["source"].is<const char*>())
      source = String((const char*)in["source"]);
  }
  source.trim();
  source.toUpperCase();

  if (source.length() == 0 || source.length() > 8) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"invalid source\"}");
    return;
  }

  // ---- USB: handled entirely on the server --------------------------------
  if (source == "USB") {
    if (!startServerUsbAudio()) {
      const char *why = usbLastError();
      LOGE("Source -> USB failed: UAC device did not start (reason: %s)", why);
      String msg = String("{\"ok\":false,\"reason\":\"") + why +
                   "\",\"error\":\"USB audio device could not start\"}";
      request->send(strcmp(why, "no_library") == 0 ||
                    strcmp(why, "unsupported_target") == 0 ? 501 : 502,
                    "application/json", msg);
      return;
    }
    // Point the read task at USB. Must happen AFTER the device is
    // enumerated so the first read() doesn't return 0 for a full frame.
    serverInputSource  = SRC_FROM_USB;
    receiverSource     = "USB";
    lastReceiverSource = "USB";
    // Still forward to the source board so its log reflects the change.
    sendReceiverSourceCommand("USB");
    LOGI("Source -> USB (server-side UAC device active)");
    request->send(200, "application/json",
                  "{\"ok\":true,\"source\":\"USB\",\"transport\":\"local-usb\"}");
    return;
  }

  // ---- BT / AUX: stop USB if it was active, then use the UART bridge ------
  if (source == "BT" || source == "AUX") {
    stopServerUsbAudio();
    serverInputSource = SRC_FROM_I2S;

    String resp; uint16_t code = 0;
    bool ok = setReceiverSource(source, resp, code);
    lastReceiverSource = source;
    if (ok) request->send(200, "application/json", resp);
    else    request->send(502, "application/json",
                         String("{\"ok\":false,\"bridgeHttp\":") + String(code) +
                         ",\"receiver\":" + resp + "}");
    return;
  }

  request->send(400, "application/json",
                "{\"ok\":false,\"error\":\"invalid source\"}");
}

inline void handleApiCmdBridge(AsyncWebServerRequest *request, uint8_t *data,
                               size_t len, size_t, size_t) {
  String cmd = "";
  if (len > 0 && data) {
    DynamicJsonDocument in(256);
    if (!deserializeJson(in, data, len) && in["cmd"].is<const char*>())
      cmd = String((const char*)in["cmd"]);
  }
  cmd.toLowerCase();
  if (!(cmd == "next" || cmd == "prev" || cmd == "bt" || cmd == "aux" || cmd == "usb")) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid cmd\"}"); return;
  }
  String resp; uint16_t code = 0;
  bool ok = bridgeSendReceiverCmd(cmd, resp, code);
  if (ok) request->send(200, "application/json", resp);
  else    request->send(502, "application/json",
                       String("{\"ok\":false,\"bridgeHttp\":") + String(code) + ",\"receiver\":" + resp + "}");
}