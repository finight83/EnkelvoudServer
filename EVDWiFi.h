#pragma once
// ============================================================================
//  EVDWiFi.h - AP/STA setup, discovery responder, WiFi scan, network test
// ============================================================================
#include "EVDHelpers.h"
#include "EVDNTW.h"          // provides networkSettingsPage()

// ---- Discovery -------------------------------------------------------------
inline void setupDiscoveryResponder() {
  discoveryUdp.begin(DISCOVERY_PORT);
  LOGI("UDP discovery responder listening on port %d", DISCOVERY_PORT);
}

inline void handleDiscoveryPacket() {
  int sz = discoveryUdp.parsePacket();
  if (sz <= 0) return;

  char buf[64];
  int n = discoveryUdp.read(buf, sizeof(buf) - 1);
  if (n <= 0) return;
  buf[n] = '\0';
  while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' ||
                   buf[n-1] == ' '  || buf[n-1] == '\t')) buf[--n] = '\0';
  if (strcmp(buf, DISCOVERY_MAGIC_REQ) != 0) return;

  // [servers-decide] Only the elected leader answers node discovery; a
  // standby stays silent so the nodes converge on the leader (EVDPeers.h).
  if (!g_peerIsLeader) return;

  IPAddress remote     = discoveryUdp.remoteIP();
  uint16_t  remotePort = discoveryUdp.remotePort();

  IPAddress replyIp;
  bool onApSubnet = false;
  if (accessPointActive) {
    uint32_t r = ((uint32_t)remote[0] << 24) | ((uint32_t)remote[1] << 16) |
                 ((uint32_t)remote[2] << 8)  | (uint32_t)remote[3];
    IPAddress apIp = WiFi.softAPIP();
    IPAddress apMk = WiFi.softAPSubnetMask();
    uint32_t a = ((uint32_t)apIp[0] << 24) | ((uint32_t)apIp[1] << 16) |
                 ((uint32_t)apIp[2] << 8)  | (uint32_t)apIp[3];
    uint32_t m = ((uint32_t)apMk[0] << 24) | ((uint32_t)apMk[1] << 16) |
                 ((uint32_t)apMk[2] << 8)  | (uint32_t)apMk[3];
    if ((r & m) == (a & m)) onApSubnet = true;
  }
  if      (onApSubnet)                    replyIp = WiFi.softAPIP();
  else if (WiFi.status() == WL_CONNECTED) replyIp = WiFi.localIP();
  else                                    replyIp = WiFi.softAPIP();

  String reply = String(DISCOVERY_MAGIC_RPY) + " " + serverName + " " +
                 FW_VERSION + " " + replyIp.toString();

  discoveryUdp.beginPacket(remote, remotePort);
  discoveryUdp.write((const uint8_t*)reply.c_str(), reply.length());
  discoveryUdp.endPacket();

  LOGI("Discovery: replied to %s -> %s",
       remote.toString().c_str(), replyIp.toString().c_str());
}

// ---- Per-mode encoder settings (see EVDState.h) ---------------------------
// loadEncoderModeSettings: read all three modes' presets from NVS into RAM.
// saveEncoderModeSettings: persist one mode's preset to NVS.
// applyModeSettingsToLive: copy a mode's preset into the six live g_new* /
//   g_pendingComplexity variables and set g_encoderSettingsDirty so the audio
//   task drains them on its next frame.
// captureLiveIntoMode: read the six live variables into a mode's preset,
//   used by the Save button and by handleApiConfig() before a mode change.
inline void loadEncoderModeSettings() {
  PrefsGuard pg;
  if (!pg.ok()) { LOGW("loadEncoderModeSettings: prefsMutex busy"); return; }
  // [begin-return-check] Without this, getInt/getBool/getUInt return their
  // defaults silently and the caller has no way to distinguish "user has
  // defaults" from "NVS unreadable". Keeps existing g_modeSettings values
  // as the fallback so a running preset is not lost.
  if (!preferences.begin("enkelvoud", true)) {
    LOGW("loadEncoderModeSettings: preferences.begin failed; "
         "keeping in-RAM presets");
    return;
  }
  for (int i = 0; i < MODE_COUNT; i++) {
    const char *n = modeIdxToName((ModeIdx)i);
    g_modeSettings[i].bitrate       = preferences.getInt ((String("enc_")+n+"_br").c_str(),  g_modeSettings[i].bitrate);
    g_modeSettings[i].complexity    = preferences.getInt ((String("enc_")+n+"_cx").c_str(),  g_modeSettings[i].complexity);
    g_modeSettings[i].vbr           = preferences.getBool((String("enc_")+n+"_vbr").c_str(), g_modeSettings[i].vbr);
    g_modeSettings[i].vbrConstraint = preferences.getBool((String("enc_")+n+"_vc").c_str(),  g_modeSettings[i].vbrConstraint);
    g_modeSettings[i].signal        = preferences.getInt ((String("enc_")+n+"_sig").c_str(), g_modeSettings[i].signal);
    g_modeSettings[i].application   = preferences.getInt ((String("enc_")+n+"_app").c_str(), g_modeSettings[i].application);
    g_modeSettings[i].bufferMs      = (int32_t)preferences.getUShort((String("enc_")+n+"_buf").c_str(), (uint16_t)g_modeSettings[i].bufferMs);
    // [nvs-range-clamp] Clamp every field on load. A corrupted or
    // out-of-range NVS value otherwise breaks audio silently:
    //   - bitrate > 192000 -> opus_encode() returns negative (VBR peaks
    //     exceed OPUS_MAX_PACKET_BYTES), every frame dropped
    //   - application out of range -> infinite recreate loop in
    //     audioEncodeTask (see [infinite-recreate-fix] in EVDAudio.h)
    //   - complexity/signal out of range -> undefined libopus behaviour
    if (g_modeSettings[i].bitrate < 6000)   g_modeSettings[i].bitrate = 6000;
    if (g_modeSettings[i].bitrate > 192000) g_modeSettings[i].bitrate = 192000;
    if (g_modeSettings[i].complexity < 0)   g_modeSettings[i].complexity = 0;
    if (g_modeSettings[i].complexity > 10)  g_modeSettings[i].complexity = 10;
    if (g_modeSettings[i].signal != OPUS_SIGNAL_MUSIC &&
        g_modeSettings[i].signal != OPUS_SIGNAL_VOICE &&
        g_modeSettings[i].signal != OPUS_AUTO) {
      g_modeSettings[i].signal = OPUS_SIGNAL_MUSIC;
    }
    if (g_modeSettings[i].application != OPUS_APPLICATION_AUDIO &&
        g_modeSettings[i].application != OPUS_APPLICATION_VOIP &&
        g_modeSettings[i].application != OPUS_APPLICATION_RESTRICTED_LOWDELAY) {
      g_modeSettings[i].application = OPUS_APPLICATION_AUDIO;
    }
    if (g_modeSettings[i].bufferMs < AUDIO_BUFFER_MIN_MS) g_modeSettings[i].bufferMs = AUDIO_BUFFER_MIN_MS;
    if (g_modeSettings[i].bufferMs > AUDIO_BUFFER_MAX_MS) g_modeSettings[i].bufferMs = AUDIO_BUFFER_MAX_MS;
    // [opus-sample-rate] Per-mode sample rate. Validate to the two legal
    // values; anything else (corrupted NVS) falls back to 48 kHz.
    g_modeSettings[i].sampleRate = (int32_t)preferences.getUInt(
        (String("enc_")+n+"_sr").c_str(), (uint32_t)g_modeSettings[i].sampleRate);
    if (g_modeSettings[i].sampleRate != OPUS_SAMPLE_RATE_48K &&
        g_modeSettings[i].sampleRate != OPUS_SAMPLE_RATE_24K) {
      g_modeSettings[i].sampleRate = OPUS_SAMPLE_RATE_48K;
    }
  }
  preferences.end();
  LOGI("Per-mode encoder presets loaded (off/music/movie)");
}


