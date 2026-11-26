#pragma once
// ============================================================================
//  EVDPlayers.h - Player registration, receiver link, per-node perf,
//                 /api/config|source|cmd
// ============================================================================
#include "EVDHelpers.h"
#include "EVDUsb.h"

// [ota-forward-declarations]
// EVDPlayers.h is included before EVDRoutes.h (which includes EVDOTA.h).
// Forward-declare the OTA state flags here so the new OTA guards can
// compile without creating a circular include.
extern volatile bool otaUploadInProgress;
extern volatile bool otaInProgress;

// ---- Receiver (upstream BT/aux module over UART) ---------------------------
inline void setupReceiverLink() {
  receiverUart.begin(115200, SERIAL_8N1, pinSrcUartRx, pinSrcUartTx);
}
// [uart-tx-mutex] HardwareSerial::printf is not re-entrant. Callers span
// core 1 (loop()'s GET_BT_DEVICES poll, boot) and core 0/1 (AsyncTCP
// handlers: /api/source, /api/cmd, /api/bt/kick, /api/bt/remove). Without
// the mutex a button press can interleave its "CMD:KICK_BT\n" with the
// poll's "CMD:GET_BT_DEVICES\n" on the wire, and the source board's UART
// parser drops both. The same uartMutex guards handleReceiverLink()'s
// BT_JSON| and LOG| branches, so it already exists at boot.
inline void sendReceiverSourceCommand(const String &source) {
  if (!receiverUart) return;
  if (!lockFor(uartMutex, 50)) return;
  receiverUart.print('\n');
  receiverUart.printf("SRC:%s\n", source.c_str());
  unlockFor(uartMutex);
}
inline void sendReceiverCommand(const String &cmdIn) {
  if (!receiverUart) return;
  String cmd = cmdIn;
  cmd.replace("\r", "");
  cmd.replace("\n", "");
  if (!lockFor(uartMutex, 50)) return;
  receiverUart.printf("CMD:%s\n", cmd.c_str());
  unlockFor(uartMutex);
}

inline void handleReceiverLink() {
  // [ota-source-uart-exclusive] otaPushToSource() (EVDOTA.h) reads the
  // same receiverUart synchronously while otaInProgress is true. Two
  // readers on one FIFO split the bytes: this handler eats the
  // OTA_ACK|N replies and every chunk times out. Yield the link for the
  // entire flash. otaInProgress is set by handleOtaApply() before the
  // otaJob task starts and cleared in the task epilogue — no extra state
  // needed. Source LOG| / SRCSTAT| lines emitted during OTA are dropped
  // (SRCSTAT self-heals on the source's next post-reboot heartbeat).
  if (otaInProgress) return;

  // Fixed-size char buffer, no String heap churn. The previous version
  // ...
  // [ram-save-rx-line] Was 2048, then 1024 to save 1 KB DRAM.
  //
  // [rx-line-cross-repo] 1024 was WRONG: the source board builds its
  // BT_JSON payload in a 1024-byte buffer (EnkelvoudSource/EVDBTBluetooth.h,
  // `out[1024]`) and prefixes it with the 8-byte "BT_JSON|" tag. The longest
  // line it can legally send is therefore 1031 bytes + '\n', which never
  // fitted in a 1024-byte buffer — every BT_JSON line was silently dropped
  // and the dashboard's paired-device list froze at its last good value.
  // Size this from the prefix + the peer's payload, not from a guess.
  // Overlong lines are still dropped (rxLen reset below) rather than
  // truncated, so an oversized packet costs one dropped update, never
  // corruption.
  static constexpr size_t BT_JSON_PREFIX_LEN = 8;      // "BT_JSON|"
  static constexpr size_t SOURCE_BT_JSON_PAYLOAD_MAX = 1024;  // peer's out[]
  static constexpr size_t RX_LINE_MAX =
      BT_JSON_PREFIX_LEN + SOURCE_BT_JSON_PAYLOAD_MAX + 8;   // + '\n', NUL, slack
  static char   rxLine[RX_LINE_MAX];
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
        StaticJsonDocument<512> d;
        if (!deserializeJson(d, rxLine + 8)) {
          // [srcstat-string-mutex] The three String members updated here
          // (currentConnectedMacCache / receiverSource / currentSourceFwVersion)
          // are read from handleApiStats (EVDSTS.h), handleApiState (EVDSTS.h),
          // and /api/source/info (EVDRoutes.h) on a different task. String
          // operator= is free+alloc+swap; a concurrent reader walks a freed
          // _buffer. Guard the whole assignment block. All other fields below
          // are plain 8/32-bit scalars: single aligned store, atomic on S3.
          if (lockFor(uartMutex, 50)) {
            // [srcstat-key-names-match-source]
            // Three keys were spelled differently from the source board's
            // SRCSTAT producer (EVDBTBluetooth.h sendBtDeviceList()):
            //   server parser   source field   server-side effect before fix
            //   "loop"    ->    "ticks"        currentSourceLoopTicks always 0
            //   "srcC"    ->    "srcCh"        currentSourceChanges always 0
            //   "of"      ->    "uartOvf"      currentSourceUartOverflow always 0
            // The other three ("fw", "txd") are also not currently emitted
            // by the source; "fw" is added in the source patch alongside
            // this fix, "txd" is left as zero (the source does not track
            // UART TX drops and adding that tracking is a separate change).
            if (d.containsKey("mac"))    currentConnectedMacCache = d["mac"].as<String>();
            if (d.containsKey("vol"))    currentSourceBtVolume    = (uint8_t)(d["vol"] | 0);
            if (d.containsKey("avrc"))   avrcConnectedCache       = (d["avrc"] | 0) != 0;
            if (d.containsKey("a2dpC"))  diagA2dpConnected        = (uint32_t)(d["a2dpC"] | 0UL);
            if (d.containsKey("a2dpD"))  diagA2dpDisconnected     = (uint32_t)(d["a2dpD"] | 0UL);
            if (d.containsKey("volEv"))  diagVolumeEvents         = (uint32_t)(d["volEv"] | 0UL);
            if (d.containsKey("uartL"))  diagUartRxLines          = (uint32_t)(d["uartL"] | 0UL);
            if (d.containsKey("src"))    receiverSource           = d["src"].as<String>();
            if (d.containsKey("fw"))     currentSourceFwVersion         = d["fw"].as<String>();
            if (d.containsKey("ticks"))  currentSourceLoopTicks         = (uint32_t)(d["ticks"]  | 0UL);
            if (d.containsKey("heap"))   currentSourceHeap              = (uint32_t)(d["heap"]   | 0UL);
            if (d.containsKey("up"))     currentSourceUptime            = (uint32_t)(d["up"]     | 0UL);
            if (d.containsKey("srcCh"))  currentSourceChanges           = (uint32_t)(d["srcCh"]  | 0UL);
            if (d.containsKey("volAp"))  currentSourceVolumeApplied     = (uint32_t)(d["volAp"]  | 0UL);
            if (d.containsKey("uartB"))  currentSourceUartBytes         = (uint32_t)(d["uartB"]  | 0UL);
            if (d.containsKey("uartOvf")) currentSourceUartOverflow     = (uint32_t)(d["uartOvf"]| 0UL);
            if (d.containsKey("txd"))    currentSourceUartTxDropped     = (uint32_t)(d["txd"]    | 0UL);
            if (d.containsKey("a2dpCg")) currentSourceA2dpConnecting    = (uint32_t)(d["a2dpCg"] | 0UL);
            if (d.containsKey("a2dpDg")) currentSourceA2dpDisconnecting = (uint32_t)(d["a2dpDg"] | 0UL);
            receiverState        = (d["connected"] | 0) ? "connected" : "idle";
            lastHeartbeatMsCache = millis();
            unlockFor(uartMutex);
          }
        }
      } else {
        LOGI("UART RX: %s", rxLine);
      }
      rxLen = 0;
    } else {
      if (rxLen < sizeof(rxLine) - 1) {
        rxLine[rxLen++] = c;
      } else {
        // Overlong line (no newline within RX_LINE_MAX chars). Drop rather
        // than corrupt state; a well-behaved source never emits one. Counted
        // so "the device list froze" is diagnosable from /api/stats instead
        // of looking like a dead UART.
        rxLen = 0;
        statUartLineDrops++;
      }
    }
  }
}

