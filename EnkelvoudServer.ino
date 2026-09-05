#include <Arduino.h>
#include <Esp.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <vector>
#include "AudioTools.h"
#include "AudioTools/Communication/USB/USBAudioStream.h"
#include "AudioTools/AudioCodecs/CodecOpus.h"

using namespace audio_tools;

Preferences preferences;

const char* SETTINGS_VERSION = "v1.0.10"; 

const char* default_ap_ssid = "Enkelvoud-Server";
const char* default_ap_password = "";

IPAddress ap_local_ip(192, 168, 4, 1);
IPAddress ap_gateway(192, 168, 4, 1);
IPAddress ap_subnet(255, 255, 255, 0);

WebServer server(80);
WebSocketsServer webSocket(8081);

AudioInfo currentAudioInfo(48000, 2, 16);
USBAudioStream in;
I2SStream i2s;

unsigned long totalOpusBytesStreamed = 0;
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

// Single DAC Pins
int pinLrc  = 11;
int pinDout = 12;
int pinBclk = 13;

// Aux Pins
int pinAuxLrc = 18;
int pinAuxDin = 23;
int pinAuxBclk = 19;

// Bluetooth Pins
int pinBtLrc = 15;
int pinBtDin = 16;
int pinBtBclk = 17;

float serverVolumeMultiplier = 1.0f;
String serverAudioInputMode = "Bluetooth";

std::vector<String> recentLogs;
const size_t maxLogs = 50;

class WebSocketPrint;

#include "EVDCTRL.h"
#include "EVDPLR.h"
#include "EVDUSB.h"
#include "EVDBT.h"
#include "EVDAUX.h"

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
        ".container { max-width: 600px; margin: 0 auto; background: var(--card-bg); border: 1px solid var(--border-color); border-radius: 8px; padding: 24px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }"
        ".header-container { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border-color); padding-bottom: 12px; margin-bottom: 16px; gap: 12px; flex-wrap: wrap; }"
        "h2 { margin: 0; font-size: 22px; }"
        "input[type='text'], input[type='password'] { font-size: 14px; padding: 10px; box-sizing: border-box; background: var(--bg-color); color: var(--text-color); border: 1px solid var(--border-color); border-radius: 6px; width: 100%; }"
        "label { display: block; margin-top: 12px; font-size: 14px; font-weight: 600; }"
        ".row-group { display: flex; gap: 10px; align-items: center; margin-top: 4px; }"
        ".row-group input { flex: 1; margin-top: 0; }"
        ".btn { background: var(--accent-color); color: #fff; font-weight: bold; cursor: pointer; border: none; padding: 10px 16px; border-radius: 6px; text-align: center; display: inline-block; transition: background 0.2s; margin-top: 10px; }"
        ".btn:hover { background: var(--accent-hover); }"
        ".button-row { display: flex; gap: 10px; margin-top: 20px; }"
        ".checkbox-label { display: flex; align-items: center; gap: 10px; margin-top: 12px; cursor: pointer; font-weight: normal; }"
        ".checkbox-label input { width: 18px; height: 18px; accent-color: var(--accent-color); margin: 0; }"
        "</style></head>"
        "<body>"
        "<div class='container'>"
        
        "<form id='setupForm' onkeydown='if(event.key === \"Enter\") { event.preventDefault(); return false; }'>"
        
        "<div class='header-container'>"
        "<h2>Enkelvoud Network Setup</h2>"
        "</div>"

        "<div>"
        "<label>Server Name (Alphanumeric only):</label>"
        "<input type='text' name='friendly_name' value='" + String(friendly_name) + "' pattern='[a-zA-Z0-9]+' title='Only alphabetic and numeric characters are allowed (no spaces or special symbols)' required>"
        "<label style='font-size: 13px; color: var(--accent-color); margin-top: 6px;'>Server Hostname: http://" + String(mdns_target_host) + "</label>" 
        "<input type='hidden' name='mdns_host' value='" + String(mdns_target_host) + "'>"
        
        "<label>Wi-Fi SSID:</label>"
        "<div class='row-group'>"
        "<input type='text' id='ssidInput' name='ssid' value='" + String(wifi_ssid) + "' placeholder='Enter SSID'>"
        "</div>"
        
        "<label>Wi-Fi Password:</label>"
        "<div class='row-group'>"
        "<input type='password' id='passInput' name='pass' value='" + String(wifi_pass) + "' placeholder='Enter Password'>"
        "</div>"
        
        "<label class='checkbox-label'>"
        "<input type='checkbox' id='staticCheck' name='use_static' " + String(use_static_ip ? "checked" : "") + " onchange='toggleStaticIp()'> Use Static IP Configuration"
        "</label>"
        
        "<div id='staticIpFields' style='display: " + String(use_static_ip ? "block" : "none") + ";'>"
        "<label>Static IP Address:</label>"
        "<input type='text' id='staticIpInput' name='static_ip' value='" + String(static_ip_str) + "'>"
        "<label>Gateway IP:</label>"
        "<input type='text' id='gatewayInput' name='static_gw' value='" + String(static_gw_str) + "'>"
        "<label>Subnet Mask:</label>"
        "<input type='text' id='subnetInput' name='static_sn' value='" + String(static_sn_str) + "'>"
        "<label>DNS Server:</label>"
        "<input type='text' id='dnsInput' name='static_dns' value='" + String(static_dns_str) + "'>"
        "</div>"
        "</div>"

        "<div class='button-row'>"
        "<button type='button' class='btn' onclick='submitForm()' style='flex:1; margin-top:0;'>Save and Connect</button>"
        "</div>"
        "</form></div>"

        "<script>"
        "function toggleStaticIp() { const isChecked = document.getElementById('staticCheck').checked; document.getElementById('staticIpFields').style.display = isChecked ? 'block' : 'none'; }"
        "function submitForm() { const formData = new URLSearchParams(new FormData(document.getElementById('setupForm'))); fetch('/save', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: formData }).then(() => { alert('Settings saved. Device is restarting and connecting...'); setTimeout(() => { window.location.href = '/'; }, 4000); }); }"
        "</script></body></html>";

    return html;
}