inline void saveEncoderModeSettings(ModeIdx i) {
  PrefsGuard pg;
  if (!pg.ok()) {
    LOGW("saveEncoderModeSettings(%s): prefsMutex busy; preset NOT saved",
         modeIdxToName(i));
    return;
  }
  // [begin-return-check] Preferences::begin() returns false when NVS cannot
  // be mounted; without this check, all following putX() calls silently
  // fail and the operator gets a "Saved" toast for a write that never
  // happened.
  if (!preferences.begin("enkelvoud", false)) {
    LOGW("saveEncoderModeSettings(%s): preferences.begin failed; preset NOT saved",
         modeIdxToName(i));
    return;
  }
  const char *n = modeIdxToName(i);
  preferences.putInt   ((String("enc_")+n+"_br").c_str(),  g_modeSettings[i].bitrate);
  preferences.putInt   ((String("enc_")+n+"_cx").c_str(),  g_modeSettings[i].complexity);
  preferences.putBool  ((String("enc_")+n+"_vbr").c_str(), g_modeSettings[i].vbr);
  preferences.putBool  ((String("enc_")+n+"_vc").c_str(),  g_modeSettings[i].vbrConstraint);
  preferences.putInt   ((String("enc_")+n+"_sig").c_str(), g_modeSettings[i].signal);
  preferences.putInt   ((String("enc_")+n+"_app").c_str(), g_modeSettings[i].application);
  preferences.putUShort((String("enc_")+n+"_buf").c_str(), (uint16_t)g_modeSettings[i].bufferMs);
  // [opus-sample-rate] Persist per-mode sample rate.
  preferences.putUInt  ((String("enc_")+n+"_sr").c_str(),  (uint32_t)g_modeSettings[i].sampleRate);
  preferences.putString("play_mode", playbackModeCStr());
  preferences.end();

  // [save-trace] One line per save, showing exactly what hit NVS. When the
  // operator reports "my app change didn't stick", this line proves whether
  // the wrong value arrived at the backend or whether the UI showed stale.
  LOGI("Per-mode preset SAVED: %s br=%ld cx=%ld vbr=%d vc=%d sig=%ld app=%ld buf=%d sr=%ld",
       n,
       (long)g_modeSettings[i].bitrate,
       (long)g_modeSettings[i].complexity,
       (int)g_modeSettings[i].vbr,
       (int)g_modeSettings[i].vbrConstraint,
       (long)g_modeSettings[i].signal,
       (long)g_modeSettings[i].application,
       (int)g_modeSettings[i].bufferMs,
       (long)g_modeSettings[i].sampleRate);
}

inline void applyModeSettingsToLive(ModeIdx i) {
  g_newBitrate           = g_modeSettings[i].bitrate;
  g_pendingComplexity    = g_modeSettings[i].complexity;
  g_newVbr               = g_modeSettings[i].vbr ? 1 : 0;
  g_newVbrConstraint     = g_modeSettings[i].vbrConstraint ? 1 : 0;
  g_newSignal            = g_modeSettings[i].signal;
  g_newApplication       = g_modeSettings[i].application;

  int bm = (int)g_modeSettings[i].bufferMs;
  if (bm < AUDIO_BUFFER_MIN_MS) bm = AUDIO_BUFFER_MIN_MS;
  if (bm > AUDIO_BUFFER_MAX_MS) bm = AUDIO_BUFFER_MAX_MS;
  audioBufferMs = (uint16_t)bm;

  // [opus-sample-rate] If the destination mode's rate differs from the live
  // encoder, arm a re-create. The audio task drains it on the next frame.
  // If the destination rate matches, clear any stale pending request that
  // was queued by a fast back-and-forth mode toggle.
  if ((uint32_t)g_modeSettings[i].sampleRate != g_opusSampleRate) {
    g_pendingOpusSampleRate = (uint32_t)g_modeSettings[i].sampleRate;
  } else {
    g_pendingOpusSampleRate = 0;
  }

  g_encoderSettingsDirty = true;

  // [mode-load-trace] Proves which preset was loaded. If the app dropdown
  // shows stale values after a switch, this line settles whether the backend
  // loaded the right preset or the frontend failed to render it.
  LOGI("Mode preset applied: %s br=%ld cx=%ld vbr=%d vc=%d sig=%ld app=%ld buf=%d",
       modeIdxToName(i),
       (long)g_modeSettings[i].bitrate,
       (long)g_modeSettings[i].complexity,
       (int)g_modeSettings[i].vbr,
       (int)g_modeSettings[i].vbrConstraint,
       (long)g_modeSettings[i].signal,
       (long)g_modeSettings[i].application,
       bm);
}

