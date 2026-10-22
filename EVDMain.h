#pragma once
// ============================================================================
//  EVDMain.h - setup(), loop(), factory-reset pin handling
//
//  NOTE: setup() and loop() MUST NOT be 'inline'. The Arduino core's main.cpp
//  references them as external symbols, and the compiler does not emit an
//  out-of-line definition for an inline function that isn't called from its
//  own translation unit. Everything else in this file can stay inline.
// ============================================================================
#include "EVDRoutes.h"
#include "EVDUsb.h"
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <esp_system.h>   // esp_random() for serverEpoch
#include <nvs_flash.h>    // NEW: full NVS wipe for factory reset
#include <nvs.h>

unsigned long lastStatsMs = 0;

inline void setup_serial() {
  Serial.begin(LOG_BAUD);
  delay(300);
  LOGI("=========================================================");
  LOGI("EnkelvoudServer ESP32-S3 I2S->Opus->UDP-Multicast booting");
  LOGI("=========================================================");

  // [boot-loop-diagnostics]
  // Log the reset reason at the very top of every boot so a boot loop
  // shows *why* the previous run ended. Without this the loop is
  // indistinguishable from "AP just keeps cycling" from the outside, and
  // every candidate fix is a guess. The mapping covers every reason the
  // ESP-IDF can produce; the interesting ones for a boot loop are PANIC
  // (a crash with a saved backtrace), TASK_WDT / INT_WDT (a task that
  // stopped yielding), BROWNOUT (power supply cannot sustain AP+audio),
  // and SW (something called esp_restart() deliberately).
  esp_reset_reason_t reason = esp_reset_reason();
  const char *why = "UNKNOWN";
  switch (reason) {
    case ESP_RST_POWERON:   why = "POWERON (cold boot)";        break;
    case ESP_RST_SW:        why = "SOFTWARE (esp_restart)";     break;
    case ESP_RST_PANIC:     why = "PANIC (exception)";          break;
    case ESP_RST_INT_WDT:   why = "INT_WDT (interrupt watchdog)"; break;
    case ESP_RST_TASK_WDT:  why = "TASK_WDT (task watchdog)";   break;
    case ESP_RST_WDT:       why = "WDT (other watchdog)";       break;
    case ESP_RST_DEEPSLEEP: why = "DEEPSLEEP wake";             break;
    case ESP_RST_BROWNOUT:  why = "BROWNOUT (power supply)";    break;
    case ESP_RST_SDIO:      why = "SDIO";                       break;
    default: break;
  }
  LOGI("Reset reason: %s", why);
  LOGI("Free heap at boot: %u bytes", (unsigned)ESP.getFreeHeap());
}

// -----------------------------------------------------------------------------
//  Factory reset - wipes the entire NVS partition (every namespace:
//  "enkelvoud", zones, WiFi credentials, the LittleFS format flag, etc.),
//  then reboots. On the next boot the sketch re-creates defaults, and
//  LittleFS is reformatted on first boot because "fs_formatted" is gone.
//
//  This is a true factory reset: nothing the user ever configured survives.
// -----------------------------------------------------------------------------
inline void factoryResetEverything() {
  LOGW("=================================================");
  LOGW(" FACTORY RESET: wiping all persistent storage");
  LOGW("=================================================");
  Serial.flush();

  // 1) Shut the radios down FIRST. The Wi-Fi driver owns an internal NVS
  //    handle; erasing the partition under it causes aborts mid-erase,
  //    which leaves NVS half-written and the next boot reads stale keys.
  WiFi.disconnect(true, true);   // wifi_off = true, erase_ap = true
  WiFi.mode(WIFI_OFF);
  delay(100);

  // 2) Close our own Preferences handle if it happened to be open.
  preferences.end();
  delay(50);

  // 3) Wipe the whole NVS partition: every namespace, every key.
  esp_err_t err = nvs_flash_erase();
  if (err != ESP_OK) LOGE("nvs_flash_erase() failed: %d", (int)err);
  else                LOGW("NVS erased (all namespaces)");

  // 4) DO NOT call nvs_flash_init() here. esp_restart() reboots the chip;
  //    the boot ROM/IDF will format and mount NVS cleanly on the way up.
  //    Re-initialising while the Wi-Fi task is still winding down is what
  //    caused the abort.
  LOGW("Rebooting into first-boot state (AP mode, defaults)");
  Serial.flush();
  delay(300);
  stopServerUsbAudio();
  delay(100);
  esp_restart();
}

