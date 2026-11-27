// ============================================================================
//  EVDEqApi.h -- HTTP surface for the 10-band equaliser.
//
//  GET  /api/audio/eq  -> the same JSON shape the PC server returns
//                         (enkelvoud/eq.py state()), so the phone dashboard
//                         and the PC dashboard can share one equalizer UI:
//                         {"ok":true,"enabled":bool,"gains":[dB x10],
//                          "bands":[Hz x10],"maxGainDb":12}
//  POST /api/audio/eq  <- {"enabled":bool} and/or {"gains":[dB x10]}
//
//  Everything that touches gEq's coefficients or the saved prefs runs on the
//  AsyncWebServer task; the audio task only reads them (see EVDEq.h). The
//  rebuild is done here, under eqConfigMux, so a POST never leaves the
//  coefficients half-written while a frame is being filtered.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Serialises coefficient rebuilds between overlapping POSTs.
static SemaphoreHandle_t eqConfigMux = nullptr;

inline void eqApiInit() {
  if (!eqConfigMux) eqConfigMux = xSemaphoreCreateMutex();
  eqInit();
}

// Persists the EQ so a reboot keeps it. Uses the shared `preferences` object
// under the same prefsMutex every other writer uses (PrefsGuard), never a
// second NVS handle.
inline void eqSaveToNvs() {
  PrefsGuard g;
  if (!g.ok() || !preferences.begin("enkelvoud", false)) {
    LOGW("EQ: NVS open failed; gains not persisted");
    return;
  }
  preferences.putBool  ("eq_en",  gEq.enabled);
  preferences.putUChar ("eq_preset", gEq.preset);
  for (int i = 0; i < EQ_BAND_COUNT; ++i) {
    char k[12];
    snprintf(k, sizeof(k), "eq_g%d", i);
    preferences.putFloat(k, gEq.gains[i]);
    snprintf(k, sizeof(k), "eq_c%d", i);
    preferences.putFloat(k, gEq.custom[i]);
  }
  preferences.end();
}

inline void eqLoadFromNvs() {
  PrefsGuard g;
  if (!g.ok() || !preferences.begin("enkelvoud", true)) return;
  gEq.enabled = preferences.getBool("eq_en", false);
  uint8_t preset = preferences.getUChar("eq_preset", EQ_PRESET_CUSTOM);
  gEq.preset = (preset < EQ_PRESET_COUNT) ? preset : EQ_PRESET_CUSTOM;
  for (int i = 0; i < EQ_BAND_COUNT; ++i) {
    char k[12];
    snprintf(k, sizeof(k), "eq_g%d", i);
    float v = preferences.getFloat(k, 0.0f);
    if (v >  EQ_MAX_GAIN_DB) v =  EQ_MAX_GAIN_DB;
    if (v < -EQ_MAX_GAIN_DB) v = -EQ_MAX_GAIN_DB;
    gEq.gains[i] = v;
    snprintf(k, sizeof(k), "eq_c%d", i);
    float c = preferences.getFloat(k, v);      // custom defaults to the live curve
    if (c >  EQ_MAX_GAIN_DB) c =  EQ_MAX_GAIN_DB;
    if (c < -EQ_MAX_GAIN_DB) c = -EQ_MAX_GAIN_DB;
    gEq.custom[i] = c;
  }
  preferences.end();
  gEq.dirty = true;   // coefficients are rebuilt on the first frame
  LOGI("EQ: restored from NVS (%s)", gEq.enabled ? "enabled" : "disabled");
}

// Builds the state JSON in the same shape as the PC's eq.state().
inline String eqStateJson() {
  String s;
  s.reserve(420);
  s += "{\"ok\":true,\"enabled\":";
  s += gEq.enabled ? "true" : "false";
  s += ",\"gains\":[";
  for (int i = 0; i < EQ_BAND_COUNT; ++i) {
    if (i) s += ',';
    s += String(gEq.gains[i], 1);
  }
  s += "],\"bands\":[";
  for (int i = 0; i < EQ_BAND_COUNT; ++i) {
    if (i) s += ',';
    s += String(EQ_BAND_HZ[i], 1);
  }
  // [eq-presets] Which named curve is selected, plus the hand-made curve the
  // "Custom" entry restores. The gains themselves are always the live curve,
  // so a client that ignores these two fields still shows the truth.
  s += "],\"preset\":\"";
  s += EQ_PRESET_NAMES[gEq.preset < EQ_PRESET_COUNT ? gEq.preset : 0];
  s += "\",\"custom\":[";
  for (int i = 0; i < EQ_BAND_COUNT; ++i) {
    if (i) s += ',';
    s += String(gEq.custom[i], 1);
  }
  s += "],\"maxGainDb\":12}";
  return s;
}

