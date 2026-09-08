/*
  Enkelvoud ESP32-S3 server.

  Architecture:
    Phone / PC --BT/AUX/USB--> [source ESP32] --I2S 15/16/17--> [this ESP32-S3]
                                                                resample + Opus
                                                                ws://<ip>/audio
                                                                http://<name>.local/
                                                                http://<name>.local/player
*/

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <vector>
#include <algorithm>
#include <driver/i2s.h>
#include "opus.h"

const char* SETTINGS_VERSION = "v2.0.0";
const char* default_ap_ssid = "Enkelvoud-Server";
const char* default_ap_password = "";

IPAddress ap_local_ip(192, 168, 4, 1);
IPAddress ap_gateway(192, 168, 4, 1);
IPAddress ap_subnet(255, 255, 255, 0);

#define WIFI_CONNECT_TIMEOUT_MS 20000
#define LOG_BAUD 115200
#define LOGI(fmt, ...) Serial.printf("[INFO ] %8lu ms | " fmt "\n", millis(), ##__VA_ARGS__)
#define LOGW(fmt, ...) Serial.printf("[WARN ] %8lu ms | " fmt "\n", millis(), ##__VA_ARGS__)
#define LOGE(fmt, ...) Serial.printf("[ERROR] %8lu ms | " fmt "\n", millis(), ##__VA_ARGS__)

#define SOURCE_SAMPLE_RATE    44100
#define OPUS_SAMPLE_RATE      48000
#define CHANNELS              2
#define OPUS_FRAME_SAMPLES    960
#define OPUS_MAX_PACKET_BYTES 1500
#define I2S_PORT              I2S_NUM_0
#define I2S_READ_CHUNK_BYTES  1024
#define RAW_QUEUE_LEN         32
#define OPUS_QUEUE_LEN        64
#define PCM_QUEUE_LEN         32
#define PENDING_MAX_FRAMES    4096

Preferences preferences;
AsyncWebServer server(80);
AsyncWebSocket wsAudio("/audio");
AsyncWebSocket wsPcm("/pcm");
OpusEncoder *opusEncoder = nullptr;

int pinBtLrc  = 15;
int pinBtDin  = 16;
int pinBtBclk = 17;
int pinSrcUartTx = 8;
int pinSrcUartRx = 9;

bool hostMuted = false;
bool serverStreamingEnabled = false;
int serverAudioBuffer = 0;
String serverOpusBitrate = "mid";
String serverThemeMode = "theme-blue";
String serverFriendlyName = "enkelvoud";
String serverWifiSsid = "";
String serverWifiPass = "";
bool serverUseStaticIp = false;
String serverStaticIp = "192.168.1.100";
String serverStaticGw = "192.168.1.1";
String serverStaticSn = "255.255.255.0";
String serverStaticDns = "192.168.1.1";
String serverAudioInputMode = "Bluetooth";
String serverSourceHost = "192.168.100.249";
float serverVolumeMultiplier = 1.0f;

std::vector<String> recentLogs;
const size_t maxLogs = 50;

struct NodeInfo {
    String uuid;
    String name;
    String ip;
    String group;
    bool muted;
    bool connected;
    unsigned long lastSeenMs;
};

std::vector<NodeInfo> nodeList = {
    {"server_node", "Server Node", "", "Main Room", false, true, 0}
};

struct RawChunk {
    uint8_t *data;
    uint32_t len;
    uint32_t captured_ms;
};

struct OpusPacket {
    uint8_t data[OPUS_MAX_PACKET_BYTES];
    int len;
    uint32_t captured_ms;
};

struct PcmFrame {
    int16_t data[OPUS_FRAME_SAMPLES * CHANNELS];
    uint32_t captured_ms;
};

static QueueHandle_t rawQueue = nullptr;
static QueueHandle_t opusQueue = nullptr;
static QueueHandle_t pcmQueue = nullptr;

static volatile uint32_t statI2sBytesIn = 0;
static volatile uint32_t statI2sReadErrors = 0;
static volatile uint32_t statRawDropped = 0;
static volatile uint32_t statOpusEncoded = 0;
static volatile uint32_t statOpusDropped = 0;
static volatile uint32_t statWsPacketsSent = 0;
static volatile uint32_t statWsBytesSent = 0;
static volatile uint64_t statLatencySumMs = 0;
static volatile uint32_t statLatencyCount = 0;
static volatile int wsAudioClientCount = 0;
static volatile int wsPcmClientCount = 0;