inline bool setReceiverSource(const String &source, String &respBody, uint16_t &httpCode) {
  // [uart-send-fail-surfaced] sendReceiverSourceCommand() returns void
  // and silently drops on lock timeout. Take the lock here, do BOTH the
  // wire send and the String updates under the same lock, and report
  // 503 to the HTTP caller on timeout.
  if (!receiverUart) {
    httpCode = 503;
    respBody = "{\"ok\":false,\"error\":\"UART unavailable\"}";
    return false;
  }
  if (!lockFor(uartMutex, 50)) {
    httpCode = 503;
    respBody = "{\"ok\":false,\"error\":\"UART busy\"}";
    return false;
  }
  receiverUart.print('\n');
  receiverUart.printf("SRC:%s\n", source.c_str());
  receiverSource     = source;
  lastReceiverSource = source;
  unlockFor(uartMutex);
  httpCode = 200;
  respBody = String("{\"ok\":true,\"source\":\"") + source + "\",\"transport\":\"uart\"}";
  return true;
}
inline bool bridgeSendReceiverCmd(const String &cmd, String &respBody, uint16_t &httpCode) {
  if (!receiverUart) {
    httpCode = 503;
    respBody = "{\"ok\":false,\"error\":\"UART unavailable\"}";
    return false;
  }
  if (!lockFor(uartMutex, 50)) {
    httpCode = 503;
    respBody = "{\"ok\":false,\"error\":\"UART busy\"}";
    return false;
  }
  receiverUart.printf("CMD:%s\n", cmd.c_str());
  unlockFor(uartMutex);
  httpCode = 200;
  respBody = String("{\"ok\":true,\"cmd\":\"") + cmd + "\",\"transport\":\"uart\"}";
  return true;
}