inline void captureLiveIntoMode(ModeIdx i) {
  g_modeSettings[i].bitrate       = g_newBitrate;
  g_modeSettings[i].complexity    = (g_pendingComplexity >= 0)
                                    ? g_pendingComplexity : currentOpusComplexity;
  g_modeSettings[i].vbr           = (g_newVbr != 0);
  g_modeSettings[i].vbrConstraint = (g_newVbrConstraint != 0);
  g_modeSettings[i].signal        = g_newSignal;
  g_modeSettings[i].application   = g_newApplication;
  g_modeSettings[i].bufferMs      = (int32_t)audioBufferMs;
  // [opus-sample-rate] Capture the INTENDED rate (pending if armed, else
  // current) so a Save right after a dropdown change persists what the
  // user selected, even if the audio task has not yet re-created the
  // encoder.
  g_modeSettings[i].sampleRate    = (g_pendingOpusSampleRate != 0)
                                    ? (int32_t)g_pendingOpusSampleRate
                                    : (int32_t)g_opusSampleRate;

  // [mode-capture-trace] Paired with the applied trace above. Interpretation:
  //   captured app=X, applied app=Y, X==Y, wrong in UI -> frontend render bug
  //   captured app=X, applied app=Y, X!=Y            -> wrong preset loaded
  //   captured app=Y but operator expected X         -> UI never sent X
  LOGI("Mode preset captured: %s br=%ld cx=%ld vbr=%d vc=%d sig=%ld app=%ld buf=%d",
       modeIdxToName(i),
       (long)g_modeSettings[i].bitrate,
       (long)g_modeSettings[i].complexity,
       (int)g_modeSettings[i].vbr,
       (int)g_modeSettings[i].vbrConstraint,
       (long)g_modeSettings[i].signal,
       (long)g_modeSettings[i].application,
       (int)g_modeSettings[i].bufferMs);
}

// ---- NVS / settings --------------------------------------------------------
inline void loadWiFiSettings() {
  // [scope-guard] PrefsGuard is scoped so it releases prefsMutex
  // BEFORE loadEncoderModeSettings() is called below. Previously,
  // loadEncoderModeSettings() tried to acquire the same mutex, failed
  // instantly, logged "prefsMutex busy", and returned without loading
  // the user's Opus presets from NVS.
  {
    PrefsGuard pg;                              // [v1.1.1-prefs-mutex]
    if (!pg.ok()) {
      LOGW("loadWiFiSettings: prefsMutex busy; using defaults");
      // [nvs-begin-fallthrough] Still apply per-mode defaults so
      // audioBufferMs is never left at 0.
      applyModeSettingsToLive((ModeIdx)playbackModeIdx);
      return;
    }

    // [prefs-begin-check] An unreadable NVS made storedVersion == "",
    // which triggered preferences.clear() and WIPED the entire namespace
    // (Wi-Fi, zones, encoder presets). Bail out instead.
    if (!preferences.begin("enkelvoud", false)) {
      LOGW("loadWiFiSettings: preferences.begin() failed; using defaults");
      preferences.end();                          // harmless if not begun
      applyModeSettingsToLive((ModeIdx)playbackModeIdx);
      return;
    }
    String storedVersion = preferences.getString("s_ver", "");
    if (storedVersion != SETTINGS_VERSION) {
      // [preserve-fs-formatted] setup() reads fs_formatted BEFORE this
      // function runs. A bare clear() wipes it, so the NEXT boot sees
      // false, calls LittleFS.begin(true), and formats the partition —
      // destroying the entire OTA spool. Read the flag, clear, then
      // write it back so a version bump does NOT cause a spool wipe.
      bool hadFsFormatted = preferences.getBool("fs_formatted", true);
      preferences.clear();
      preferences.putString("s_ver", SETTINGS_VERSION);
      if (hadFsFormatted) preferences.putBool("fs_formatted", true);
      LOGI("Settings version changed; restored defaults "
           "(fs_formatted preserved=%d)", (int)hadFsFormatted);
    }
    wifiStaSsid       = preferences.getString("wifi_ssid", DEFAULT_WIFI_STA_SSID);
    wifiStaPassword   = preferences.getString("wifi_pass", DEFAULT_WIFI_STA_PASSWORD);
    wifiUseStaticIp   = preferences.getBool  ("wifi_static", false);
    wifiStaticIp      = preferences.getString("wifi_ip", "");
    wifiStaticGateway = preferences.getString("wifi_gateway", "");
    wifiStaticSubnet  = preferences.getString("wifi_subnet", "255.255.255.0");
    wifiStaticDns     = preferences.getString("wifi_dns", "");
    serverName        = sanitizeServerName(preferences.getString("server_name", "Enkelvoud"));
    
    masterMuted = false;
    if (preferences.getBool("master_muted", false)) {
      preferences.putBool("master_muted", false);
    }
    masterVolume      = preferences.getUChar ("master_volume", 100);
    // [master-volume-range-clamp] A corrupted NVS byte would multiply the
    // PCM stream by >1x here and hard-clip every sample. 0-100 matches the
    // dashboard slider, the API validator, and the 100-denominator math in
    // audioProcessingTask.
    if (masterVolume > 100) masterVolume = 100;
    // [quietmode-persist] Was never loaded, so a saved Quiet Mode was
    // silently dropped at the next boot. configureQuietFilters() in
    // setup() acts on this value; loading here is single-threaded safe.
    quietModeEnabled  = preferences.getBool("quiet_mode", false);
    latencyAdjustmentMs = 0;
    // [cross-core-string-fix] CRIT-1: decode into the atomic index; the
    // modeNameToIdx / bootSourceNameToIdx helpers already return a valid
    // default (0) for anything outside the whitelist, so the old triple
    // validation is redundant.
    {
      String pm = preferences.getString("play_mode", "off");
      playbackModeIdx = (uint8_t)modeNameToIdx(pm);
      String bs = preferences.getString("boot_source", "BT");
      bs.toUpperCase();
      bootSourceIdx = bootSourceNameToIdx(bs);
    }
    preferences.end();
  } // <--- prefsMutex is released here

  loadEncoderModeSettings();
  applyModeSettingsToLive((ModeIdx)playbackModeIdx);
}