// Non-blocking poll. Call this from loop(). Must not be starved.
inline void handleFactoryResetPin() {
static bool     tracking      = false;
static uint32_t pinLowSinceMs = 0;
static uint32_t pinHighSince  = 0;

if (digitalRead(PIN_FACTORY_RESET) != LOW) {
  if (tracking) {
    if (pinHighSince == 0) pinHighSince = millis();
    if (millis() - pinHighSince > 50) {
      LOGI("Factory reset button released; timer cleared");
      tracking = false;
      pinHighSince = 0;
    }
  }
  return;
}
pinHighSince = 0;   // pin is LOW; cancel any in-progress release timer

if (!tracking) {
  tracking = true;
  pinLowSinceMs = millis();
  LOGI("Factory reset button pressed; hold for %d ms to wipe", FACTORY_RESET_HOLD_MS);
  return;
}
if (millis() - pinLowSinceMs < FACTORY_RESET_HOLD_MS) return;

  LOGW("Factory reset button held %d ms - triggering wipe", FACTORY_RESET_HOLD_MS);
  factoryResetEverything();   // does not return
}

// ---- Pot taper -------------------------------------------------------------
// ---- Pot taper -------------------------------------------------------------
// Pure square-law (x^2) curve. Anchors:
//    0% ->   0    25% ->   6     50% ->  25
//   75% ->  56   100% -> 100
// Below 25% is whisper-quiet, 50% is a usable medium level, and 75%+ is
// where the "true" headroom of the master gain lives. Do not change this
// to a piecewise-linear approximation: the smooth x^2 derivative is what
// makes small rotations near the low end feel proportional instead of
// either dead or suddenly loud.
inline uint8_t potCurveToVolume(int potPct) {
  if (potPct < 0)   potPct = 0;
  if (potPct > 100) potPct = 100;
  const float x = (float)potPct / 100.0f;
  float y = x * x * 100.0f;
  if (y < 0.0f)   y = 0.0f;
  if (y > 100.0f) y = 100.0f;
  return (uint8_t)(y + 0.5f);
}

// ---- GPIO7 volume pot ------------------------------------------------------
// Read-only ADC on GPIO7 acts as a second, hardware-side master volume.
// Multiplied with the dashboard slider in audioProcessingTask (EVDAudio.h)
// so both controls are always active; the slider value (masterVolume) is
// unchanged. potVolume is a separate, non-persisted gain.
//
// Renamed from serviceTestPot: the pot is the primary hardware-side volume
// control in this build, not an experimental hook.
inline void serviceVolumePot() {
  static uint32_t lastReadMs   = 0;
  static float    smoothedPct  = -1.0f;
  static int      lastPrintPct = -1;

  uint32_t now = millis();
  if (now - lastReadMs < 100) return;
  lastReadMs = now;

  const int N = 16;
  uint32_t acc = 0;
  for (int i = 0; i < N; ++i) acc += analogReadMilliVolts(pinVolumePot);
  float mv  = (float)acc / (float)N;
  float pct = (mv * 100.0f) / 3300.0f;
  if (pct < 0.0f)   pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;

  // Asymmetric EMA: fast on real motion, slow on noise.
  const float NOISE_BAND = 4.0f;
  const float ALPHA_FAST = 0.60f;
  const float ALPHA_SLOW = 0.05f;
  if (smoothedPct < 0.0f) smoothedPct = pct;
  else {
    float delta = pct - smoothedPct;
    float a = (fabsf(delta) > NOISE_BAND) ? ALPHA_FAST : ALPHA_SLOW;
    smoothedPct += a * delta;
  }

  int     potPct = (int)(smoothedPct + 0.5f);
  uint8_t newVol = potCurveToVolume(potPct);
  potVolume = newVol;      // single aligned byte store, atomic on ESP32

  if (lastPrintPct >= 0 && abs(potPct - lastPrintPct) < 2) return;
  lastPrintPct = potPct;
  // [v1.1.1-pot-log-gate] Route through LOGI so the line respects
  // gSerialLogLevel (errors-only in steady state) and also lands in the
  // dashboard's advanced log viewer.  Bare Serial.printf here bypassed
  // the level gate, so a noisy pot emitted a continuous stream even at
  // level 1.
  LOGI("[POT] GPIO7: %d%% -> vol %u%% (raw %.0f mV)",
       potPct, (unsigned)newVol, mv);
}