static int16_t pendingBuf[PENDING_MAX_FRAMES * 2];
static uint32_t pendingCount = 0;
static uint32_t pendingOldestCapturedMs = 0;
unsigned long lastStatsMs = 0;
unsigned long lastNodePruneMs = 0;

#include "EVDCTRL.h"
#include "EVDPLR.h"
#include "EVDSRC.h"
#include "EVDUSB.h"
#include "EVDBT.h"
#include "EVDAUX.h"

class ResamplerToOpusRate {
public:
    // Clears interpolation state so the next chunk starts cleanly.
    void reset() {
        _pos = 0.0;
        _haveLast = false;
    }

    // Linear-interpolates interleaved stereo PCM from source rate to 48 kHz.
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
                l1 = in[0]; r1 = in[1];
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

inline String getWebPageTemplate(
    const char* friendly_name, const char* mdns_target_host,
    const char* wifi_ssid, const char* wifi_pass, bool use_static_ip,
    const char* static_ip_str, const char* static_gw_str, const char* static_sn_str, const char* static_dns_str
) {
    String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Enkelvoud Network Setup</title>"
        "<style>"
        ":root { --bg-color: #f1f5f9; --card-bg: #ffffff; --border-color: #cbd5e1; --text-color: #0f172a; --accent-color: #2563eb; --accent-hover: #1d4ed8; }"
        "body { font-family: sans-serif; background: var(--bg-color); color: var(--text-color); padding: 20px; margin: 0; }"
        ".container { max-width: 600px; margin: 0 auto; background: var(--card-bg); border: 1px solid var(--border-color); border-radius: 8px; padding: 24px; }"
        "h2 { margin: 0 0 16px 0; }"
        "input[type='text'], input[type='password'] { font-size: 14px; padding: 10px; box-sizing: border-box; background: var(--bg-color); color: var(--text-color); border: 1px solid var(--border-color); border-radius: 6px; width: 100%; }"
        "label { display: block; margin-top: 12px; font-size: 14px; font-weight: 600; }"
        ".btn { background: var(--accent-color); color: #fff; font-weight: bold; cursor: pointer; border: none; padding: 10px 16px; border-radius: 6px; margin-top: 16px; width: 100%; }"
        ".checkbox-label { display: flex; align-items: center; gap: 10px; margin-top: 12px; cursor: pointer; font-weight: normal; }"
        "</style></head><body><div class='container'>"
        "<form id='setupForm'>"
        "<h2>Enkelvoud Network Setup</h2>"
        "<label>Server Name (Alphanumeric only):</label>"
        "<input type='text' name='friendly_name' value='" + String(friendly_name) + "' pattern='[a-zA-Z0-9]+' required>"
        "<label style='font-size: 13px; color: var(--accent-color); margin-top: 6px;'>Server Hostname: http://" + String(mdns_target_host) + "</label>"
        "<label>Wi-Fi SSID:</label>"
        "<input type='text' id='ssidInput' name='ssid' value='" + String(wifi_ssid) + "'>"
        "<label>Wi-Fi Password:</label>"
        "<input type='password' id='passInput' name='pass' value='" + String(wifi_pass) + "'>"
        "<label class='checkbox-label'><input type='checkbox' id='staticCheck' name='use_static' " + String(use_static_ip ? "checked" : "") + " onchange='document.getElementById(\"staticIpFields\").style.display=this.checked?\"block\":\"none\"'> Use Static IP Configuration</label>"
        "<div id='staticIpFields' style='display: " + String(use_static_ip ? "block" : "none") + ";'>"
        "<label>Static IP Address:</label><input type='text' name='static_ip' value='" + String(static_ip_str) + "'>"
        "<label>Gateway IP:</label><input type='text' name='static_gw' value='" + String(static_gw_str) + "'>"
        "<label>Subnet Mask:</label><input type='text' name='static_sn' value='" + String(static_sn_str) + "'>"
        "<label>DNS Server:</label><input type='text' name='static_dns' value='" + String(static_dns_str) + "'>"
        "</div>"
        "<button type='button' class='btn' onclick='submitForm()'>Save and Connect</button>"
        "</form></div>"
        "<script>function submitForm(){const formData=new URLSearchParams(new FormData(document.getElementById('setupForm')));fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formData}).then(()=>{alert('Settings saved. Device is restarting...');setTimeout(()=>{window.location.href='/';},4000);});}</script>"
        "</body></html>";
    return html;
}

// Appends a timestamped log line for Serial and the control-panel log window.
void logAction(const String& functionName, const String& eventType, const String& details) {
    char buf[256];
    snprintf(buf, sizeof(buf), "[%s] (%s) %s", functionName.c_str(), eventType.c_str(), details.c_str());
    Serial.println(buf);
    recentLogs.push_back(String(buf));
    if (recentLogs.size() > maxLogs) recentLogs.erase(recentLogs.begin());
}

// Strips a server hostname down to lowercase alphanumeric characters for mDNS.
String sanitizeAlphanumeric(const String& input) {
    String output = "";
    for (unsigned int i = 0; i < input.length(); i++) {
        if (isAlphaNumeric(input[i])) output += (char)tolower(input[i]);
    }
    if (output.length() == 0) output = "enkelvoud";
    return output;
}

// Converts the UI bitrate label into an Opus bits-per-second value.
int opusBitrateFromLabel(const String& label) {
    if (label == "low") return 32000;
    if (label == "high") return 128000;
    return 64000;
}

// Applies the stored Opus bitrate to the live encoder.
void applyOpusBitrate() {
    if (!opusEncoder) return;
    opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(opusBitrateFromLabel(serverOpusBitrate)));
}

