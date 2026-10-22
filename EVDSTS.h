#pragma once
// ============================================================================
//  EVDSTS.h - /api/state and /api/stats endpoints
// ============================================================================
#include <Arduino.h>
#include <vector>
#include <ESPAsyncWebServer.h>

inline void appendZoneJson(AsyncResponseStream *response, const Zone &z,
                           const std::vector<PlayerNode> &players) {
  response->print("{\"id\":\"");
  response->print(jsonEscape(z.id).c_str());
  response->print("\",\"name\":\"");
  response->print(jsonEscape(z.name).c_str());
  response->printf("\",\"volume\":%d,\"muted\":%s,\"nodes\":[",
                   z.volume, z.muted ? "true" : "false");
  bool first = true;
  for (auto &ip : z.nodeIps) {
    bool known = false;
    String name = ip;
    String version = "";                                // NEW
    for (auto &p : players) {
      if (p.ip == ip) {
        name = p.name;
        version = p.version;                            // NEW
        known = true; break;
      }
    }
    if (!first) response->print(",");
    response->print("{\"name\":\"");
    response->print(jsonEscape(name).c_str());
    response->print("\",\"ip\":\"");
    response->print(jsonEscape(ip).c_str());
    response->print("\",\"version\":\"");               // NEW
    response->print(jsonEscape(version).c_str());       // NEW
    response->print("\",\"known\":");
    response->print(known ? "true" : "false");
    response->print("}");
    first = false;
  }
  response->print("]}");
}

