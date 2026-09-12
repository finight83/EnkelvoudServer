/*
  ============================================================================
  EnkelvoudServer (ESP32-S3)
  I2S AUDIO INPUT -> Opus Encoder -> Async WebSocket Server (+ EVD pages)
  ============================================================================

  DESCRIPTION
  -----------
  This board (ESP32-S3) receives 16-bit stereo PCM over I2S from an upstream
  ESP32 source node, resamples 44.1kHz -> 48kHz, encodes Opus (20ms frames),
  and broadcasts packets over websocket at:
      ws://<server-ip>/audio

  Added on top of the known-good working audio pipeline:
    - HTTP GET /            -> EVDCTRL_HTML (control page; routes registered
                               via evdctrlRegisterRoutes() in EVDCTRL.h)
    - HTTP GET /player      -> EVDPLR_HTML
    - HTTP GET /api/status  -> JSON status
    - POST /api/config      -> update volume/mute/latency/buffer/name
    - POST /api/source      -> bridge source selection to upstream receiver
    - POST /api/cmd         -> bridge command (next/prev/bt/aux/usb) upstream
    - POST /api/restart     -> restart the ESP32-S3

  IMPORTANT
  ---------
  - Audio pipeline is kept functionally equivalent to the working
    esp32s3_bt_opus_ws_server.ino logic.
  - ESP32-S3 is configured as I2S SLAVE RX.
  - Upstream ESP32 must be I2S MASTER TX and drive BCLK + WS + DATA.
  ============================================================================
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <algorithm>
#include <vector>
#include <driver/i2s.h>
#include <ArduinoJson.h>
#include "opus.h"

#include "EVDCTRL.h"
#include "EVDPLR.h"
#include "EVDSRC.h"

// ----------------------------------------------------------------------------
// PIN CONFIG - I2S INPUT pins (from upstream ESP32 source board)
// ----------------------------------------------------------------------------
int pinBtLrc  = 15;   // I2S Word Select / LRCLK (input)
int pinBtDin  = 16;   // I2S Data IN (input)
int pinBtBclk = 17;   // I2S Bit Clock (input)
int pinSrcUartTx = 9; // Server TX -> receiver GPIO 32
int pinSrcUartRx = 10; // Server RX <- receiver GPIO 33

// ----------------------------------------------------------------------------
// PIN CONFIG - I2S OUTPUT pins (to local DAC for local playback)
// ----------------------------------------------------------------------------
int pinDacLrc  = 11;   // I2S Word Select / LRCLK (output to DAC)
int pinDacDout = 12;   // I2S Data OUT (output to DAC)
int pinDacBclk = 13;   // I2S Bit Clock (output to DAC)

// ----------------------------------------------------------------------------
// WIFI / SERVER CONFIG
// ----------------------------------------------------------------------------

const char *SETTINGS_VERSION = "v2.0.4";
const char *DEFAULT_WIFI_STA_SSID = "";
const char *DEFAULT_WIFI_STA_PASSWORD = "";
const char *default_ap_ssid = "Enkelvoud-Server";
const char *default_ap_password = "";

IPAddress ap_local_ip(192, 168, 4, 1);
IPAddress ap_gateway(192, 168, 4, 1);
IPAddress ap_subnet(255, 255, 255, 0);

String wifiStaSsid = DEFAULT_WIFI_STA_SSID;
String wifiStaPassword = DEFAULT_WIFI_STA_PASSWORD;
bool wifiUseStaticIp = false;
String wifiStaticIp = "";
String wifiStaticGateway = "";
String wifiStaticSubnet = "255.255.255.0";
String wifiStaticDns = "";
String serverName = "Enkelvoud";
bool masterMuted = false;
uint8_t masterVolume = 100;
uint16_t latencyAdjustmentMs = 0;
uint16_t audioBufferMs = 0;
String receiverSource = "BT";
String receiverState = "unknown";
uint32_t receiverSampleRate = 0;
uint8_t receiverChannels = 0;
uint8_t receiverBits = 0;
uint32_t receiverBytes = 0;
uint32_t receiverFrames = 0;
uint32_t receiverErrors = 0;


#define WIFI_CONNECT_TIMEOUT_MS 20000
#define WIFI_TEST_TIMEOUT_MS 15000

// ----------------------------------------------------------------------------
// FACTORY RESET CONFIG (GPIO 0 pulled to GND)
// ----------------------------------------------------------------------------
#define PIN_FACTORY_RESET      0
#define FACTORY_RESET_HOLD_MS  3000

// ----------------------------------------------------------------------------
// RECEIVER CONTROL BRIDGE CONFIG (S3 -> EnkelvoudReceiver)
// ----------------------------------------------------------------------------
const char *RECEIVER_HOST = "192.168.100.240";   // e.g. receiver IP or "enkelvoudserver.local"
const uint16_t RECEIVER_PORT = 80;
const char *RECEIVER_TOKEN = ""; // set if receiver CONTROL_TOKEN is enabled

// ----------------------------------------------------------------------------
// AUDIO / OPUS CONFIG
// ----------------------------------------------------------------------------
#define SOURCE_SAMPLE_RATE    44100
#define OPUS_SAMPLE_RATE      48000
#define CHANNELS              2
#define OPUS_FRAME_SAMPLES    960
#define OPUS_BITRATE          64000
#define OPUS_MAX_PACKET_BYTES 1500
#define OGG_TEST_PACKET_COUNT 150
#define OGG_TEST_MAX_PACKET_BYTES 256
#define LOCAL_DAC_WRITE_TIMEOUT_MS 2

#define I2S_PORT_IN           I2S_NUM_0
#define I2S_PORT_OUT          I2S_NUM_1
#define I2S_READ_CHUNK_BYTES  1024

// ----------------------------------------------------------------------------
// LOGGING HELPERS
// ----------------------------------------------------------------------------
#define LOG_BAUD 115200
#define LOGI(fmt, ...) Serial.printf("[INFO ] %8lu ms | " fmt "\n", millis(), ##__VA_ARGS__)
#define LOGW(fmt, ...) Serial.printf("[WARN ] %8lu ms | " fmt "\n", millis(), ##__VA_ARGS__)
#define LOGE(fmt, ...) Serial.printf("[ERROR] %8lu ms | " fmt "\n", millis(), ##__VA_ARGS__)

// ----------------------------------------------------------------------------
// GLOBAL OBJECTS
// ----------------------------------------------------------------------------
AsyncWebServer server(80);
AsyncWebSocket ws("/audio");
OpusEncoder *opusEncoder = nullptr;
Preferences preferences;

// ----------------------------------------------------------------------------
// QUEUED DATA STRUCTURES
// ----------------------------------------------------------------------------
/** Raw I2S PCM chunk produced by i2sReadTask and consumed by audioProcessingTask. */
struct RawChunk {
  uint8_t data[I2S_READ_CHUNK_BYTES];
  uint32_t len;
  uint32_t captured_ms;
};
static QueueHandle_t rawQueue = nullptr;
static QueueHandle_t rawFreeQueue = nullptr;
#define RAW_QUEUE_LEN 32
#define RAW_CHUNK_POOL_LEN (RAW_QUEUE_LEN + 1)
static RawChunk rawChunkPool[RAW_CHUNK_POOL_LEN];

/** Encoded Opus packet produced by audioProcessingTask and consumed by wsSendTask. */
struct OpusPacket {
  uint8_t data[OPUS_MAX_PACKET_BYTES];
  int     len;
  uint32_t captured_ms;
};
static QueueHandle_t opusQueue = nullptr;
#define OPUS_QUEUE_LEN 64

/** Compact Ogg history. CBR Opus at 64 kbps uses 160 bytes per 20 ms packet. */
struct OggTestPacket {
  uint8_t data[OGG_TEST_MAX_PACKET_BYTES];
  uint16_t len;
};
static OggTestPacket oggTestPackets[OGG_TEST_PACKET_COUNT];
static uint8_t oggTestWriteIndex = 0;
static uint8_t oggTestPacketCount = 0;
static portMUX_TYPE oggTestMux = portMUX_INITIALIZER_UNLOCKED;