// Pulls settings from NVS, including source-board host and I2S / UART pins.
void loadSettings() {
    preferences.begin("enkelvoud", false);
    String storedVersion = preferences.getString("s_ver", "");
    if (storedVersion != SETTINGS_VERSION) {
        logAction("loadSettings", "NVM", "Firmware " + String(SETTINGS_VERSION) + " - updating version tag.");
        preferences.putString("s_ver", SETTINGS_VERSION);
    }

    serverThemeMode = preferences.getString("theme", "theme-black");
    serverFriendlyName = sanitizeAlphanumeric(preferences.getString("srv_name", "enkelvoud"));
    serverWifiSsid = preferences.getString("ssid", "");
    serverWifiPass = preferences.getString("pass", "");
    serverUseStaticIp = preferences.getBool("use_static", false);
    serverStaticIp = preferences.getString("s_ip", "192.168.1.100");
    serverStaticGw = preferences.getString("s_gw", "192.168.1.1");
    serverStaticSn = preferences.getString("s_sn", "255.255.255.0");
    serverStaticDns = preferences.getString("s_dns", "192.168.1.1");
    pinBtLrc = preferences.getInt("bt_lrc", 15);
    pinBtDin = preferences.getInt("bt_din", 16);
    pinBtBclk = preferences.getInt("bt_bclk", 17);
    pinSrcUartTx = preferences.getInt("src_tx", 8);
    pinSrcUartRx = preferences.getInt("src_rx", 9);
    serverVolumeMultiplier = preferences.getFloat("vol", 1.0f);
    serverAudioInputMode = preferences.getString("audio_in", "Bluetooth");
    serverSourceHost = preferences.getString("src_host", "192.168.100.249");
    serverStreamingEnabled = false;
    serverAudioBuffer = preferences.getInt("audio_buf", 0);
    serverOpusBitrate = preferences.getString("bitrate", "mid");
    preferences.end();
    logAction("loadSettings", "NVM", "Settings loaded.");
}

// Reads I2S PCM from the source ESP32 and queues raw chunks for encoding.
void i2sReadTask(void *param) {
    LOGI("i2sReadTask started on core %d", xPortGetCoreID());
    static uint8_t i2sBuf[I2S_READ_CHUNK_BYTES];
    for (;;) {
        size_t bytesRead = 0;
        esp_err_t res = i2s_read(I2S_PORT, i2sBuf, sizeof(i2sBuf), &bytesRead, portMAX_DELAY);
        if (res != ESP_OK) {
            statI2sReadErrors++;
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

// Resamples 44.1 kHz stereo PCM to 48 kHz, encodes 20 ms Opus frames, and queues PCM for phones.
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
            inSamples, inFrames, resampledScratch,
            sizeof(resampledScratch) / (2 * sizeof(int16_t)));
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
                if (xQueueSend(opusQueue, &pkt, 0) != pdTRUE) statOpusDropped++;
                else statOpusEncoded++;
            }

            if (wsPcmClientCount > 0) {
                PcmFrame pcm;
                pcm.captured_ms = pendingOldestCapturedMs;
                memcpy(pcm.data, pendingBuf, sizeof(pcm.data));
                xQueueSend(pcmQueue, &pcm, 0);
            }

            uint32_t remaining = pendingCount - OPUS_FRAME_SAMPLES;
            memmove(pendingBuf, pendingBuf + OPUS_FRAME_SAMPLES * 2, remaining * 2 * sizeof(int16_t));
            pendingCount = remaining;
            pendingOldestCapturedMs += 20;
        }
    }
}

