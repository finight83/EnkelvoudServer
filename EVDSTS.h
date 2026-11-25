#pragma once
// ============================================================================
//  EVDSTS.h - /api/state and /api/stats endpoints
// ============================================================================
//
// [single-string-response]
// Both handlers below used AsyncResponseStream and a long series of
// response->print()/printf() calls. Each print is a separate AsyncTCP
// chunk - ~50 for /api/state and ~70 for /api/stats - each of which
// requires a fresh lwIP pbuf allocation. On a healthy heap that is
// invisible. On a heap that is already at 90 %+ it is the single
// biggest cause of "the dashboard feels slow but the server is fine":
// every pbuf allocation walks the heap_caps free list, and any failed
// allocation is retried by AsyncTCP before the segment is sent.
//
// The two handlers the dashboard polls most frequently were the ONLY
// two that used the chunked pattern; every other endpoint already
// builds a single String and uses beginResponse(200, ..., body). This
// file brings the state/stats endpoints into line with the rest of the
// tree. The output JSON is byte-for-byte identical.
// ============================================================================

#include <Arduino.h>
#include <vector>
#include <ESPAsyncWebServer.h>
#include <esp_heap_caps.h>

inline void appendZoneJson(String &body, const Zone &z,
                           const std::vector<PlayerNode> &players) {
  body += "{\"id\":\"";
  body += jsonEscape(z.id);
  body += "\",\"name\":\"";
  body += jsonEscape(z.name);
  body += "\",\"volume\":";
  body += String(z.volume);
  body += ",\"muted\":";
  body += z.muted ? "true" : "false";
  body += ",\"nodes\":[";
  bool first = true;
  for (auto &ip : z.nodeIps) {
    bool known = false;
    String name = ip;
    String version = "";
    for (auto &p : players) {
      if (p.ip == ip) {
        name = p.name;
        version = p.version;
        known = true; break;
      }
    }
    if (!first) body += ",";
    body += "{\"name\":\"";
    body += jsonEscape(name);
    body += "\",\"ip\":\"";
    body += jsonEscape(ip);
    body += "\",\"version\":\"";
    body += jsonEscape(version);
    body += "\",\"known\":";
    body += known ? "true" : "false";
    body += "}";
    first = false;
  }
  body += "]}";
}

