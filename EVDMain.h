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
  LOGI("Dashboard gzip: %u bytes (source: EVDCTRL.h via gen_gz.py)",
       (unsigned)EVDCTRL_HTML_GZ_LEN);

  // [build-fingerprint-encoder]
  // The diag recordings showed opusSampleRate=48000 but
  // serverEncBitrate=192000 while the header in EnkelvoudServer.ino
  // defines OPUS_BITRATE 96000. That mismatch means the flashed binary
  // does not match the source tree. Log every compile-time encoder
  // constant once at boot so a future "192 kbps when the header says
  // 96" is caught on the first serial line, not from an operator
  // complaining about bandwidth.
  LOGI("Encoder fingerprint (compile-time):");
  LOGI("  OPUS_BITRATE          = %d bps", OPUS_BITRATE);
  LOGI("  OPUS_SAMPLE_RATE_48K  = %d Hz",  OPUS_SAMPLE_RATE_48K);
  LOGI("  OPUS_SAMPLE_RATE_24K  = %d Hz",  OPUS_SAMPLE_RATE_24K);
  LOGI("  OPUS_FRAME_MS         = %d ms",  OPUS_FRAME_MS);
  LOGI("  OPUS_MAX_PACKET_BYTES = %d B",   OPUS_MAX_PACKET_BYTES);
  LOGI("  PENDING_MAX_FRAMES    = %d frames (%d B DRAM)",
       PENDING_MAX_FRAMES, PENDING_MAX_FRAMES * 2 * (int)sizeof(int16_t));
  LOGI("  RAW_QUEUE_LEN         = %d", RAW_QUEUE_LEN);
  LOGI("  OPUS_QUEUE_LEN        = %d", OPUS_QUEUE_LEN);
  LOGI("  ENCODE_QUEUE_LEN      = %d", ENCODE_QUEUE_LEN);
  LOGI("  MAX_UDP_SUBSCRIBERS   = %d", MAX_UDP_SUBSCRIBERS);
  LOGI("If serverEncBitrate reported by a node differs from OPUS_BITRATE");
  LOGI("above, the flashed binary is stale — rebuild and reflash.");
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
  if (now - lastReadMs < 25) return;
  lastReadMs = now;

  const int N = 8;
  uint32_t acc = 0;
  for (int i = 0; i < N; ++i) acc += analogReadMilliVolts(pinVolumePot);
  float mv  = (float)acc / (float)N;
  float pct = (mv * 100.0f) / 3300.0f;
  if (pct < 0.0f)   pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;

  // Asymmetric EMA: fast on real motion, slow on noise.
  const float NOISE_BAND = 1.5f;
  const float ALPHA_FAST = 0.70f;
  const float ALPHA_SLOW = 0.15f;
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

  // [usb-lifecycle-mutex] Create the USB lifecycle mutex here, alongside
  // every other mutex and BEFORE the boot-source restore below calls
  // startServerUsbAudio(). Idempotent; the no-lib branch defines a no-op
  // stub so this compiles unconditionally.
  initUsbLifecycle();

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
      // [nvs-begin-failure-assume-formatted]
      // If NVS itself is unreadable (corrupted, mid-erase, or a prior
      // factory reset left it uninitialised), Preferences::begin() returns
      // false and getBool() silently returns its default (false). Treating
      // that as "not formatted" would run LittleFS.begin(true) — wiping
      // the OTA spool — on every boot where NVS is transiently unreadable.
      // Only trust fs_formatted when begin() actually succeeded.
      if (preferences.begin("enkelvoud", true)) {
        fsFormatted = preferences.getBool("fs_formatted", false);
        preferences.end();
      } else {
        LOGW("setup: preferences.begin() failed for fs_formatted read; "
             "assuming formatted to avoid a spurious wipe");
        fsFormatted = true;
      }
    } else {
      LOGW("setup: prefsMutex unavailable for fs_formatted read; "
           "assuming formatted to avoid a spurious wipe");
      fsFormatted = true;
    }

    if (!fsFormatted) {
      LOGI("LittleFS: first-boot format (this wipes any prior filesystem)");
      if (LittleFS.begin(true)) {          // true = format on fail
        LittleFS.end();
        PrefsGuard pg2;                   // [v1.1.1-prefs-mutex]
        bool markerWritten = false;
        if (pg2.ok()) {
          // [fs-formatted-begin-check] If begin() fails, the marker is
          // never written, but the old code still logged "ready". The
          // next boot then reformatted LittleFS and wiped the OTA spool.
          if (preferences.begin("enkelvoud", false)) {
            markerWritten = preferences.putBool("fs_formatted", true);
            preferences.end();
          }
        }
        if (markerWritten) {
          LOGI("LittleFS: formatted and marked ready");
        } else {
          LOGW("LittleFS: formatted but fs_formatted marker NOT written; "
               "next boot will reformat and wipe the OTA spool");
        }
      } else {
        LOGW("LittleFS: format failed; OTA spool will stay unavailable");
      }
    }
  }

  pinMode(PIN_FACTORY_RESET, INPUT_PULLUP);
  pinMode(pinVolumePot, INPUT);
  loadWiFiSettings();
  loadZonesFromNVS();

  // [audio-buffer-per-mode] audioBufferMs is now part of the per-mode
  // encoder preset (ModeEncSettings.bufferMs) and is populated by
  // applyModeSettingsToLive() during loadWiFiSettings(). The old boot-path
  // clamps here (floor 60 if <20, then normalizeAudioBufferForMode() forcing
  // the mode's default value) are gone: they overwrote the user's saved
  // buffer with a hardcoded value on every boot, which is what made the
  // "Target Buffer Latency" slider appear to be unpersisted.

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

  // [opus-sample-rate] If loadWiFiSettings() -> applyModeSettingsToLive()
  // armed a sample-rate change (the persisted mode had a non-default rate),
  // honor it here so we do not double-create the encoder on the first
  // audio frame.
  setup_opus((g_pendingOpusSampleRate != 0) ? g_pendingOpusSampleRate
                                            : g_opusSampleRate);