WebSocketPrint* wsPrint = nullptr;
OpusAudioEncoder* opus = nullptr;
EncodedAudioStream* encoder = nullptr;
StreamCopy* copier = nullptr;
StreamCopy* usbToDacCopier = nullptr;

TaskHandle_t audioTaskHandle = NULL;

class WebSocketPrint : public Print {
public:
    size_t write(uint8_t c) override {
        uint8_t buf[1] = {c};
        webSocket.broadcastBIN(buf, 1);
        totalOpusBytesStreamed += 1;
        return 1;
    }
    size_t write(const uint8_t *buffer, size_t size) override {
        if (size > 0) {
            webSocket.broadcastBIN((uint8_t*)buffer, size);
            totalOpusBytesStreamed += size;
        }
        return size;
    }
};

void logAction(const String& functionName, const String& eventType, const String& details) {
    char buf[256];
    snprintf(buf, sizeof(buf), "[%s] (%s) %s", functionName.c_str(), eventType.c_str(), details.c_str());
    Serial.println(buf);

    recentLogs.push_back(String(buf));
    if (recentLogs.size() > maxLogs) {
        recentLogs.erase(recentLogs.begin());
    }
}

String sanitizeAlphanumeric(const String& input) {
    String output = "";
    for (unsigned int i = 0; i < input.length(); i++) {
        char c = input[i];
        if (isAlphaNumeric(c)) {
            output += (char)tolower(c);
        }
    }
    if (output.length() == 0) output = "enkelvoud";
    return output;
}

void loadSettings() {
    preferences.begin("enkelvoud", false);
    
    String storedVersion = preferences.getString("s_ver", "");
    
    if (storedVersion != SETTINGS_VERSION) {
        logAction("loadSettings", "NVM", "New firmware version detected (" + String(SETTINGS_VERSION) + "). Updating version tag.");
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

    pinLrc  = preferences.getInt("lrc", 11);
    pinDout = preferences.getInt("dout", 12);
    pinBclk = preferences.getInt("bclk", 13);

    pinAuxLrc = preferences.getInt("aux_lrc", 18);
    pinAuxDin = preferences.getInt("aux_din", 23);
    pinAuxBclk = preferences.getInt("aux_bclk", 19);

    pinBtLrc = preferences.getInt("bt_lrc", 15);
    pinBtDin = preferences.getInt("bt_din", 16);
    pinBtBclk = preferences.getInt("bt_bclk", 17);

    serverVolumeMultiplier = preferences.getFloat("vol", 1.0f);
    serverAudioInputMode = preferences.getString("audio_in", "Bluetooth");
    
    serverStreamingEnabled = false;

    serverAudioBuffer = preferences.getInt("audio_buf", 0);
    serverOpusBitrate = preferences.getString("bitrate", "mid");
    
    preferences.end();
    logAction("loadSettings", "NVM", "Settings loaded successfully from NVM.");
}

void initializeAudioObjects() {
    if (!wsPrint) wsPrint = new WebSocketPrint();
    if (!opus) opus = new OpusAudioEncoder();
    if (!encoder) encoder = new EncodedAudioStream(wsPrint, opus);
    if (!copier) copier = new StreamCopy(*encoder, in);

    auto cfg = i2s.defaultConfig(TX_MODE);
    cfg.pin_bck = pinBclk;
    cfg.pin_ws = pinLrc;
    cfg.pin_data = pinDout;
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    cfg.sample_rate = 44100;
    i2s.begin(cfg);
    if (!usbToDacCopier) usbToDacCopier = new StreamCopy(i2s, in);
}

void handleSettings(); // Forward declaration

void handleRoot() { 
    if (serverWifiSsid.length() == 0) {
        handleSettings();
    } else {
        server.send(200, "text/html", getControlPageTemplate(serverFriendlyName.c_str())); 
    }
}

void handlePlayer() { server.send(200, "text/html", PLAYER_HTML); }

void handleSettings() {
    String mdnsHost = serverFriendlyName + ".local";
    String html = getWebPageTemplate(
        serverFriendlyName.c_str(), mdnsHost.c_str(),
        serverWifiSsid.c_str(), serverWifiPass.c_str(), serverUseStaticIp,
        serverStaticIp.c_str(), serverStaticGw.c_str(), serverStaticSn.c_str(), serverStaticDns.c_str()
    );
    server.send(200, "text/html", html);
}

void handleApiScanWifi() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    server.send(200, "application/json", json);
}

