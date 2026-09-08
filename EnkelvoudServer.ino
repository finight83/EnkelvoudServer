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
    - HTTP GET /            -> EVDCTRL_HTML
    - HTTP GET /player      -> EVDPLR_HTML
    - HTTP GET /api/status  -> JSON status
    - POST /api/source      -> bridge source selection to upstream receiver
    - POST /api/cmd         -> bridge command (next/prev/bt/aux/usb) upstream

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
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>
#include "opus.h"

#include "EVDCTRL.h"
#include "EVDPLR.h"

// ----------------------------------------------------------------------------
// PIN CONFIG - I2S INPUT pins (from upstream ESP32 source board)
// ----------------------------------------------------------------------------
int pinBtLrc  = 15;   // I2S Word Select / LRCLK (input)
int pinBtDin  = 16;   // I2S Data IN (input)
int pinBtBclk = 17;   // I2S Bit Clock (input)

// ----------------------------------------------------------------------------
// WIFI / SERVER CONFIG
// ----------------------------------------------------------------------------
const char *WIFI_STA_SSID      = "-";
const char *WIFI_STA_PASSWORD  = "nopassword";
#define WIFI_CONNECT_TIMEOUT_MS 20000

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

#define I2S_PORT              I2S_NUM_0
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

// ----------------------------------------------------------------------------
// QUEUED DATA STRUCTURES
// ----------------------------------------------------------------------------
/** Raw I2S PCM chunk produced by i2sReadTask and consumed by audioProcessingTask. */
struct RawChunk {
  uint8_t *data;
  uint32_t len;
  uint32_t captured_ms;
};
static QueueHandle_t rawQueue = nullptr;
#define RAW_QUEUE_LEN 32

/** Encoded Opus packet produced by audioProcessingTask and consumed by wsSendTask. */
struct OpusPacket {
  uint8_t data[OPUS_MAX_PACKET_BYTES];
  int     len;
  uint32_t captured_ms;
};
static QueueHandle_t opusQueue = nullptr;
#define OPUS_QUEUE_LEN 64

// ----------------------------------------------------------------------------
// STATS
// ----------------------------------------------------------------------------
static volatile uint32_t statI2sBytesIn     = 0;
static volatile uint32_t statI2sReadErrors  = 0;
static volatile uint32_t statRawDropped     = 0;
static volatile uint32_t statOpusEncoded    = 0;
static volatile uint32_t statOpusDropped    = 0;
static volatile uint32_t statWsPacketsSent  = 0;
static volatile uint32_t statWsBytesSent    = 0;
static volatile uint64_t statLatencySumMs   = 0;
static volatile uint32_t statLatencyCount   = 0;
static volatile int      wsClientCount      = 0;

// Bridge telemetry.
static String lastReceiverSource = "unknown";
static uint16_t lastBridgeHttpCode = 0;
static String lastBridgeMsg = "";

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