inline void resetNetworkSettingsToDefaults() {
  wifiStaSsid       = DEFAULT_WIFI_STA_SSID;
  wifiStaPassword   = DEFAULT_WIFI_STA_PASSWORD;
  wifiUseStaticIp   = false;
  wifiStaticIp      = "";
  wifiStaticGateway = "";
  wifiStaticSubnet  = "255.255.255.0";
  wifiStaticDns     = "";
  serverName        = "Enkelvoud";

  PrefsGuard pg;                                         // [v1.1.1-prefs-mutex]
  if (!pg.ok()) {
    LOGW("resetNetworkSettingsToDefaults: prefsMutex busy; NOT saved");
    return;
  }
  if (!preferences.begin("enkelvoud", false)) {
    LOGW("resetNetworkSettingsToDefaults: preferences.begin failed; NOT saved");
    return;
  }
  preferences.putString("s_ver", SETTINGS_VERSION);
  preferences.putString("wifi_ssid", wifiStaSsid);
  preferences.putString("wifi_pass", wifiStaPassword);
  preferences.putBool  ("wifi_static", wifiUseStaticIp);
  preferences.putString("wifi_ip", wifiStaticIp);
  preferences.putString("wifi_gateway", wifiStaticGateway);
  preferences.putString("wifi_subnet", wifiStaticSubnet);
  preferences.putString("wifi_dns", wifiStaticDns);
  preferences.putString("server_name", serverName);
  preferences.end();
  LOGW("Factory reset: default IP settings restored");
}

// ---- Network settings endpoints --------------------------------------------
inline void handleNetworkSettings(AsyncWebServerRequest *request) {
  request->send(200, "text/html; charset=utf-8", networkSettingsPage());
}

// ---------------------------------------------------------------------------
//  /api/network/state - JSON snapshot of the current Wi-Fi configuration.
//  Consumed by the inline Network tab in the dashboard (EVDCTRL.h). Kept
//  separate from /settings (which still serves the standalone HTML page) so
//  the dashboard can populate the form without a page reload.
// ---------------------------------------------------------------------------
inline void handleNetworkState(AsyncWebServerRequest *request) {
  String body;
  body.reserve(640);
  body  = "{\"ok\":true";
  body += ",\"serverName\":\"";  body += jsonEscape(serverName);         body += "\"";
  body += ",\"ssid\":\"";        body += jsonEscape(wifiStaSsid);        body += "\"";
  // [security-password-not-echoed] Never echo the configured password.
  // Anyone on the LAN could curl /api/network/state and read it. Clients
  // only need to know whether one is set (for placeholder rendering).
  body += ",\"passwordSet\":";
  body += (wifiStaPassword.length() > 0 ? "true" : "false");
  body += ",\"useStatic\":";     body += (wifiUseStaticIp ? "true" : "false");
  body += ",\"ip\":\"";          body += jsonEscape(wifiStaticIp);       body += "\"";
  body += ",\"gateway\":\"";     body += jsonEscape(wifiStaticGateway);  body += "\"";
  body += ",\"subnet\":\"";      body += jsonEscape(wifiStaticSubnet);   body += "\"";
  body += ",\"dns\":\"";         body += jsonEscape(wifiStaticDns);      body += "\"";
  body += ",\"activeIp\":\"";    body += activeIpAddress().toString();   body += "\"";
  body += ",\"apMode\":";        body += (accessPointActive ? "true" : "false");
  body += "}";
  AsyncWebServerResponse *resp =
      request->beginResponse(200, "application/json", body);
  resp->addHeader("Cache-Control", "no-store, max-age=0");
  request->send(resp);
}

inline bool saveTestedNetworkSettings() {
  PrefsGuard pg;                                         // [v1.1.1-prefs-mutex]
  if (!pg.ok()) {
    LOGW("saveTestedNetworkSettings: prefsMutex busy; NOT saved");
    return false;
  }
  // [begin-return-check] Without this, all putX() silently fail on a
  // corrupt/full NVS and the caller still reports NETWORK_TEST_SUCCESS.
  if (!preferences.begin("enkelvoud", false)) {
    LOGW("saveTestedNetworkSettings: preferences.begin failed; NOT saved");
    return false;
  }
  serverName        = pendingServerName;
  wifiStaSsid       = pendingSsid;
  wifiStaPassword   = pendingPassword;
  wifiUseStaticIp   = pendingUseStaticIp;
  wifiStaticIp      = pendingStaticIp;
  wifiStaticGateway = pendingStaticGateway;
  wifiStaticSubnet  = pendingStaticSubnet;
  wifiStaticDns     = pendingStaticDns;

  preferences.putString("s_ver", SETTINGS_VERSION);
  preferences.putString("wifi_ssid", wifiStaSsid);
  preferences.putString("wifi_pass", wifiStaPassword);
  preferences.putBool  ("wifi_static", wifiUseStaticIp);
  preferences.putString("wifi_ip", wifiStaticIp);
  preferences.putString("wifi_gateway", wifiStaticGateway);
  preferences.putString("wifi_subnet", wifiStaticSubnet);
  preferences.putString("wifi_dns", wifiStaticDns);
  preferences.putString("server_name", serverName);
  preferences.end();
  LOGI("Network settings saved to NVS (SSID: \"%s\", static IP: %s)",
       wifiStaSsid.c_str(), wifiUseStaticIp ? "enabled" : "disabled");
  return true;
}