// ----------------------------------------------------------------------------
// STATS
// ----------------------------------------------------------------------------
static volatile uint32_t statI2sBytesIn     = 0;
static volatile uint32_t statI2sReadErrors  = 0;
static volatile uint32_t statLocalDacStalls = 0;
static volatile uint32_t statRawDropped     = 0;
static volatile uint32_t statOpusEncoded    = 0;
static volatile uint32_t statOpusDropped    = 0;
static volatile uint32_t statWsPacketsSent  = 0;
static volatile uint32_t statWsBytesSent    = 0;
static volatile uint32_t statWsClientsBackpressured = 0;
static volatile uint32_t statI2sBytesLast5s = 0;
static volatile uint32_t statRawDroppedLast5s = 0;
static volatile uint32_t statOpusEncodedLast5s = 0;
static volatile uint32_t statOpusDroppedLast5s = 0;
static volatile uint32_t statWsPacketsLast5s = 0;
static volatile uint32_t statWsBytesLast5s = 0;
static volatile uint64_t statI2sBytesTotal = 0;
static volatile uint64_t statLatencySumMs   = 0;
static volatile uint32_t statLatencyCount   = 0;
static volatile int      wsClientCount      = 0;

// Bridge telemetry.
static String lastReceiverSource = "BT";
static uint16_t lastBridgeHttpCode = 0;
static String lastBridgeMsg = "";
static bool accessPointActive = false;
enum NetworkTestState { NETWORK_TEST_IDLE, NETWORK_TEST_RUNNING, NETWORK_TEST_SUCCESS, NETWORK_TEST_FAILED };
static volatile NetworkTestState networkTestState = NETWORK_TEST_IDLE;
static String networkTestIp = "";
static String networkTestError = "";
static String pendingSsid = "";
static String pendingServerName = "";
static String pendingPassword = "";
static bool pendingUseStaticIp = false;
static String pendingStaticIp = "";
static String pendingStaticGateway = "";
static String pendingStaticSubnet = "";
static String pendingStaticDns = "";

struct PlayerNode {
  String name;
  String ip;
  unsigned long lastSeenMs;
};
static std::vector<PlayerNode> playerNodes;
static const unsigned long PLAYER_STALE_TIMEOUT_MS = 15000;

void pruneStalePlayers(unsigned long nowMs) {
  playerNodes.erase(
      std::remove_if(playerNodes.begin(), playerNodes.end(),
                     [nowMs](const PlayerNode &player) {
                       return (nowMs - player.lastSeenMs) > PLAYER_STALE_TIMEOUT_MS;
                     }),
      playerNodes.end());
}

String sanitizeServerName(const String &value) {
  String result;
  result.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    if (isAlphaNumeric(value[i])) result += value[i];
  }
  return result.length() ? result : "Enkelvoud";
}

String htmlEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&': escaped += F("&amp;"); break;
      case '"': escaped += F("&quot;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      default: escaped += value[i]; break;
    }
  }
  return escaped;
}

IPAddress activeIpAddress() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP()
      : (accessPointActive ? WiFi.softAPIP() : IPAddress(0, 0, 0, 0));
}

void loadWiFiSettings() {
  preferences.begin("enkelvoud", false);
  String storedVersion = preferences.getString("s_ver", "");
  if (storedVersion != SETTINGS_VERSION) {
    preferences.clear();
    preferences.putString("s_ver", SETTINGS_VERSION);
    LOGI("Settings version changed; restored default settings");
  }
  wifiStaSsid = preferences.getString("wifi_ssid", DEFAULT_WIFI_STA_SSID);
  wifiStaPassword = preferences.getString("wifi_pass", DEFAULT_WIFI_STA_PASSWORD);
  wifiUseStaticIp = preferences.getBool("wifi_static", false);
  wifiStaticIp = preferences.getString("wifi_ip", "");
  wifiStaticGateway = preferences.getString("wifi_gateway", "");
  wifiStaticSubnet = preferences.getString("wifi_subnet", "255.255.255.0");
  wifiStaticDns = preferences.getString("wifi_dns", "");
  serverName = sanitizeServerName(preferences.getString("server_name", "Enkelvoud"));
  masterMuted = preferences.getBool("master_muted", false);
  masterVolume = preferences.getUChar("master_volume", 100);
  latencyAdjustmentMs = preferences.getUShort("latency_ms", 0);
  audioBufferMs = preferences.getUShort("buffer_ms", 0);
  preferences.end();
}

/** Restores the default network settings in RAM and in NVS. */
void resetNetworkSettingsToDefaults() {
  wifiStaSsid = DEFAULT_WIFI_STA_SSID;
  wifiStaPassword = DEFAULT_WIFI_STA_PASSWORD;
  wifiUseStaticIp = false;
  wifiStaticIp = "";
  wifiStaticGateway = "";
  wifiStaticSubnet = "255.255.255.0";
  wifiStaticDns = "";
  serverName = "Enkelvoud";

  preferences.begin("enkelvoud", false);
  preferences.putString("s_ver", SETTINGS_VERSION);
  preferences.putString("wifi_ssid", wifiStaSsid);
  preferences.putString("wifi_pass", wifiStaPassword);
  preferences.putBool("wifi_static", wifiUseStaticIp);
  preferences.putString("wifi_ip", wifiStaticIp);
  preferences.putString("wifi_gateway", wifiStaticGateway);
  preferences.putString("wifi_subnet", wifiStaticSubnet);
  preferences.putString("wifi_dns", wifiStaticDns);
  preferences.putString("server_name", serverName);
  preferences.end();
  LOGW("Factory reset: default IP settings restored");
}

/** Returns true while GPIO 0 stays connected to GND for the whole hold time. */
bool factoryResetPinHeld(uint32_t holdMs) {
  uint32_t startMs = millis();
  while (millis() - startMs < holdMs) {
    if (digitalRead(PIN_FACTORY_RESET) != LOW) return false;
    delay(25);
  }
  return digitalRead(PIN_FACTORY_RESET) == LOW;
}