// ---- Player registration (also the heartbeat endpoint) ---------------------
// Nodes POST here on boot and then periodically. The handler refreshes the
// subscriber table, and on first sight pushes the current config so the node
// gets masterVolume / zone / mode without a separate handshake.
inline void handlePlayerRegistration(AsyncWebServerRequest *request, uint8_t *data,
                                     size_t len, size_t index, size_t total) {
  // [ota-block-registration] Reject heartbeats during OTA. The node is
  // rebooting anyway, and parsing/responding consumes precious heap.
  if (otaUploadInProgress || otaInProgress) {
    request->send(503, "application/json", "{\"ok\":false,\"error\":\"OTA in progress\"}");
    return;
  }
  // [servers-decide] Only the elected leader serves nodes. A standby answers
  // 503 so the node's contact fails and it re-discovers the leader.
  if (!g_peerIsLeader) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"status\":\"standby\"}");
    return;
  }
  // [register-single-chunk] All node firmwares send the full register
  // body in one segment. A fragmented body must NOT be parsed chunk-wise:
  // the first partial chunk would omit audioProtocolVersion, forcing
  // declaredProtoVer=-1 -> "no_protocol_declared" -> node marked
  // incompatible and every frame withheld. Reject and let the node retry.
  if (index != 0 || len != total) {
    if (index != 0) return;   // ignore trailing chunks after we rejected
    request->send(413, "application/json",
                  "{\"ok\":false,\"error\":\"register body fragmented; retry\"}");
    return;
  }
  String ip    = request->client()->remoteIP().toString();
  String pName = "Player Node";
  String pVer  = "";                    // NEW
  int    declaredProtoVer = -1;         // NEW: -1 = not declared (legacy/unknown firmware)

  if (len > 0 && data) {
    DynamicJsonDocument in(384);        // was 256; name+version fits either way
    if (!deserializeJson(in, data, len)) {
      if (in.containsKey("name")) {
        // [security-xss-node-name] Player names are rendered in the
        // dashboard via innerHTML with only `"` escaped; any node on the
        // LAN could POST `<img src=x onerror=...>` as its name and
        // execute arbitrary JS in the operator's browser. Whitelist
        // alphanumeric, space, dash, underscore, period. 32 chars max,
        // matching the server name rule.
        String raw = in["name"].as<String>();
        String safe; safe.reserve(raw.length());
        for (size_t i = 0; i < raw.length() && safe.length() < 32; ++i) {
          // [sanitize-unsigned-char] See sanitizeServerName in EVDHelpers.h.
          unsigned char c = (unsigned char)raw[i];
          if (isAlphaNumeric(c) || c == ' ' || c == '-' || c == '_' || c == '.')
            safe += (char)c;
        }
        pName = safe.length() ? safe : String("Player Node");
      }
      if (in.containsKey("version")) pVer  = sanitizeVersion(in["version"].as<String>());
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
    if (slot >= 0) {
      // [provisional-classification] A real heartbeat upgrades the slot
      // from any provisional classification. Old firmware that never
      // declares a version still lands here as "genuinely incompatible"
      // (provisional == false, compatible == false).
      subscribers[slot].provisional = false;
      if (isNew) subscribers[slot].lastSeenMs = millis();
    }
    unlockFor(subsMutex);
  }

  // ---------- Discovered pool: do NOT auto-assign --------------------------
  // [discovered-nodes] A newly-registered node stays unassigned until the
  // operator drags it into a room on the dashboard. Until then it is muted
  // (every zone-state dispatch below defaults unassigned nodes to muted).
  // Previously this block auto-assigned every new node to Main Room, which
  // made every node immediately audible and hid the "first join" step.
  // -------------------------------------------------------------------------

  if (isNew) {
    LOGI("Player registered: %s v%s from %s (audioProtocolVersion=%d compatible=%s reason=%s)",
         pName.c_str(), pVer.length() ? pVer.c_str() : "?", ip.c_str(),
         declaredProtoVer, decision.compatible ? "true" : "false", decision.reason);

    // Resolve the zone state that applies to this node BEFORE sending the
    // first-sight packet, so the node does not run on node-side zone
    // defaults until the next broadcastNodeConfig() fires.
    int  zvol  = 100;
    bool zmute = false;
    {
      std::vector<Zone> zc = snapshotZones();
      bool found = false;
      for (auto &z : zc) {
        for (auto &zip : z.nodeIps) {
          if (zip == ip) { zvol = z.volume; zmute = z.muted; found = true; break; }
        }
        if (found) break;
      }
      if (!found) {
        // [discovered-nodes] Unassigned -> muted until dragged into a room.
        zvol  = 0;
        zmute = true;
      }
    }

    String json = String("{\"target\":\"") + ip +
                  "\",\"type\":\"host_audio_sync\",\"volume\":" +
                  String((float)masterVolume / 100.0f, 3) +
                  ",\"muted\":" + String(masterMuted ? "true" : "false") +
                  ",\"zoneVolume\":" + String((float)zvol / 100.0f, 3) +
                  ",\"zoneMuted\":" + String(zmute ? "true" : "false") +
                  ",\"bufferMs\":" + String(audioBufferMs) +
                  ",\"sourceSampleRate\":" + String((unsigned)g_sourceSampleRate) +
                  // [opus-sample-rate] Node must re-create its decoder if
                  // this differs from its current decoder Fs.
                  ",\"opusSampleRate\":" + String((unsigned)g_opusSampleRate) +
                  ",\"mode\":\"" + String(playbackModeCStr()) +
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

  // [zone-state-on-every-heartbeat]
  // Resolve this node's zoneVolume / zoneMuted for EVERY registration,
  // not only on first-sight. The server prunes subscriber slots at
  // PLAYER_STALE_TIMEOUT_MS (now 70 s, was 45 s). A node that reboots
  // inside that window re-registers as "not new" (isNew == false) and
  // never receives the first-sight host_audio_sync push that carries
  // zoneVolume / zoneMuted. It then boots with the node-side defaults
  // zone_volume_multiplier=1.0 / zone_muted=false and plays a muted zone
  // unmuted until the next dashboard interaction touches broadcastNodeConfig.
  // Mirrored into the response below so the node self-heals within one
  // heartbeat interval instead of waiting for operator action.
  int  zoneVolNow  = 100;
  bool zoneMuteNow = false;
  {
    std::vector<Zone> zc = snapshotZones();
    bool found = false;
    for (auto &z : zc) {
      for (auto &zip : z.nodeIps) {
        if (zip == ip) { zoneVolNow = z.volume; zoneMuteNow = z.muted; found = true; break; }
      }
      if (found) break;
    }
    if (!found) {
      // [discovered-nodes] Unassigned -> muted until dragged into a room.
      zoneVolNow  = 0;
      zoneMuteNow = true;
    }
  }

  // Every registration/heartbeat response (not just the first) carries the
  // current negotiated state, so a player can detect a server-side policy
  // change (e.g. it was compatible, now isn't) without waiting on the ctrl
  // push above, which only fires on first sight.
  // Re-send the current config in the response body on EVERY registration.
  // [encoder-info-passthrough]
  // Six new fields carry the server's *live* encoder state so the node's
  // status page can prove the settings the dashboard shows are the ones
  // actually running. Sent on every registration response AND on every
  // ctrl push (see broadcastNodeConfig() below). currentOpusComplexity is
  // the last value the audio task actually pushed into the encoder; the
  // other five come from the g_current* / g_new* mirror. On a healthy
  // system the two agree; a divergence here is the operator's first clue
  // that the audio task has not drained a pending change.
  const char *encSignalStr =
      (g_newSignal == OPUS_SIGNAL_MUSIC) ? "music" :
      (g_newSignal == OPUS_SIGNAL_VOICE) ? "voice" :
      (g_newSignal == OPUS_AUTO)         ? "auto"  : "unknown";
  const char *encAppStr =
      (g_currentApplication == OPUS_APPLICATION_AUDIO)             ? "audio"    :
      (g_currentApplication == OPUS_APPLICATION_VOIP)              ? "voip"     :
      (g_currentApplication == OPUS_APPLICATION_RESTRICTED_LOWDELAY) ? "lowdelay" : "unknown";

  // [double-attenuation-intentional]
  // Master volume is applied ONCE, server-side, before encode. The value
  // is still mirrored to the node (for /api/health visibility), but the
  // node does NOT re-apply it. See [no-double-attenuation] in EVDPAudio.h.
  String respJson = String("{\"ok\":true,\"audioProtocolVersion\":") + String(AUDIO_PROTO_VERSION) +
                    ",\"negotiatedAudioProtocolVersion\":" + String(decision.negotiatedProtocolVersion) +
                    ",\"audioCompatible\":" + String(decision.compatible ? "true" : "false") +
                    ",\"audioCompatReason\":\"" + decision.reason + "\"" +
                    ",\"epoch\":" + String(serverEpoch) +
                    ",\"clockSyncPort\":" + String(CLOCK_SYNC_UDP_PORT) +
                    ",\"bufferMs\":" + String(audioBufferMs) +
                    ",\"sourceSampleRate\":" + String((unsigned)g_sourceSampleRate) +
                    // [opus-sample-rate] Same field on every heartbeat so a
                    // node that missed the ctrl push self-heals within one
                    // heartbeat interval.
                    ",\"opusSampleRate\":" + String((unsigned)g_opusSampleRate) +
                    ",\"mode\":\"" + String(playbackModeCStr()) + "\"" +
                    ",\"masterVolume\":" + String((float)masterVolume / 100.0f, 3) +
                    ",\"masterMuted\":" + String(masterMuted ? "true" : "false") +
                    ",\"zoneVolume\":" + String((float)zoneVolNow / 100.0f, 3) +
                    ",\"zoneMuted\":" + String(zoneMuteNow ? "true" : "false") +
                    ",\"encBitrate\":" + String(g_currentBitrate) +
                    ",\"encComplexity\":" + String(currentOpusComplexity) +
                    ",\"encVbr\":" + String(g_currentVbr ? "true" : "false") +
                    ",\"encVbrConstraint\":" + String(g_currentVbrConstraint ? "true" : "false") +
                    ",\"encSignal\":\"" + encSignalStr + "\"" +
                    ",\"encApplication\":\"" + encAppStr + "\"" +
                    "}";
  request->send(200, "application/json", respJson);
}

// ---- Node stats (replaces the WS node_stats message) -----------------------
inline void handleNodeStats(AsyncWebServerRequest *request, uint8_t *data,
                            size_t len, size_t index, size_t total) {
  // [ota-block-stats]
  if (otaUploadInProgress || otaInProgress) {
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  // [node-stats-single-chunk] Reject fragmented bodies (mirror of
  // /api/player/register). The old handler ignored index/total and parsed
  // each chunk independently, silently dropping stats on any multi-segment
  // body. Real payloads are ~120 B; a 413 is a retry signal for the node.
  if (index != 0 || len != total) {
    if (index != 0) return;
    request->send(413, "application/json",
                  "{\"ok\":false,\"error\":\"stats body fragmented; retry\"}");
    return;
  }
  if (len > 0 && data) {
    DynamicJsonDocument in(384);
    if (!deserializeJson(in, data, len)) {
      String ip = request->client()->remoteIP().toString();
      // [node-stats-log-head-and-late]
      // [node-stats-silent-on-discard] The handler used to accept only
      // latencyMs / queue / target / rebuffers. The 2026-11-15 diagnostic
      // shows a node that discards 36 frames and conceals 34 over 10 min
      // while rbuf stays 0 -- the server saw a healthy node. head,
      // ld20/disc/conc are already present in the node's /api/health
      // payload and (in current node firmware) in this POST as
      // `head`, `leadBelow20`, `framesDiscarded`, `framesConcealed`.
      // Log them at WARN when the node is struggling, so the operator
      // gets a server-side signal without the node dashboard open.
      {
        int16_t  headNow  = (int16_t)(in["head"]             | 0);
        uint16_t ld20Now  = (uint16_t)(in["leadBelow20"]     | 0);
        uint16_t discNow  = (uint16_t)(in["framesDiscarded"] | 0);
        uint16_t concNow  = (uint16_t)(in["framesConcealed"] | 0);
        if (headNow < 20 || ld20Now > 5) {
          static uint32_t lastWarnMs = 0;
          if (millis() - lastWarnMs > 5000) {
            lastWarnMs = millis();
            LOGW("node %s struggling: head=%d ms ld20=%u disc=%u conc=%u "
                 "lat=%u q=%u rbuf=%u",
                 ip.c_str(), (int)headNow, (unsigned)ld20Now,
                 (unsigned)discNow, (unsigned)concNow,
                 (unsigned)(in["latencyMs"] | 0),
                 (unsigned)(in["queue"]     | 0),
                 (unsigned)(in["rebuffers"] | 0));
          }
        }
      }
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
                            size_t len, size_t index, size_t total) {
  // [ota-result-single-chunk] Node sends ~150 B; reject fragments to
  // mirror handleNodeStats/handlePlayerRegistration.
  if (index != 0 || len != total) {
    if (index != 0) return;
    request->send(413, "application/json",
                  "{\"ok\":false,\"error\":\"body fragmented; retry\"}");
    return;
  }
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
  // [zone-push-carries-compat]
  // A node whose compat state lapsed to UNKNOWN (stale epoch, transient
  // register failure) rejects incoming audio until its next 30 s heart-
  // beat. A zone move/delete is the operator touching exactly that node,
  // so it is the natural place to also restore compat. Look the node up
  // in playerNodes (callers are human-triggered, so a snapshot is cheap);
  // only emit the compat fields if we actually know this node — a node
  // that has never registered must not be silently whitelisted.
  bool    haveCompat   = false;
  bool    compatible   = false;
  int     negotiatedVer = 0;
  String  compatReason;
  {
    std::vector<PlayerNode> ps = snapshotPlayers(millis());
    for (auto &p : ps) {
      if (p.ip == ip) {
        haveCompat    = true;
        compatible    = p.audioCompatible;
        negotiatedVer = p.negotiatedAudioProtocolVersion;
        compatReason  = p.audioCompatReason;
        break;
      }
    }
  }

  String json = String("{\"target\":\"") + ip +
                "\",\"type\":\"host_audio_sync\"" +
                ",\"zoneVolume\":" + String((float)zvol / 100.0f, 3) +
                ",\"zoneMuted\":"  + String(zmute ? "true" : "false") +
                ",\"sourceSampleRate\":" + String((unsigned)g_sourceSampleRate) +
                ",\"opusSampleRate\":"   + String((unsigned)g_opusSampleRate) +
                ",\"bufferMs\":"         + String(audioBufferMs) +
                ",\"mode\":\""           + String(playbackModeCStr()) + "\"" +
                ",\"epoch\":"            + String(serverEpoch) +
                ",\"clockSyncPort\":"    + String(CLOCK_SYNC_UDP_PORT);
  if (haveCompat) {
    json += ",\"audioProtocolVersion\":" + String(AUDIO_PROTO_VERSION) +
            ",\"negotiatedAudioProtocolVersion\":" + String(negotiatedVer) +
            ",\"audioCompatible\":" + String(compatible ? "true" : "false") +
            ",\"audioCompatReason\":\"" + compatReason + "\"";
  }
  json += "}";
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
    // [discovered-nodes] Unassigned nodes are muted (0 % / muted). Only a
    // node that is actually a member of a zone gets that zone's state; it is
    // un-muted the moment the operator drags it into a room.
    int  zvol  = 0;
    bool zmute = true;
    for (auto &z : zcopy) {
      bool member = false;
      for (auto &n : z.nodeIps) {
        if (n == s.ip) { member = true; break; }
      }
      if (member) { zvol = z.volume; zmute = z.muted; break; }
    }

    // [encoder-info-passthrough] Same six fields the registration response
    // carries, repeated on every ctrl push so a mid-stream encoder change
    // reaches a node that is already subscribed without waiting up to 30 s
    // for the next registration heartbeat.
    const char *encSignalStr =
        (g_currentSignal == OPUS_SIGNAL_MUSIC) ? "music" :
        (g_currentSignal == OPUS_SIGNAL_VOICE) ? "voice" :
        (g_currentSignal == OPUS_AUTO)         ? "auto"  : "unknown";
    const char *encAppStr =
        (g_currentApplication == OPUS_APPLICATION_AUDIO)             ? "audio"    :
        (g_currentApplication == OPUS_APPLICATION_VOIP)              ? "voip"     :
        (g_currentApplication == OPUS_APPLICATION_RESTRICTED_LOWDELAY) ? "lowdelay" : "unknown";

    String json = String("{\"target\":\"") + s.ip +
                  "\",\"type\":\"host_audio_sync\",\"volume\":" +
                  String((float)masterVolume / 100.0f, 3) +
                  ",\"muted\":"     + String(masterMuted ? "true" : "false") +
                  ",\"zoneVolume\":" + String((float)zvol / 100.0f, 3) +
                  ",\"zoneMuted\":"  + String(zmute ? "true" : "false") +
                  ",\"bufferMs\":"   + String(audioBufferMs) +
                  ",\"sourceSampleRate\":" + String((unsigned)g_sourceSampleRate) +
                  // [opus-sample-rate] Node re-creates decoder when this
                  // differs from its current decoder Fs.
                  ",\"opusSampleRate\":" + String((unsigned)g_opusSampleRate) +
                  ",\"mode\":\"" + String(playbackModeCStr()) +
                  "\",\"audioProtocolVersion\":" + String(AUDIO_PROTO_VERSION) +
                  ",\"negotiatedAudioProtocolVersion\":" + String(s.negotiatedAudioProtocolVersion) +
                  ",\"audioCompatible\":" + String(s.audioCompatible ? "true" : "false") +
                  ",\"epoch\":" + String(serverEpoch) +
                  ",\"clockSyncPort\":" + String(CLOCK_SYNC_UDP_PORT) +
                  ",\"encBitrate\":" + String(g_currentBitrate) +
                  ",\"encComplexity\":" + String(currentOpusComplexity) +
                  ",\"encVbr\":" + String(g_currentVbr ? "true" : "false") +
                  ",\"encVbrConstraint\":" + String(g_currentVbrConstraint ? "true" : "false") +
                  ",\"encSignal\":\"" + encSignalStr + "\"" +
                  ",\"encApplication\":\"" + encAppStr + "\"" +
                  "}";
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

// ---- Node performance diagnostics ------------------------------------------
inline void updateNodePerf(const String &ip, uint32_t latencyMs, uint32_t queueDepth,
                           uint32_t target, uint16_t rebuffers, unsigned long now) {
  if (!lockFor(perfMutex, 40)) return;
  bool found = false;
  for (auto &p : nodePerf) {
    if (p.ip == ip) {
      p.latencyMs  = latencyMs;
      p.queueDepth = queueDepth;
      p.target     = target;
      p.rebuffers  = rebuffers;
      p.updatedMs  = now;
      p.hasData    = true;
      found        = true;
      break;
    }
  }
  if (!found) {
    // Bound growth: drop stale entries first, then refuse new IPs when full.
    pruneStalePerfLocked(now);
    if (nodePerf.size() >= NODE_PERF_MAX_ENTRIES) { unlockFor(perfMutex); return; }
    nodePerf.push_back({ip, latencyMs, queueDepth, target, rebuffers, now, true});
  }
  unlockFor(perfMutex);
}

// ---- HTTP: /api/config, /api/source, /api/cmd ------------------------------
inline void handleApiConfig(AsyncWebServerRequest *request, uint8_t *data,
                            size_t len, size_t index, size_t total) {
  // [ota-gate-config] Reject /api/config during OTA. Mutations here either
  // stall the flash cache (saveEncoderModeSettings, below) or restart mDNS
  // (serverName change) — both are enough to disrupt the OTA push. The
  // dashboard's OTA wizard only POSTs /api/config at Step 1 (masterMuted)
  // BEFORE otaInProgress is set, so this gate never breaks the wizard.
  if (otaUploadInProgress || otaInProgress || otaPauseAudio) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"OTA in progress\"}");
    return;
  }
  // [config-body-concurrency] The body is accumulated per request (see
  // accumulateReqBody in EVDHelpers.h), so overlapping requests cannot
  // interleave bytes and no global latch is needed.
  const char *cfgBody = nullptr;
  size_t      cfgLen  = 0;
  if (!accumulateReqBody(request, data, len, index, total, 1024,
                         "{\"ok\":false,\"error\":\"Config payload too large\"}",
                         cfgBody, cfgLen)) return;
  if (cfgLen == 0) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"Empty request body\"}");
    return;
  }
  DynamicJsonDocument input(256);
  if (deserializeJson(input, cfgBody, cfgLen)) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }

  // ---- Phase 1: validate every field that has a range/format check ----
  // Any failure returns 400 BEFORE any mutation, so a request cannot
  // half-apply (e.g. switch mode then reject the buffer value).
  bool muteExplicit = input.containsKey("masterMuted") && input["masterMuted"].is<bool>();
  if (input.containsKey("masterVolume") && input["masterVolume"].is<int>()) {
    int v = input["masterVolume"];
    if (v < 0 || v > 100) {
      request->send(400, "application/json",
        "{\"ok\":false,\"error\":\"Volume must be between 0 and 100\"}");
      return;
    }
  }
  if (input.containsKey("audioBufferMs") && input["audioBufferMs"].is<int>()) {
    int v = input["audioBufferMs"];
    if (v < AUDIO_BUFFER_MIN_MS || v > AUDIO_BUFFER_MAX_MS) {
      request->send(400, "application/json",
        String("{\"ok\":false,\"error\":\"Buffer must be between ") +
        String(AUDIO_BUFFER_MIN_MS) + " and " + String(AUDIO_BUFFER_MAX_MS) +
        " ms\"}");
      return;
    }
  }
  if (input.containsKey("mode") && input["mode"].is<const char*>()) {
    String m = String((const char*)input["mode"]); m.toLowerCase();
    if (!(m == "off" || m == "music" || m == "movie")) {
      request->send(400, "application/json",
        "{\"ok\":false,\"error\":\"mode must be off, music, or movie\"}");
      return;
    }
  }
  if (input.containsKey("serverName") && input["serverName"].is<const char*>()) {
    String v = input["serverName"];
    if (v.length() == 0 || v.length() > 32 || v != sanitizeServerName(v)) {
      request->send(400, "application/json",
        "{\"ok\":false,\"error\":\"Server name must use 1-32 letters or numbers\"}");
      return;
    }
  }
  if (input.containsKey("defaultSource") && input["defaultSource"].is<const char*>()) {
    String v = input["defaultSource"]; v.toUpperCase();
    if (!(v == "BT" || v == "AUX" || v == "USB")) {
      request->send(400, "application/json",
        "{\"ok\":false,\"error\":\"defaultSource must be BT, AUX or USB\"}");
      return;
    }
  }

  // ---- Phase 2: apply. No early returns here. ----
  bool changed = false;
  if (muteExplicit) {
    masterMuted = input["masterMuted"].as<bool>();
    changed = true;
  }
  if (input.containsKey("mode") && input["mode"].is<const char*>()) {
    String m = String((const char*)input["mode"]); m.toLowerCase();
    ModeIdx oldMode = (ModeIdx)playbackModeIdx;
    captureLiveIntoMode(oldMode);
    playbackModeIdx = (uint8_t)modeNameToIdx(m);
    ModeIdx newMode = (ModeIdx)playbackModeIdx;
    applyModeSettingsToLive(newMode);
    changed = true;
  }
  if (input.containsKey("quietMode") && input["quietMode"].is<bool>()) {
    quietModeEnabled = input["quietMode"].as<bool>();
    // [quiet-reconfig-flag] Run configureQuietFilters() on the audio task,
    // not here (AsyncWebServer task). Also re-runs on DISABLE, which the
    // old `if (quietModeEnabled)` never did, so filters stayed applied.
    g_quietFiltersNeedReconfig = true;
    changed = true;
  }
  if (input.containsKey("masterVolume") && input["masterVolume"].is<int>()) {
    masterVolume = (uint8_t)(int)input["masterVolume"];
    if (!muteExplicit) masterMuted = false;   // slider still unmutes; explicit mute wins
    changed = true;
  }
  if (input.containsKey("audioBufferMs") && input["audioBufferMs"].is<int>()) {
    // [audio-buffer-slider-clamp] The slider POSTs continuously while it is
    // dragged (250 ms debounce in EVDCTRL.h), and the range attribute is not
    // a validation. Clamp to the same floor/ceiling /api/config enforces so
    // a stale cached page cannot set a buffer the players reject.
    {
      int bmv = (int)input["audioBufferMs"];
      if (bmv < AUDIO_BUFFER_MIN_MS) bmv = AUDIO_BUFFER_MIN_MS;
      if (bmv > AUDIO_BUFFER_MAX_MS) bmv = AUDIO_BUFFER_MAX_MS;
      audioBufferMs = (uint16_t)bmv;
    }
    // [audio-buffer-persist-mode-safe] The deferred loop() writer only
    // persists the mode that is CURRENT at flush time (up to 30 s later),
    // so a buffer change followed by a mode switch loses the change at
    // reboot: captureLiveIntoMode(OLD) leaves the change in g_modeSettings
    // but the flush writes the NEW mode's key, and enc_<old>_buf is never
    // updated.
    //
    // [audio-buffer-deferred-nvs] The fix for that was to write NVS
    // synchronously from this HTTP handler — one 4 KB sector erase (~100 ms
    // of cache stall, WiFi ACKs missed) PER SLIDER EVENT, i.e. 10-30 times
    // during one drag. Instead: stamp the change into the mode that is
    // current NOW (that is what made the old code correct) and let the
    // deferred writer persist it. g_bufferDirtyMode is captured here so the
    // flush writes the RIGHT mode's key even if the user switches modes
    // before it runs.
    ModeIdx cm = (ModeIdx)playbackModeIdx;
    g_modeSettings[cm].bufferMs = (int32_t)audioBufferMs;
    g_bufferDirtyMode   = (uint8_t)cm;
    g_bufferDirtyValid  = true;
    g_bufferDirtyAtMs   = millis();
    armSettingsFlush();
    changed = true;
  }
  if (input.containsKey("serverName") && input["serverName"].is<const char*>()) {
    String newName = String((const char*)input["serverName"]);
    if (newName != serverName) {
      serverName = newName;
      // [mdns-rename] Without this, mDNS kept the OLD name until reboot,
      // so <newName>.local never resolved and the old one still did.
      // [servers-decide] Only the leader owns mDNS; a standby has none.
      if (WiFi.status() == WL_CONNECTED && g_peerIsLeader) {
        MDNS.end();
        if (MDNS.begin(serverName.c_str())) {
          MDNS.addService("http",     "tcp", 80);
          MDNS.addService("enkelsrv", "tcp", 80);
        } else {
          LOGW("mDNS restart failed for new server name");
        }
      }
    }
    changed = true;
  }
  if (input.containsKey("defaultSource") && input["defaultSource"].is<const char*>()) {
    String v = String((const char*)input["defaultSource"]); v.toUpperCase();
    bootSourceIdx = bootSourceNameToIdx(v);
    LOGI("Boot default source queued for NVS flush: %s", bootSourceCStr());
    changed = true;
  }

  if (!changed) {
    request->send(400, "application/json",
      "{\"ok\":false,\"error\":\"No valid settings were supplied\"}");
    return;
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
                                  size_t len, size_t index, size_t total) {
  // [ota-gate-source] otaPushToSource() (EVDOTA.h) writes OTA_DATA lines
  // directly to receiverUart while otaInProgress is true. setReceiverSource()
  // below writes SRC:<name>\n to the same UART under uartMutex, but the
  // OTA push does NOT hold uartMutex for its per-line println calls — the
  // two sequences can interleave mid-line, sending "SRC:BT" inside an
  // OTA_DATA frame. The source's line parser then switches input source
  // mid-flash. Gate the endpoint entirely; the wizard never calls
  // /api/source, so operator tooling is unaffected.
  if (otaUploadInProgress || otaInProgress || otaPauseAudio) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"OTA in progress\"}");
    return;
  }
  if (index != 0 || len != total) {
    if (index != 0) return;
    request->send(413, "application/json",
                  "{\"ok\":false,\"error\":\"body fragmented; retry\"}");
    return;
  }
  String source = "";
  if (len > 0 && data) {
    DynamicJsonDocument in(256);
    if (!deserializeJson(in, data, len) && in["source"].is<const char*>())
      source = String((const char*)in["source"]);
  }
  source.trim();
  source.toUpperCase();

  if (source.length() == 0 || source.length() > 8 ||
      !(source == "BT" || source == "AUX" || source == "USB")) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"invalid source\"}");
    return;
  }

  // [dlna-exclusivity] Any manual source change (BT/AUX/USB) must stop the
  // DLNA fetch task, otherwise rawQueue would have two producers and the
  // stream would be corrupted. dlnaStopFetch() is idempotent. Done only
  // after the request validated so a bad POST cannot kill a DLNA stream.
  dlnaStopFetch();
  dlnaPlaying = false;
  dlnaPaused  = false;

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
    // serverInputSource is volatile enum (aligned byte store, atomic);
    // the two String assigns need uartMutex.
    serverInputSource = SRC_FROM_USB;
    if (lockFor(uartMutex, 50)) {
      receiverSource     = "USB";
      lastReceiverSource = "USB";
      unlockFor(uartMutex);
    }
    // Still forward to the source board so its log reflects the change.
    sendReceiverSourceCommand("USB");
    // [source-select-persist] Persist the boot default the moment the change
    // is ACCEPTED, not from a dashboard-side debounce. The old path relied on
    // EVDCTRL.h POSTing defaultSource 6 s later, which never ran if the tab
    // was closed, the phone slept, or the user rebooted first — and then the
    // dashboard came back up showing BT while the USB device was already
    // streaming. armSettingsFlush() is the same deferred writer every other
    // setting uses, so this costs no flash write from the HTTP task.
    bootSourceIdx = 2;                 // USB
    armSettingsFlush();
    LOGI("Source -> USB (server-side UAC device active), boot default persisted");
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
    // [src-assign-mutex] setReceiverSource() already writes
    // lastReceiverSource under uartMutex. The old extra unsynchronized
    // assignment raced SRCSTAT parsing and could tear the String buffer.
    if (ok) {
      // [source-select-persist] Same reasoning as the USB branch above: the
      // accepted source becomes the boot default, through the shared deferred
      // NVS writer. AUX is stored as AUX (the dashboard must show what was
      // chosen); the source board reports back "AUX not implemented" through
      // /api/source/info so the operator sees why it is silent.
      bootSourceIdx = bootSourceNameToIdx(source);
      armSettingsFlush();
      LOGI("Source -> %s accepted, boot default persisted", source.c_str());
      request->send(200, "application/json", resp);
    } else {
      request->send(502, "application/json",
                    String("{\"ok\":false,\"bridgeHttp\":") + String(code) +
                    ",\"receiver\":" + resp + "}");
    }
    return;
  }

  request->send(400, "application/json",
                "{\"ok\":false,\"error\":\"invalid source\"}");
}