inline void networkTestTask(void *parameter) {
  (void)parameter;
  IPAddress ip, gateway, subnet, dns;

  WiFi.disconnect(false, false);
  WiFi.mode(accessPointActive ? WIFI_AP_STA : WIFI_STA);
  if (!pendingUseStaticIp) {
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  } else if (!ip.fromString(pendingStaticIp) || !gateway.fromString(pendingStaticGateway) ||
             !subnet.fromString(pendingStaticSubnet) || !dns.fromString(pendingStaticDns) ||
             !WiFi.config(ip, gateway, subnet, dns)) {
    // [failed-save-restore-old-credentials] WiFi.disconnect() above already
    // tore down the previous association. Restore the last-known-good
    // credentials so the STA can come back up while the operator fixes
    // the /settings form via the AP.
    if (!accessPointActive && wifiStaSsid.length() > 0) {
      WiFi.begin(wifiStaSsid.c_str(), wifiStaPassword.c_str());
    }
    networkTestError = "Unable to apply the static IP configuration";
    networkTestState = NETWORK_TEST_FAILED;
    vTaskDelete(nullptr);
    return;
  }

  WiFi.begin(pendingSsid.c_str(), pendingPassword.c_str());
  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_TEST_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(250));
  }

  if (WiFi.status() != WL_CONNECTED || (pendingUseStaticIp && WiFi.localIP() != ip)) {
    WiFi.disconnect(false, false);
    // Stay in WIFI_AP_STA, not WIFI_AP: startSetupAccessPoint() deliberately
    // keeps the STA interface enabled so /api/wifi/scan/* works. Dropping to
    // WIFI_AP here would disable the STA interface again, and a subsequent
    // Scan from the same page would fail with WIFI_SCAN_FAILED — the exact
    // "No networks found" symptom the AP_STA change was made to fix.
    // WiFi.disconnect() alone is enough to unwind the failed association;
    // it does not touch the AP interface.
    if (accessPointActive) {
      WiFi.mode(WIFI_AP_STA);
      // Confirm the driver actually switched before this task returns.
      // Without this, a Scan request arriving within the same ~30 ms as
      // the failure response finds WiFi.getMode() still reporting
      // WIFI_STA and the new AP_STA guard in handleWifiScanStart()
      // delays it by 30 ms on every retry.
      unsigned long wMode = millis();
      while (WiFi.getMode() != WIFI_AP_STA && millis() - wMode < 250) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    // [failed-save-restore-old-credentials] Restore the last-known-good
    // credentials. WiFi.begin(pendingSsid, pendingPassword) above replaced
    // the radio state; without this the STA stays associated with nothing
    // and the event handler (now excluded for NETWORK_TEST_FAILED) will
    // never bring it back.
    if (!accessPointActive && wifiStaSsid.length() > 0) {
      WiFi.begin(wifiStaSsid.c_str(), wifiStaPassword.c_str());
    }
    networkTestError = "Unable to connect using the supplied Wi-Fi and IP settings";
    networkTestState = NETWORK_TEST_FAILED;
    vTaskDelete(nullptr);
    return;
  }

  networkTestIp = WiFi.localIP().toString();
  // [save-must-succeed-before-success] If NVS is unwritable we still have
  // a live STA association, but the operator's settings are NOT persisted;
  // reporting SUCCESS makes the "Connected — rebooting..." page a lie.
  if (!saveTestedNetworkSettings()) {
    WiFi.disconnect(false, false);
    if (!accessPointActive && wifiStaSsid.length() > 0) {
      WiFi.begin(wifiStaSsid.c_str(), wifiStaPassword.c_str());
    }
    networkTestError = "Wi-Fi connected but NVS write failed; settings NOT saved";
    networkTestState = NETWORK_TEST_FAILED;
    vTaskDelete(nullptr);
    return;
  }
  accessPointActive = false;
  if (WiFi.status() == WL_CONNECTED && g_peerIsLeader) {
    MDNS.end();
    if (MDNS.begin(serverName.c_str())) {
      MDNS.addService("http",     "tcp", 80);
      MDNS.addService("enkelsrv", "tcp", 80);
      LOGI("Network test: mDNS restarted at %s.local",
           serverName.c_str());
    }
  }
  networkTestState = NETWORK_TEST_SUCCESS;
  vTaskDelete(nullptr);
}

inline bool validStaticStationAddress(const IPAddress &ip, const IPAddress &gateway, const IPAddress &subnet) {
  uint32_t address = ((uint32_t)ip[0]      << 24) | ((uint32_t)ip[1]      << 16) | ((uint32_t)ip[2]      << 8) | ip[3];
  uint32_t router  = ((uint32_t)gateway[0] << 24) | ((uint32_t)gateway[1] << 16) | ((uint32_t)gateway[2] << 8) | gateway[3];
  uint32_t mask    = ((uint32_t)subnet[0]  << 24) | ((uint32_t)subnet[1]  << 16) | ((uint32_t)subnet[2]  << 8) | subnet[3];
  uint32_t hostMask = ~mask;

  // Mask must be contiguous ones followed by zeros, and not 0.
  if (mask == 0 || (hostMask & (hostMask + 1)) != 0) return false;

  return address != router && address != 0 && address != 0xFFFFFFFF &&
         address != 0xC0A80401 && (address & mask) == (router & mask) &&
         (address & hostMask) != 0 && (address & hostMask) != hostMask;
}