String networkSettingsPage() {
  String page = F(
      "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Enkelvoud Network Setup</title><style>"
      "body{font:15px system-ui,sans-serif;margin:0;padding:24px;color:#f6f6f8;background:radial-gradient(circle at top right,#2a1b52,transparent 28rem),#050507}"
      "form{max-width:34rem;margin:5vh auto;padding:26px;border:1px solid #30303a;border-radius:20px;background:linear-gradient(145deg,#1a1a21,#101014);box-shadow:0 24px 60px #0008}"
      "h1,p{max-width:34rem;margin:0 auto}h1{font-size:2rem;letter-spacing:-.05em}p{margin-top:10px;color:#a1a1ad;line-height:1.5}"
      "label,input,button{display:block;width:100%;box-sizing:border-box}label{margin-top:1rem;font-weight:700}input,button{padding:.8rem;font-size:1rem;border-radius:11px}"
      "input{margin-top:7px;color:#f6f6f8;background:#09090c;border:1px solid #343440}button{margin-top:1.5rem;border:0;background:#9d7bff;color:#140b2c;font-weight:800;cursor:pointer}"
      "small{color:#a1a1ad}.checkbox{display:flex;gap:.6rem;align-items:center}.checkbox input{width:auto}.hidden{display:none}"
      "</style></head><body><h1>Enkelvoud Network Setup</h1>"
      "<p>Connect the server to your Wi-Fi network. The access point is available at 192.168.4.1 while setup is required.</p>"
      "<form id='network-settings'><label for='server-name'>Server name</label><input id='server-name' name='server_name' pattern='[A-Za-z0-9]+' required value='");
  page += htmlEscape(serverName);
  page += F("'><small>Letters and numbers only; this becomes the <strong>.local</strong> address.</small>"
      "<label for='ssid'>Wi-Fi SSID</label><input id='ssid' name='ssid' required value='");
  page += htmlEscape(wifiStaSsid);
  page += F("'><label for='password'>Wi-Fi password</label><input id='password' name='password' type='password' value='");
  page += htmlEscape(wifiStaPassword);
  page += F("'><label class='checkbox'><input id='static-ip' name='use_static_ip' type='checkbox' onchange='document.getElementById(\"static-settings\").className=this.checked?\"\":\"hidden\"'");
  if (wifiUseStaticIp) page += F(" checked");
  page += F("> Use a static IP for this Wi-Fi network</label><div id='static-settings' class='");
  page += wifiUseStaticIp ? "" : "hidden";
  page += F("'><label for='ip'>IP address</label><input id='ip' name='ip' value='");
  page += htmlEscape(wifiStaticIp);
  page += F("'><label for='gateway'>Gateway</label><input id='gateway' name='gateway' value='");
  page += htmlEscape(wifiStaticGateway);
  page += F("'><label for='subnet'>Subnet mask</label><input id='subnet' name='subnet' value='");
  page += htmlEscape(wifiStaticSubnet);
  page += F("'><label for='dns'>DNS server</label><input id='dns' name='dns' value='");
  page += htmlEscape(wifiStaticDns);
  page += F("'></div>");
  page += F("<button id='save-button' type='submit'>Save</button></form>"
      "<script>const form=document.getElementById('network-settings'),button=document.getElementById('save-button');"
      "function suggest(){const p=document.getElementById('ip').value.trim().split('.');if(p.length!==4||p.some(v=>!/^\\d+$/.test(v)||+v>255))return;"
      "const base=p.slice(0,3).join('.')+'.1';document.getElementById('gateway').value=base;"
      "document.getElementById('dns').value=base;document.getElementById('subnet').value='255.255.255.0';}"
      "document.getElementById('ip').addEventListener('input',suggest);"
      "form.addEventListener('submit',async e=>{e.preventDefault();button.disabled=true;button.textContent='Testing connection...';"
      "try{const r=await fetch('/save',{method:'POST',body:new URLSearchParams(new FormData(form))}),d=await r.json();"
      "if(!r.ok||!d.ok)throw new Error(d.error||'Unable to connect');let s;do{await new Promise(done=>setTimeout(done,500));s=await (await fetch('/api/network-test')).json();}"
      "while(s.state==='running');if(s.state!=='success')throw new Error(s.error||'Unable to connect');"
      "alert('Success\\n\\nConnected to '+s.ip+'. Settings were saved and the server is restarting.');const host=document.getElementById('server-name').value+'.local';await fetch('/api/restart',{method:'POST'});setTimeout(()=>location.href='http://'+host+'/',1200);}"
      "catch(error){alert('Unable to connect\\n\\n'+error.message+'\\n\\nCheck the SSID, password, and static IP settings, then try again.');button.disabled=false;button.textContent='Test, save and restart';}});"
      "</script></body></html>");
  return page;
}

void handleNetworkSettings(AsyncWebServerRequest *request) {
  request->send(200, "text/html; charset=utf-8", networkSettingsPage());
}

void saveTestedNetworkSettings() {
  serverName = pendingServerName;
  wifiStaSsid = pendingSsid;
  wifiStaPassword = pendingPassword;
  wifiUseStaticIp = pendingUseStaticIp;
  wifiStaticIp = pendingStaticIp;
  wifiStaticGateway = pendingStaticGateway;
  wifiStaticSubnet = pendingStaticSubnet;
  wifiStaticDns = pendingStaticDns;

  preferences.begin("enkelvoud", false);
  preferences.putString("s_ver", SETTINGS_VERSION);
  preferences.putString("wifi_ssid", wifiStaSsid);
  preferences.putString("wifi_pass", wifiStaPassword);
  preferences.putBool("wifi_static", wifiUseStaticIp);
  preferences.putString("wifi_ip", wifiStaticIp);
  preferences.putString("wifi_gateway", wifiStaticGateway);
  preferences.putString("wifi_subnet", wifiStaticSubnet);
  preferences.putString("wifi_dns", wifiStaticDns);
  preferences.putString("server_name", serverName);
  preferences.end();
  LOGI("Network settings saved to NVS (SSID: \"%s\", static IP: %s)",
       wifiStaSsid.c_str(), wifiUseStaticIp ? "enabled" : "disabled");
}

void networkTestTask(void *parameter) {
  (void)parameter;
  IPAddress ip, gateway, subnet, dns;

  WiFi.disconnect(false, false);
  WiFi.mode(accessPointActive ? WIFI_AP_STA : WIFI_STA);
  if (!pendingUseStaticIp) {
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  } else if (!ip.fromString(pendingStaticIp) || !gateway.fromString(pendingStaticGateway) ||
             !subnet.fromString(pendingStaticSubnet) || !dns.fromString(pendingStaticDns) ||
             !WiFi.config(ip, gateway, subnet, dns)) {
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
    if (accessPointActive) WiFi.mode(WIFI_AP);
    networkTestError = "Unable to connect using the supplied Wi-Fi and IP settings";
    networkTestState = NETWORK_TEST_FAILED;
    vTaskDelete(nullptr);
    return;
  }

  networkTestIp = WiFi.localIP().toString();
  saveTestedNetworkSettings();
  networkTestState = NETWORK_TEST_SUCCESS;
  vTaskDelete(nullptr);
}

bool validStaticStationAddress(const IPAddress &ip, const IPAddress &gateway, const IPAddress &subnet) {
  uint32_t address = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
      ((uint32_t)ip[2] << 8) | ip[3];
  uint32_t router = ((uint32_t)gateway[0] << 24) | ((uint32_t)gateway[1] << 16) |
      ((uint32_t)gateway[2] << 8) | gateway[3];
  uint32_t mask = ((uint32_t)subnet[0] << 24) | ((uint32_t)subnet[1] << 16) |
      ((uint32_t)subnet[2] << 8) | subnet[3];
  uint32_t hostMask = ~mask;

  return address != router && address != 0 && address != 0xFFFFFFFF &&
      address != 0xC0A80401 && (address & mask) == (router & mask) &&
      (address & hostMask) != 0 && (address & hostMask) != hostMask;
}

void handleSaveNetworkSettings(AsyncWebServerRequest *request) {
  if (!request->hasParam("ssid", true)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Wi-Fi SSID is required\"}");
    return;
  }

  String newServerName = request->hasParam("server_name", true)
      ? sanitizeServerName(request->getParam("server_name", true)->value())
      : "Enkelvoud";
  String newSsid = request->getParam("ssid", true)->value();
  String newPassword = request->hasParam("password", true)
      ? request->getParam("password", true)->value()
      : "";
  bool newUseStaticIp = request->hasParam("use_static_ip", true);
  String newStaticIp = request->hasParam("ip", true) ? request->getParam("ip", true)->value() : "";
  String newStaticGateway = request->hasParam("gateway", true) ? request->getParam("gateway", true)->value() : "";
  String newStaticSubnet = request->hasParam("subnet", true) ? request->getParam("subnet", true)->value() : "255.255.255.0";
  String newStaticDns = request->hasParam("dns", true) ? request->getParam("dns", true)->value() : "";

  IPAddress ip, gateway, subnet, dns;
  if (newSsid.length() == 0) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Wi-Fi SSID is required\"}");
    return;
  }

  if (newUseStaticIp && (!ip.fromString(newStaticIp) || !gateway.fromString(newStaticGateway) ||
      !subnet.fromString(newStaticSubnet) || !dns.fromString(newStaticDns) ||
      !validStaticStationAddress(ip, gateway, subnet))) {
      request->send(400, "application/json",
                    "{\"ok\":false,\"error\":\"Use a valid unused host IP on the gateway network\"}");
      return;
  }

  if (networkTestState == NETWORK_TEST_RUNNING) {
    request->send(409, "application/json",
                  "{\"ok\":false,\"error\":\"A Wi-Fi connection test is already running\"}");
    return;
  }

  pendingSsid = newSsid;
  pendingPassword = newPassword;
  pendingUseStaticIp = newUseStaticIp;
  pendingStaticIp = newStaticIp;
  pendingStaticGateway = newStaticGateway;
  pendingStaticSubnet = newStaticSubnet;
  pendingStaticDns = newStaticDns;
  pendingServerName = newServerName;
  networkTestIp = "";
  networkTestError = "";
  networkTestState = NETWORK_TEST_RUNNING;
  if (xTaskCreate(networkTestTask, "wifiTest", 4096, nullptr, 1, nullptr) != pdPASS) {
    networkTestError = "Unable to start the Wi-Fi connection test";
    networkTestState = NETWORK_TEST_FAILED;
    request->send(500, "application/json", "{\"ok\":false,\"error\":\"Unable to start the Wi-Fi connection test\"}");
    return;
  }
  request->send(202, "application/json", "{\"ok\":true,\"state\":\"running\"}");
}