// Broadcasts Opus packets to ESP player nodes on /audio.
void wsOpusSendTask(void *param) {
    LOGI("wsOpusSendTask started on core %d", xPortGetCoreID());
    OpusPacket pkt;
    for (;;) {
        if (xQueueReceive(opusQueue, &pkt, portMAX_DELAY) != pdTRUE) continue;
        if (hostMuted || !serverStreamingEnabled) continue;
        if (wsAudioClientCount > 0) {
            wsAudio.binaryAll(pkt.data, pkt.len);
            statWsPacketsSent++;
            statWsBytesSent += pkt.len;
            statLatencySumMs += (millis() - pkt.captured_ms);
            statLatencyCount++;
        }
    }
}

// Broadcasts 48 kHz PCM frames to the phone player page on /pcm.
void wsPcmSendTask(void *param) {
    LOGI("wsPcmSendTask started on core %d", xPortGetCoreID());
    PcmFrame pcm;
    for (;;) {
        if (xQueueReceive(pcmQueue, &pcm, portMAX_DELAY) != pdTRUE) continue;
        if (hostMuted || !serverStreamingEnabled) continue;
        if (wsPcmClientCount > 0) {
            wsPcm.binaryAll((uint8_t *)pcm.data, sizeof(pcm.data));
        }
    }
}

// Tracks Opus websocket clients used by EnkelvoudPlayer ESP32 nodes.
void onWsAudioEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                    AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            wsAudioClientCount++;
            LOGI("Opus WS client #%u from %s (count=%d)", client->id(), client->remoteIP().toString().c_str(), wsAudioClientCount);
            break;
        case WS_EVT_DISCONNECT:
            wsAudioClientCount = max(0, wsAudioClientCount - 1);
            break;
        default:
            break;
    }
}

// Tracks PCM websocket clients used by the HTTP phone player at /player.
void onWsPcmEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                  AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            wsPcmClientCount++;
            LOGI("PCM WS client #%u from %s (count=%d)", client->id(), client->remoteIP().toString().c_str(), wsPcmClientCount);
            break;
        case WS_EVT_DISCONNECT:
            wsPcmClientCount = max(0, wsPcmClientCount - 1);
            break;
        default:
            break;
    }
}

// Pulls a JSON string value out of a small POST body without ArduinoJson.
String jsonStringValue(const String& body, const char* key) {
    String needle = String("\"") + key + "\":\"";
    int idx = body.indexOf(needle);
    if (idx < 0) return "";
    idx += needle.length();
    int end = body.indexOf("\"", idx);
    if (end < 0) return "";
    return body.substring(idx, end);
}

// Pulls a JSON number / bool-ish value as a string from a small POST body.
String jsonRawValue(const String& body, const char* key) {
    String needle = String("\"") + key + "\":";
    int idx = body.indexOf(needle);
    if (idx < 0) return "";
    idx += needle.length();
    while (idx < (int)body.length() && body[idx] == ' ') idx++;
    int end = idx;
    while (end < (int)body.length() && body[end] != ',' && body[end] != '}' && body[end] != ' ') end++;
    return body.substring(idx, end);
}

// Returns the current station or AP address for status JSON.
String currentDeviceIp() {
    if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
    return WiFi.softAPIP().toString();
}