inline void handleSaveNetworkSettings(AsyncWebServerRequest *request) {
  // [ota-gate-save] saveTestedNetworkSettings() / networkTestTask() write
  // 9 NVS keys synchronously and may call ESP.restart(). Both stall the
  // flash cache for ~100 ms per key, or reboot the server mid-push. The
  // dashboard's Network tab does not know an OTA is running.
  if (otaUploadInProgress || otaInProgress || otaPauseAudio) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"OTA in progress; retry after update\"}");
    return;
  }
  if (!request->hasParam("ssid", true)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Wi-Fi SSID is required\"}");
    return;
  }

  String newServerNameRaw = request->hasParam("server_name", true)
      ? request->getParam("server_name", true)->value()
      : "Enkelvoud";
  if (newServerNameRaw.length() == 0 || newServerNameRaw.length() > 32) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"Server name must be 1-32 letters or digits\"}");
    return;
  }
  for (size_t i = 0; i < newServerNameRaw.length(); ++i) {
    char c = newServerNameRaw[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
      request->send(400, "application/json",
                    "{\"ok\":false,\"error\":\"Server name must contain only letters and digits\"}");
      return;
    }
  }
  String newServerName = newServerNameRaw;
  String newSsid       = request->getParam("ssid", true)->value();
  String newPassword   = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
  // [security-password-not-echoed] Client no longer receives the current
  // password; blank field means "keep existing". Only an explicit new
  // value changes it.
  if (newPassword.length() == 0) {
    newPassword = wifiStaPassword;
  }
  // [form-bool-parse] hasParam() is true even for use_static_ip=false
  // or use_static_ip=0, which silently forced static IP mode.
  bool newUseStaticIp = false;
  if (request->hasParam("use_static_ip", true)) {
    String v = request->getParam("use_static_ip", true)->value();
    v.toLowerCase();
    newUseStaticIp = (v == "1" || v == "true" || v == "on" || v == "yes");
  }
  String newStaticIp   = request->hasParam("ip", true)      ? request->getParam("ip", true)->value() : "";
  String newStaticGw   = request->hasParam("gateway", true) ? request->getParam("gateway", true)->value() : "";
  String newStaticSub  = request->hasParam("subnet", true)  ? request->getParam("subnet", true)->value() : "255.255.255.0";
  String newStaticDns  = request->hasParam("dns", true)     ? request->getParam("dns", true)->value() : "";

  IPAddress ip, gateway, subnet, dns;
  if (newSsid.length() == 0) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Wi-Fi SSID is required\"}");
    return;
  }

    if (newUseStaticIp) {
      if (!ip.fromString(newStaticIp) || !gateway.fromString(newStaticGw) ||
          !subnet.fromString(newStaticSub)) {
        request->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"IP, gateway, or subnet is not a valid address\"}");
        return;
      }
      // DNS is optional. An empty DNS field means "use the gateway as DNS",
      // which is what every home router and Windows ICS do by default anyway.
      // Requiring a non-empty DNS here was rejecting valid configs where the
      // user left the field blank -- IPAddress::fromString("") returns false.
      if (newStaticDns.length() == 0) {
        newStaticDns = newStaticGw;
        dns = gateway;
      } else if (!dns.fromString(newStaticDns)) {
        request->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"DNS server is not a valid IP address\"}");
        return;
      }
      if (!validStaticStationAddress(ip, gateway, subnet)) {
        request->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"Use a valid unused host IP on the gateway network\"}");
        return;
      }
    }

  if (networkTestState == NETWORK_TEST_RUNNING) {
    request->send(409, "application/json",
                  "{\"ok\":false,\"error\":\"A Wi-Fi connection test is already running\"}");
    return;
  }

  pendingSsid = newSsid; pendingPassword = newPassword;
  pendingUseStaticIp = newUseStaticIp;
  pendingStaticIp = newStaticIp; pendingStaticGateway = newStaticGw;
  pendingStaticSubnet = newStaticSub; pendingStaticDns = newStaticDns;
  pendingServerName = newServerName;
  networkTestIp = ""; networkTestError = "";
  networkTestState = NETWORK_TEST_RUNNING;

  if (xTaskCreate(networkTestTask, "wifiTest", 4096, nullptr, 1, nullptr) != pdPASS) {
    networkTestError = "Unable to start the Wi-Fi connection test";
    networkTestState = NETWORK_TEST_FAILED;
    request->send(500, "application/json", "{\"ok\":false,\"error\":\"Unable to start the Wi-Fi connection test\"}");
    return;
  }
  request->send(202, "application/json", "{\"ok\":true,\"state\":\"running\"}");
}

inline void handleNetworkTestStatus(AsyncWebServerRequest *request) {
  const char *state = "idle";
  if      (networkTestState == NETWORK_TEST_RUNNING) state = "running";
  else if (networkTestState == NETWORK_TEST_SUCCESS) state = "success";
  else if (networkTestState == NETWORK_TEST_FAILED)  state = "failed";

  // [json-escape-network-test] IMP-3: every other handler in this tree
  // escapes String fields with jsonEscape(); this was the lone outlier.
  // networkTestError is only populated from static strings today, but a
  // one-line future addition ("failed to connect to <ssid>") would
  // reintroduce a JSON-injection hole. Escape both fields.
  String json = String("{\"state\":\"") + state + "\"";
  if (networkTestState == NETWORK_TEST_SUCCESS) {
    json += ",\"ip\":\"";
    json += jsonEscape(networkTestIp);
    json += "\"";
  } else if (networkTestState == NETWORK_TEST_FAILED) {
    json += ",\"error\":\"";
    json += jsonEscape(networkTestError);
    json += "\"";
  }
  json += "}";
  request->send(200, "application/json", json);
}