void handleNetworkTestStatus(AsyncWebServerRequest *request) {
  const char *state = "idle";
  if (networkTestState == NETWORK_TEST_RUNNING) state = "running";
  else if (networkTestState == NETWORK_TEST_SUCCESS) state = "success";
  else if (networkTestState == NETWORK_TEST_FAILED) state = "failed";

  String json = String("{\"state\":\"") + state + "\"";
  if (networkTestState == NETWORK_TEST_SUCCESS) {
    json += ",\"ip\":\"" + networkTestIp + "\"";
  } else if (networkTestState == NETWORK_TEST_FAILED) {
    json += ",\"error\":\"" + networkTestError + "\"";
  }
  json += "}";
  request->send(200, "application/json", json);
}

void handleRestart(AsyncWebServerRequest *request) {
  request->send(200, "application/json", "{\"ok\":true}");
  delay(200);
  ESP.restart();
}

// ============================================================================
// RESAMPLER 44.1k -> 48k
// ============================================================================
/** Stateful linear stereo resampler from SOURCE_SAMPLE_RATE to OPUS_SAMPLE_RATE. */
class ResamplerToOpusRate {
public:
  /** Resets internal phase and continuity state. */
  void reset() {
    _pos = 0.0;
    _haveLast = false;
  }

  /**
   * Resamples interleaved stereo int16 input into interleaved stereo int16 output.
   * @return number of output sample-frames written.
   */
  uint32_t process(const int16_t *in, uint32_t inFrames, int16_t *out, uint32_t outCapacityFrames) {
    const double ratio = (double)SOURCE_SAMPLE_RATE / (double)OPUS_SAMPLE_RATE;
    uint32_t outCount = 0;

    if (!_haveLast && inFrames > 0) {
      _lastL = in[0];
      _lastR = in[1];
      _haveLast = true;
    }

    while (outCount < outCapacityFrames) {
      double srcIndexF = _pos;
      long idx = (long)srcIndexF;
      if (idx >= (long)inFrames - 1) break;

      double frac = srcIndexF - (double)idx;
      int16_t l0, r0, l1, r1;

      if (idx < 0) {
        l0 = _lastL; r0 = _lastR;
        l1 = in[0];  r1 = in[1];
      } else {
        l0 = in[idx * 2 + 0];
        r0 = in[idx * 2 + 1];
        l1 = in[(idx + 1) * 2 + 0];
        r1 = in[(idx + 1) * 2 + 1];
      }

      out[outCount * 2 + 0] = (int16_t)(l0 + (l1 - l0) * frac);
      out[outCount * 2 + 1] = (int16_t)(r0 + (r1 - r0) * frac);
      outCount++;
      _pos += ratio;
    }

    if (inFrames > 0) {
      _pos -= (double)inFrames;
      _lastL = in[(inFrames - 1) * 2 + 0];
      _lastR = in[(inFrames - 1) * 2 + 1];
    }

    return outCount;
  }

private:
  double _pos = 0.0;
  bool _haveLast = false;
  int16_t _lastL = 0, _lastR = 0;
};

static ResamplerToOpusRate resampler;

// Pending resampled PCM until enough for one Opus frame.
#define PENDING_MAX_FRAMES 4096
static int16_t pendingBuf[PENDING_MAX_FRAMES * 2];
static uint32_t pendingCount = 0;
static uint32_t pendingOldestCapturedMs = 0;

// ============================================================================
// BRIDGE HELPERS
// ============================================================================
/** Escapes quotes/backslashes in strings for safe embedding in JSON literal text. */
String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\' || c == '\"') out += '\\';
    out += c;
  }
  return out;
}

uint32_t oggCrc(const uint8_t *data, size_t length) {
    uint32_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
      crc ^= (uint32_t)data[i] << 24;
      for (uint8_t bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x80000000) ? (crc << 1) ^ 0x04C11DB7 : crc << 1;
      }
    }
    return crc;
  }

void writeOggPage(AsyncResponseStream *response, const uint8_t *packet, size_t packetLength,
                    uint32_t serial, uint32_t sequence, uint64_t granulePosition, uint8_t headerType) {
    const size_t segmentCount = (packetLength / 255) + 1;
    uint8_t page[27 + (OPUS_MAX_PACKET_BYTES / 255) + 2 + OPUS_MAX_PACKET_BYTES];
    memcpy(page, "OggS", 4);
    page[4] = 0;
    page[5] = headerType;
    for (uint8_t i = 0; i < 8; ++i) page[6 + i] = (granulePosition >> (i * 8)) & 0xFF;
    for (uint8_t i = 0; i < 4; ++i) page[14 + i] = (serial >> (i * 8)) & 0xFF;
    for (uint8_t i = 0; i < 4; ++i) page[18 + i] = (sequence >> (i * 8)) & 0xFF;
    memset(page + 22, 0, 4);
    page[26] = segmentCount;
    size_t remaining = packetLength;
    for (size_t i = 0; i < segmentCount; ++i) {
      page[27 + i] = remaining >= 255 ? 255 : remaining;
      remaining -= page[27 + i];
    }
    memcpy(page + 27 + segmentCount, packet, packetLength);
    size_t pageLength = 27 + segmentCount + packetLength;
    uint32_t crc = oggCrc(page, pageLength);
    for (uint8_t i = 0; i < 4; ++i) page[22 + i] = (crc >> (i * 8)) & 0xFF;
    response->write(page, pageLength);
  }

void storeOggTestPacket(const OpusPacket &packet) {
    if (packet.len <= 0 || packet.len > OGG_TEST_MAX_PACKET_BYTES) {
      LOGW("Ogg test packet skipped: %d bytes exceeds %d-byte history slot",
           packet.len, OGG_TEST_MAX_PACKET_BYTES);
      return;
    }

    portENTER_CRITICAL(&oggTestMux);
    memcpy(oggTestPackets[oggTestWriteIndex].data, packet.data, packet.len);
    oggTestPackets[oggTestWriteIndex].len = packet.len;
    oggTestWriteIndex = (oggTestWriteIndex + 1) % OGG_TEST_PACKET_COUNT;
    if (oggTestPacketCount < OGG_TEST_PACKET_COUNT) ++oggTestPacketCount;
    portEXIT_CRITICAL(&oggTestMux);
  }

void handleOggTestStream(AsyncWebServerRequest *request) {
    uint8_t count;
    uint8_t start;
    portENTER_CRITICAL(&oggTestMux);
    count = oggTestPacketCount;
    start = (oggTestWriteIndex + OGG_TEST_PACKET_COUNT - count) % OGG_TEST_PACKET_COUNT;
    portEXIT_CRITICAL(&oggTestMux);

    if (count == 0) {
      request->send(503, "application/json", "{\"ok\":false,\"error\":\"No encoded audio is available yet\"}");
      return;
    }

    AsyncResponseStream *response = request->beginResponseStream("audio/ogg");
    response->addHeader("Cache-Control", "no-store");
    const uint8_t opusHead[] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 2, 0, 0,
                                0x80, 0xBB, 0, 0, 0, 0, 0};
    const uint8_t opusTags[] = {'O', 'p', 'u', 's', 'T', 'a', 'g', 's', 0, 0, 0, 0, 0, 0, 0, 0};
    const uint32_t serial = 0x454E4B4C;
    writeOggPage(response, opusHead, sizeof(opusHead), serial, 0, 0, 0x02);
    writeOggPage(response, opusTags, sizeof(opusTags), serial, 1, 0, 0);
    for (uint8_t i = 0; i < count; ++i) {
      OggTestPacket packet;
      portENTER_CRITICAL(&oggTestMux);
      packet = oggTestPackets[(start + i) % OGG_TEST_PACKET_COUNT];
      portEXIT_CRITICAL(&oggTestMux);
      writeOggPage(response, packet.data, packet.len, serial, i + 2,
                   (uint64_t)(i + 1) * OPUS_FRAME_SAMPLES, i + 1 == count ? 0x04 : 0);
    }
    request->send(response);
}