// Builds the control-panel state payload including nodes and logs.
String buildStateJson() {
    String json = "{";
    json += "\"server_name\": \"" + serverFriendlyName + "\",";
    json += "\"host_muted\": " + String(hostMuted ? "true" : "false") + ",";
    json += "\"streaming_enabled\": " + String(serverStreamingEnabled ? "true" : "false") + ",";
    json += "\"audio_input\": \"" + serverAudioInputMode + "\",";
    json += "\"source_host\": \"" + serverSourceHost + "\",";
    json += "\"audio_buffer\": " + String(serverAudioBuffer) + ",";
    json += "\"bitrate\": \"" + serverOpusBitrate + "\",";
    json += "\"wifi_ssid\": \"" + serverWifiSsid + "\",";
    json += "\"wifi_pass\": \"" + serverWifiPass + "\",";
    json += "\"use_static\": " + String(serverUseStaticIp ? "true" : "false") + ",";
    json += "\"static_ip\": \"" + serverStaticIp + "\",";
    json += "\"static_gw\": \"" + serverStaticGw + "\",";
    json += "\"static_sn\": \"" + serverStaticSn + "\",";
    json += "\"static_dns\": \"" + serverStaticDns + "\",";
    json += "\"logs\": [";
    for (size_t i = 0; i < recentLogs.size(); ++i) {
        String logLine = recentLogs[i];
        logLine.replace("\"", "\\\"");
        json += "\"" + logLine + "\"";
        if (i + 1 < recentLogs.size()) json += ",";
    }
    json += "],\"nodes\": {";
    for (size_t i = 0; i < nodeList.size(); ++i) {
        const auto& n = nodeList[i];
        if (n.uuid == "server_node") {
            json += "\"server_node\": {\"name\": \"Server Node\", \"ip\": \"" + currentDeviceIp() +
                    "\", \"group\": \"" + n.group + "\", \"muted\": " + String(hostMuted ? "true" : "false") +
                    ", \"connected\": true, \"is_server\": true}";
        } else {
            json += "\"" + n.uuid + "\": {\"name\": \"" + n.name + "\", \"ip\": \"" + n.ip +
                    "\", \"group\": \"" + n.group + "\", \"muted\": " + String(n.muted ? "true" : "false") +
                    ", \"connected\": " + String(n.connected ? "true" : "false") + "}";
        }
        if (i + 1 < nodeList.size()) json += ",";
    }
    json += "}}";
    return json;
}

// Registers or refreshes a phone / ESP player node from an HTTP POST.
void upsertNode(const String& uuid, const String& name, const String& ip) {
    if (uuid.length() == 0 || uuid == "server_node") return;
    for (auto& n : nodeList) {
        if (n.uuid == uuid) {
            if (name.length()) n.name = name;
            if (ip.length()) n.ip = ip;
            n.connected = true;
            n.lastSeenMs = millis();
            return;
        }
    }
    nodeList.push_back({uuid, name.length() ? name : "Player", ip, "Main Room", false, true, millis()});
    logAction("upsertNode", "NODE", "Registered player node " + uuid);
}

// Marks player nodes disconnected if they stop heartbeating.
void pruneStaleNodes() {
    unsigned long now = millis();
    for (auto& n : nodeList) {
        if (n.uuid == "server_node") continue;
        if (n.connected && n.lastSeenMs > 0 && (now - n.lastSeenMs) > 30000) {
            n.connected = false;
        }
    }
}

void handleSettings(AsyncWebServerRequest *request);

// Serves the control panel at / and name.local, or setup if Wi-Fi is not stored.
void handleRoot(AsyncWebServerRequest *request) {
    if (serverWifiSsid.length() == 0) {
        handleSettings(request);
        return;
    }
    request->send(200, "text/html", getControlPageTemplate(serverFriendlyName.c_str()));
}

// Serves the phone player page at /player and name.local/player.
void handlePlayer(AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", PLAYER_HTML);
}

// Serves the first-run / advanced network setup page.
void handleSettings(AsyncWebServerRequest *request) {
    String mdnsHost = serverFriendlyName + ".local";
    request->send(200, "text/html", getWebPageTemplate(
        serverFriendlyName.c_str(), mdnsHost.c_str(),
        serverWifiSsid.c_str(), serverWifiPass.c_str(), serverUseStaticIp,
        serverStaticIp.c_str(), serverStaticGw.c_str(), serverStaticSn.c_str(), serverStaticDns.c_str()));
}

// Returns nearby Wi-Fi networks as JSON for the control-panel scanner.
void handleApiScanWifi(AsyncWebServerRequest *request) {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    request->send(200, "application/json", json);
}

// Restarts the ESP32-S3 from the control panel.
void handleApiReset(AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"status\":\"resetting\"}");
    delay(200);
    ESP.restart();
}

// Returns live server state for the control panel poller.
void handleApiState(AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildStateJson());
}