/** Sends POST /api/source to EnkelvoudReceiver and returns true on HTTP 200. */
bool bridgeSetReceiverSource(const String &source, String &respBody, uint16_t &httpCode) {
  if (WiFi.status() != WL_CONNECTED) {
    respBody = "{\"ok\":false,\"error\":\"wifi_disconnected\"}";
    httpCode = 0;
    return false;
  }

  HTTPClient http;
  String url = String("http://") + RECEIVER_HOST + ":" + String(RECEIVER_PORT) + "/api/source";
  if (!http.begin(url)) {
    respBody = "{\"ok\":false,\"error\":\"http_begin_failed\"}";
    httpCode = 0;
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  if (strlen(RECEIVER_TOKEN) > 0) {
    http.addHeader("X-EVD-Token", RECEIVER_TOKEN);
  }

  String payload = String("{\"source\":\"") + source + "\"}";
  int code = http.POST(payload);
  httpCode = (uint16_t)((code < 0) ? 0 : code);
  respBody = (code > 0) ? http.getString() : String("{\"ok\":false,\"error\":\"post_failed\"}");
  http.end();

  return code == 200;
}

/** Sends POST /api/cmd to EnkelvoudReceiver and returns true on HTTP 200. */
bool bridgeSendReceiverCmd(const String &cmd, String &respBody, uint16_t &httpCode) {
  if (WiFi.status() != WL_CONNECTED) {
    respBody = "{\"ok\":false,\"error\":\"wifi_disconnected\"}";
    httpCode = 0;
    return false;
  }

  HTTPClient http;
  String url = String("http://") + RECEIVER_HOST + ":" + String(RECEIVER_PORT) + "/api/cmd";
  if (!http.begin(url)) {
    respBody = "{\"ok\":false,\"error\":\"http_begin_failed\"}";
    httpCode = 0;
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  if (strlen(RECEIVER_TOKEN) > 0) {
    http.addHeader("X-EVD-Token", RECEIVER_TOKEN);
  }

  String payload = String("{\"cmd\":\"") + cmd + "\"}";
  int code = http.POST(payload);
  httpCode = (uint16_t)((code < 0) ? 0 : code);
  respBody = (code > 0) ? http.getString() : String("{\"ok\":false,\"error\":\"post_failed\"}");
  http.end();

  return code == 200;
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
    esp_err_t res = i2s_read(I2S_PORT, i2sBuf, sizeof(i2sBuf), &bytesRead, portMAX_DELAY);

    if (res != ESP_OK) {
      statI2sReadErrors++;
      LOGW("i2s_read() error %d", (int)res);
      continue;
    }
    if (bytesRead == 0) continue;

    statI2sBytesIn += bytesRead;

    uint8_t *copy = (uint8_t *)malloc(bytesRead);
    if (!copy) {
      statRawDropped++;
      continue;
    }
    memcpy(copy, i2sBuf, bytesRead);

    RawChunk chunk;
    chunk.data = copy;
    chunk.len = bytesRead;
    chunk.captured_ms = millis();

    if (xQueueSend(rawQueue, &chunk, 0) != pdTRUE) {
      free(copy);
      statRawDropped++;
    }
  }
}

/** Converts raw PCM to Opus by resampling and frame encoding. */
void audioProcessingTask(void *param) {
  LOGI("audioProcessingTask started on core %d", xPortGetCoreID());
  static int16_t resampledScratch[8192];
  RawChunk chunk;

  for (;;) {
    if (xQueueReceive(rawQueue, &chunk, portMAX_DELAY) != pdTRUE) continue;

    uint32_t inFrames = chunk.len / (2 * sizeof(int16_t));
    const int16_t *inSamples = (const int16_t *)chunk.data;

    if (pendingCount == 0) pendingOldestCapturedMs = chunk.captured_ms;

    uint32_t produced = resampler.process(
      inSamples, inFrames,
      resampledScratch,
      sizeof(resampledScratch) / (2 * sizeof(int16_t))
    );

    free(chunk.data);

    for (uint32_t i = 0; i < produced && pendingCount < PENDING_MAX_FRAMES; i++) {
      pendingBuf[pendingCount * 2 + 0] = resampledScratch[i * 2 + 0];
      pendingBuf[pendingCount * 2 + 1] = resampledScratch[i * 2 + 1];
      pendingCount++;
    }

    while (pendingCount >= OPUS_FRAME_SAMPLES) {
      OpusPacket pkt;
      pkt.captured_ms = pendingOldestCapturedMs;

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

    if (wsClientCount > 0) {
      ws.binaryAll(pkt.data, pkt.len);
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
  float avgLatency = statLatencyCount > 0
      ? (float)statLatencySumMs / (float)statLatencyCount
      : 0.0f;

  String json = "{";
  json += "\"ok\":true,";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"wsUrl\":\"ws://" + WiFi.localIP().toString() + "/audio\",";
  json += "\"wifiConnected\":" + String((WiFi.status() == WL_CONNECTED) ? "true" : "false") + ",";
  json += "\"wifiRssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"wsClients\":" + String(wsClientCount) + ",";
  json += "\"i2sBytesIn\":" + String(statI2sBytesIn) + ",";
  json += "\"i2sReadErrors\":" + String(statI2sReadErrors) + ",";
  json += "\"rawDropped\":" + String(statRawDropped) + ",";
  json += "\"opusEncoded\":" + String(statOpusEncoded) + ",";
  json += "\"opusDropped\":" + String(statOpusDropped) + ",";
  json += "\"wsPacketsSent\":" + String(statWsPacketsSent) + ",";
  json += "\"wsBytesSent\":" + String(statWsBytesSent) + ",";
  json += "\"avgLatencyMs\":" + String(avgLatency, 2) + ",";
  json += "\"receiverHost\":\"" + String(RECEIVER_HOST) + "\",";
  json += "\"receiverPort\":" + String(RECEIVER_PORT) + ",";
  json += "\"bridgeLastHttp\":" + String(lastBridgeHttpCode) + ",";
  json += "\"bridgeLastSource\":\"" + jsonEscape(lastReceiverSource) + "\",";
  json += "\"bridgeLastMsg\":\"" + jsonEscape(lastBridgeMsg) + "\"";
  json += "}";

  request->send(200, "application/json", json);
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
  if (!(source == "BT" || source == "AUX" || source == "USB")) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid source\"}");
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

/** Connects ESP32-S3 to WiFi in STA mode and enables reconnect on disconnect. */
void setup_wifi_sta() {
  LOGI("Connecting to WiFi router \"%s\" with static IP...", WIFI_STA_SSID);

  IPAddress local_IP(192, 168, 100, 250);
  IPAddress gateway(192, 168, 100, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(8, 8, 8, 8);
  IPAddress secondaryDNS(8, 8, 4, 4);

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    LOGE("Failed to configure static IP!");
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startMs > WIFI_CONNECT_TIMEOUT_MS) {
      LOGE("WiFi connect TIMED OUT after %lu ms (status=%d). Rebooting to retry...",
           (unsigned long)WIFI_CONNECT_TIMEOUT_MS, (int)WiFi.status());
      delay(1000);
      ESP.restart();
    }
    delay(250);
    LOGI("  ...still connecting (status=%d)", (int)WiFi.status());
  }

  IPAddress ip   = WiFi.localIP();
  IPAddress gw   = WiFi.gatewayIP();
  IPAddress mask = WiFi.subnetMask();
  IPAddress dns  = WiFi.dnsIP();

  LOGI("WiFi connected");
  LOGI("  SSID          : %s", WIFI_STA_SSID);
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

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html; charset=utf-8", EVDCTRL_HTML);
  });

  server.on("/player", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html; charset=utf-8", EVDPLR_HTML);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleApiStatus(request);
  });

  server.on("/api/source", HTTP_POST,
            [](AsyncWebServerRequest *request) {},
            nullptr,
            handleApiSourceBridge);

  server.on("/api/cmd", HTTP_POST,
            [](AsyncWebServerRequest *request) {},
            nullptr,
            handleApiCmdBridge);

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

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
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

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    LOGE("i2s_set_pin() FAILED, error %d", (int)err);
    return;
  }

  LOGI("  I2S pins -> BCLK:%d  LRC/WS:%d  DIN(DATA IN):%d", pinBtBclk, pinBtLrc, pinBtDin);
  LOGI("  I2S mode  -> SLAVE / RX, %d Hz, 16-bit, stereo", SOURCE_SAMPLE_RATE);
  LOGI("I2S input ready, waiting for clock/data from upstream ESP32...");
}