// ---- /api/audio/encoding ------------------------------------------------
// The dashboard's Advanced > Encoding tab GETs this on first visit to
// populate the form, then POSTs the whole form to apply changes. Same
// thread-safety rule as the complexity tuner: the POST handler must not
// touch opus_encoder_ctl() directly, because opus_encode() is running on
// audioProcessingTask (Core 1) and libopus is not re-entrant per encoder
// instance. The handler writes the g_new* variables and sets the dirty
// flag; the audio task drains them.

inline void handleGetAudioEncoding(AsyncWebServerRequest *request) {
  // [encoder-get-intended-state]
  // Return the INTENDED state (g_new*), not the applied state (g_current*),
  // so the dashboard form is correct immediately after a mode switch or a
  // slider move, without waiting for audioProcessingTask() to drain
  // g_encoderSettingsDirty. g_new* is set SYNCHRONOUSLY by
  // applyModeSettingsToLive() (mode switch) and by handlePostAudioEncoding()
  // (slider POSTs), so a GET issued right after either of those reflects
  // what the audio task WILL apply on its next frame. g_current* is still
  // exposed in /api/state (encComplexity, encBitrate, ...) for diagnostics.
  int effectiveComplexity = (g_pendingComplexity >= 0)
                            ? g_pendingComplexity : currentOpusComplexity;

  DynamicJsonDocument doc(512);
  doc["ok"]              = true;
  doc["mode"]            = playbackModeCStr();
  doc["bitrate"]         = g_newBitrate;
  doc["complexity"]      = effectiveComplexity;
  doc["vbr"]             = (g_newVbr != 0);
  doc["vbrConstraint"]   = (g_newVbrConstraint != 0);
  doc["bufferMs"]        = (int)audioBufferMs;   // [audio-buffer-per-mode]
  // [opus-sample-rate] Report the INTENDED (pending or current) rate so the
  // dropdown reflects the user's choice immediately, before the audio task
  // has actually re-created the encoder.
  // [app-change-recreate-fix] Ignore the 0xFFFFFFFF force-recreate signal.
  doc["sampleRate"]      = (int)((g_pendingOpusSampleRate != 0 && g_pendingOpusSampleRate != 0xFFFFFFFF)
                                 ? g_pendingOpusSampleRate
                                 : g_opusSampleRate);

  if      (g_newSignal == OPUS_SIGNAL_MUSIC) doc["signal"] = "music";
  else if (g_newSignal == OPUS_SIGNAL_VOICE) doc["signal"] = "voice";
  else                                       doc["signal"] = "auto";

  if      (g_newApplication == OPUS_APPLICATION_AUDIO) doc["application"] = "audio";
  else if (g_newApplication == OPUS_APPLICATION_VOIP)  doc["application"] = "voip";
  else                                                 doc["application"] = "lowdelay";

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

// [per-mode-encoder-settings]
// POST /api/audio/encoding applies the supplied fields to the live encoder.
// With `save:true` it additionally captures the resulting live state into
// g_modeSettings[current] and persists that mode's preset to NVS. This is
// how the dashboard's Save button works; the Revert button calls
// /api/audio/encoding/revert, and Load Defaults calls /defaults. Slider
// movements POST here with save:false so the live encoder tracks them,
// but nothing is written to NVS until Save is clicked.
inline void handlePostAudioEncoding(AsyncWebServerRequest *request, uint8_t *data,
                                    size_t len, size_t index, size_t total) {
  // [enc-single-chunk] Body is <200 B and never fragments in practice, but
  // a fragmented body parsed here would apply PARTIAL settings — the exact
  // class of silent half-apply that handlePlayerRegistration and
  // handleNodeStats already guard against. Reject and let the client retry.
  if (index != 0 || len != total) {
    if (index != 0) return;
    request->send(413, "application/json",
                  "{\"ok\":false,\"error\":\"encoding body fragmented; retry\"}");
    return;
  }
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, data, len)) {
   
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }

  // [ota-gate-enc-save] saveEncoderModeSettings() below writes 9 NVS keys
  // synchronously (~100 ms flash-cache stall) and is NOT on the deferred
  // settings-dirty path. Slider movement (save:false) only touches g_new*
  // and g_encoderSettingsDirty — the audio task drains those harmlessly
  // even during OTA — so we only block the explicit Save button.
  const bool otaActive =
      otaUploadInProgress || otaInProgress || otaPauseAudio;
  const bool wantsSave = doc["save"] | false;
  if (otaActive && wantsSave) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"OTA in progress; "
                  "Encoder Save deferred until after update\"}");
    return;
  }

  if (doc.containsKey("bitrate") && doc["bitrate"].is<int>()) {
    int b = doc["bitrate"];
    // [bitrate-cap-matches-packet]
    // OPUS_MAX_PACKET_BYTES is 1024. CBR 192 kbps = 480 B/frame; VBR
    // peak ~2x => 960 B, just under the cap. Above ~192 kbps the encoder
    // returns a negative length on VBR peaks and audioEncodeTask silently
    // drops the frame. Clamp the API so a direct POST cannot configure a
    // bitrate the transport discards. The dashboard slider max is already
    // 192 kbps — this closes the API hole.
    constexpr int kBitrateMax = 192000;
    if (b >= 6000 && b <= 510000) {
      if (b > kBitrateMax) {
        LOGW("POST /api/audio/encoding: bitrate %d clamped to %d "
             "(packet cap)", b, kBitrateMax);
        b = kBitrateMax;
      }
      g_newBitrate = b;
    }
  }
  if (doc.containsKey("complexity") && doc["complexity"].is<int>()) {
    int c = doc["complexity"];
    if (c >= 0 && c <= 10) g_pendingComplexity = c;
  }
  if (doc.containsKey("vbr") && doc["vbr"].is<bool>()) {
    g_newVbr = doc["vbr"] ? 1 : 0;
  }
  if (doc.containsKey("vbrConstraint") && doc["vbrConstraint"].is<bool>()) {
    g_newVbrConstraint = doc["vbrConstraint"] ? 1 : 0;
  }
  if (doc.containsKey("signal") && doc["signal"].is<const char*>()) {
    String s = doc["signal"];
    if      (s == "music") g_newSignal = OPUS_SIGNAL_MUSIC;
    else if (s == "voice") g_newSignal = OPUS_SIGNAL_VOICE;
    else if (s == "auto")  g_newSignal = OPUS_AUTO;
  }
  if (doc.containsKey("application") && doc["application"].is<const char*>()) {
    String a = doc["application"];
    if      (a == "audio")    g_newApplication = OPUS_APPLICATION_AUDIO;
    else if (a == "voip")     g_newApplication = OPUS_APPLICATION_VOIP;
    else if (a == "lowdelay") g_newApplication = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
  }

  if (doc.containsKey("bufferMs") && doc["bufferMs"].is<int>()) {
    int bm = doc["bufferMs"];
    if (bm >= AUDIO_BUFFER_MIN_MS && bm <= AUDIO_BUFFER_MAX_MS) {
      audioBufferMs = (uint16_t)bm;
    }
  }

  if (doc.containsKey("sampleRate") && doc["sampleRate"].is<int>()) {
    int sr = doc["sampleRate"];
    if (sr == OPUS_SAMPLE_RATE_48K || sr == OPUS_SAMPLE_RATE_24K) {
      // [preserve-force-recreate] 0xFFFFFFFF is the "app-mode changed;
      // re-create at current rate" sentinel set by the g_newApplication
      // branch above. A sample-rate POST landing before audioEncodeTask
      // drains it must NOT overwrite the sentinel — that silently loses
      // the application change until reboot.
      if (g_pendingOpusSampleRate == 0xFFFFFFFF) {
        LOGI("Opus sample rate POST ignored: app-mode re-create already pending");
      } else if ((uint32_t)sr != g_opusSampleRate) {
        g_pendingOpusSampleRate = (uint32_t)sr;
        LOGI("Opus sample rate queued: %d Hz (pending re-create)", sr);
      } else if (g_pendingOpusSampleRate != 0) {
        g_pendingOpusSampleRate = 0;
        LOGI("Opus sample rate: pending change cancelled (user re-selected %d Hz)",
             sr);
      }
    }
  }

  g_encoderSettingsDirty = true;

  bool save = doc["save"] | false;
  // [enc-save-ota-guard] A Save writes 9 NVS keys immediately on the
  // AsyncWebServer task, stalling the flash cache while audioRead/
  // audioProc/udpSend are live. During OTA it also collides with the
  // LittleFS upload. Reject Save while OTA is active; otherwise mark
  // settingsDirty and let the quiet-window writer persist it.
  if (save && (otaUploadInProgress || otaInProgress)) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"OTA in progress; Save deferred\"}");
    return;
  }
  if (save) {
    ModeIdx m = (ModeIdx)playbackModeIdx;
    // [save-incoming-trace] Log the incoming JSON's application field BEFORE
    // captureLiveIntoMode(), so a "the operator selected voip but the preset
    // stayed audio" bug is distinguishable from "operator selected audio by
    // mistake". If the incoming value already says "audio", the UI never
    // sent the change; if it says "voip" but the captured/applied values
    // below show audio, captureLiveIntoMode() or applyModeSettingsToLive()
    // is at fault.
    const char *incomingApp = doc["application"].is<const char*>()
                              ? (const char*)doc["application"] : "(missing)";
    LOGI("ENC SAVE incoming: mode=%s app_in=%s sig_in=%s",
         modeIdxToName(m), incomingApp,
         doc["signal"].is<const char*>() ? (const char*)doc["signal"] : "(missing)");
    captureLiveIntoMode(m);
    saveEncoderModeSettings(m);
  }

  broadcastNodeConfig();

  String resp = String("{\"ok\":true,\"saved\":") + (save ? "true" : "false") +
                ",\"mode\":\"" + String(playbackModeCStr()) + "\"" +
                ",\"note\":\"Application mode requires reboot to take effect; "
                "sample rate change re-creates encoder on next audio frame\"}";
  request->send(200, "application/json", resp);
}