inline void handleWifiScanStart(AsyncWebServerRequest *request) {
  // [ota-gate-scan] A synchronous scan parks the radio for ~13 × 300 ms
  // = ~4 s. During Phase 2 this stalls the TCP push to every node; during
  // Phase 1 it stalls the browser upload. Both cause retransmit cascades
  // that can trip the peer's RTO.
  if (otaUploadInProgress || otaInProgress || otaPauseAudio) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"state\":\"failed\","
                  "\"reason\":\"ota_active\"}");
    return;
  }
  // Re-assert WIFI_AP_STA. startSetupAccessPoint() sets it at boot, but
  // networkTestTask(), an OTA pause, or any prior WiFi.mode() call from
  // another handler can leave the chip in pure WIFI_AP — where the STA
  // interface is disabled and esp_wifi_scan_start() is refused with
  // WIFI_SCAN_FAILED regardless of what is in the air. Checking here
  // rather than only at boot is what makes Scan survive a failed /save
  // attempt that has run networkTestTask's error path.
  if (accessPointActive && WiFi.getMode() != WIFI_AP_STA) {
    LOGW("WiFi scan: re-asserting WIFI_AP_STA (was mode=%d)",
         (int)WiFi.getMode());
    WiFi.mode(WIFI_AP_STA);
    vTaskDelay(pdMS_TO_TICKS(30));
  }

  int state = WiFi.scanComplete();
  if (state == WIFI_SCAN_RUNNING) {
    request->send(200, "application/json", "{\"ok\":true,\"state\":\"running\"}");
    return;
  }
  WiFi.scanDelete();
  // Give the driver one tick to finish tearing the previous scan down.
  // scanDelete() returns before the radio has fully released the scan
  // state; scanNetworks() on the very next tick can return
  // WIFI_SCAN_FAILED even when the air is full of APs.
  vTaskDelay(pdMS_TO_TICKS(20));
  int16_t rc = WiFi.scanNetworks(true, false, false, 300);
  if (rc == WIFI_SCAN_FAILED) {
    LOGW("WiFi scan: scanNetworks() returned WIFI_SCAN_FAILED "
         "(mode=%d, status=%d)",
         (int)WiFi.getMode(), (int)WiFi.status());
    request->send(200, "application/json",
                  "{\"ok\":false,\"state\":\"failed\",\"reason\":\"scan_rejected\"}");
    return;
  }
  request->send(200, "application/json", "{\"ok\":true,\"state\":\"started\"}");
}

inline void handleWifiScanStatus(AsyncWebServerRequest *request) {
  int state = WiFi.scanComplete();
  if (state == WIFI_SCAN_RUNNING) {
    AsyncWebServerResponse *r =
        request->beginResponse(200, "application/json",
                               "{\"state\":\"running\"}");
    r->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    r->addHeader("Pragma", "no-cache");
    request->send(r);
    return;
  }
  if (state == WIFI_SCAN_FAILED) {
    AsyncWebServerResponse *r =
        request->beginResponse(200, "application/json",
                               "{\"state\":\"failed\",\"networks\":[]}");
    r->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    r->addHeader("Pragma", "no-cache");
    request->send(r);
    return;
  }
  // Single Content-Length-framed body, not AsyncResponseStream. During an
  // AP_STA scan the radio briefly stops servicing the AP's TCP clients
  // while it hops channels. A chunked response that is mid-flight when
  // that happens can deliver the header but drop the body, and the
  // browser's r.json() throws or returns a body with `networks`
  // undefined — which the dashboard renders as "No networks found"
  // *after* it has already drawn a previous poll's list. That flash is
  // what this change removes: `beginResponse(200, "application/json",
  // body)` always sends a real Content-Length, so either the whole body
  // arrives or the socket fails and the client's catch branch runs
  // (leaving the previous list intact). Same fix pattern as
  // handleOtaPlan().
  String body;
  body.reserve(64 + state * 56);
  body  = "{\"state\":\"done\",\"networks\":[";
  for (int i = 0; i < state; ++i) {
    if (i) body += ",";
    body += "{\"ssid\":\"";
    body += jsonEscape(WiFi.SSID(i));
    body += "\",\"rssi\":";
    body += String((int)WiFi.RSSI(i));
    body += "}";
  }
  body += "]}";
  AsyncWebServerResponse *r =
      request->beginResponse(200, "application/json", body);
  r->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  r->addHeader("Pragma", "no-cache");
  request->send(r);
}

inline void handleRestart(AsyncWebServerRequest *request) {
  // [ota-gate-restart] A stray click on /api/restart mid-OTA reboots the
  // server during Phase 2, sending a TCP RST to every node whose
  // Update.write() is in flight. The node's 90 s OTA_STUCK_TIMEOUT_MS
  // then fires Update.abort() — the entire push is silently discarded.
  if (otaUploadInProgress || otaInProgress || otaPauseAudio) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"OTA in progress\"}");
    return;
  }
  request->send(200, "application/json", "{\"ok\":true}");
  // Restart from a separate task so the async_tcp handler returns and the
  // response can flush.
  xTaskCreate([](void *) {
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP.restart();
    vTaskDelete(nullptr);
  }, "restartTask", 2048, nullptr, 1, nullptr);
}

// ---- AP / STA bring-up -----------------------------------------------------
inline bool startSetupAccessPoint() {
  // Set this FIRST so the STA_DISCONNECTED event fired by WiFi.disconnect()
  // below sees accessPointActive == true. Setting it at the end (as the
  // previous version did) let the disconnect event reach setup_wifi_sta()'s
  // handler while the flag was still false, which started a doomed
  // WiFi.reconnect() against the credentials that had just failed and
  // produced the "sta is connecting, return error" loop visible in the log
  // every ~2.8 s.
  accessPointActive = true;

  // Disable Arduino's built-in auto-reconnect for this boot. Combined with
  // the !accessPointActive guard in the event handler, this makes the sketch
  // the only code that ever calls WiFi.reconnect(), and only when not in AP
  // fallback. Without it, every disconnect fires two reconnect attempts
  // (ours + the library's) and one of them is always rejected with
  // "sta is connecting, return error".
  WiFi.setAutoReconnect(false);

  WiFi.disconnect(false, false);
  // WIFI_AP_STA, not WIFI_AP: pure AP mode disables the STA interface, and
  // esp_wifi_scan_start() refuses to run without it. Arduino-ESP32 then
  // returns WIFI_SCAN_FAILED immediately, and the dashboard's
  // /api/wifi/scan/status reports "no networks found" regardless of what
  // is actually in the air. WIFI_AP_STA keeps the AP beaconing for
  // clients connected to the config page while leaving the STA interface
  // available for scanning. The AP shares the radio with the scan, so
  // clients see a ~4 s beacon gap while a full 13-channel scan runs
  // (300 ms * 13); that is well below the typical STA disconnect
  // threshold (~10 s), so the browser session survives.
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  if (!WiFi.softAPConfig(ap_local_ip, ap_gateway, ap_subnet)) {
    LOGE("Failed to configure setup access point");
    accessPointActive = false;
    return false;
  }
  if (!WiFi.softAP(default_ap_ssid, default_ap_password)) {
    LOGE("Failed to start setup access point");
    accessPointActive = false;
    return false;
  }
  LOGI("Setup AP \"%s\" available at %s",
       default_ap_ssid, WiFi.softAPIP().toString().c_str());
  return true;
}