// Collects URL-encoded fields and writes network settings, then reboots.
void handleSaveSettings(AsyncWebServerRequest *request) {
    preferences.begin("enkelvoud", false);
    preferences.putString("s_ver", SETTINGS_VERSION);
    if (request->hasParam("theme_mode", true)) {
        serverThemeMode = request->getParam("theme_mode", true)->value();
        preferences.putString("theme", serverThemeMode);
    }
    if (request->hasParam("friendly_name", true)) {
        serverFriendlyName = sanitizeAlphanumeric(request->getParam("friendly_name", true)->value());
        preferences.putString("srv_name", serverFriendlyName);
    }
    if (request->hasParam("ssid", true)) {
        serverWifiSsid = request->getParam("ssid", true)->value();
        preferences.putString("ssid", serverWifiSsid);
    }
    if (request->hasParam("pass", true)) {
        serverWifiPass = request->getParam("pass", true)->value();
        preferences.putString("pass", serverWifiPass);
    }
    serverUseStaticIp = request->hasParam("use_static", true);
    preferences.putBool("use_static", serverUseStaticIp);
    if (request->hasParam("static_ip", true)) {
        serverStaticIp = request->getParam("static_ip", true)->value();
        preferences.putString("s_ip", serverStaticIp);
    }
    if (request->hasParam("static_gw", true)) {
        serverStaticGw = request->getParam("static_gw", true)->value();
        preferences.putString("s_gw", serverStaticGw);
    }
    if (request->hasParam("static_sn", true)) {
        serverStaticSn = request->getParam("static_sn", true)->value();
        preferences.putString("s_sn", serverStaticSn);
    }
    if (request->hasParam("static_dns", true)) {
        serverStaticDns = request->getParam("static_dns", true)->value();
        preferences.putString("s_dns", serverStaticDns);
    }
    if (request->hasParam("source_host", true)) {
        serverSourceHost = request->getParam("source_host", true)->value();
        preferences.putString("src_host", serverSourceHost);
    }
    preferences.end();
    request->send(200, "text/plain", "Saved and Restarting");
    delay(200);
    ESP.restart();
}

// Joins the configured router as a station, or opens the setup access point.
void setup_wifi() {
    bool connectedToNetwork = false;
    if (serverWifiSsid.length() > 0) {
        LOGI("Connecting to WiFi \"%s\"...", serverWifiSsid.c_str());
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        if (serverUseStaticIp) {
            IPAddress ip, gateway, subnet, dns;
            ip.fromString(serverStaticIp);
            gateway.fromString(serverStaticGw);
            subnet.fromString(serverStaticSn);
            dns.fromString(serverStaticDns);
            WiFi.config(ip, gateway, subnet, dns);
        }
        WiFi.begin(serverWifiSsid.c_str(), serverWifiPass.c_str());
        unsigned long startMs = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS) {
            delay(250);
        }
        connectedToNetwork = (WiFi.status() == WL_CONNECTED);
    }

    if (!connectedToNetwork) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAPConfig(ap_local_ip, ap_gateway, ap_subnet);
        WiFi.softAP(default_ap_ssid, default_ap_password);
        logAction("setup_wifi", "NET", "STA failed; AP " + String(default_ap_ssid) + " at 192.168.4.1");
    } else {
        logAction("setup_wifi", "NET", "Connected as " + WiFi.localIP().toString());
        WiFi.onEvent([](WiFiEvent_t event) {
            if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) WiFi.reconnect();
        });
    }
}

// Creates the Opus encoder used for ESP player nodes.
void setup_opus() {
    int err = 0;
    opusEncoder = opus_encoder_create(OPUS_SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &err);
    if (err != OPUS_OK || !opusEncoder) {
        LOGE("opus_encoder_create() FAILED, error %d", err);
        return;
    }
    applyOpusBitrate();
    opus_encoder_ctl(opusEncoder, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(opusEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
}

// Configures I2S slave RX on pins 15 (WS), 16 (DIN), 17 (BCLK).
void setup_i2s_in() {
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
    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) != ESP_OK) {
        LOGE("i2s_driver_install() FAILED");
        return;
    }
    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = pinBtBclk,
        .ws_io_num = pinBtLrc,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = pinBtDin
    };
    i2s_set_pin(I2S_PORT, &pin_config);
    LOGI("I2S slave RX BCLK:%d WS:%d DIN:%d", pinBtBclk, pinBtLrc, pinBtDin);
}