// [per-mode-encoder-settings] loadWiFiSettings() (via
// applyModeSettingsToLive()) has already populated g_newBitrate /
// g_pendingComplexity / g_newVbr / g_newVbrConstraint / g_newSignal /
// g_newApplication for the boot mode, and audioProcessingTask() will
// drain them on the first frame.
  // g_pendingComplexity / g_newVbr / g_newVbrConstraint / g_newSignal /
  // g_newApplication for the boot mode, and audioProcessingTask() will
  // drain them on the first frame. The old call would have overwritten
  // the boot mode's saved complexity with profileForMode()'s start value.
  configureQuietFilters();
  setupDiscoveryResponder();
  // [setup-order-ctrl-mutex] Transport must be brought up BEFORE the HTTP
  // server, because setup_udp_transport() creates ctrlMutex and every HTTP
  // handler that calls sendCtrlToSubscriber() / broadcastNodeConfig() /
  // sendZoneConfigToIp() needs it to exist. Previously the server began
  // accepting requests with ctrlMutex == nullptr, and any player that
  // registered in that window had its first-sight config push silently
  // dropped (and statUdpErrors incremented once for the benign race).
  setup_udp_transport();
  setup_http_server();
  initDlna();                        // NEW: DLNA/UPnP MediaRenderer (EVDDLNA.h)
  // Clock sync lives on its own task so loop()'s NVS writes and UART
  // parsing can never delay a sync_resp. See clockSyncTask() in EVDUDP.h.
  // [clock-sync-task-check] Log failure so a silent clock-sync outage
  // (which presents as nodes drifting behind live) is visible in the
  // serial log at boot. Without this check, an out-of-heap failure here
  // causes every player to fall back to millis()-based playout with no
  // diagnostic — the operator sees "green dashboard, audio behind".
  // [core-assign-clocksync] pri 4 -> pri 3. At pri 4 it outranked audioRead,
  // and it wakes every 5 ms purely to poll for a request that arrives ~1.6x
  // per second. There is no path by which it needs to preempt capture; the
  // 5 ms poll interval already dominates its RTT error, and that error is
  // absorbed by the node's min-RTT-of-8 filter.
  BaseType_t csRc = xTaskCreatePinnedToCore(clockSyncTask, "ClockSync",
                                            3072, NULL, 3, NULL, 0);
  if (csRc != pdPASS) {
    LOGE("FAILED to create clockSyncTask (rc=%d) — players will fall back "
         "to local millis() playout and drift behind live", (int)csRc);
  }
  setupReceiverLink();
  // Restore the persisted boot-source default instead of the last runtime
  // selection, so a dashboard source change during the previous session does
  // not leak into the next boot until the user explicitly re-picks it.
  lastReceiverSource = String(bootSourceCStr());
  receiverSource     = String(bootSourceCStr());
  if (bootSourceIdx == 2) {   // USB
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
    sendReceiverSourceCommand(String(bootSourceCStr()));
    // [boot-source-honest] BT and AUX both arrive over the source board's
    // I2S link, so serverInputSource must say so. Without this it keeps its
    // static initialiser and the first /api/state poll after boot reports
    // the wrong input path until something writes it.
    serverInputSource = SRC_FROM_I2S;
  }
  // [boot-source-broadcast]
  // The restored source is the authoritative answer for the whole session, so
  // tell the players before the first frame goes out. Otherwise a node that
  // came up while the server was mid-boot shows the default source until its
  // next 30 s heartbeat, which is exactly the "dashboard says BT but it is
  // playing USB" report.
  if (bootSourceIdx == 2 && serverInputSource == SRC_FROM_USB) {
    broadcastNodeConfig();
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
        // [two-stage-resample-seed]
        // Stage 1 seeds from measured I2S clock / SOURCE_SAMPLE_RATE. The
        // adaptive estimator in updateInputRateEstimate() fine-tunes from
        // there. Stage 2 is initialised to its constant ratio inside
        // audioProcessingTask(). No forced nominal ratio here — forcing
        // SOURCE/OPUS on the raw I2S stream made pendingBuf flood when
        // I2S was clocked at 48 kHz (52 245 out/s vs 48 000 consumed/s).
        if (seedRate > 30000.0 && seedRate < 52000.0) {
            g_sourceSampleRate = (uint32_t)(seedRate + 0.5);
            resampler.setRatio(seedRate / (double)SOURCE_SAMPLE_RATE);
            LOGI("Resampler seed: I2S clock %.1f Hz -> source %u Hz, "
                 "stage1 ratio %.6f",
                 seedRate, (unsigned)g_sourceSampleRate,
                 seedRate / (double)SOURCE_SAMPLE_RATE);
        } else {
            g_sourceSampleRate = SOURCE_SAMPLE_RATE;
            resampler.setRatio(1.0);
            LOGW("Resampler seed rejected: %.1f Hz (%u frames in %u ms); "
                 "assuming %u Hz nominal",
                 seedRate, (unsigned)frames, (unsigned)elapsedMs,
                 (unsigned)SOURCE_SAMPLE_RATE);
        }
    }

  setup_queues_and_tasks();

  LOGI("=========================================================");
  LOGI("Setup complete. Waiting for I2S audio + UDP subscribers...");
  LOGI("Boot source: %s  |  playback mode: %s", bootSourceCStr(),
       playbackModeCStr());
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
  //
  // [factory-reset-ota-gate] factoryResetEverything() calls
  // nvs_flash_erase() + esp_restart(); doing that mid-Update.write()
  // bricks the partition. Skip the reset poll entirely while any OTA
  // state is active. The pin poller's internal state machine is left
  // frozen — if the button was held when OTA started, pinLowSinceMs
  // holds the ORIGINAL press time, and the first post-OTA poll sees
  // elapsed > 3 s and fires immediately. That is intentional: a user
  // who holds the button for the entire OTA duration has clearly
  // signalled intent. A 50 ms release latch clears tracking, so a
  // user who pressed before OTA, released, then pressed again gets
  // the normal 3 s window.
  if (!otaUploadInProgress && !otaInProgress && !otaPauseAudio) {
    handleFactoryResetPin();
  }

  // [ota-pause-watchdog-unified] The pre-H-A 600 s watchdog is removed.
  // It cleared otaUploadInProgress from a possibly-still-open chunk stream
  // and zeroed g_otaLastProgressMs, which disarmed the new 120 s
  // otaCheckStuckJob below for the rest of the boot. otaCheckStuckJob is
  // now the SINGLE authority: it releases otaPauseAudio at 120 s of no
  // progress, leaves otaInProgress / otaUploadInProgress set, and lets the
  // otaJob task or a subsequent onDisconnect clean up its own state.
  // Recovery from a truly stuck job requires an explicit reboot (see the
  // 'stalled' field the wizard now surfaces).
  otaCheckStuckJob();   // [ota-stuck-job-loop-watchdog]
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
    // [force-persist-deadline] The source board clocks I2S continuously
    // (silence still produces samples), so the counter never stops
    // incrementing while any audio is flowing and stream_is_quiet is
    // never true in normal operation. Every config/zone change armed
    // settingsDirty but was never flushed -> reboot reverted everything.
    // Bound the deferral so a change is persisted within 30 s even if
    // the stream is hot. Accepts one ~100 ms flash stall per config change
    // (rare, bounded); a lost config after every reboot is worse.
    static const unsigned long MAX_DEFER_MS = 30000UL;
    bool stream_is_quiet = (now - last_i2s_change_ms) > 1000;
    bool force_persist   = (now - settingsDirtySince) >= MAX_DEFER_MS;

    // [ota-no-nvs-flush] Suppress the NVS flush during an OTA upload or
    // flash. The upload's own LittleFS writes already monopolise the SPI
    // flash; an interleaved 4 KB NVS sector erase stalls the flash cache
    // long enough that the WiFi task misses ACKs and lwIP RSTs the
    // browser's upload socket — which the client shows as "network error"
    // in the OTA wizard, pointing the operator at the wrong subsystem.
    // settingsDirty stays armed; the save fires on the first quiet tick
    // after the OTA completes or the pause is force-cleared.
    const bool otaBlocking =
        otaUploadInProgress || otaInProgress || otaPauseAudio;
    if (otaBlocking && settingsDirty) {
      // Visible in the log so the guard's effect is observable; throttled
      // to one line per 5 s so a long upload doesn't flood the ring.
      static unsigned long lastOtaBlockLogMs = 0;
      if (millis() - lastOtaBlockLogMs > 5000) {
        lastOtaBlockLogMs = millis();
        LOGI("NVS flush deferred: OTA active (dirty %lu ms ago)",
             (unsigned long)(now - settingsDirtySince));
      }
    }
    if (!otaBlocking && (stream_is_quiet || force_persist)) {
        // [settings-dirty-generation] Capture arming generation BEFORE
        // serializeZones(). A Core-0 handler can arm settingsDirty at any
        // point below; clearing the flag anyway would drop the new arm
        // (settingsDirtySince survives, flag does not, gate never fires).
        const unsigned long armedSince = settingsDirtySince;
        String zonesSnapshot;
        bool gotZones = false;
        if (lockFor(zonesMutex, 100)) {
            zonesSnapshot = serializeZones();
            unlockFor(zonesMutex);
            gotZones = true;
        }
        if (!gotZones) {
            // Zones lock contended; retry next tick.
        } else {
            settingsDirty = false;
            PrefsGuard pg;
            if (pg.ok()) {
                // [nvs-begin-check] The old code ignored begin()'s return;
                // all puts silently failed and the log still said "saved".
                if (!preferences.begin("enkelvoud", false)) {
                    LOGW("Settings NVM flush skipped: preferences.begin failed");
                    settingsDirty = true;
                } else {
                    preferences.putString("server_name",   serverName);
                    preferences.putBool  ("master_muted",  false);
                    preferences.putUChar ("master_volume", masterVolume);
                    preferences.putString("play_mode",     playbackModeCStr());
                    preferences.putString("boot_source",   bootSourceCStr());
                    preferences.putString("zones_v1",      zonesSnapshot);
                    // [quietmode-persist]
                    preferences.putBool  ("quiet_mode",    quietModeEnabled);
                    // [audio-buffer-persist] Persist the live buffer as
                    // the current mode's per-mode preset buffer, so
                    // applyModeSettingsToLive() on the next boot loads it.
                    // [cross-core-string-fix] CRIT-1: read the atomic index,
                    // not the removed String symbol.
                    {
                      ModeIdx cm = (ModeIdx)playbackModeIdx;
                      g_modeSettings[cm].bufferMs = (int32_t)audioBufferMs;
                      String bkey = String("enc_") + modeIdxToName(cm) + "_buf";
                      preferences.putUShort(bkey.c_str(), (uint16_t)audioBufferMs);
                    }
                    preferences.end();
                    LOGI("Settings saved to NVM during quiet window");
                }
            } else {
                LOGW("Settings NVM flush skipped: prefsMutex busy");
                settingsDirty = true;
            }
            // [settings-dirty-generation-rearm] If a handler advanced
            // settingsDirtySince after armedSince was captured, re-arm so
            // the next tick re-serializes zones (which may have changed)
            // and re-persists the mutation. Redundant save is harmless.
            if (settingsDirtySince != armedSince && !settingsDirty) {
                settingsDirty = true;
            }

            // [audio-buffer-deferred-nvs] Persist the buffer change that
            // /api/audio/encoding recorded, into the mode it belongs to. Same
            // quiet-window gate as everything else, so the flash erase never
            // lands inside a slider drag or an OTA.
            if (g_bufferDirtyValid && (now - g_bufferDirtyAtMs) >= 1500) {
                uint8_t m = g_bufferDirtyMode;
                g_bufferDirtyValid = false;
                if (m < MODE_COUNT) {
                    saveEncoderModeSettings((ModeIdx)m);
                    LOGI("Audio buffer %u ms persisted for mode %s",
                         (unsigned)audioBufferMs, modeIdxToName((ModeIdx)m));
                }
            }
        }
    }
  }

  // [rate-change-broadcast]
  // Push a fresh host_audio_sync packet to every subscriber when the
  // measured source rate changes by >3 %. The node uses the field for
  // diagnostics (the Opus bitstream is always 48 kHz; the node does not
  // resample), and it lets the dashboard's Source tab update without
  // polling. See [rate-change-broadcast-flag] in EVDState.h.
  // [opus-sample-rate] Two independent triggers share one broadcast:
  //   g_rateChangeBroadcastHz           - measured SOURCE rate moved >3 %
  //   g_opusRateChangeNeedsBroadcast    - Opus encoder was re-created
  // Both push the same host_audio_sync packet which now carries both
  // sourceSampleRate and opusSampleRate.
  if (g_rateChangeBroadcastHz != 0 || g_opusRateChangeNeedsBroadcast) {
    uint32_t hz = g_rateChangeBroadcastHz;
    bool     opusChanged = g_opusRateChangeNeedsBroadcast;
    g_rateChangeBroadcastHz         = 0;
    g_opusRateChangeNeedsBroadcast  = false;
    if (opusChanged) {
      LOGI("Opus rate changed to %u Hz - broadcasting to subscribers",
           (unsigned)g_opusSampleRate);
    } else {
      LOGI("Source rate changed to %lu Hz - broadcasting to subscribers",
           (unsigned long)hz);
    }
    broadcastNodeConfig();
  }

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
    // [kbps-window] statUdpBytesSent is reset by the 15 s block below,
    // not every 5 s. Old divisor 5.0 reported 3x the true bitrate.
    float kbps = (statUdpBytesSent * 8.0f / 1000.0f) / 15.0f;

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
      // [stat-i2s-read-lock] audioReadTask increments statI2sBytesIn under
      // gStatsMux; reading it unlocked here silently misses whichever
      // chunk landed between the read and the next reset, making the
      // verbose dump disagree with /api/stats by one chunk.
      uint32_t i2sBytesInSnap;
      portENTER_CRITICAL(&gStatsMux);
        i2sBytesInSnap = statI2sBytesIn;
      portEXIT_CRITICAL(&gStatsMux);
      LOGI("I2S bytes in       : %u  | read errors: %u",
           i2sBytesInSnap, statI2sReadErrors);
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
      statNetBytesLast5s   = statNetBytesIn;   statNetBytesIn   = 0;   // NEW: DLNA
      statRawDroppedLast5s = rawDroppedDelta;
      statOpusEncodedLast5s = opusEncodedDelta;
      statOpusDroppedLast5s = opusDroppedDelta;
      statUdpPacketsLast5s = statUdpPacketsSent; statUdpPacketsSent = 0;
      statUdpBytesLast5s   = statUdpBytesSent;   statUdpBytesSent   = 0;
      statLatencySumMs     = 0;
      statLatencyCount     = 0;
    portEXIT_CRITICAL(&gStatsMux);
  }

  // ---- Input-rate anomaly detector -----------------------------------
  // Fires at most once per 5 s. Sources can drop to 32 kHz (SBC
  // low-bitrate fallback on weak BT links, or a phone that suspended
  // its audio stack) or rise to 48 kHz (codec renegotiation). Silence
  // encoding masks the rate deficit in the packet-size column, so the
  // operator needs an explicit warning when the measured rate stays
  // materially off 44.1 kHz.
  //
  // [rate-measure-cross-check-report]
  // Also reports the byte-counter-derived rate. If the two disagree by
  // >3 %, updateInputRateEstimate() (EVDAudio.h) uses the byte counter
  // for the resampler ratio. This log line is the operator-visible
  // signal that the two paths disagree - and which side is right.
  // Reported as a 15 s average using the accumulated byte counter
  // window (statI2sBytesLast5s), not the instantaneous per-3 s
  // estimate, so a single source stall inside the 3 s estimator window
  // cannot make the reference itself wrong.
  {
    static uint32_t lastRateWarnMs = 0;
    if (statMeasuredInputHz > 1000.0f &&
        (millis() - lastRateWarnMs) > 5000) {
      float hz      = statMeasuredInputHz;
      float nominal = (float)SOURCE_SAMPLE_RATE;
      float offBy   = (hz - nominal) / nominal;
      if (fabsf(offBy) > 0.10f) {
        if (statI2sBytesLast5s > 0) {
          double byteHz = ((double)statI2sBytesLast5s / 4.0) / 15.0;
          LOGE("Input rate anomaly: %.0f Hz (%.1f%% off nominal %.0f) "
               "| byte counter 15s-avg: %.0f Hz (%.1f%% off)",
               hz, offBy * 100.0f, nominal,
               byteHz,
               ((byteHz - SOURCE_SAMPLE_RATE) / SOURCE_SAMPLE_RATE) * 100.0);
        } else {
          LOGE("Input rate anomaly: %.0f Hz (%.1f%% off nominal %.0f)",
               hz, offBy * 100.0f, nominal);
        }
        lastRateWarnMs = millis();
      }
    }
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