inline void handleApiState(AsyncWebServerRequest *request) {
  std::vector<PlayerNode> players = snapshotPlayers(millis());
  std::vector<Zone> zcopy = snapshotZones();

  if (!players.empty()) {
    bool hasMain = false;
    for (auto &z : zcopy) if (z.id == MAIN_ZONE_ID) hasMain = true;
    if (!hasMain) {
      zcopy.insert(zcopy.begin(),
                   Zone{MAIN_ZONE_ID, "Main Room", 100, false, std::vector<String>()});
    }
    for (auto &p : players) {
      bool assigned = false;
      for (auto &z : zcopy) {
        for (auto &ip : z.nodeIps) {
          if (ip == p.ip) { assigned = true; break; }
        }
        if (assigned) break;
      }
      if (!assigned) {
        for (auto &z : zcopy) {
          if (z.id == MAIN_ZONE_ID) { z.nodeIps.push_back(p.ip); break; }
        }
      }
    }
  }

  // ---- CPU / RAM / PSRAM stats (always-visible dashboard row) ----------
  // CPU% requires CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS (see build_opt.h).
  // When the flag is off, uxTaskGetSystemState() leaves *pulTotalRunTime at
  // 0 and every task's ulRunTimeCounter at 0; the sanity gate below then
  // fails and cpuN stays -1, which the dashboard renders as "n/a".
  //
  // [cpu-stat-delta-wrap-fix]
  // configRUN_TIME_COUNTER_TYPE is uint32_t in ESP-IDF FreeRTOS, and
  // portGET_RUN_TIME_COUNTER_VALUE() == (uint32_t)esp_timer_get_time().
  // That is a 32-bit microsecond counter that wraps every 2^32 us
  // (~71.6 minutes).  The previous absolute-value math (idle/total * 100)
  // silently reported "100 % CPU" for whichever core's idle task happened
  // to be first to wrap, until the next wrap 71 minutes later.
  //
  // This version samples between polls and takes a *delta* of idle and
  // total runtime.  uint32_t subtraction is modulo-2^32 and therefore
  // already wrap-safe; the sanity gate rejects a window longer than
  // ~35 minutes of accumulated runtime (the only way a delta could wrap
  // twice, e.g. a paused browser tab) and rejects zero-window noise from
  // a second caller arriving in the same tick.  A minimum window of
  // 100 ms of accumulated runtime keeps a fast double-poll from clobbering
  // the baseline with a near-zero delta.
  //
  // First /api/state poll after boot returns cpu0Pct == cpu1Pct == -1
  // (n/a) because there is no baseline yet; the next 4 s poll reports
  // live values.
  //
  // N16R8 = 16 MB flash + 8 MB octal PSRAM; ESP.getPsramSize() returns
  // 0 on a build where PSRAM is disabled in the board config, in which
  // case the PSRAM pill is hidden by the JS.
  int8_t cpu0Pct = -1, cpu1Pct = -1;
  {
    // 48 slots gives ~50 % headroom over the typical N16R8 task count
    // (audioRead, audioProc, udpSend, ClockSync, loopTask, AsyncTCP,
    // tcpip, wifi, BT controller + host, IDLE0, IDLE1, Tmr Svc, esp_timer,
    // ipc0, ipc1, …).  32 was already tight and would silently truncate
    // *pulTotalRunTime on a future IDF that adds tasks.  Static storage
    // so no stack growth in the AsyncTCP handler.
    static TaskStatus_t taskSnap[48];
    static uint32_t      prevIdle0 = 0;
    static uint32_t      prevIdle1 = 0;
    static uint32_t      prevTotal = 0;
    static bool          haveBase  = false;

    uint32_t totalRuntime = 0;
    UBaseType_t nTasks = uxTaskGetSystemState(taskSnap, 48, &totalRuntime);
    if (nTasks > 0 && totalRuntime > 0) {
      TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCore(0);
      TaskHandle_t idle1 = xTaskGetIdleTaskHandleForCore(1);
      uint32_t currIdle0 = 0, currIdle1 = 0;
      for (UBaseType_t i = 0; i < nTasks; i++) {
        if (taskSnap[i].xHandle == idle0) currIdle0 = taskSnap[i].ulRunTimeCounter;
        if (taskSnap[i].xHandle == idle1) currIdle1 = taskSnap[i].ulRunTimeCounter;
      }

      if (haveBase) {
        // Unsigned subtraction handles the 32-bit wrap correctly.
        uint32_t dIdle0 = currIdle0 - prevIdle0;
        uint32_t dIdle1 = currIdle1 - prevIdle1;
        uint32_t dTotal = totalRuntime - prevTotal;
        // Reject: zero-window (<100 ms of accumulated runtime), a window
        // that looks like two wraps (> ~35 min of runtime), and any
        // impossible idle count.
        if (dTotal >= 100000u && dTotal < 0x80000000u &&
            dIdle0 <= dTotal && dIdle1 <= dTotal) {
          uint32_t idle0Pct = (uint32_t)((uint64_t)dIdle0 * 100ULL / dTotal);
          uint32_t idle1Pct = (uint32_t)((uint64_t)dIdle1 * 100ULL / dTotal);
          if (idle0Pct > 100) idle0Pct = 100;
          if (idle1Pct > 100) idle1Pct = 100;
          cpu0Pct = (int8_t)(100 - idle0Pct);
          cpu1Pct = (int8_t)(100 - idle1Pct);

          // Advance the baseline only when the delta was usable.  A
          // zero-window double-poll then cannot poison the next sample.
          prevIdle0 = currIdle0;
          prevIdle1 = currIdle1;
          prevTotal = totalRuntime;
        }
      } else {
        // First sample after boot: seed the baseline, report n/a.
        prevIdle0 = currIdle0;
        prevIdle1 = currIdle1;
        prevTotal = totalRuntime;
        haveBase  = true;
      }
    }
  }

  uint32_t heapTotal = ESP.getHeapSize();
  uint32_t heapFree  = ESP.getFreeHeap();
  uint32_t heapUsed  = heapTotal > heapFree ? heapTotal - heapFree : 0;
  uint8_t  heapPct   = heapTotal
                       ? (uint8_t)((uint64_t)heapUsed * 100 / heapTotal)
                       : 0;

  uint32_t psramTotal = ESP.getPsramSize();
  uint32_t psramFree  = ESP.getFreePsram();
  uint32_t psramUsed  = psramTotal > psramFree ? psramTotal - psramFree : 0;
  uint8_t  psramPct   = psramTotal
                        ? (uint8_t)((uint64_t)psramUsed * 100 / psramTotal)
                        : 0;

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->print("{");
  response->print("\"ok\":true,");
  response->printf("\"ip\":\"%s\",", activeIpAddress().toString().c_str());
  response->printf("\"serverName\":\"%s\",", jsonEscape(serverName).c_str());
  response->printf("\"masterMuted\":%s,", masterMuted ? "true" : "false");
  response->printf("\"cpuCore0Pct\":%d,", (int)cpu0Pct);
  response->printf("\"cpuCore1Pct\":%d,", (int)cpu1Pct);
  response->printf("\"heapUsedPct\":%u,", heapPct);
  response->printf("\"heapFreeBytes\":%u,", heapFree);
  response->printf("\"heapTotalBytes\":%u,", heapTotal);
  response->printf("\"psramUsedPct\":%u,", psramPct);
  response->printf("\"psramFreeBytes\":%u,", psramFree);
  response->printf("\"psramTotalBytes\":%u,", psramTotal);
  response->printf("\"masterVolume\":%d,", masterVolume);
  response->printf("\"latencyAdjustmentMs\":%d,", latencyAdjustmentMs);
  response->printf("\"audioBufferMs\":%d,", audioBufferMs);
  response->printf("\"mode\":\"%s\",", jsonEscape(playbackMode).c_str());
  response->printf("\"audioProtocolVersion\":%d,", AUDIO_PROTO_VERSION);
  response->printf("\"serverEpoch\":%lu,", (unsigned long)serverEpoch);

  response->print("\"receiverAudio\":{");
  response->printf("\"source\":\"%s\"", jsonEscape(receiverSource).c_str());
  response->print("},");

  response->print("\"players\":[");
  for (size_t i = 0; i < players.size(); ++i) {
    if (i > 0) response->print(",");
    response->print("{");
    response->printf("\"name\":\"%s\",", jsonEscape(players[i].name).c_str());
    response->printf("\"ip\":\"%s\",",   jsonEscape(players[i].ip).c_str());
    response->printf("\"version\":\"%s\",", jsonEscape(players[i].version).c_str());  // NEW
    response->printf("\"declaredAudioProtocolVersion\":%d,", players[i].declaredAudioProtocolVersion);
    response->printf("\"negotiatedAudioProtocolVersion\":%d,", players[i].negotiatedAudioProtocolVersion);
    response->printf("\"audioCompatible\":%s,", players[i].audioCompatible ? "true" : "false");
    response->printf("\"audioCompatReason\":\"%s\"", jsonEscape(players[i].audioCompatReason).c_str());
    response->print("}");
  }
  response->print("],");

  response->print("\"zones\":[");
  for (size_t i = 0; i < zcopy.size(); ++i) {
    if (i > 0) response->print(",");
    appendZoneJson(response, zcopy[i], players);
  }
  response->print("]");

  response->print("}");
  request->send(response);
}