void handleApiTestWifi() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        String testSsid = "";
        String testPass = "";

        int ssidIdx = body.indexOf("\"ssid\":\"");
        if (ssidIdx != -1) {
            ssidIdx += 8;
            int endIdx = body.indexOf("\"", ssidIdx);
            if (endIdx != -1) testSsid = body.substring(ssidIdx, endIdx);
        }

        int passIdx = body.indexOf("\"pass\":\"");
        if (passIdx != -1) {
            passIdx += 8;
            int endIdx = body.indexOf("\"", passIdx);
            if (endIdx != -1) testPass = body.substring(passIdx, endIdx);
        }

        if (testSsid.length() == 0) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"SSID is empty\"}");
            return;
        }

        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(testSsid.c_str(), testPass.c_str());

        unsigned long startAttempt = millis();
        bool connected = false;
        while (millis() - startAttempt < 8000) {
            if (WiFi.status() == WL_CONNECTED) {
                connected = true;
                break;
            }
            delay(500);
        }

        String jsonResponse = connected ? "{\"success\":true,\"message\":\"Connected successfully\"}" : 
                                          "{\"success\":false,\"message\":\"Connection timed out or incorrect password\"}";

        if (connected) {
            WiFi.enableSTA(true);
            WiFi.enableAP(false);
        } else {
            WiFi.mode(WIFI_AP);
            WiFi.softAPConfig(ap_local_ip, ap_gateway, ap_subnet);
            WiFi.softAP(default_ap_ssid, default_ap_password);
        }
        server.send(200, "application/json", jsonResponse);
    } else {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid payload\"}");
    }
}

void handleApiVolume() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        int volIdx = body.indexOf("\"volume\":");
        if (volIdx != -1) {
            volIdx += 9;
            float parsedVol = body.substring(volIdx).toFloat();
            if (parsedVol >= 0.0f && parsedVol <= 1.0f) {
                serverVolumeMultiplier = parsedVol;
                preferences.begin("enkelvoud", false);
                preferences.putString("s_ver", SETTINGS_VERSION);
                preferences.putFloat("vol", serverVolumeMultiplier);
                preferences.end();
            }
        }
    }
    server.send(200, "application/json", "{\"status\":\"ok\",\"volume\":" + String(serverVolumeMultiplier) + "}");
}