inline void setup_wifi_sta() {
  // Register the WiFi event handler BEFORE attempting any connection.
  //
  // The previous code registered this handler only after the initial STA
  // connect succeeded, which meant that:
  //   - a disconnect during the initial connect attempt was not counted;
  //   - if the initial connect failed and we fell back to AP mode
  //     (startSetupAccessPoint), the handler was never registered at all,
  //     so statWifiDisconnects never incremented in AP mode.
  //
  // With the handler installed unconditionally, every subsequent drop is
  // counted regardless of operating mode. In STA mode the events are
  // ARDUINO_EVENT_WIFI_STA_DISCONNECTED; in AP mode (setup only) they are
  // ARDUINO_EVENT_WIFI_AP_STADISCONNECTED — one per client that was
  // connected and then lost. Both are surfaced through the same counter so
  // the dashboard's /api/stats can answer "was the gap a WiFi issue?" with
  // a single number.
  //
  // IMPORTANT: this is the ONLY registration of this handler in the file.
  // The previous edit left a second WiFi.onEvent() at the bottom of this
  // function; on Arduino-ESP32 core 3.x that appends a second listener, so
  // every real disconnect incremented statWifiDisconnects twice (corrupting
  // the metric) and WiFi.reconnect() was invoked twice. If you grep for
  // WiFi.onEvent in EVDWiFi.h you must find exactly one hit, right here.
  WiFi.onEvent([](WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      statWifiDisconnects++;
      // Do NOT reconnect while any of the following is true:
      //   - accessPointActive: the saved STA credentials just failed and
      //     the operator is expected to enter new ones via the setup AP.
      //     Retrying the known-bad credentials here is what produced the
      //     STA scan loop that hid the "Enkelvoud Server" SSID from phones:
      //     in WIFI_AP_STA the AP shares the radio with the STA, so a STA
      //     that is constantly scanning for a nonexistent SSID parks the
      //     radio on a different channel every ~250 ms, and the AP's
      //     beacons become too intermittent to be discovered.
      //   - networkTestState == NETWORK_TEST_RUNNING: networkTestTask owns
      //     the STA state during a /save test; a reconnect() from this
      //     handler races the new association and wastes radio time.
      // [failed-state-bounded-reconnect]
      // NETWORK_TEST_FAILED is a permanent latch: nothing ever resets it
      // back to IDLE after a failed /save, so excluding it unconditionally
      // means a *later* real disconnect (router reboot, RF drop) is never
      // recovered — the device sits offline until a manual power cycle.
      // The fix 1.3 restore-credentials call already ran inside
      // networkTestTask before FAILED was set, so reconnecting is safe
      // after a short settle window. Bound the exclusion to 30 s.
      //
      // [failed-latch-resets-on-transition]
      // networkTestFailedAt must be cleared whenever the state is NOT
      // FAILED. Without this, a *second* /save failure that starts more
      // than 30 s after the first sees the stale first-failure timestamp,
      // concludes "not recently failed", and reconnects immediately —
      // skipping the settle window the second failure is supposed to
      // honour. The transition NETWORK_TEST_RUNNING -> FAILED must reset
      // the latch so the second failure gets a fresh 30 s window.
      static unsigned long networkTestFailedAt = 0;
      if (networkTestState != NETWORK_TEST_FAILED) {
        networkTestFailedAt = 0;             // reset latch on any non-FAILED state
      } else if (networkTestFailedAt == 0) {
        networkTestFailedAt = millis();      // arm the latch on entry to FAILED
      }
      bool recentlyFailed = (networkTestState == NETWORK_TEST_FAILED &&
                             (millis() - networkTestFailedAt) < 30000UL);
      if (!accessPointActive &&
          networkTestState != NETWORK_TEST_RUNNING &&
          !recentlyFailed) {
        LOGW("STA WiFi disconnected from router - reconnecting... (total=%u)",
             (unsigned)statWifiDisconnects);
        WiFi.reconnect();
      } else {
        LOGW("STA WiFi disconnected (%s; not reconnecting) (total=%u)",
             accessPointActive ? "AP fallback active"
                               : "network test in progress",
             (unsigned)statWifiDisconnects);
      }
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
      statWifiDisconnects++;
      LOGW("AP client disconnected (total=%u)", (unsigned)statWifiDisconnects);
    }
  });

  WiFi.setAutoReconnect(false);
  LOGI("Connecting to WiFi router \"%s\"...", wifiStaSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  if (wifiUseStaticIp) {
    IPAddress ip, gateway, subnet, dns;
    if (!ip.fromString(wifiStaticIp) || !gateway.fromString(wifiStaticGateway) ||
        !subnet.fromString(wifiStaticSubnet) || !dns.fromString(wifiStaticDns) ||
        !WiFi.config(ip, gateway, subnet, dns)) {
      LOGE("Invalid or failed static IP configuration; using DHCP instead");
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }
  }
  WiFi.begin(wifiStaSsid.c_str(), wifiStaPassword.c_str());

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    LOGI("  ...still connecting (status=%d)", (int)WiFi.status());
  }

  if (WiFi.status() != WL_CONNECTED) {
    LOGW("WiFi connection failed; starting setup access point");
    startSetupAccessPoint();
    return;
  }

  LOGI("WiFi connected");
  LOGI("  SSID          : %s", wifiStaSsid.c_str());
  LOGI("  Channel       : %d", WiFi.channel());
  LOGI("  RSSI          : %d dBm", WiFi.RSSI());
  LOGI("  Device IP     : %s", WiFi.localIP().toString().c_str());
  LOGI("  Gateway       : %s", WiFi.gatewayIP().toString().c_str());
  LOGI("  Subnet mask   : %s", WiFi.subnetMask().toString().c_str());
  LOGI("  DNS server    : %s", WiFi.dnsIP().toString().c_str());
  LOGI("  MAC addr      : %s", WiFi.macAddress().c_str());
  LOGI("  WebSocket URL : ws://%s/audio", WiFi.localIP().toString().c_str());
}