/** Sends source selection to the receiver over its dedicated UART control link. */
bool bridgeSetReceiverSource(const String &source, String &respBody, uint16_t &httpCode) {
  sendReceiverSourceCommand(source);
  receiverSource = source;
  lastReceiverSource = source;
  httpCode = 200;
  respBody = String("{\"ok\":true,\"source\":\"") + source + "\",\"transport\":\"uart\"}";
  return true;
}

/** Sends a transport command to the receiver over its UART control link. */
bool bridgeSendReceiverCmd(const String &cmd, String &respBody, uint16_t &httpCode) {
  sendReceiverCommand(cmd);
  httpCode = 200;
  respBody = String("{\"ok\":true,\"cmd\":\"") + cmd + "\",\"transport\":\"uart\"}";
  return true;
}

// ============================================================================
// TASKS
// ============================================================================
/** Reads PCM from I2S RX continuously and enqueues chunks for processing. */
void i2sReadTask(void *param) {
  LOGI("i2sReadTask started on core %d", xPortGetCoreID());
  static uint8_t i2sBuf[I2S_READ_CHUNK_BYTES];

  for (;;) {
    size_t bytesRead = 0;
    esp_err_t res = i2s_read(I2S_PORT_IN, i2sBuf, sizeof(i2sBuf), &bytesRead, portMAX_DELAY);

    if (res != ESP_OK) {
      statI2sReadErrors++;
      LOGW("i2s_read() error %d", (int)res);
      continue;
    }
    if (bytesRead == 0) continue;

    statI2sBytesIn += bytesRead;
    statI2sBytesTotal += bytesRead;

    RawChunk *chunk = nullptr;
    if (xQueueReceive(rawFreeQueue, &chunk, 0) != pdTRUE || !chunk) {
      statRawDropped++;
      continue;
    }

    memcpy(chunk->data, i2sBuf, bytesRead);
    chunk->len = bytesRead;
    chunk->captured_ms = millis();

    if (xQueueSend(rawQueue, &chunk, 0) != pdTRUE) {
      xQueueSend(rawFreeQueue, &chunk, portMAX_DELAY);
      statRawDropped++;
    }
  }
}

/** Converts raw PCM to Opus by resampling and frame encoding. */
void audioProcessingTask(void *param) {
  LOGI("audioProcessingTask started on core %d", xPortGetCoreID());
  static int16_t resampledScratch[8192];
  RawChunk *chunk = nullptr;

  for (;;) {
    if (xQueueReceive(rawQueue, &chunk, portMAX_DELAY) != pdTRUE) continue;
    if (!chunk) continue;

    uint32_t inFrames = chunk->len / (2 * sizeof(int16_t));
    const int16_t *inSamples = (const int16_t *)chunk->data;

    if (pendingCount == 0) pendingOldestCapturedMs = chunk->captured_ms;

    uint32_t produced = resampler.process(
      inSamples, inFrames,
      resampledScratch,
      sizeof(resampledScratch) / (2 * sizeof(int16_t))
    );

    for (uint32_t i = 0; i < produced && pendingCount < PENDING_MAX_FRAMES; i++) {
      pendingBuf[pendingCount * 2 + 0] = resampledScratch[i * 2 + 0];
      pendingBuf[pendingCount * 2 + 1] = resampledScratch[i * 2 + 1];
      pendingCount++;
    }

    xQueueSend(rawFreeQueue, &chunk, portMAX_DELAY);

    while (pendingCount >= OPUS_FRAME_SAMPLES) {
      OpusPacket pkt;
      pkt.captured_ms = pendingOldestCapturedMs;

      if (masterVolume < 100) {
        for (uint32_t i = 0; i < OPUS_FRAME_SAMPLES * CHANNELS; ++i) {
          pendingBuf[i] = (int16_t)((int32_t)pendingBuf[i] * masterVolume / 100);
        }
      }

      // Send to local DAC (if not muted). Bounded wait: a stalled or slow
      // local DAC must never be able to block Opus encoding / streaming to
      // remote players, which happens right after this in the same task.
      if (!masterMuted) {
        size_t bytes_written = 0;
        esp_err_t werr = i2s_write(I2S_PORT_OUT, pendingBuf,
                                    OPUS_FRAME_SAMPLES * CHANNELS * sizeof(int16_t),
                                    &bytes_written, pdMS_TO_TICKS(LOCAL_DAC_WRITE_TIMEOUT_MS));
        if (werr != ESP_OK || bytes_written == 0) {
          statLocalDacStalls++;
        }
      }

      int nbytes = opus_encode(opusEncoder, pendingBuf, OPUS_FRAME_SAMPLES, pkt.data, OPUS_MAX_PACKET_BYTES);
      if (nbytes < 0) {
        LOGE("opus_encode() failed, error code %d", nbytes);
      } else {
        pkt.len = nbytes;
        if (xQueueSend(opusQueue, &pkt, 0) != pdTRUE) {
          statOpusDropped++;
        } else {
          statOpusEncoded++;
        }
      }

      uint32_t remaining = pendingCount - OPUS_FRAME_SAMPLES;
      memmove(pendingBuf, pendingBuf + OPUS_FRAME_SAMPLES * 2, remaining * 2 * sizeof(int16_t));
      pendingCount = remaining;
      pendingOldestCapturedMs += 20;
    }
  }
}

/** Sends encoded Opus packets to all connected websocket clients. */
void wsSendTask(void *param) {
  LOGI("wsSendTask started on core %d", xPortGetCoreID());
  OpusPacket pkt;

  for (;;) {
    if (xQueueReceive(opusQueue, &pkt, portMAX_DELAY) != pdTRUE) continue;

    storeOggTestPacket(pkt);
    if (wsClientCount > 0 && !masterMuted) {
      uint32_t targetDelayMs = (uint32_t)audioBufferMs + (uint32_t)latencyAdjustmentMs;
      if (targetDelayMs > 0) {
        int32_t remainingMs = (int32_t)((pkt.captured_ms + targetDelayMs) - millis());
        if (remainingMs > 0) vTaskDelay(pdMS_TO_TICKS((uint32_t)remainingMs));
      }

      // Send per-client instead of ws.binaryAll(): a client that can't keep
      // up (weak Wi-Fi, its own decode task falling behind, etc.) would
      // otherwise get an ever-growing backlog queued for it in heap, which
      // eventually starves malloc() everywhere else in this pipeline
      // (including i2sReadTask's per-chunk allocation) and takes local
      // playback down with it. Skipping a backed-up client drops audio for
      // that client only, instead of memory for everyone.
      for (AsyncWebSocketClient &client : ws.getClients()) {
        if (client.status() != WS_CONNECTED) continue;
        if (client.queueIsFull()) {
          statWsClientsBackpressured++;
          continue;
        }
        client.binary(pkt.data, pkt.len);
      }
      statWsPacketsSent++;
      statWsBytesSent += pkt.len;

      uint32_t latency = millis() - pkt.captured_ms;
      statLatencySumMs += latency;
      statLatencyCount++;
    }
  }
}

// ============================================================================
// WEBSOCKET + HTTP HANDLERS
// ============================================================================
/** Handles websocket connect/disconnect/error/data events for /audio endpoint. */
void onWsEvent(AsyncWebSocket *serverPtr, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)serverPtr;
  (void)arg;
  (void)data;

  switch (type) {
    case WS_EVT_CONNECT:
      wsClientCount++;
      LOGI("WS client #%u CONNECTED from %s (WS clients: %d)",
           client->id(), client->remoteIP().toString().c_str(), wsClientCount);
      break;

    case WS_EVT_DISCONNECT:
      wsClientCount = max(0, wsClientCount - 1);
      LOGI("WS client #%u DISCONNECTED (WS clients: %d)", client->id(), wsClientCount);
      break;

    case WS_EVT_ERROR:
      LOGE("WS client #%u error", client->id());
      break;

    case WS_EVT_DATA:
      LOGI("WS client #%u sent %u bytes (ignored - TX only endpoint)", client->id(), (unsigned)len);
      break;

    default:
      break;
  }
}