void handleSaveSettings() {
    preferences.begin("enkelvoud", false);
    preferences.putString("s_ver", SETTINGS_VERSION);

    if (server.hasArg("restart_device") && server.arg("restart_device") == "on") {
        preferences.end();
        server.send(200, "text/plain", "Restarting");
        delay(500);
        ESP.restart();
        return;
    }

    if (server.hasArg("theme_mode")) { serverThemeMode = server.arg("theme_mode"); preferences.putString("theme", serverThemeMode); }
    if (server.hasArg("friendly_name")) { 
        serverFriendlyName = sanitizeAlphanumeric(server.arg("friendly_name")); 
        preferences.putString("srv_name", serverFriendlyName); 
    }
    if (server.hasArg("ssid")) { serverWifiSsid = server.arg("ssid"); preferences.putString("ssid", serverWifiSsid); }
    if (server.hasArg("pass")) { serverWifiPass = server.arg("pass"); preferences.putString("pass", serverWifiPass); }

    serverUseStaticIp = server.hasArg("use_static");
    preferences.putBool("use_static", serverUseStaticIp);

    if (server.hasArg("static_ip")) { serverStaticIp = server.arg("static_ip"); preferences.putString("s_ip", serverStaticIp); }
    if (server.hasArg("static_gw")) { serverStaticGw = server.arg("static_gw"); preferences.putString("s_gw", serverStaticGw); }
    if (server.hasArg("static_sn")) { serverStaticSn = server.arg("static_sn"); preferences.putString("s_sn", serverStaticSn); }
    if (server.hasArg("static_dns")) { serverStaticDns = server.arg("static_dns"); preferences.putString("s_dns", serverStaticDns); }

    preferences.end();
    server.send(200, "text/plain", "Saved and Restarting");
    delay(500);
    ESP.restart();
}

void handleCancelSettings() { server.send(200, "text/plain", "Cancelled"); }
void handleApiReset() {
    logAction("handleApiReset", "SYS", "Manual reset requested via control panel.");
    server.send(200, "application/json", "{\"status\":\"resetting\"}");
    delay(500);
    ESP.restart();
}

void handleApiServerName() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        int nameIdx = body.indexOf("\"name\":\"");
        if (nameIdx != -1) {
            nameIdx += 8;
            int endQuote = body.indexOf("\"", nameIdx);
            if (endQuote != -1) {
                serverFriendlyName = sanitizeAlphanumeric(body.substring(nameIdx, endQuote));
                preferences.begin("enkelvoud", false);
                preferences.putString("s_ver", SETTINGS_VERSION);
                preferences.putString("srv_name", serverFriendlyName);
                preferences.end();
                logAction("handleApiServerName", "SYS", "Friendly server name changed to: " + serverFriendlyName);
            }
        }
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleApiState() {
    String json = "{";
    json += "\"server_name\": \"" + serverFriendlyName + "\",";
    json += "\"host_muted\": " + String(hostMuted ? "true" : "false") + ",";
    json += "\"streaming_enabled\": " + String(serverStreamingEnabled ? "true" : "false") + ",";
    json += "\"audio_input\": \"" + serverAudioInputMode + "\",";
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
    
    size_t totalLogs = recentLogs.size();
    size_t startIndex = (totalLogs > 15) ? (totalLogs - 15) : 0;
    
    for (size_t i = startIndex; i < totalLogs; ++i) {
        String logLine = recentLogs[i];
        logLine.replace("\"", "\\\"");
        json += "\"" + logLine + "\"";
        if (i < totalLogs - 1) json += ",";
    }
    json += "],";
    json += "\"nodes\": {";
    json += "\"esp32_node_1\": {\"name\": \"Enkelvoud Node\", \"ip\": \"" + WiFi.localIP().toString() + "\", \"group\": \"Main Room\", \"muted\": false}";
    json += "}}";
    server.send(200, "application/json", json);
}

void handleApiControl() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        if (body.indexOf("toggle_host_mute") != -1) {
            hostMuted = !hostMuted;
        }
        else if (body.indexOf("toggle_streaming") != -1) {
            serverStreamingEnabled = !serverStreamingEnabled;
        }
        else if (body.indexOf("set_buffer") != -1) {
            int bufIdx = body.indexOf("\"buffer\":");
            if (bufIdx != -1) {
                bufIdx += 9;
                int parsedBuf = body.substring(bufIdx).toInt();
                if (parsedBuf >= 0 && parsedBuf <= 200) {
                    serverAudioBuffer = parsedBuf;
                    preferences.begin("enkelvoud", false);
                    preferences.putString("s_ver", SETTINGS_VERSION);
                    preferences.putInt("audio_buf", serverAudioBuffer);
                    preferences.end();
                    logAction("handleApiControl", "BUFFER", "Audio buffer adjusted to: " + String(serverAudioBuffer) + " ms");
                }
            }
        }
        else if (body.indexOf("set_bitrate") != -1) {
            int brIdx = body.indexOf("\"bitrate\":\"");
            if (brIdx != -1) {
                brIdx += 11;
                int endQuote = body.indexOf("\"", brIdx);
                if (endQuote != -1) {
                    String newBr = body.substring(brIdx, endQuote);
                    if (newBr == "low" || newBr == "mid" || newBr == "high") {
                        serverOpusBitrate = newBr;
                        preferences.begin("enkelvoud", false);
                        preferences.putString("s_ver", SETTINGS_VERSION);
                        preferences.putString("bitrate", serverOpusBitrate);
                        preferences.end();
                        logAction("handleApiControl", "BITRATE", "Opus bitrate changed to: " + serverOpusBitrate);
                    }
                }
            }
        }
        else if (body.indexOf("set_audio_input") != -1) {
            String newMode = serverAudioInputMode;
            if (body.indexOf("\"input\":\"USB\"") != -1) newMode = "USB";
            else if (body.indexOf("\"input\":\"Bluetooth\"") != -1) newMode = "Bluetooth";
            else if (body.indexOf("\"input\":\"AUX in\"") != -1) newMode = "AUX in";
            else if (body.indexOf("\"input\":\"None\"") != -1) newMode = "None";

            if (newMode != serverAudioInputMode) {
                serverAudioInputMode = newMode;
                preferences.begin("enkelvoud", false);
                preferences.putString("s_ver", SETTINGS_VERSION);
                preferences.putString("audio_in", serverAudioInputMode);
                preferences.end();
                logAction("handleApiControl", "INPUT", "Audio input switched to: " + serverAudioInputMode);
            }
        }
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {}