void setup() {
  setup_serial();

  // [v1.1.1-prefs-mutex-ordering]
  // MUST run before ANY code path that constructs a PrefsGuard. The
  // LittleFS format-on-first-boot block below is the earliest such path:
  // it reads/writes the "fs_formatted" NVS key, and PrefsGuard's
  // constructor is a no-op (locked = false) while prefsMutex is null.
  // With the guard failing, fsFormatted was always false and pg2.ok()
  // was always false, so the sketch reformatted LittleFS on every boot
  // and the "fs_formatted" marker was never persisted - which in turn
  // wiped the OTA spool on every power cycle.
  createSyncPrimitives();

  // Random per-boot session identifier. Sent to players so they can detect
  // a server reboot/timestamp reset and discard any cached clock offset or
  // audio sequence-number baseline. See EVDConfig.h clock-sync docs.
  serverEpoch = esp_random();
  LOGI("Server epoch: %lu  | audio protocol v%d",
       (unsigned long)serverEpoch, AUDIO_PROTO_VERSION);

  // ---------------------------------------------------------------------
  //  One-shot LittleFS format-on-first-boot.
  //
  //  The custom partition table adds a 7.9 MB "spiffs" partition, but the
  //  partition table upload only writes the table itself; the filesystem
  //  area is left uninitialised. LittleFS refuses to mount an unformatted
  //  region on purpose (no silent data loss), so we format exactly once
  //  and record a marker in NVS so future boots never do this again.
  //
  //  After this runs successfully on the first boot, ensureOtaFs() in
  //  EVDOTA.h will find a valid filesystem and mount it in normal mode.
  // ---------------------------------------------------------------------
  {
    PrefsGuard pg;                        // [v1.1.1-prefs-mutex]
    bool fsFormatted = false;
    if (pg.ok()) {
      preferences.begin("enkelvoud", true);
      fsFormatted = preferences.getBool("fs_formatted", false);
      preferences.end();
    } else {
      // Should never happen now that createSyncPrimitives() runs first,
      // but fail loud rather than silently reformatting the filesystem.
      LOGW("setup: prefsMutex unavailable for fs_formatted read; "
           "assuming formatted to avoid a spurious wipe");
      fsFormatted = true;
    }

    if (!fsFormatted) {
      LOGI("LittleFS: first-boot format (this wipes any prior filesystem)");
      if (LittleFS.begin(true)) {          // true = format on fail
        LittleFS.end();
        PrefsGuard pg2;                   // [v1.1.1-prefs-mutex]
        if (pg2.ok()) {
          preferences.begin("enkelvoud", false);
          preferences.putBool("fs_formatted", true);
          preferences.end();
        } else {
          LOGW("setup: prefsMutex unavailable for fs_formatted write; "
               "next boot will re-check the filesystem");
        }
        LOGI("LittleFS: formatted and marked ready");
      } else {
        LOGW("LittleFS: format failed; OTA spool will stay unavailable");
      }
    }
  }

  pinMode(PIN_FACTORY_RESET, INPUT_PULLUP);
  pinMode(pinVolumePot, INPUT);
  loadWiFiSettings();
  loadZonesFromNVS();

  if (audioBufferMs < 20) audioBufferMs = 60;

  // NOTE: GPIO0 is the ESP32-S3 BOOT strapping pin. If it is LOW at reset,
  // the ROM bootloader enters UART download mode and this sketch never
  // runs, so a "hold GPIO0 at boot to factory reset" check here can never
  // fire. It is also actively harmful: after a factory wipe, GPIO0 LOW at
  // the next boot would leave the chip stuck in download mode.
  //
  // The runtime factory reset is handled by handleFactoryResetPin() in
  // loop(). Do NOT try to detect it here.
  setup_wifi_sta();

  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin(serverName.c_str())) {
      MDNS.addService("http",     "tcp", 80);
      MDNS.addService("enkelsrv", "tcp", 80);
      LOGI("mDNS available at http://%s.local/", serverName.c_str());
    } else {
      LOGW("mDNS startup failed");
    }
  }

  setup_opus();
  applyOpusProfileForMode(playbackMode, true);   // NEW: set initial complexity for boot mode
  configureQuietFilters();
  setupDiscoveryResponder();
  setup_http_server();
  setup_udp_transport();
  // Clock sync lives on its own task so loop()'s NVS writes and UART
  // parsing can never delay a sync_resp. See clockSyncTask() in EVDUDP.h.
  xTaskCreatePinnedToCore(clockSyncTask, "ClockSync", 3072, NULL, 4, NULL, 1);
  setupReceiverLink();
  // Restore the persisted boot-source default instead of the last runtime
  // selection, so a dashboard source change during the previous session does
  // not leak into the next boot until the user explicitly re-picks it.
  lastReceiverSource = defaultBootSource;
  receiverSource     = defaultBootSource;
  if (defaultBootSource == "USB") {
    // USB path is server-local; try to bring the UAC device up.
    if (startServerUsbAudio()) {
      serverInputSource = SRC_FROM_USB;
      LOGI("Boot source: USB (server-side UAC device active)");
    } else {
      LOGW("Boot source USB requested but UAC start failed; falling back to I2S");
      serverInputSource = SRC_FROM_I2S;
      sendReceiverSourceCommand("BT");
      lastReceiverSource = "BT";
      receiverSource     = "BT";
    }
  } else {
    sendReceiverSourceCommand(defaultBootSource);
  }
  setup_i2s_in();

    // Give the adaptive resampler a head start. On a fresh boot the first
    // few windows are pure noise; seeding from a one-shot 2-second average
    // gets the ratio within 0.1 % almost immediately, so the node never
    // sees the convergence transient.
    //
    // IMPORTANT: this must run BEFORE setup_queues_and_tasks(). Otherwise
    // audioReadTask is already pulling from the same I2S DMA ring, this
    // loop sees roughly half the frames in the window, and the computed
    // seedRate lands near 22 kHz - outside the sanity guard below, so the
    // seed is rejected and the first two seconds of audio are needlessly
    // split between this loop and the real audio pipeline.
    {
        uint32_t t0     = millis();
        uint64_t bytes  = 0;
        while (millis() - t0 < 2000) {
            size_t got = 0;
            uint8_t scratch[1024];
            if (i2s_read(I2S_PORT_IN, scratch, sizeof(scratch), &got, 50) == ESP_OK && got > 0) {
                bytes += got;
            }
            // No delay(1) here: i2s_read() with a 50 ms timeout already
            // blocks until data is available or the timeout fires. Adding
            // a 1 ms sleep on top just wastes a slot every 1 ms without
            // changing the measured rate.
        }
        uint32_t elapsedMs = millis() - t0;
        uint32_t frames    = (uint32_t)(bytes / (2 * sizeof(int16_t)));
        double   seedRate  = (elapsedMs > 0)
                             ? (double)frames * 1000.0 / (double)elapsedMs
                             : 0.0;
        if (seedRate > 30000.0 && seedRate < 52000.0) {
            resampler.setRatio(seedRate / (double)OPUS_SAMPLE_RATE);
            LOGI("Resampler seeded at %.1f Hz (%u frames in %u ms)",
                 seedRate, (unsigned)frames, (unsigned)elapsedMs);
        } else {
            LOGW("Resampler seed rejected: %.1f Hz (%u frames in %u ms); "
                 "converging via updateInputRateEstimate()",
                 seedRate, (unsigned)frames, (unsigned)elapsedMs);
        }
    }

  setup_queues_and_tasks();

  LOGI("=========================================================");
  LOGI("Setup complete. Waiting for I2S audio + UDP subscribers...");
  LOGI("Boot source: %s  |  playback mode: %s", defaultBootSource.c_str(),
       playbackMode.c_str());
  LOGI("=========================================================");

  // Boot trace is done. Steady state emits errors only; the dashboard
  // (Status / Source tabs) still gets the full picture via /api/stats.
  gSerialLogLevel = 1;
  Serial.printf("[Boot] log level now = %u\n", (unsigned)gSerialLogLevel);
}