/** Serves JSON status with pipeline, network, and bridge counters. */
void handleApiStatus(AsyncWebServerRequest *request) {
  pruneStalePlayers(millis());

  float avgLatency = statLatencyCount > 0
      ? (float)statLatencySumMs / (float)statLatencyCount
      : 0.0f;

  String json = "{";
  json += "\"ok\":true,";
  IPAddress ip = activeIpAddress();
  json += "\"ip\":\"" + ip.toString() + "\",";
  json += "\"wsUrl\":\"ws://" + ip.toString() + "/audio\",";
  json += "\"wifiConnected\":" + String((WiFi.status() == WL_CONNECTED) ? "true" : "false") + ",";
  json += "\"wifiRssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"wifiSsid\":\"" + jsonEscape(wifiStaSsid) + "\",";
  json += "\"wifiPassword\":\"" + jsonEscape(wifiStaPassword) + "\",";
  json += "\"wifiUseStaticIp\":" + String(wifiUseStaticIp ? "true" : "false") + ",";
  json += "\"wifiStaticIp\":\"" + jsonEscape(wifiStaticIp) + "\",";
  json += "\"wifiStaticGateway\":\"" + jsonEscape(wifiStaticGateway) + "\",";
  json += "\"wifiStaticSubnet\":\"" + jsonEscape(wifiStaticSubnet) + "\",";
  json += "\"wifiStaticDns\":\"" + jsonEscape(wifiStaticDns) + "\",";
  json += "\"serverName\":\"" + jsonEscape(serverName) + "\",";
  json += "\"masterMuted\":" + String(masterMuted ? "true" : "false") + ",";
  json += "\"masterVolume\":" + String(masterVolume) + ",";
  json += "\"latencyAdjustmentMs\":" + String(latencyAdjustmentMs) + ",";
  json += "\"audioBufferMs\":" + String(audioBufferMs) + ",";
  json += "\"wsClients\":" + String(wsClientCount) + ",";
  json += "\"rawQueueDepth\":" + String(rawQueue ? uxQueueMessagesWaiting(rawQueue) : 0) + ",";
  json += "\"rawPoolFree\":" + String(rawFreeQueue ? uxQueueMessagesWaiting(rawFreeQueue) : 0) + ",";
  json += "\"opusQueueDepth\":" + String(opusQueue ? uxQueueMessagesWaiting(opusQueue) : 0) + ",";
  json += "\"i2sBytesIn\":" + String(statI2sBytesIn) + ",";
  json += "\"i2sReadErrors\":" + String(statI2sReadErrors) + ",";
  json += "\"localDacStalls\":" + String(statLocalDacStalls) + ",";
  json += "\"rawDropped\":" + String(statRawDropped) + ",";
  json += "\"opusEncoded\":" + String(statOpusEncoded) + ",";
  json += "\"opusDropped\":" + String(statOpusDropped) + ",";
  json += "\"wsPacketsSent\":" + String(statWsPacketsSent) + ",";
  json += "\"wsBytesSent\":" + String(statWsBytesSent) + ",";
  json += "\"wsBackpressured\":" + String(statWsClientsBackpressured) + ",";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"minFreeHeap\":" + String(ESP.getMinFreeHeap()) + ",";
  json += "\"streaming\":{\"i2sBytesLast5s\":" + String(statI2sBytesLast5s) + ",";
  json += "\"i2sBytesTotal\":" + String((uint32_t)statI2sBytesTotal) + ",";
  json += "\"rawDroppedLast5s\":" + String(statRawDroppedLast5s) + ",";
  json += "\"opusEncodedLast5s\":" + String(statOpusEncodedLast5s) + ",";
  json += "\"opusDroppedLast5s\":" + String(statOpusDroppedLast5s) + ",";
  json += "\"wsPacketsLast5s\":" + String(statWsPacketsLast5s) + ",";
  json += "\"wsBytesLast5s\":" + String(statWsBytesLast5s) + "},";
  json += "\"avgLatencyMs\":" + String(avgLatency, 2) + ",";
  json += "\"receiverHost\":\"" + String(RECEIVER_HOST) + "\",";
  json += "\"receiverPort\":" + String(RECEIVER_PORT) + ",";
  json += "\"bridgeLastHttp\":" + String(lastBridgeHttpCode) + ",";
  json += "\"bridgeLastSource\":\"" + jsonEscape(lastReceiverSource) + "\",";
  json += "\"bridgeLastMsg\":\"" + jsonEscape(lastBridgeMsg) + "\",";
  json += "\"receiverAudio\":{\"source\":\"" + jsonEscape(receiverSource) + "\",";
  json += "\"state\":\"" + jsonEscape(receiverState) + "\",";
  json += "\"rate\":" + String(receiverSampleRate) + ",";
  json += "\"channels\":" + String(receiverChannels) + ",";
  json += "\"bits\":" + String(receiverBits) + ",";
  json += "\"bytes\":" + String(receiverBytes) + ",";
  json += "\"frames\":" + String(receiverFrames) + ",";
  json += "\"errors\":" + String(receiverErrors) + "},";
  json += "\"players\":[";
  for (size_t i = 0; i < playerNodes.size(); ++i) {
    if (i) json += ",";
    json += "{\"name\":\"" + jsonEscape(playerNodes[i].name) + "\",\"ip\":\"" +
        jsonEscape(playerNodes[i].ip) + "\"}";
  }
  json += "]";
  json += "}";

  request->send(200, "application/json", json);
}

void handlePlayerRegistration(AsyncWebServerRequest *request) {
  pruneStalePlayers(millis());

  String ip = request->client()->remoteIP().toString();
  for (auto &player : playerNodes) {
    if (player.ip == ip) {
      player.lastSeenMs = millis();
      request->send(200, "application/json", "{\"ok\":true}");
      return;
    }
  }
  playerNodes.push_back({"Player", ip, millis()});
  LOGI("Player registered from %s", ip.c_str());
  request->send(200, "application/json", "{\"ok\":true}");
}

/** Handles POST /api/source by forwarding desired source to EnkelvoudReceiver. */
void handleApiSourceBridge(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  (void)index;
  (void)total;

  String source = "";
  if (len > 0 && data) {
    DynamicJsonDocument in(256);
    DeserializationError err = deserializeJson(in, data, len);
    if (!err && in["source"].is<const char*>()) {
      source = String((const char*)in["source"]);
    }
  }

  source.toUpperCase();
  if (source != "BT") {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Only Bluetooth is enabled during testing\"}");
    return;
  }

  String resp;
  uint16_t code = 0;
  bool ok = bridgeSetReceiverSource(source, resp, code);

  lastReceiverSource = source;
  lastBridgeHttpCode = code;
  lastBridgeMsg = resp;

  if (ok) {
    request->send(200, "application/json", resp);
  } else {
    String out = String("{\"ok\":false,\"bridgeHttp\":") + String(code) + ",\"receiver\":" + resp + "}";
    request->send(502, "application/json", out);
  }
}

/** Handles POST /api/cmd by forwarding command to EnkelvoudReceiver. */
void handleApiCmdBridge(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  (void)index;
  (void)total;

  String cmd = "";
  if (len > 0 && data) {
    DynamicJsonDocument in(256);
    DeserializationError err = deserializeJson(in, data, len);
    if (!err && in["cmd"].is<const char*>()) {
      cmd = String((const char*)in["cmd"]);
    }
  }

  cmd.toLowerCase();
  if (!(cmd == "next" || cmd == "prev" || cmd == "bt" || cmd == "aux" || cmd == "usb")) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid cmd\"}");
    return;
  }

  String resp;
  uint16_t code = 0;
  bool ok = bridgeSendReceiverCmd(cmd, resp, code);

  lastBridgeHttpCode = code;
  lastBridgeMsg = resp;

  if (ok) {
    request->send(200, "application/json", resp);
  } else {
    String out = String("{\"ok\":false,\"bridgeHttp\":") + String(code) + ",\"receiver\":" + resp + "}";
    request->send(502, "application/json", out);
  }
}

void handleApiConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  (void)index;
  if (len != total) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Incomplete request body\"}");
    return;
  }

  DynamicJsonDocument input(256);
  if (deserializeJson(input, data, len)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }

  bool changed = false;
  if (input.containsKey("masterMuted") && input["masterMuted"].is<bool>()) {
    masterMuted = input["masterMuted"];
    changed = true;
  }
  if (input.containsKey("masterVolume") && input["masterVolume"].is<int>()) {
    int value = input["masterVolume"];
    if (value < 0 || value > 100) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Volume must be between 0 and 100\"}");
      return;
    }
    masterVolume = value;
    changed = true;
  }
  if (input.containsKey("latencyAdjustmentMs") && input["latencyAdjustmentMs"].is<int>()) {
    int value = input["latencyAdjustmentMs"];
    if (value < 0 || value > 200) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Latency must be between 0 and 200 ms\"}");
      return;
    }
    latencyAdjustmentMs = value;
    changed = true;
  }
  if (input.containsKey("audioBufferMs") && input["audioBufferMs"].is<int>()) {
    int value = input["audioBufferMs"];
    if (value < 0 || value > 200) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Buffer must be between 0 and 200 ms\"}");
      return;
    }
    audioBufferMs = value;
    changed = true;
  }
  if (input.containsKey("serverName") && input["serverName"].is<const char*>()) {
    String value = input["serverName"];
    if (value.length() == 0 || value.length() > 32 || value != sanitizeServerName(value)) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Server name must use 1-32 letters or numbers\"}");
      return;
    }
    serverName = value;
    changed = true;
  }
  if (!changed) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"No valid settings were supplied\"}");
    return;
  }

  preferences.begin("enkelvoud", false);
  preferences.putString("server_name", serverName);
  preferences.putBool("master_muted", masterMuted);
  preferences.putUChar("master_volume", masterVolume);
  preferences.putUShort("latency_ms", latencyAdjustmentMs);
  preferences.putUShort("buffer_ms", audioBufferMs);
  preferences.end();
  request->send(200, "application/json", "{\"ok\":true}");
}

// ============================================================================
// SETUP HELPERS
// ============================================================================
/** Initializes serial logging output and prints boot banner. */
void setup_serial() {
  Serial.begin(LOG_BAUD);
  delay(300);
  LOGI("=========================================================");
  LOGI("EnkelvoudServer ESP32-S3 I2S->Opus->WebSocket booting");
  LOGI("=========================================================");
}

/** Starts the setup access point on 192.168.4.1. */
bool startSetupAccessPoint() {
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  if (!WiFi.softAPConfig(ap_local_ip, ap_gateway, ap_subnet)) {
    LOGE("Failed to configure setup access point");
  }
  if (!WiFi.softAP(default_ap_ssid, default_ap_password)) {
    LOGE("Failed to start setup access point");
    accessPointActive = false;
    return false;
  }
  accessPointActive = true;
  LOGI("Setup AP \"%s\" available at %s",
       default_ap_ssid, WiFi.softAPIP().toString().c_str());
  return true;
}

/** Connects to Wi-Fi through DHCP, or keeps a setup access point available. */
void setup_wifi_sta() {
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

  IPAddress ip   = WiFi.localIP();
  IPAddress gw   = WiFi.gatewayIP();
  IPAddress mask = WiFi.subnetMask();
  IPAddress dns  = WiFi.dnsIP();

  LOGI("WiFi connected");
  LOGI("  SSID          : %s", wifiStaSsid.c_str());
  LOGI("  Channel       : %d", WiFi.channel());
  LOGI("  RSSI          : %d dBm", WiFi.RSSI());
  LOGI("  Device IP     : %s", ip.toString().c_str());
  LOGI("  Gateway       : %s", gw.toString().c_str());
  LOGI("  Subnet mask   : %s", mask.toString().c_str());
  LOGI("  DNS server    : %s", dns.toString().c_str());
  LOGI("  MAC addr      : %s", WiFi.macAddress().c_str());
  LOGI("  WebSocket URL : ws://%s/audio", ip.toString().c_str());

  WiFi.onEvent([](WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      LOGW("WiFi disconnected from router - reconnecting...");
      WiFi.reconnect();
    }
  });
}

/** Creates and configures the Opus encoder used by audioProcessingTask. */
void setup_opus() {
  LOGI("Initializing Opus encoder (%d Hz, %d ch, %d bps)...",
       OPUS_SAMPLE_RATE, CHANNELS, OPUS_BITRATE);

  int err = 0;
  opusEncoder = opus_encoder_create(OPUS_SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &err);
  if (err != OPUS_OK || opusEncoder == nullptr) {
    LOGE("opus_encoder_create() FAILED, error %d", err);
    return;
  }

  opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(OPUS_BITRATE));
  opus_encoder_ctl(opusEncoder, OPUS_SET_VBR(0));
  opus_encoder_ctl(opusEncoder, OPUS_SET_COMPLEXITY(5));
  opus_encoder_ctl(opusEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));

  LOGI("Opus encoder ready (frame = %d samples / %d ms)",
       OPUS_FRAME_SAMPLES, (OPUS_FRAME_SAMPLES * 1000) / OPUS_SAMPLE_RATE);
}

/** Configures HTTP routes and websocket endpoint, then starts async web server. */
void setup_websocket_server() {
  LOGI("Configuring Async HTTP/WebSocket server...");

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // "/" control page + /api/status, /api/config, /api/source, /api/cmd, /api/restart
  evdctrlRegisterRoutes(server);

  server.on("/settings", HTTP_GET, handleNetworkSettings);
  server.on("/save", HTTP_POST, handleSaveNetworkSettings);
  server.on("/api/network-test", HTTP_GET, handleNetworkTestStatus);

  server.on("/player", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html; charset=utf-8", EVDPLR_HTML);
  });

  server.on("/stream.ogg", HTTP_GET, handleOggTestStream);
  server.on("/api/player/register", HTTP_POST, handlePlayerRegistration);

  server.on("/api/discover", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"name\":\"" + String(serverName) + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"hostname\":\"" + String(serverName) + ".local\"}";
    request->send(200, "application/json", json);
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
  });

  server.begin();
  LOGI("HTTP/WebSocket server started on port 80");
}

/** Sets up I2S peripheral in SLAVE RX mode to receive upstream ESP32 PCM. */
void setup_i2s_in() {
  LOGI("Configuring I2S input (receiving audio from upstream ESP32)...");

  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_SLAVE | I2S_MODE_RX),
      .sample_rate = SOURCE_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0
  };

  esp_err_t err = i2s_driver_install(I2S_PORT_IN, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    LOGE("i2s_driver_install() FAILED, error %d", (int)err);
    return;
  }

  i2s_pin_config_t pin_config = {
      .mck_io_num   = I2S_PIN_NO_CHANGE,
      .bck_io_num   = pinBtBclk,
      .ws_io_num    = pinBtLrc,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num  = pinBtDin
  };

  err = i2s_set_pin(I2S_PORT_IN, &pin_config);
  if (err != ESP_OK) {
    LOGE("i2s_set_pin() FAILED, error %d", (int)err);
    return;
  }

  LOGI("  I2S pins -> BCLK:%d  LRC/WS:%d  DIN(DATA IN):%d", pinBtBclk, pinBtLrc, pinBtDin);
  LOGI("  I2S mode  -> SLAVE / RX, %d Hz, 16-bit, stereo", SOURCE_SAMPLE_RATE);
  LOGI("I2S input ready, waiting for clock/data from upstream ESP32...");
}