inline void handleApiState(AsyncWebServerRequest *request) {
  // [ota-heap-safety] Skip heavy JSON generation during OTA upload/flash.
  // Still return the scalar fields the dashboard polls, otherwise
  // refreshStatus() assigns `undefined` to the volume/buffer sliders and
  // mute button, which snap to defaults for the whole update.
  if (otaUploadInProgress || otaInProgress) {
    String stub;
    stub.reserve(384);
    stub  = "{\"ok\":true,\"ota\":true";
    stub += ",\"serverName\":\""; stub += jsonEscape(serverName); stub += "\"";
    stub += ",\"masterMuted\":"; stub += masterMuted ? "true" : "false";
    stub += ",\"quietMode\":";   stub += quietModeEnabled ? "true" : "false";
    stub += ",\"masterVolume\":"; stub += String((int)masterVolume);
    stub += ",\"potVolume\":";    stub += String((int)potVolume);
    stub += ",\"audioBufferMs\":"; stub += String((int)audioBufferMs);
    stub += ",\"mode\":\"";        stub += jsonEscape(String(playbackModeCStr())); stub += "\"";
    stub += ",\"receiverAudio\":{\"source\":\"\"}";
    stub += ",\"players\":[],\"zones\":[]}";
    AsyncWebServerResponse *resp =
        request->beginResponse(200, "application/json", stub);
    resp->addHeader("Cache-Control", "no-store, max-age=0");
    request->send(resp);
    return;
  }
  
  std::vector<PlayerNode> players = snapshotPlayers(millis());
  std::vector<Zone> zcopy = snapshotZones();

  // [discovered-nodes] Keep a synthetic Main room for the UI, but do NOT
  // attach unassigned players to it: they belong to the Discovered pool and
  // are muted until the operator drags them into a room.
  {
    bool hasMain = false;
    for (auto &z : zcopy) if (z.id == MAIN_ZONE_ID) hasMain = true;
    if (!hasMain) {
      zcopy.insert(zcopy.begin(),
                   Zone{MAIN_ZONE_ID, "Main Room", 100, false, std::vector<String>()});
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
  // (~71.6 minutes). The absolute-value math (idle/total * 100) silently
  // reported "100 % CPU" for whichever core's idle task happened to be
  // first to wrap, until the next wrap 71 minutes later.
  //
  // This version samples between polls and takes a *delta* of idle and
  // total runtime. uint32_t subtraction is modulo-2^32 and therefore
  // already wrap-safe; the sanity gate rejects a window longer than
  // ~35 minutes of accumulated runtime and rejects zero-window noise
  // from a second caller arriving in the same tick. A minimum window of
  // 100 ms of accumulated runtime keeps a fast double-poll from
  // clobbering the baseline with a near-zero delta.
  //
  // First /api/state poll after boot returns cpu0Pct == cpu1Pct == -1
  // (n/a) because there is no baseline yet; the next 4 s poll reports
  // live values.
  int8_t cpu0Pct = -1, cpu1Pct = -1;
  {
    // [cpu-stat-task-array-headroom]
    // 64 slots because ESP-IDF 5.x on an S3 with WiFi + AsyncTCP + four
    // audio tasks + system timers can exceed 48. If nTasks (the ACTUAL
    // count) is greater than the array size, uxTaskGetSystemState fills
    // the first N slots and the rest are silently dropped - including
    // their runtime contributions to *pulTotalRunTime. IDLE0 / IDLE1 are
    // typically the last entries, so on a task-heavy build they are the
    // first to disappear from the snapshot. That pathology produces
    // exactly "dIdle0 == 0 forever, c0 == 100".
    static TaskStatus_t taskSnap[64];
    static uint32_t      prevIdle0 = 0;
    static uint32_t      prevIdle1 = 0;
    static uint32_t      prevTotal = 0;
    static bool          haveBase  = false;

    uint32_t totalRuntime = 0;
    UBaseType_t nTasks = uxTaskGetSystemState(taskSnap, 64, &totalRuntime);
    if (nTasks > 64) nTasks = 64;
    if (nTasks > 0 && totalRuntime > 0) {
      TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCore(0);
      TaskHandle_t idle1 = xTaskGetIdleTaskHandleForCore(1);

      // [idle-lookup-strict-handle]
      // Match by handle ONLY. Name-based fallback ("IDLE0"/"IDLE1"/"IDLE")
      // is unsafe: it overwrites on any name hit, so a user task named
      // "IDLE0" would have its counter used instead of the real idle
      // task; and the bare "IDLE" fallback assigns the same counter to
      // both cores, producing identical C0/C1 pills on a dual-core
      // build. If xTaskGetIdleTaskHandleForCore() returns a handle that
      // does not appear in taskSnap (IDF bug, truncation), we accept
      // n/a and log once.
      uint32_t currIdle0 = 0, currIdle1 = 0;
      bool     foundIdle0 = false, foundIdle1 = false;
      for (UBaseType_t i = 0; i < nTasks; i++) {
        if (taskSnap[i].xHandle == idle0) { currIdle0 = taskSnap[i].ulRunTimeCounter; foundIdle0 = true; }
        if (taskSnap[i].xHandle == idle1) { currIdle1 = taskSnap[i].ulRunTimeCounter; foundIdle1 = true; }
      }
      // [idle-unicore-handle-equal]
      // If the two per-core handle getters return the SAME handle, we
      // are on a unicore build (or an IDF that maps both cores to one
      // idle task). Assign that single counter to both cores. This is
      // safe because the handles being equal means it genuinely IS the
      // one idle task; it is not a name-based guess.
      if ((!foundIdle0 || !foundIdle1) && idle0 != nullptr && idle0 == idle1) {
        for (UBaseType_t i = 0; i < nTasks; i++) {
          if (taskSnap[i].xHandle == idle0) {
            currIdle0 = currIdle1 = taskSnap[i].ulRunTimeCounter;
            foundIdle0 = foundIdle1 = true;
            break;
          }
        }
      }

      if (!foundIdle0 || !foundIdle1) {
        static uint32_t lastIdleWarnMs = 0;
        if (millis() - lastIdleWarnMs > 30000) {
          lastIdleWarnMs = millis();
          LOGW("CPU stats: idle task lookup failed "
               "(idle0=%d idle1=%d nTasks=%u)",
               (int)foundIdle0, (int)foundIdle1, (unsigned)nTasks);
          for (UBaseType_t i = 0; i < nTasks; i++) {
            LOGW("  task[%u] name='%s' handle=%p runtime=%u",
                 (unsigned)i,
                 taskSnap[i].pcTaskName ? taskSnap[i].pcTaskName : "?",
                 (void*)taskSnap[i].xHandle,
                 (unsigned)taskSnap[i].ulRunTimeCounter);
          }
        }
      }

      if (!foundIdle0 || !foundIdle1) {
        // [cpu-idle-task-truncated-hold-baseline]
        // Snapshot truncated the array before reaching IDLE0/IDLE1
        // (task count > sizeof(taskSnap)/sizeof(taskSnap[0])). When
        // this happens, currIdle0 and currIdle1 are both 0 (their
        // initialisers). The old code fell straight through to the
        // `if (haveBase)` block below, computed
        //     dIdle0 = 0 - prevIdle0      // huge unsigned value
        //     dIdle0 <= dTotal            // false
        // and then entered the final `else` which set prevIdle0 = 0.
        // That POISONED the baseline: the next poll that did find
        // IDLE0 computed
        //     dIdle0 = currIdle0 - 0      // absolute, not delta
        //     dIdle0 <= dTotal            // false again
        // and the pills showed "n/a" until either a real long-gap
        // branch reset the baseline or the counter wrapped 71 min
        // later. The 2026-10-30 server log shows exactly this:
        // c0/c1 = -1 alternating with real values for ~50 s at a
        // time, recovering, then flipping again.
        //
        // Correct behaviour: hold the last-known percentage and
        // leave prevIdle* / prevTotal untouched. The next poll that
        // DOES find both idle tasks computes a correct delta against
        // the still-valid previous baseline. The warning block above
        // still fires at 1 Hz with the truncated snapshot, so the
        // operator sees the truncation in the serial log.
        //
        // Node-side equivalent that already works this way:
        // EVDPWeb.h's [cpu-stat-array-truncation] guard.
      } else if (haveBase) {
        uint32_t dIdle0 = currIdle0 - prevIdle0;
        uint32_t dIdle1 = currIdle1 - prevIdle1;
        uint32_t dTotal = totalRuntime - prevTotal;
        // [cpu-scaling-100]
        // pulTotalRunTime on ESP-IDF is esp_timer wall clock
        // (portGET_RUN_TIME_COUNTER_VALUE() == esp_timer_get_time_u32()),
        // NOT the sum of task runtimes. dIdle0/dTotal is therefore
        // already the core-0 idle fraction on a 0..1 scale. Multiply by
        // 100, not 200. The old 200 hid everything under 50 % load as
        // "0 %" and under-reported the rest by a factor of two.
        if (dTotal >= 100000u && dTotal < 0x80000000u &&
            dIdle0 <= dTotal && dIdle1 <= dTotal) {
          uint32_t idle0Pct = (uint32_t)((uint64_t)dIdle0 * 100ULL / dTotal);
          uint32_t idle1Pct = (uint32_t)((uint64_t)dIdle1 * 100ULL / dTotal);
          if (idle0Pct > 100) idle0Pct = 100;
          if (idle1Pct > 100) idle1Pct = 100;
          cpu0Pct = (int8_t)(100 - idle0Pct);
          cpu1Pct = (int8_t)(100 - idle1Pct);

          static uint32_t lastCpuDiagMs = 0;
          if (cpu0Pct <= 1 && cpu1Pct > 10 &&
              (millis() - lastCpuDiagMs) > 30000UL) {
            lastCpuDiagMs = millis();
            LOGW("CPU diag: c0=0 c1=%d | dIdle0=%u dIdle1=%u dTotal=%u "
                 "(dIdle0/dTotal=%.3f, expect 0.10-1.00) | "
                 "prevIdle0=%u prevIdle1=%u prevTotal=%u nTasks=%u",
                 (int)cpu1Pct,
                 (unsigned)dIdle0, (unsigned)dIdle1, (unsigned)dTotal,
                 (double)dIdle0 / (double)dTotal,
                 (unsigned)prevIdle0, (unsigned)prevIdle1,
                 (unsigned)prevTotal, (unsigned)nTasks);
          }
          prevIdle0 = currIdle0;
          prevIdle1 = currIdle1;
          prevTotal = totalRuntime;
        } else if (dTotal < 100000u) {
          // [cpu-short-window-no-baseline-burn]
          // Rapid double-poll (drag a zone slider then a mute click,
          // tab switch during the 4 s cycle). Do NOT advance the
          // baseline: the next legitimate 4 s poll needs the full
          // pre-double-poll window to compute a real percentage. The
          // pills keep their previous value for one tick.
        } else {
          // Genuine long-gap / wrap. Advance unconditionally so a
          // background-tab throttle does not freeze the pills forever.
          prevIdle0 = currIdle0;
          prevIdle1 = currIdle1;
          prevTotal = totalRuntime;
        }
      } else {
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

  // [heap-fragmentation-exposed]
  // A green RAM % pill is not sufficient: heapFree can be 60 % while the
  // largest single contiguous block is 3 KB, at which point the next
  // 4 KB OTA-job task stack or 1.5 KB ArduinoJson body fails. Surface
  // the largest free block so it is visible before it takes an update
  // down.
  uint32_t heapMaxAlloc = (uint32_t)ESP.getMaxAllocHeap();

  // [single-string-response] - see file header.
  String body;
  body.reserve(4096);

  body += "{\"ok\":true,";
  body += "\"ip\":\"";
  body += activeIpAddress().toString();
  body += "\",\"serverName\":\"";
  body += jsonEscape(serverName);
  body += "\",\"masterMuted\":";
  body += masterMuted ? "true" : "false";
  body += ",\"quietMode\":";
  body += quietModeEnabled ? "true" : "false";
  body += ",\"cpuCore0Pct\":";
  body += String((int)cpu0Pct);
  body += ",\"cpuCore1Pct\":";
  body += String((int)cpu1Pct);
  body += ",\"heapUsedPct\":";
  body += String((unsigned)heapPct);
  body += ",\"heapFreeBytes\":";
  body += String(heapFree);
  body += ",\"heapTotalBytes\":";
  body += String(heapTotal);
  body += ",\"heapMaxAlloc\":";
  body += String(heapMaxAlloc);
  body += ",\"psramUsedPct\":";
  body += String((unsigned)psramPct);
  body += ",\"psramFreeBytes\":";
  body += String(psramFree);
  body += ",\"psramTotalBytes\":";
  body += String(psramTotal);
  body += ",\"masterVolume\":";
  body += String((int)masterVolume);
  // [pot-volume-exposed] potVolume is applied jointly with masterVolume
  // in audioProcessingTask() (effectiveVolume = master*pot/100). If the
  // physical pot on GPIO7 is disconnected, floats at mid-rail, or has a
  // linear taper instead of the assumed log/square curve, potVolume can
  // sit at 20-40 for a user who believes the pot is at 100%. Until this
  // is visible in /api/state, "sounds quiet despite master=100" is
  // undebuggable from the dashboard alone.
  body += ",\"potVolume\":";
  body += String((int)potVolume);
  body += ",\"latencyAdjustmentMs\":";
  body += String((int)latencyAdjustmentMs);
  body += ",\"audioBufferMs\":";
  body += String((int)audioBufferMs);
  // [post-resample-rate-naming]
  // `sourceSampleRate` was always the PIPELINE rate after the resampler,
  // not the source's nominal rate. The field name lied: a 44.1 kHz BT
  // source whose resampler is doing its job reports ~48000 here, not
  // 44100. Add the correct name, keep the old one as a deprecated
  // alias for existing scrapers and for the node firmware (which reads
  // `sourceSampleRate` in applyEncoderInfoFromJson() to detect >500 Hz
  // jumps). The old key is emitted for exactly one release cycle; a
  // future cleanup can delete it after the node is bumped.
  body += ",\"postResampleRate\":";
  body += String((unsigned)g_sourceSampleRate);
  body += ",\"sourceSampleRate\":";          // DEPRECATED — same value
  body += String((unsigned)g_sourceSampleRate);
  // [source-nominal-rate] The source's NOMINAL rate. BT A2DP is 44.1 or
  // 48 kHz; I2S input is fixed at SOURCE_SAMPLE_RATE. This is what a
  // dashboard should show as "Source Rate", not the post-resample
  // value. Populated from the last ctrl/register packet's own claim
  // (sourceBoard.fw + activeSource) — not synthesised here, because
  // the server has no independent measurement.
  body += ",\"sourceNominalRate\":";
  body += (serverInputSource == SRC_FROM_USB || serverInputSource == SRC_FROM_I2S)
            ? String((unsigned)SOURCE_SAMPLE_RATE)   // I2S path is 48 kHz nominal
            : String((unsigned)44100);               // NETWORK path = DLNA, 44.1 kHz typical
  // [opus-sample-rate] Actual rate the encoder is CURRENTLY created at.
  // Diagnostics only — the dashboard form reads the pending/effective
  // value from /api/audio/encoding.
  body += ",\"opusSampleRate\":";
  body += String((unsigned)g_opusSampleRate);
  body += ",\"mode\":\"";
  body += jsonEscape(String(playbackModeCStr()));
  body += "\",\"audioProtocolVersion\":";
  body += String((int)AUDIO_PROTO_VERSION);
  body += ",\"serverEpoch\":";
  body += String((unsigned long)serverEpoch);
  // [srcstat-string-snapshot] Single locked read — see handleApiStats().
  String snapSrc;
  if (lockFor(uartMutex, 20)) { snapSrc = receiverSource; unlockFor(uartMutex); }
  body += ",\"receiverAudio\":{\"source\":\"";
  body += jsonEscape(snapSrc);
  body += "\"},\"players\":[";

  for (size_t i = 0; i < players.size(); ++i) {
    if (i > 0) body += ",";
    body += "{\"name\":\"";
    body += jsonEscape(players[i].name);
    body += "\",\"ip\":\"";
    body += jsonEscape(players[i].ip);
    body += "\",\"version\":\"";
    body += jsonEscape(players[i].version);
    body += "\",\"assigned\":";
    bool pAssigned = false;
    for (auto &z : zcopy) {
      for (auto &zip : z.nodeIps) {
        if (zip == players[i].ip) { pAssigned = true; break; }
      }
      if (pAssigned) break;
    }
    body += pAssigned ? "true" : "false";
    body += ",\"declaredAudioProtocolVersion\":";
    body += String((int)players[i].declaredAudioProtocolVersion);
    body += ",\"negotiatedAudioProtocolVersion\":";
    body += String((int)players[i].negotiatedAudioProtocolVersion);
    body += ",\"audioCompatible\":";
    body += players[i].audioCompatible ? "true" : "false";
    body += ",\"audioCompatReason\":\"";
    body += jsonEscape(players[i].audioCompatReason);
    body += "\"}";
  }

  body += "],\"zones\":[";
  for (size_t i = 0; i < zcopy.size(); ++i) {
    if (i > 0) body += ",";
    appendZoneJson(body, zcopy[i], players);
  }
  body += "]}";

  AsyncWebServerResponse *resp =
      request->beginResponse(200, "application/json", body);
  resp->addHeader("Cache-Control", "no-store, max-age=0");
  request->send(resp);
}

inline void handleApiStats(AsyncWebServerRequest *request) {
  // [ota-heap-safety] Skip heavy JSON generation during OTA.
  if (otaUploadInProgress || otaInProgress) {
    request->send(200, "application/json", "{\"ok\":true,\"ota\":true}");
    return;
  }

  uint32_t latSum, latCnt;
  portENTER_CRITICAL(&gStatsMux);
    latSum = statLatencySumMs;
    latCnt = statLatencyCount;
  portEXIT_CRITICAL(&gStatsMux);
  float avgLatency = latCnt > 0 ? (float)latSum / (float)latCnt : 0.0f;
  std::vector<NodePerf> perf = snapshotPerf();

  // [per-task-cpu-breakdown]
  // The dashboard only sees CPU0/CPU1 aggregate. When a task is eating
  // 30 % of a core, that number is useless for diagnosis — is it encode?
  // udpSend? loop()'s ADC polling? uxTaskGetSystemState() gives us every
  // task's ulRunTimeCounter. Computing the fraction of the interval since
  // the previous call gives an accurate %-of-core for each task.
  //
  // Sampling is on-demand: /api/stats is polled once every 4 s by the
  // dashboard. Two calls establish the interval. The first call after
  // boot returns zeros; that is intentional — no baseline exists yet.
  //
  // Not accounting for ISR time: ulRunTimeCounter is task-execution
  // cycles only. The sum of task-%s will be less than 100 %; the
  // residual is ISR + context-switch overhead. That residual is itself
  // useful (a WiFi-storm on Core 0 shows up here on Core 1 as the CPU
  // servicing radio ISRs even when no Core-1 task is busy).
  static uint32_t s_prevTaskCount = 0;
  static TaskStatus_t s_prevTaskSnap[32];
  static uint32_t s_prevTaskTotal = 0;
  char taskBreakdownJson[1024];
  taskBreakdownJson[0] = '\0';
  {
    TaskStatus_t snap[32];
    uint32_t totalRun = 0;
    UBaseType_t n = uxTaskGetSystemState(snap, 32, &totalRun);
    if (n > 32) n = 32;
    if (s_prevTaskTotal > 0 && totalRun > s_prevTaskTotal) {
      uint32_t dTotal = totalRun - s_prevTaskTotal;
      if (dTotal > 100000u) {   // >= 100 ms window; ignore very short windows
        // Match each current task to its previous snapshot entry by handle.
        size_t pos = 0;
        pos += snprintf(taskBreakdownJson + pos,
                        sizeof(taskBreakdownJson) - pos, "\"tasks\":{");
        bool first = true;
        for (UBaseType_t i = 0; i < n && pos < sizeof(taskBreakdownJson) - 64; i++) {
          uint32_t prev = 0;
          for (uint32_t k = 0; k < s_prevTaskCount; k++) {
            if (s_prevTaskSnap[k].xHandle == snap[i].xHandle) {
              prev = s_prevTaskSnap[k].ulRunTimeCounter;
              break;
            }
          }
          uint32_t delta = snap[i].ulRunTimeCounter - prev;
          // Report only tasks worth seeing; suppress sub-0.1 % noise.
          uint32_t pctX100 = (uint32_t)((uint64_t)delta * 10000ULL / dTotal);
          if (pctX100 < 10 && strcmp(snap[i].pcTaskName, "IDLE0") != 0 &&
              strcmp(snap[i].pcTaskName, "IDLE1") != 0) continue;
          // Skip IDLE — it's the inverse of total load and the dashboard
          // already exposes cpuCore0Pct/cpuCore1Pct.
          if (strncmp(snap[i].pcTaskName, "IDLE", 4) == 0) continue;
          if (!first) pos += snprintf(taskBreakdownJson + pos,
                                      sizeof(taskBreakdownJson) - pos, ",");
          first = false;
          pos += snprintf(taskBreakdownJson + pos, sizeof(taskBreakdownJson) - pos,
                          "\"%s\":%u.%02u",
                          snap[i].pcTaskName ? snap[i].pcTaskName : "?",
                          (unsigned)(pctX100 / 100), (unsigned)(pctX100 % 100));
        }
        snprintf(taskBreakdownJson + pos,
                 sizeof(taskBreakdownJson) - pos, "}");
      }
    }
    // Save this snapshot for the next call's delta computation.
    s_prevTaskCount = n;
    for (UBaseType_t i = 0; i < n; i++) s_prevTaskSnap[i] = snap[i];
    s_prevTaskTotal = totalRun;
  }

  uint32_t rawQD  = rawQueue  ? (uint32_t)uxQueueMessagesWaiting(rawQueue)  : 0;
  uint32_t opusQD = opusQueue ? (uint32_t)uxQueueMessagesWaiting(opusQueue) : 0;

  // [single-string-response] - see file header.
  String body;
  body.reserve(3072);

  body += "{\"ok\":true,";
  body += "\"ip\":\"";
  body += activeIpAddress().toString();
  body += "\",\"wifiConnected\":";
  body += (WiFi.status() == WL_CONNECTED) ? "true" : "false";
  body += ",\"wifiRssi\":";
  body += String((int)WiFi.RSSI());
  body += ",\"wifiSsid\":\"";
  body += jsonEscape(wifiStaSsid);
  body += "\",\"wifiDisconnects\":";
  body += String((unsigned)statWifiDisconnects);
  body += ",\"wsClients\":";
  body += String((int)subscriberCount);
  body += ",\"mode\":\"";
  body += jsonEscape(String(playbackModeCStr()));
  body += "\",\"audioBufferMs\":";
  body += String((unsigned)audioBufferMs);
  // [post-resample-rate-naming] Same deprecation as handleApiState().
  // Correct name first, old name kept for one release cycle as an alias.
  body += ",\"postResampleRate\":";
  body += String((unsigned)g_sourceSampleRate);
  body += ",\"sourceSampleRate\":";          // DEPRECATED — same value
  body += String((unsigned)g_sourceSampleRate);
  body += ",\"sourceNominalRate\":";
  body += (serverInputSource == SRC_FROM_USB || serverInputSource == SRC_FROM_I2S)
            ? String((unsigned)SOURCE_SAMPLE_RATE)
            : String((unsigned)44100);
  // [opus-sample-rate] Current encoder rate + frame size in samples.
  body += ",\"opusSampleRate\":";
  body += String((unsigned)g_opusSampleRate);
  body += ",\"opusFrameSamples\":";
  body += String((unsigned)g_opusFrameSamples);
  body += ",\"latencyAdjustmentMs\":";
  body += String((unsigned)latencyAdjustmentMs);
  body += ",\"masterVolume\":";
  body += String((unsigned)masterVolume);
  body += ",\"potVolume\":";
  body += String((unsigned)potVolume);
  body += ",\"masterMuted\":";
  body += masterMuted ? "true" : "false";
  body += ",\"i2sReadErrors\":";
  body += String((unsigned)statI2sReadErrors);
  // [ws-backpressure-removed]
  // `wsBackpressureDrops` was never a WebSocket metric. The server has
  // no WebSocket — AsyncWebServer only. The field was a stale legacy
  // name that aliased statUdpErrors, so any dashboard or operator that
  // watched "wsBackpressureDrops: 229" was reading UDP send failures
  // mislabelled as a WebSocket queue drop. Removed entirely. No
  // dashboard consumer reads it (grep EVDCTRL.h for the key: zero
  // hits). External scrapers that read it will now see the key absent,
  // which is strictly more honest than the previous behaviour.
  body += ",\"udpErrors\":";
  body += String((unsigned)statUdpErrors);
  // A genuinely distinct WS counter, if a WebSocket ingress is ever
  // added, must be its own stat and must increment on a real socket
  // backpressure event — not on the same branch as statUdpErrors.
  body += ",\"avgEncodeUs\":";
  body += String((unsigned)gAvgEncodeUs);
  // [stutter-visible-counters] The three fields that prove whether
  // "green but stuttering" is encoder-preemption or rate mismatch.
  // Watch these alongside the node's capturedMsGapGt40Total.
  body += ",\"encodeOverrunsLastSec\":";
  body += String((unsigned)statEncodeOverrunsLastSec);
  body += ",\"encodeOverrunsTotal\":";
  body += String((unsigned)statEncodeOverrunsTotal);
  body += ",\"encodePeakUsLastSec\":";
  body += String((unsigned)statEncodePeakUsLastSec);
  body += ",\"pendingBufDroppedLastSec\":";
  body += String((unsigned)statPendingBufDroppedLastSec);
  body += ",\"pendingBufDroppedTotal\":";
  body += String((unsigned)statPendingBufDroppedTotal);
  body += ",\"pendingBufPeakDepthLastSec\":";
  body += String((unsigned)statPendingBufPeakDepthLastSec);
  body += ",\"measuredInputHz\":";
  body += String((double)statMeasuredInputHz, 1);
  body += ",\"clockDriftPpm\":";
  body += String((long)statDriftPpm);
  body += ",\"audioProtocolVersion\":";
  body += String((int)AUDIO_PROTO_VERSION);
  body += ",\"serverEpoch\":";
  body += String((unsigned long)serverEpoch);
  body += ",\"clockSyncPort\":";
  body += String((int)CLOCK_SYNC_UDP_PORT);
  body += ",\"syncRequestsServed\":";
  body += String((unsigned)statSyncRequestsServed);
  body += ",\"syncRequestErrors\":";
  body += String((unsigned)statSyncRequestErrors);
  body += ",\"syncRequestsThrottled\":";
  body += String((unsigned)statSyncRequestsThrottled);
  body += ",\"audioIncompatibleSkips\":";
  body += String((unsigned)statAudioIncompatibleSkips);
  // [stat-audio-provisional-skips] See EVDState.h. A non-zero value here
  // on a healthy network means a node is mid-handshake (normal during
  // boot / reboot windows); a persistent value means the heartbeat
  // endpoint is being starved. audioIncompatibleSkips should stay 0 on
  // an all-compatible fleet.
  body += ",\"audioProvisionalSkips\":";
  body += String((unsigned)statAudioProvisionalSkips);
  body += ",\"audioSyncPending\":";
  body += String((unsigned)statAudioSyncPending);
  // [uart-line-drops] >0 means a receiver-UART line was too long for
  // RX_LINE_MAX and was discarded (see EVDPlayers.h). The only line that can
  // exceed it is BT_JSON, so a rising value here is the fingerprint of "the
  // dashboard's paired-device list stopped updating".
  body += ",\"uartLineDrops\":";
  body += String((unsigned)statUartLineDrops);
  // [sub-stall-cooldown] Frames skipped because a node was inside its
  // post-failure cooldown. This is the counter that used to be paid for in
  // stutter on the surviving nodes.
  body += ",\"audioCooldownSkips\":";
  body += String((unsigned)statAudioCooldownSkips);
  body += ",\"usbBytesLastWin\":";
  body += String((unsigned)statUsbBytesLast5s);
  // [server-input-source-honest] This used to report USB or, for everything
  // else, the literal "I2S" — so a DLNA stream claimed to be the I2S input.
  // Report the real producer; the dashboard prints this string verbatim.
  body += ",\"serverInputSource\":\"";
  body += (serverInputSource == SRC_FROM_USB)     ? "USB"     :
          (serverInputSource == SRC_FROM_NETWORK) ? "NETWORK" : "I2S";
  body += "\",\"streaming\":{";
  body += "\"i2sBytesLast5s\":";
  body += String((unsigned)statI2sBytesLast5s);
  body += ",\"rawDroppedLast5s\":";
  body += String((unsigned)statRawDroppedLast5s);
  body += ",\"opusDroppedLast5s\":";
  body += String((unsigned)statOpusDroppedLast5s);
  body += "},\"rawQueueDepth\":";
  body += String(rawQD);
  body += ",\"rawQueueLen\":";
  body += String((int)RAW_QUEUE_LEN);
  body += ",\"opusQueueDepth\":";
  body += String(opusQD);
  body += ",\"opusQueueLen\":";
  body += String((int)OPUS_QUEUE_LEN);
  body += ",\"opusEncodedLastWin\":";
  body += String((unsigned)statOpusEncodedLast5s);
  body += ",\"opusDroppedLastWin\":";
  body += String((unsigned)statOpusDroppedLast5s);
  body += ",\"wsSentLastWin\":";
  body += String((unsigned)statUdpPacketsLast5s);
  body += ",\"wsBytesLastWin\":";
  body += String((unsigned)statUdpBytesLast5s);
  body += ",\"avgLatencyMs\":";
  body += String((double)avgLatency, 2);
  // [per-task-cpu-breakdown] See the block above for why this is per-call.
  if (taskBreakdownJson[0] != '\0') {
    body += ",";
    body += taskBreakdownJson;
  }
  body += ",\"nodes\":[";

  for (size_t i = 0; i < perf.size(); ++i) {
    if (i > 0) body += ",";
    body += "{\"ip\":\"";
    body += jsonEscape(perf[i].ip);
    body += "\",\"latencyMs\":";
    body += String(perf[i].latencyMs);
    body += ",\"queueDepth\":";
    body += String(perf[i].queueDepth);
    body += ",\"target\":";
    body += String(perf[i].target);
    body += ",\"rebuffers\":";
    body += String((unsigned)perf[i].rebuffers);
    body += "}";
  }
  body += "],";

  // Aggregate rebuffers across all nodes so the dashboard's diagnostic
  // recorder has a single top-level value to plot.
  {
    uint32_t totalRebuf = 0;
    for (auto &p : perf) totalRebuf += p.rebuffers;
    body += "\"rebuffers\":";
    body += String(totalRebuf);
    body += ",";
  }

  // [srcstat-string-snapshot] Take ONE locked snapshot of the four String
  // globals that SRCSTAT parsing writes, then build the JSON from the
  // locals. Without the lock, a mid-rebuild String in receiverState /
  // receiverSource / currentSourceFwVersion / currentConnectedMacCache can
  // be observed as a dangling _buffer (String operator= is
  // free-old + alloc-new + swap-pointer).
  String snapFw, snapMac, snapSrc, snapState;
  if (lockFor(uartMutex, 50)) {
    snapFw    = currentSourceFwVersion;
    snapMac   = currentConnectedMacCache;
    snapSrc   = receiverSource;
    snapState = receiverState;
    unlockFor(uartMutex);
  }

  // Source-board health, mirrored from the last SRCSTAT line the source
  // sent over UART. Consumed by the dashboard's Status tab and by the
  // diagnostic recorder (see EVDCTRL.h's startRecording()).
  // [source-link-vs-bt] snapState is the SRCSTAT "connected" field, i.e.
  // BT pairing state, not UART link health. A UART link can be healthy
  // while BT is idle. Link = heartbeat freshness; btConnected = BT state.
  const unsigned long srcNow = millis();
  bool srcLinkFresh = (lastHeartbeatMsCache != 0) &&
                      ((srcNow - lastHeartbeatMsCache) < 30000UL);
  body += "\"sourceBoard\":{";
  body += "\"fw\":\"";
  body += jsonEscape(snapFw);
  body += "\",\"link\":";
  body += srcLinkFresh ? "true" : "false";
  body += ",\"btConnected\":";
  body += (snapState == "connected") ? "true" : "false";
  body += ",\"mac\":\"";
  body += jsonEscape(snapMac);
  body += "\",\"volume\":";
  body += String((unsigned)currentSourceBtVolume);
  body += ",\"avrc\":";
  body += avrcConnectedCache ? "true" : "false";
  body += ",\"activeSource\":\"";
  body += jsonEscape(snapSrc);
  body += "\",\"loopTicks\":";
  body += String((unsigned long)currentSourceLoopTicks);
  body += ",\"heapFree\":";
  body += String((unsigned long)currentSourceHeap);
  body += ",\"uptimeSec\":";
  body += String((unsigned long)currentSourceUptime);
  body += ",\"a2dpConnecting\":";
  body += String((unsigned long)currentSourceA2dpConnecting);
  body += ",\"a2dpConnected\":";
  body += String((unsigned long)diagA2dpConnected);
  body += ",\"a2dpDisconnecting\":";
  body += String((unsigned long)currentSourceA2dpDisconnecting);
  body += ",\"a2dpDisconnected\":";
  body += String((unsigned long)diagA2dpDisconnected);
  body += ",\"volumeEvents\":";
  body += String((unsigned long)diagVolumeEvents);
  body += ",\"volumeApplied\":";
  body += String((unsigned long)currentSourceVolumeApplied);
  body += ",\"uartBytes\":";
  body += String((unsigned long)currentSourceUartBytes);
  body += ",\"uartLines\":";
  body += String((unsigned long)diagUartRxLines);
  body += ",\"uartOverflow\":";
  body += String((unsigned long)currentSourceUartOverflow);
  body += ",\"uartTxDropped\":";
  body += String((unsigned long)currentSourceUartTxDropped);
  body += ",\"sourceChanges\":";
  body += String((unsigned long)currentSourceChanges);
  body += ",\"heartbeatAgeMs\":";
  body += String((unsigned long)(millis() - lastHeartbeatMsCache));
  body += "},";

  // [udp-send-liveness] Exposed so a stall is diagnosable at a glance:
  //   udpSendLoopCount static  + opusDroppedLastWin > 0  ->  task stuck
  //   udpSendLoopCount rising  + wsSentLastWin   == 0    ->  blocked in send
  //   udpSendLoopCount rising  + wsSentLastWin   > 0     ->  pipeline healthy
  body += "\"udpSendLoopCount\":";
  body += String((unsigned)g_udpSendLoopCount);
  body += ",\"udpSendStallResets\":";
  body += String((unsigned)g_udpSendStallResets);
  // [udp-stall-split] See EVDState.h. The two active counters alias stale
  // backlog drops; the ahead counter remains zero for API compatibility.
  body += ",\"udpPacerAheadReset\":";
  body += String((unsigned)g_udpPacerAheadReset);
  body += ",\"udpPacerBehindReset\":";
  body += String((unsigned)g_udpPacerBehindReset);
  body += ",\"udpPacerDriftReset\":";
  body += String((unsigned)g_udpPacerDriftReset);
  body += ",\"udpStageAfterRecv\":";
  body += String((unsigned)g_udpStage_afterRecv);
  body += ",\"udpStageAfterStore\":";
  body += String((unsigned)g_udpStage_afterStore);

  // [ota-pause-watchdog]  Age of the most recent OTA progress event.
  // 0xFFFFFFFF means "never"; else ms since last chunk or job start.
  {
    uint32_t otaAge = g_otaLastProgressMs
                      ? (uint32_t)(millis() - g_otaLastProgressMs)
                      : 0xFFFFFFFFu;
    body += ",\"otaLastProgressAgeMs\":";
    body += String(otaAge);
  }

  body += ",\"receiverAudio\":{\"state\":\"";
  body += jsonEscape(snapState);
  body += "\"}}";

  AsyncWebServerResponse *resp =
      request->beginResponse(200, "application/json", body);
  resp->addHeader("Cache-Control", "no-store, max-age=0");
  request->send(resp);
}