void loop() {
  static uint32_t lastRawDroppedTotal  = 0;
  static uint32_t lastOpusEncodedTotal = 0;
  static uint32_t lastOpusDroppedTotal = 0;
  static unsigned long lastBtPollMs    = 0;

  // MUST run first and every iteration. Everything else in this loop can
  // block (UDP, HTTP, Opus stalls, Wi-Fi), and if the reset check is
  // starved the button appears dead. GPIO0 is a hardware pull-up so the
  // pin itself is stable; we just need to poll it promptly.
  handleFactoryResetPin();
  serviceVolumePot();
  handleDiscoveryPacket();

  unsigned long now = millis();

  if (settingsDirty && (now - settingsDirtySince >= NVM_SAVE_DELAY_MS)) {
    // Never touch flash while the i2s/Opus pipeline is hot. A single 4 KB
    // sector erase can stall the flash cache for up to ~100 ms, which is
    // longer than the I2S input DMA ring and will cost us samples. Defer
    // until the stream goes quiet for a full second. Playback is unaffected
    // because a settings change is already rare and the in-RAM value has
    // been applied long before this fires.
    //
    // The previous comparison was `statI2sBytesTotal != last_i2s_bytes_seen`
    // where the LHS is volatile uint64_t and the RHS is uint32_t (promoted
    // to uint64_t). Once the 64-bit counter passes 4 GB — roughly 6.7 h at
    // 176 kB/s — the right-hand side can never match, so the test is
    // unconditionally true on every loop() iteration. That permanently
    // breaks `stream_is_quiet` and means deferred settings writes never
    // flush. Take an explicit 32-bit snapshot inside the same critical
    // section the periodic reset uses so the low-word read is atomic and
    // the comparison is apples-to-apples.
    static uint32_t last_i2s_bytes_seen = 0;
    static unsigned long last_i2s_change_ms = 0;
    uint32_t i2s_bytes_now;
    portENTER_CRITICAL(&gStatsMux);
      i2s_bytes_now = (uint32_t)statI2sBytesTotal;
    portEXIT_CRITICAL(&gStatsMux);
    if (i2s_bytes_now != last_i2s_bytes_seen) {
        last_i2s_bytes_seen    = i2s_bytes_now;
        last_i2s_change_ms     = now;
    }
    bool stream_is_quiet = (now - last_i2s_change_ms) > 1000;
    if (stream_is_quiet) {
        // [defer-nvs-zones-snapshot] Snapshot the zones vector BEFORE
        // clearing settingsDirty.  If we cannot acquire zonesMutex we
        // must NOT clear the flag; otherwise the zone change that armed
        // the flush would be silently lost on the next reboot.
        String zonesSnapshot;
        bool gotZones = false;
        if (lockFor(zonesMutex, 100)) {
            zonesSnapshot = serializeZones();
            unlockFor(zonesMutex);
            gotZones = true;
        }
        if (!gotZones) {
            // Zones lock contended (user editing a zone right now).
            // Leave settingsDirty set and retry on the next loop() tick.
        } else {
            settingsDirty = false;
            PrefsGuard pg;                       // [v1.1.1-prefs-mutex]
            if (pg.ok()) {
                preferences.begin("enkelvoud", false);
                preferences.putString("server_name",   serverName);
                // Mute state is intentionally never persisted; the server
                // always starts unmuted after a restart.
                preferences.putBool  ("master_muted",  false);
                preferences.putUChar ("master_volume", masterVolume);
                preferences.putUShort("latency_ms",    latencyAdjustmentMs);
                preferences.putUShort("buffer_ms",     audioBufferMs);
                preferences.putString("play_mode",     playbackMode);
                preferences.putString("boot_source",   defaultBootSource);
                preferences.putString("zones_v1",      zonesSnapshot);
                preferences.end();
                LOGI("Settings saved to NVM during quiet window");
            } else {
                LOGW("Settings NVM flush skipped: prefsMutex busy");  // [v1.1.1-prefs-mutex]
                settingsDirty = true;            // retry on the next quiet window
            }
        }
    }
  }

  // updateAdaptiveBuffers() was removed from this loop. The node now
  // self-tunes its own playout delay from its own local jitter
  // measurements (see audioPlaybackTask in EVDPAudio.h), which are more
  // accurate than a 3 s HTTP snapshot and do not require the server to
  // run a control loop per subscriber. audioBufferMs on the server is
  // now read-only telemetry.
  autoTuneOpusComplexity(now);

  // sendTimeSync() was removed. The node discards every time_sync packet
  // as soon as its first RTT clock-sync exchange completes, which is
  // within ~2 s of boot; see applyCtrlJson() in EVDPNet.h. The one-shot
  // time_sync in handlePlayerRegistration() is retained for the brief
  // window before that first RTT exchange.
  if (now - lastStatsMs >= 15000) {
    lastStatsMs = now;
    pruneStalePlayers(now);

    // Wrap every LOGI in this 15 s dump behind gSerialLogLevel >= 3, but
    // keep every counter update unconditional so the dashboard values stay
    // live no matter what the serial verbosity is.
    const bool verboseStats = (gSerialLogLevel >= 3);

    uint32_t rawDroppedDelta  = statRawDropped    - lastRawDroppedTotal;
    uint32_t opusEncodedDelta = statOpusEncoded   - lastOpusEncodedTotal;
    uint32_t opusDroppedDelta = statOpusDropped   - lastOpusDroppedTotal;
    lastRawDroppedTotal  = statRawDropped;
    lastOpusEncodedTotal = statOpusEncoded;
    lastOpusDroppedTotal = statOpusDropped;

    uint32_t latSum, latCnt;
    portENTER_CRITICAL(&gStatsMux);
      latSum = statLatencySumMs;
      latCnt = statLatencyCount;
    portEXIT_CRITICAL(&gStatsMux);
    float avgLatency = latCnt > 0
        ? (float)latSum / (float)latCnt : 0.0f;
    float kbps = (statUdpBytesSent * 8.0f / 1000.0f) / 5.0f;

    if (verboseStats) {
      bool wifiUp = (WiFi.status() == WL_CONNECTED);
      IPAddress devIp      = activeIpAddress();
      IPAddress devGateway = wifiUp ? WiFi.gatewayIP() : ap_gateway;

      LOGI("---- STATS (last 15s) ---------------------------------");
      LOGI("WiFi status        : %s (RSSI: %d dBm)",
           wifiUp ? "CONNECTED" : (accessPointActive ? "AP MODE" : "DISCONNECTED"),
           WiFi.RSSI());
      LOGI("Device IP addr     : %s  | Gateway: %s",
           devIp.toString().c_str(), devGateway.toString().c_str());
      LOGI("UDP audio target   : %s:%d", multicastGroup.toString().c_str(),
           AUDIO_UDP_PORT);
      LOGI("I2S bytes in       : %u  | read errors: %u",
           statI2sBytesIn, statI2sReadErrors);
      LOGI("USB bytes in (now) : %u  | input path: %s", statUsbBytesIn,
           (serverInputSource == SRC_FROM_USB) ? "USB" : "I2S");
      LOGI("Raw chunks dropped : %u", rawDroppedDelta);
      LOGI("Opus frames encoded: %u  | dropped: %u (complexity %d)",
           opusEncodedDelta, opusDroppedDelta, currentOpusComplexity);
      LOGI("UDP subscribers    : %d", subscriberCount);
      LOGI("UDP packets sent   : %u  | bytes: %u (%.1f kbps)",
           statUdpPacketsSent, statUdpBytesSent, kbps);
      LOGI("Avg latency (capture->send): %.1f ms", avgLatency);
      LOGI("UDP send errors: %u", statUdpErrors);
      LOGI("Audio frames withheld (fail-closed, incompatible subscriber): %u",
           statAudioIncompatibleSkips);
      LOGI("Clock drift trim     : %+ld ppm (resampler ratio %.8f)",
           (long)statDriftPpm, resampler.ratio());
      LOGI("Clock sync served  : %u  | errors: %u  | throttled: %u  | epoch: %lu",
           statSyncRequestsServed, statSyncRequestErrors,
           statSyncRequestsThrottled, (unsigned long)serverEpoch);
      LOGI("Free heap          : %u bytes", ESP.getFreeHeap());
      LOGI("---------------------------------------------------------");

      {
        uint32_t rawQD  = rawQueue  ? (uint32_t)uxQueueMessagesWaiting(rawQueue)  : 0;
        uint32_t opusQD = opusQueue ? (uint32_t)uxQueueMessagesWaiting(opusQueue) : 0;
        LOGI("---- AUDIO PIPELINE ------------------------------------");
        LOGI("rawQueue depth      : %u / %d", rawQD,  RAW_QUEUE_LEN);
        LOGI("opusQueue depth     : %u / %d", opusQD, OPUS_QUEUE_LEN);
        LOGI("Opus encoded (win)  : %u", opusEncodedDelta);
        LOGI("Opus dropped (win)  : %u", opusDroppedDelta);
        LOGI("UDP packets (win)   : %u  (%u bytes)",
             statUdpPacketsSent, statUdpBytesSent);
        if (lockFor(subsMutex, 50)) {
          for (int i = 0; i < subscriberTableCount; i++)
            LOGI("  sub    %-15s name=%-12s audioCompatible=%s declaredProto=%d "
                 "negotiatedProto=%d synced=%s",
                 subscribers[i].ip.c_str(), subscribers[i].name.c_str(),
                 subscribers[i].audioCompatible ? "true" : "false",
                 subscribers[i].declaredAudioProtocolVersion,
                 subscribers[i].negotiatedAudioProtocolVersion,
                 subscribers[i].hasCompletedClockSync ? "yes" : "no");
          unlockFor(subsMutex);
        }
      }
    }

    // [v1.1.1-atomic-window]
    // The byte counters are incremented on Core 1 (audioReadTask /
    // udpSendTask). The read-then-zero sequence below was a plain RMW that
    // silently discarded any increment that landed between the two
    // operations. Use a short critical section to make each snapshot+reset
    // atomic.
    portENTER_CRITICAL(&gStatsMux);
      statI2sBytesLast5s   = statI2sBytesIn;   statI2sBytesIn   = 0;
      statUsbBytesLast5s   = statUsbBytesIn;   statUsbBytesIn   = 0;
      statRawDroppedLast5s = rawDroppedDelta;
      statOpusEncodedLast5s = opusEncodedDelta;
      statOpusDroppedLast5s = opusDroppedDelta;
      statUdpPacketsLast5s = statUdpPacketsSent; statUdpPacketsSent = 0;
      statUdpBytesLast5s   = statUdpBytesSent;   statUdpBytesSent   = 0;
      statLatencySumMs     = 0;
      statLatencyCount     = 0;
    portEXIT_CRITICAL(&gStatsMux);
  }

  // GET_BT_DEVICES is only useful when the dashboard's Source tab is
  // actually open. Every poll triggers a full BT scan report from the
  // source board, which arrives as a burst of UART data and feeds the
  // line parser. Only poll while /api/source/info has been requested
  // recently (see the handler in EVDRoutes.h, which updates
  // gLastSourceInfoRequestMs).
  if (now - lastBtPollMs >= 5000 &&                       // was 3000
      (millis() - gLastSourceInfoRequestMs) < 15000) {
    lastBtPollMs = now;
    sendReceiverCommand("GET_BT_DEVICES");
  }

  handleReceiverLink();
  delay(10);
}