// Dedicated FreeRTOS task for audio loops to isolate stack usage from loopTask
void audioProcessingTask(void *pvParameters) {
    while (true) {
        handleUSBInputLoop();
        handleBluetoothInputLoop();
        handleAuxInputLoop();
        vTaskDelay(pdMS_TO_TICKS(1)); // Yield to prevent core starvation and watchdog triggers
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    loadSettings();
    initializeAudioObjects();

    bool connectedToNetwork = false;
    if (serverWifiSsid.length() > 0) {
        WiFi.mode(WIFI_STA);
        if (serverUseStaticIp) {
            IPAddress ip, gateway, subnet, dns;
            ip.fromString(serverStaticIp);
            gateway.fromString(serverStaticGw);
            subnet.fromString(serverStaticSn);
            dns.fromString(serverStaticDns);
            WiFi.config(ip, gateway, subnet, dns);
        } else {
            WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        }

        WiFi.begin(serverWifiSsid.c_str(), serverWifiPass.c_str());

        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
            delay(500);
        }
        if (WiFi.status() == WL_CONNECTED) connectedToNetwork = true;
    }

    if (!connectedToNetwork) {
        WiFi.mode(WIFI_AP_STA); // Enable both so AP works even if STA tried to connect
        WiFi.softAPConfig(ap_local_ip, ap_gateway, ap_subnet);
        WiFi.softAP(default_ap_ssid, default_ap_password);
        logAction("setup", "NET", "Failed to connect to STA. Started AP: " + String(default_ap_ssid));
    }

    server.on("/", HTTP_GET, handleRoot);
    server.on("/player", HTTP_GET, handlePlayer);
    server.on("/settings", HTTP_GET, handleSettings);
    server.on("/scan", HTTP_GET, handleApiScanWifi);
    server.on("/api/test_wifi", HTTP_POST, handleApiTestWifi);
    server.on("/api/volume", HTTP_POST, handleApiVolume);
    server.on("/api/reset", HTTP_POST, handleApiReset);
    server.on("/api/server_name", HTTP_POST, handleApiServerName);
    server.on("/save", HTTP_POST, handleSaveSettings);
    server.on("/cancel", HTTP_POST, handleCancelSettings);
    server.on("/api/state", HTTP_GET, handleApiState);
    server.on("/api/control", HTTP_POST, handleApiControl);

    if (!MDNS.begin(serverFriendlyName.c_str())) {
            Serial.println("Error setting up MDNS responder!");
            logAction("setup", "MDNS", "Error setting up MDNS responder");
        } else {
            Serial.println("mDNS responder started: http://" + serverFriendlyName + ".local");
            logAction("setup", "MDNS", "mDNS responder started successfully");
            MDNS.addService("http", "tcp", 80);
        }

    server.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    if (opus) {
        auto &cfg = opus->config();
        cfg.sample_rate = currentAudioInfo.sample_rate;
        cfg.channels = currentAudioInfo.channels;
        cfg.bits_per_sample = currentAudioInfo.bits_per_sample;
        opus->begin(cfg);
    }
    if (encoder) encoder->begin(currentAudioInfo);

    // Create a dedicated background task for audio streaming on Core 0 with an 8KB stack
    xTaskCreatePinnedToCore(
        audioProcessingTask,
        "AudioTask",
        8192,
        NULL,
        1,
        &audioTaskHandle,
        0
    );
}

void loop() {
    server.handleClient();
    webSocket.loop();
    delay(1); // Yield main loop task to keep watchdog happy
}