inline void handleApiEqGet(AsyncWebServerRequest *request) {
  request->send(200, "application/json", eqStateJson());
}

inline void handleApiEqPost(AsyncWebServerRequest *request, uint8_t *data,
                            size_t len, size_t index, size_t total) {
  if (otaUploadInProgress || otaInProgress) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"OTA in progress\"}");
    return;
  }
  const char *body = nullptr;
  size_t bodyLen = 0;
  if (!accumulateReqBody(request, data, len, index, total, 768,
                         "{\"ok\":false,\"error\":\"EQ payload too large\"}",
                         body, bodyLen)) return;
  DynamicJsonDocument in(512);
  if (bodyLen == 0 || deserializeJson(in, body, bodyLen)) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  if (!in.containsKey("enabled") && !in.containsKey("gains") &&
      !in.containsKey("preset") && !in.containsKey("custom")) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"enabled, gains, preset or custom required\"}");
    return;
  }
  if (in.containsKey("gains") && !in["gains"].is<JsonArray>()) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"gains must be an array of 10 dB values\"}");
    return;
  }

  if (eqConfigMux) xSemaphoreTake(eqConfigMux, portMAX_DELAY);
  if (in.containsKey("enabled") && in["enabled"].is<bool>())
    gEq.enabled = in["enabled"].as<bool>();
  if (in.containsKey("custom") && in["custom"].is<JsonArray>()) {
    JsonArray carr = in["custom"].as<JsonArray>();
    for (int i = 0; i < EQ_BAND_COUNT; ++i) {
      float c = 0.0f;
      if (i < (int)carr.size() && carr[i].is<float>()) c = carr[i].as<float>();
      if (c >  EQ_MAX_GAIN_DB) c =  EQ_MAX_GAIN_DB;
      if (c < -EQ_MAX_GAIN_DB) c = -EQ_MAX_GAIN_DB;
      gEq.custom[i] = c;
    }
  }
  if (in.containsKey("preset") && in["preset"].is<const char *>()) {
    String pn = String((const char *)in["preset"]);
    pn.toLowerCase();
    uint8_t found = EQ_PRESET_CUSTOM;
    for (uint8_t i = 0; i < EQ_PRESET_COUNT; ++i)
      if (pn == EQ_PRESET_NAMES[i]) { found = i; break; }
    gEq.preset = found;
  }
  if (in.containsKey("gains")) {
    JsonArray arr = in["gains"].as<JsonArray>();
    for (int i = 0; i < EQ_BAND_COUNT; ++i) {
      float g = 0.0f;
      if (i < (int)arr.size() && arr[i].is<float>()) g = arr[i].as<float>();
      // Clamp here as well as in eqRebuild so a bad value can never reach the
      // coefficients even for one frame.
      if (g >  EQ_MAX_GAIN_DB) g =  EQ_MAX_GAIN_DB;
      if (g < -EQ_MAX_GAIN_DB) g = -EQ_MAX_GAIN_DB;
      gEq.gains[i] = g;
    }
  }
  // Rebuild now, at the rate the encoder is actually running at, so the very
  // next frame uses the new coefficients.
  eqRebuild(g_opusSampleRate);
  gEq.dirty = false;
  if (eqConfigMux) xSemaphoreGive(eqConfigMux);

  eqSaveToNvs();
  LOGI("EQ: %s, preset=%s, gains=[%.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f] (%u active)",
       gEq.enabled ? "enabled" : "disabled",
       EQ_PRESET_NAMES[gEq.preset < EQ_PRESET_COUNT ? gEq.preset : 0],
       gEq.gains[0], gEq.gains[1], gEq.gains[2], gEq.gains[3], gEq.gains[4],
       gEq.gains[5], gEq.gains[6], gEq.gains[7], gEq.gains[8], gEq.gains[9],
       (unsigned)gEq.activeCount);
  request->send(200, "application/json", eqStateJson());
}