inline void handleApiStats(AsyncWebServerRequest *request) {
  uint32_t latSum, latCnt;
  portENTER_CRITICAL(&gStatsMux);
    latSum = statLatencySumMs;
    latCnt = statLatencyCount;
  portEXIT_CRITICAL(&gStatsMux);
  float avgLatency = latCnt > 0 ? (float)latSum / (float)latCnt : 0.0f;
  std::vector<NodePerf> perf = snapshotPerf();

  uint32_t rawQD  = rawQueue  ? (uint32_t)uxQueueMessagesWaiting(rawQueue)  : 0;
  uint32_t opusQD = opusQueue ? (uint32_t)uxQueueMessagesWaiting(opusQueue) : 0;

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->print("{");
  response->print("\"ok\":true,");
  response->printf("\"ip\":\"%s\",", activeIpAddress().toString().c_str());
  response->printf("\"wifiConnected\":%s,", (WiFi.status() == WL_CONNECTED) ? "true" : "false");
  response->printf("\"wifiRssi\":%d,", WiFi.RSSI());
  response->printf("\"wifiSsid\":\"%s\",", jsonEscape(wifiStaSsid).c_str());
  response->printf("\"wifiDisconnects\":%u,", (unsigned)statWifiDisconnects);
  response->printf("\"wsClients\":%d,", subscriberCount);   // keep key name for dashboard

  response->printf("\"mode\":\"%s\",", jsonEscape(playbackMode).c_str());
  response->printf("\"audioBufferMs\":%u,", audioBufferMs);
  response->printf("\"latencyAdjustmentMs\":%u,", latencyAdjustmentMs);
  response->printf("\"masterVolume\":%u,", masterVolume);
  response->printf("\"masterMuted\":%s,", masterMuted ? "true" : "false");

  response->printf("\"i2sReadErrors\":%u,", statI2sReadErrors);
  response->printf("\"wsBackpressureDrops\":%u,", statUdpErrors);
  response->printf("\"avgEncodeUs\":%u,", (unsigned)gAvgEncodeUs);
  response->printf("\"measuredInputHz\":%.1f,", (double)statMeasuredInputHz);
  response->printf("\"clockDriftPpm\":%ld,", (long)statDriftPpm);
  response->printf("\"audioProtocolVersion\":%d,", AUDIO_PROTO_VERSION);
  response->printf("\"serverEpoch\":%lu,", (unsigned long)serverEpoch);
  response->printf("\"clockSyncPort\":%d,", CLOCK_SYNC_UDP_PORT);
  response->printf("\"syncRequestsServed\":%u,", statSyncRequestsServed);
  response->printf("\"syncRequestErrors\":%u,", statSyncRequestErrors);
  response->printf("\"syncRequestsThrottled\":%u,", statSyncRequestsThrottled);
  response->printf("\"audioIncompatibleSkips\":%u,", statAudioIncompatibleSkips);
  response->printf("\"audioSyncPending\":%u,",       statAudioSyncPending);   // [stat-audio-sync-pending]
  response->printf("\"usbBytesLastWin\":%u,", statUsbBytesLast5s);
  response->printf("\"serverInputSource\":\"%s\",", (serverInputSource == SRC_FROM_USB) ? "USB" : "I2S");
  response->print("\"streaming\":{");
  response->printf("\"i2sBytesLast5s\":%u,", statI2sBytesLast5s);
  response->printf("\"rawDroppedLast5s\":%u,", statRawDroppedLast5s);
  response->printf("\"opusDroppedLast5s\":%u", statOpusDroppedLast5s);
  response->print("},");

  response->printf("\"rawQueueDepth\":%u,",  rawQD);
  response->printf("\"rawQueueLen\":%d,",    RAW_QUEUE_LEN);
  response->printf("\"opusQueueDepth\":%u,", opusQD);
  response->printf("\"opusQueueLen\":%d,",   OPUS_QUEUE_LEN);
  response->printf("\"opusEncodedLastWin\":%u,", statOpusEncodedLast5s);
  response->printf("\"opusDroppedLastWin\":%u,", statOpusDroppedLast5s);
  response->printf("\"wsSentLastWin\":%u,",      statUdpPacketsLast5s);
  response->printf("\"wsBytesLastWin\":%u,",     statUdpBytesLast5s);
  response->printf("\"avgLatencyMs\":%.2f,", avgLatency);

  response->print("\"nodes\":[");
  for (size_t i = 0; i < perf.size(); ++i) {
    if (i > 0) response->print(",");
    response->print("{\"ip\":\"");
    response->print(jsonEscape(perf[i].ip).c_str());
    response->printf("\",\"latencyMs\":%u,\"queueDepth\":%u,\"target\":%u,\"rebuffers\":%u}",
                     perf[i].latencyMs, perf[i].queueDepth, perf[i].target, perf[i].rebuffers);
  }
  response->print("],");

  response->print("\"receiverAudio\":{");
  response->printf("\"state\":\"%s\"", jsonEscape(receiverState).c_str());
  response->print("}");

  response->print("}");
  request->send(response);
}