// Handles JSON POST bodies for control, volume, naming, nodes, and Wi-Fi test.
void handleJsonBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index + len < total) return;
    String body;
    body.reserve(total + 1);
    for (size_t i = 0; i < total && i < 2048; i++) body += (char)data[i];
    if (index == 0 && total == len) body = String((const char*)data, len);

    String url = request->url();
    if (url == "/api/volume") {
        float parsedVol = jsonRawValue(body, "volume").toFloat();
        if (parsedVol >= 0.0f && parsedVol <= 1.0f) {
            serverVolumeMultiplier = parsedVol;
            preferences.begin("enkelvoud", false);
            preferences.putFloat("vol", serverVolumeMultiplier);
            preferences.end();
        }
        request->send(200, "application/json", "{\"status\":\"ok\",\"volume\":" + String(serverVolumeMultiplier) + "}");
        return;
    }

    if (url == "/api/server_name") {
        String newName = jsonStringValue(body, "name");
        if (newName.length()) {
            serverFriendlyName = sanitizeAlphanumeric(newName);
            preferences.begin("enkelvoud", false);
            preferences.putString("srv_name", serverFriendlyName);
            preferences.end();
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        return;
    }

    if (url == "/api/nodes") {
        upsertNode(jsonStringValue(body, "node_uuid"), jsonStringValue(body, "name"), jsonStringValue(body, "ip"));
        if (request->client()) {
            // ip may come from the socket if the player omitted it
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        return;
    }

    if (url == "/api/test_wifi") {
        request->send(200, "application/json", "{\"success\":false,\"message\":\"Reconnect from setup page after saving Wi-Fi\"}");
        return;
    }

    if (url != "/api/control") {
        request->send(404);
        return;
    }

    if (body.indexOf("toggle_host_mute") != -1) {
        hostMuted = !hostMuted;
        logAction("handleApiControl", "MUTE", hostMuted ? "MUTED" : "UNMUTED");
    } else if (body.indexOf("toggle_streaming") != -1) {
        serverStreamingEnabled = !serverStreamingEnabled;
        logAction("handleApiControl", "STREAM", serverStreamingEnabled ? "ON" : "OFF");
    } else if (body.indexOf("set_buffer") != -1) {
        int parsedBuf = jsonRawValue(body, "buffer").toInt();
        if (parsedBuf >= 0 && parsedBuf <= 200) {
            serverAudioBuffer = parsedBuf;
            preferences.begin("enkelvoud", false);
            preferences.putInt("audio_buf", serverAudioBuffer);
            preferences.end();
        }
    } else if (body.indexOf("set_bitrate") != -1) {
        String newBr = jsonStringValue(body, "bitrate");
        if (newBr == "low" || newBr == "mid" || newBr == "high") {
            serverOpusBitrate = newBr;
            preferences.begin("enkelvoud", false);
            preferences.putString("bitrate", serverOpusBitrate);
            preferences.end();
            applyOpusBitrate();
        }
    } else if (body.indexOf("set_audio_input") != -1) {
        String newMode = serverAudioInputMode;
        String input = jsonStringValue(body, "input");
        if (input == "USB" || input == "Bluetooth" || input == "AUX in" || input == "None") newMode = input;
        if (newMode != serverAudioInputMode) {
            serverAudioInputMode = newMode;
            preferences.begin("enkelvoud", false);
            preferences.putString("audio_in", serverAudioInputMode);
            preferences.end();
            queueSourceSelectCommand(serverAudioInputMode);
            logAction("handleApiControl", "INPUT", "Audio input -> " + serverAudioInputMode);
        }
    } else if (body.indexOf("create_group") != -1) {
        logAction("handleApiControl", "GROUP", "Group: " + jsonStringValue(body, "group"));
    } else if (body.indexOf("move_node") != -1) {
        String targetUuid = jsonStringValue(body, "node_uuid");
        String targetGroup = jsonStringValue(body, "group");
        for (auto& n : nodeList) if (n.uuid == targetUuid) n.group = targetGroup;
    } else if (body.indexOf("delete_node") != -1) {
        String targetUuid = jsonStringValue(body, "node_uuid");
        nodeList.erase(std::remove_if(nodeList.begin(), nodeList.end(),
            [targetUuid](const NodeInfo& n){ return n.uuid == targetUuid; }), nodeList.end());
    } else if (body.indexOf("delete_group") != -1) {
        String targetGroup = jsonStringValue(body, "group");
        for (auto& n : nodeList) if (n.group == targetGroup) n.group = "Main Room";
    } else if (body.indexOf("rename_group") != -1) {
        String oldG = jsonStringValue(body, "old_group");
        String newG = jsonStringValue(body, "new_group");
        for (auto& n : nodeList) if (n.group == oldG) n.group = newG;
    } else if (body.indexOf("set_group_volume") != -1) {
        logAction("handleApiControl", "VOLUME", "Group volume " + jsonStringValue(body, "group"));
    }

    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// Registers HTTP routes, websockets, and mDNS (name.local and name.local/player).
void setup_http_server() {
    wsAudio.onEvent(onWsAudioEvent);
    wsPcm.onEvent(onWsPcmEvent);
    server.addHandler(&wsAudio);
    server.addHandler(&wsPcm);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/player", HTTP_GET, handlePlayer);
    server.on("/settings", HTTP_GET, handleSettings);
    server.on("/scan", HTTP_GET, handleApiScanWifi);
    server.on("/api/state", HTTP_GET, handleApiState);
    server.on("/api/reset", HTTP_POST, handleApiReset);
    server.on("/save", HTTP_POST, handleSaveSettings);
    server.on("/cancel", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Cancelled");
    });

    auto jsonHandler = [](AsyncWebServerRequest *request) {};
    server.on("/api/control", HTTP_POST, jsonHandler, NULL, handleJsonBody);
    server.on("/api/volume", HTTP_POST, jsonHandler, NULL, handleJsonBody);
    server.on("/api/server_name", HTTP_POST, jsonHandler, NULL, handleJsonBody);
    server.on("/api/nodes", HTTP_POST, jsonHandler, NULL, handleJsonBody);
    server.on("/api/test_wifi", HTTP_POST, jsonHandler, NULL, handleJsonBody);

    if (!MDNS.begin(serverFriendlyName.c_str())) {
        logAction("setup_http_server", "MDNS", "mDNS failed");
    } else {
        MDNS.addService("http", "tcp", 80);
        logAction("setup_http_server", "MDNS", "http://" + serverFriendlyName + ".local");
    }
    server.begin();
}

// Creates queues and pins the I2S / encode / websocket tasks to core 1.
void setup_queues_and_tasks() {
    rawQueue = xQueueCreate(RAW_QUEUE_LEN, sizeof(RawChunk));
    opusQueue = xQueueCreate(OPUS_QUEUE_LEN, sizeof(OpusPacket));
    pcmQueue = xQueueCreate(PCM_QUEUE_LEN, sizeof(PcmFrame));
    xTaskCreatePinnedToCore(i2sReadTask, "i2sRead", 4096, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(audioProcessingTask, "audioProc", 8192, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(wsOpusSendTask, "wsOpus", 4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(wsPcmSendTask, "wsPcm", 4096, nullptr, 2, nullptr, 1);
}

// Boots Serial, Wi-Fi, Opus, HTTP, I2S, source UART, and audio tasks.
void setup() {
    Serial.begin(LOG_BAUD);
    delay(300);
    LOGI("Enkelvoud ESP32-S3 server booting");
    loadSettings();
    setup_wifi();
    setup_opus();
    setup_http_server();
    setup_i2s_in();
    setupSourceLink();
    setupBluetoothInput();
    setupAuxInput();
    setupUSBInput();
    setup_queues_and_tasks();
    queueSourceSelectCommand(serverAudioInputMode);
    LOGI("Control: http://%s.local/  Player: http://%s.local/player  Opus WS: /audio",
         serverFriendlyName.c_str(), serverFriendlyName.c_str());
}

// Logs stats, prunes nodes, flushes source commands, and cleans websocket clients.
void loop() {
    unsigned long now = millis();
    if (now - lastStatsMs >= 5000) {
        lastStatsMs = now;
        float kbps = (statWsBytesSent * 8.0f / 1000.0f) / 5.0f;
        LOGI("I2S in=%u drops=%u opus=%u ws=%d pcm=%d kbps=%.1f heap=%u",
             statI2sBytesIn, statRawDropped, statOpusEncoded,
             wsAudioClientCount, wsPcmClientCount, kbps, ESP.getFreeHeap());
        statI2sBytesIn = 0;
        statWsPacketsSent = 0;
        statWsBytesSent = 0;
        statLatencySumMs = 0;
        statLatencyCount = 0;
    }
    if (now - lastNodePruneMs >= 5000) {
        lastNodePruneMs = now;
        pruneStaleNodes();
    }
    handleSourceCommandLoop();
    handleBluetoothInputLoop();
    handleAuxInputLoop();
    handleUSBInputLoop();
    wsAudio.cleanupClients();
    wsPcm.cleanupClients();
    delay(10);
}