/** Sets up I2S peripheral in MASTER TX mode to send audio to local DAC. */
void setup_i2s_out() {
  LOGI("Configuring I2S output (sending audio to local DAC)...");

  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = OPUS_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0
  };

  esp_err_t err = i2s_driver_install(I2S_PORT_OUT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    LOGE("i2s_driver_install() FAILED for output, error %d", (int)err);
    return;
  }

  i2s_pin_config_t pin_config = {
      .mck_io_num   = I2S_PIN_NO_CHANGE,
      .bck_io_num   = pinDacBclk,
      .ws_io_num    = pinDacLrc,
      .data_out_num = pinDacDout,
      .data_in_num  = I2S_PIN_NO_CHANGE
  };

  err = i2s_set_pin(I2S_PORT_OUT, &pin_config);
  if (err != ESP_OK) {
    LOGE("i2s_set_pin() FAILED for output, error %d", (int)err);
    return;
  }

  LOGI("  I2S pins -> BCLK:%d  LRC/WS:%d  DOUT(DATA OUT):%d", pinDacBclk, pinDacLrc, pinDacDout);
  LOGI("  I2S mode  -> MASTER / TX, %d Hz, 16-bit, stereo", OPUS_SAMPLE_RATE);
  LOGI("I2S output ready, audio will be sent to local DAC...");
}

/** Allocates queues and starts I2S read, processing, and websocket sender tasks. */
void setup_queues_and_tasks() {
  LOGI("Creating queues and background tasks...");

  rawQueue = xQueueCreate(RAW_QUEUE_LEN, sizeof(RawChunk *));
  rawFreeQueue = xQueueCreate(RAW_CHUNK_POOL_LEN, sizeof(RawChunk *));
  opusQueue = xQueueCreate(OPUS_QUEUE_LEN, sizeof(OpusPacket));

  if (!rawQueue || !rawFreeQueue || !opusQueue) {
    LOGE("Failed to create one or more queues!");
    return;
  }

  for (size_t i = 0; i < RAW_CHUNK_POOL_LEN; ++i) {
    RawChunk *chunk = &rawChunkPool[i];
    if (xQueueSend(rawFreeQueue, &chunk, 0) != pdTRUE) {
      LOGE("Failed to seed raw chunk pool");
      return;
    }
  }

  xTaskCreatePinnedToCore(i2sReadTask,         "i2sRead",   4096, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(audioProcessingTask, "audioProc", 8192, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(wsSendTask,          "wsSend",    4096, nullptr, 2, nullptr, 1);

  LOGI("Tasks created: i2sReadTask, audioProcessingTask, wsSendTask");
}

/** Arduino setup entrypoint initializing network, codec pipeline, and workers. */
void setup() {
  setup_serial();
  pinMode(PIN_FACTORY_RESET, INPUT_PULLUP);
  loadWiFiSettings();

  if (digitalRead(PIN_FACTORY_RESET) == LOW && factoryResetPinHeld(FACTORY_RESET_HOLD_MS)) {
    resetNetworkSettingsToDefaults();
    startSetupAccessPoint();
  } else {
    setup_wifi_sta();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin(serverName.c_str())) {
      MDNS.addService("http", "tcp", 80);
      MDNS.addService("enkelsrv", "tcp", 80);
      LOGI("mDNS available at http://%s.local/", serverName.c_str());
    } else {
      LOGW("mDNS startup failed");
    }
  }
  setup_opus();
  setup_websocket_server();
  setupReceiverLink();
  sendReceiverSourceCommand(lastReceiverSource);
  setup_i2s_in();
  setup_i2s_out();
  setup_queues_and_tasks();

  LOGI("=========================================================");
  LOGI("Setup complete. Waiting for I2S audio + WS clients...");
  LOGI("=========================================================");
}

// ============================================================================
// LOOP
// ============================================================================
unsigned long lastStatsMs = 0;

/**
 * Restores the default IP settings and reboots into AP mode when GPIO 0 is
 * held to GND for FACTORY_RESET_HOLD_MS.
 */
void handleFactoryResetPin() {
  static unsigned long pinLowSinceMs = 0;

  if (digitalRead(PIN_FACTORY_RESET) != LOW) {
    pinLowSinceMs = 0;
    return;
  }

  unsigned long now = millis();
  if (pinLowSinceMs == 0) {
    pinLowSinceMs = now;
    return;
  }

  if (now - pinLowSinceMs < FACTORY_RESET_HOLD_MS) return;

  resetNetworkSettingsToDefaults();
  LOGW("Factory reset: restarting into AP mode at %s", ap_local_ip.toString().c_str());
  delay(200);
  ESP.restart();
}

/** Arduino loop for periodic stats and websocket housekeeping. */
void loop() {
  static uint32_t lastRawDroppedTotal = 0;
  static uint32_t lastOpusEncodedTotal = 0;
  static uint32_t lastOpusDroppedTotal = 0;

  unsigned long now = millis();
  if (now - lastStatsMs >= 5000) {
    lastStatsMs = now;
    pruneStalePlayers(now);

    uint32_t rawDroppedDelta = statRawDropped - lastRawDroppedTotal;
    uint32_t opusEncodedDelta = statOpusEncoded - lastOpusEncodedTotal;
    uint32_t opusDroppedDelta = statOpusDropped - lastOpusDroppedTotal;
    lastRawDroppedTotal = statRawDropped;
    lastOpusEncodedTotal = statOpusEncoded;
    lastOpusDroppedTotal = statOpusDropped;

    float avgLatency = statLatencyCount > 0
        ? (float)statLatencySumMs / (float)statLatencyCount
        : 0.0f;
    float kbps = (statWsBytesSent * 8.0f / 1000.0f) / 5.0f;

    bool wifiUp = (WiFi.status() == WL_CONNECTED);
    IPAddress devIp = activeIpAddress();
    IPAddress devGateway = wifiUp ? WiFi.gatewayIP() : ap_gateway;

    LOGI("---- STATS (last 5s) ----------------------------------");
    LOGI("WiFi status        : %s (RSSI: %d dBm)",
         wifiUp ? "CONNECTED" : (accessPointActive ? "AP MODE" : "DISCONNECTED"), WiFi.RSSI());
    LOGI("Device IP addr     : %s  | Gateway: %s", devIp.toString().c_str(), devGateway.toString().c_str());
    LOGI("WebSocket URL      : ws://%s/audio", devIp.toString().c_str());
    LOGI("Queue depth        : raw=%u/%u  rawFree=%u/%u  opus=%u/%u",
             rawQueue ? (unsigned)uxQueueMessagesWaiting(rawQueue) : 0U, RAW_QUEUE_LEN,
             rawFreeQueue ? (unsigned)uxQueueMessagesWaiting(rawFreeQueue) : 0U, RAW_CHUNK_POOL_LEN,
             opusQueue ? (unsigned)uxQueueMessagesWaiting(opusQueue) : 0U, OPUS_QUEUE_LEN);
    LOGI("I2S bytes in       : %u  | read errors: %u  | local DAC stalls: %u", statI2sBytesIn, statI2sReadErrors, statLocalDacStalls);
    LOGI("WS backpressured   : %u packets skipped total for slow clients", statWsClientsBackpressured);
    LOGI("Raw chunks dropped : %u", rawDroppedDelta);
    LOGI("Opus frames encoded: %u  | dropped: %u", opusEncodedDelta, opusDroppedDelta);
    LOGI("WS clients         : %d", wsClientCount);
    LOGI("WS packets sent    : %u  | bytes: %u (%.1f kbps)", statWsPacketsSent, statWsBytesSent, kbps);
    LOGI("Avg latency (capture->send): %.1f ms", avgLatency);
    LOGI("Bridge -> host:%s code:%u src:%s", RECEIVER_HOST, lastBridgeHttpCode, lastReceiverSource.c_str());
    LOGI("Free heap          : %u bytes (min: %u)", ESP.getFreeHeap(), ESP.getMinFreeHeap());
    LOGI("---------------------------------------------------------");

    statI2sBytesLast5s = statI2sBytesIn;
    statRawDroppedLast5s = rawDroppedDelta;
    statOpusEncodedLast5s = opusEncodedDelta;
    statOpusDroppedLast5s = opusDroppedDelta;
    statWsPacketsLast5s = statWsPacketsSent;
    statWsBytesLast5s = statWsBytesSent;
    statI2sBytesIn = 0;
    statWsPacketsSent = 0;
    statWsBytesSent = 0;
    statLatencySumMs = 0;
    statLatencyCount = 0;
  }

  handleFactoryResetPin();
  ws.cleanupClients();
  handleReceiverLink();
  delay(10);
}