// [per-mode-encoder-settings] Reload the current mode's preset from NVS into
// the live encoder. The Revert button calls this.
inline void handlePostAudioEncodingRevert(AsyncWebServerRequest *request) {
  // [ota-gate-enc-revert] loadEncoderModeSettings() reads ~24 NVS keys
  // (fast, but not free) and broadcastNodeConfig() iterates the subscriber
  // table and does one UDP send per subscriber. Neither is fatal, but
  // during a 90 %-busy Core 0 they consume AsyncTCP scheduling slots that
  // the OTA push needs. Cheap to gate.
  if (otaUploadInProgress || otaInProgress || otaPauseAudio) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"OTA in progress\"}");
    return;
  }
  ModeIdx m = (ModeIdx)playbackModeIdx;
  loadEncoderModeSettings();
  applyModeSettingsToLive(m);
  broadcastNodeConfig();   // [revert-broadcast] match Save's push cadence
  request->send(200, "application/json",
                String("{\"ok\":true,\"mode\":\"") + modeIdxToName(m) + "\"}");
}

// [per-mode-encoder-settings] Apply factory defaults to the current mode.
// Does NOT persist; the Save button is the only path to NVS.
inline void handlePostAudioEncodingDefaults(AsyncWebServerRequest *request) {
  // [ota-gate-enc-defaults] RAM-only, but broadcastNodeConfig() does one
  // UDP send per subscriber, which competes with the OTA push for AsyncTCP
  // scheduling slots on the saturated Core 0. Same reasoning as Revert.
  if (otaUploadInProgress || otaInProgress || otaPauseAudio) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"OTA in progress\"}");
    return;
  }
  ModeIdx m = (ModeIdx)playbackModeIdx;
  // [load-defaults-buffer-fix] The 6-arg aggregate left bufferMs=0,
  // which applyModeSettingsToLive() clamps to AUDIO_BUFFER_MIN_MS (60).
  // Supply the real per-mode defaults from EVDState.h's table.
  static const int32_t kDefaultBufMs[MODE_COUNT] = { 120, 150, 80 };
  // [opus-sample-rate] 8-field aggregate: bitrate, complexity, vbr,
  // vbrConstraint, signal, application, sampleRate, bufferMs.
  // sampleRate MUST come before bufferMs to match the struct order in
  // EVDState.h. All modes default to 48 kHz.
  g_modeSettings[m] = ModeEncSettings{
      OPUS_BITRATE, 4, true, true,
      OPUS_SIGNAL_MUSIC, OPUS_APPLICATION_AUDIO,
      OPUS_SAMPLE_RATE_48K, kDefaultBufMs[m] };
  applyModeSettingsToLive(m);
  broadcastNodeConfig();   // [defaults-broadcast] match Save's push cadence
  request->send(200, "application/json",
                String("{\"ok\":true,\"mode\":\"") + modeIdxToName(m) + "\"}");
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
  // [cmd-uppercase-uart] Source board UART grammar uses uppercase
  // (KICK_BT, GET_BT_DEVICES, SRC:BT). Sending CMD:next was silently
  // dropped. Preserve lowercase for validation, send uppercase on wire.
  cmd.toUpperCase();
  String resp; uint16_t code = 0;
  bool ok = bridgeSendReceiverCmd(cmd, resp, code);
  if (ok) request->send(200, "application/json", resp);
  else    request->send(502, "application/json",
                       String("{\"ok\":false,\"bridgeHttp\":") + String(code) + ",\"receiver\":" + resp + "}");
}