/** Allocates queues and starts I2S read, processing, and websocket sender tasks. */
void setup_queues_and_tasks() {
  LOGI("Creating queues and background tasks...");

  rawQueue  = xQueueCreate(RAW_QUEUE_LEN, sizeof(RawChunk));
  opusQueue = xQueueCreate(OPUS_QUEUE_LEN, sizeof(OpusPacket));

  if (!rawQueue || !opusQueue) {
    LOGE("Failed to create one or more queues!");
    return;
  }

  xTaskCreatePinnedToCore(i2sReadTask,         "i2sRead",   4096, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(audioProcessingTask, "audioProc", 8192, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(wsSendTask,          "wsSend",    4096, nullptr, 2, nullptr, 1);

  LOGI("Tasks created: i2sReadTask, audioProcessingTask, wsSendTask");
}

/** Arduino setup entrypoint initializing network, codec pipeline, and workers. */
void setup() {
  setup_serial();
  setup_wifi_sta();
  setup_opus();
  setup_websocket_server();
  setup_i2s_in();
  setup_queues_and_tasks();

  LOGI("=========================================================");
  LOGI("Setup complete. Waiting for I2S audio + WS clients...");
  LOGI("=========================================================");
}

// ============================================================================
// LOOP
// ============================================================================
unsigned long lastStatsMs = 0;

/** Arduino loop for periodic stats and websocket housekeeping. */
void loop() {
  unsigned long now = millis();
  if (now - lastStatsMs >= 5000) {
    lastStatsMs = now;

    float avgLatency = statLatencyCount > 0
        ? (float)statLatencySumMs / (float)statLatencyCount
        : 0.0f;
    float kbps = (statWsBytesSent * 8.0f / 1000.0f) / 5.0f;

    IPAddress devIp = WiFi.localIP();
    bool wifiUp = (WiFi.status() == WL_CONNECTED);

    LOGI("---- STATS (last 5s) ----------------------------------");
    LOGI("WiFi status        : %s (RSSI: %d dBm)", wifiUp ? "CONNECTED" : "DISCONNECTED", WiFi.RSSI());
    LOGI("Device IP addr     : %s  | Gateway: %s", devIp.toString().c_str(), WiFi.gatewayIP().toString().c_str());
    LOGI("WebSocket URL      : ws://%s/audio", devIp.toString().c_str());
    LOGI("I2S bytes in       : %u  | read errors: %u", statI2sBytesIn, statI2sReadErrors);
    LOGI("Raw chunks dropped : %u", statRawDropped);
    LOGI("Opus frames encoded: %u  | dropped: %u", statOpusEncoded, statOpusDropped);
    LOGI("WS clients         : %d", wsClientCount);
    LOGI("WS packets sent    : %u  | bytes: %u (%.1f kbps)", statWsPacketsSent, statWsBytesSent, kbps);
    LOGI("Avg latency (capture->send): %.1f ms", avgLatency);
    LOGI("Bridge -> host:%s code:%u src:%s", RECEIVER_HOST, lastBridgeHttpCode, lastReceiverSource.c_str());
    LOGI("Free heap          : %u bytes", ESP.getFreeHeap());
    LOGI("---------------------------------------------------------");

    // Reset rolling 5-second counters.
    statI2sBytesIn = 0;
    statWsPacketsSent = 0;
    statWsBytesSent = 0;
    statLatencySumMs = 0;
    statLatencyCount = 0;
  }

  ws.cleanupClients();
  delay(10);
}