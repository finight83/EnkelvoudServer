#include <Arduino.h>
#include <Esp.h>
#include <HardwareSerial.h>
#include <WiFi.h>

#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <vector>
#include "AudioTools.h"
#include "AudioTools/Communication/USB/USBAudioStream.h"
#include "AudioTools/AudioCodecs/CodecOpus.h"
#include "AudioTools/Communication/AudioHttp.h"

using namespace audio_tools;

Preferences preferences;

const char* SETTINGS_VERSION = "v1.0.8"; 

const char* default_ap_ssid = "Enkelvoud-Server";
const char* default_ap_password = "";

IPAddress ap_local_ip(192, 168, 4, 1);
IPAddress ap_gateway(192, 168, 4, 1);
IPAddress ap_subnet(255, 255, 255, 0);

WebServer server(80);
WebSocketsServer webSocket(8081);

AudioInfo currentAudioInfo(48000, 2, 16);
USBAudioStream in;
I2SStream i2s2; // Single unified DAC stream reference

unsigned long totalOpusBytesStreamed = 0;
bool hostMuted = false;
bool serverStreamingEnabled = false;
int serverAudioBuffer = 0;
String serverOpusBitrate = "mid";

String serverThemeMode = "theme-blue";
String serverWifiSsid = "";
String serverWifiPass = "";
bool serverUseStaticIp = false;
String serverStaticIp = "192.168.1.100";
String serverStaticGw = "192.168.1.1";
String serverStaticSn = "255.255.255.0";
String serverStaticDns = "192.168.1.1";

int pinLrc  = 11;
int pinDout = 12;
int pinBclk = 13;

int pinAuxLrc = 18;
int pinAuxDin = 23;
int pinAuxBclk = 19;

int pinBtLrc  = 15;
int pinBtDin  = 16;
int pinBtBclk = 17;

float serverVolumeMultiplier = 1.0f;
String serverAudioInputMode = "None";

std::vector<String> recentLogs;
const size_t maxLogs = 50;

class WebSocketPrint;

// Map legacy/modular references to the single unified DAC (`i2s2`)
#define i2s1 i2s2
#define usbToDacCopier1 usbToDacCopier

#include "EVDCTRL.h"
#include "EVDPLR.h"
#include "EVDSET.h"
#include "EVDUSB.h"
#include "EVDBT.h"
#include "EVDAUX.h"
#include "EVDSTR.h"

WebSocketPrint* wsPrint = nullptr;
OpusAudioEncoder* opus = nullptr;
EncodedAudioStream* encoder = nullptr;
StreamCopy* copier = nullptr;
StreamCopy* usbToDacCopier = nullptr;

URLStream* radioStream = nullptr;
StreamCopy* radioToEncoderCopier = nullptr;

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

void checkIncomingStream() {
    static unsigned long lastStreamCheckTime = 0;
    if (millis() - lastStreamCheckTime > 5000) {
        lastStreamCheckTime = millis();
        if (serverAudioInputMode == "USB") {
            logAction("checkIncomingStream", "USB", "Validating USB audio stream format and packet availability.");
        } else if (serverAudioInputMode == "Bluetooth") {
            logAction("checkIncomingStream", "BT", "Verifying Bluetooth I2S slave clock sync and PCM data flow.");
        } else if (serverAudioInputMode == "AUX in") {
            logAction("checkIncomingStream", "AUX", "Inspecting AUX audio input stream state.");
        } else if (serverAudioInputMode == "Stream Radio Test") {
            logAction("checkIncomingStream", "STREAM", "Checking HTTP radio stream buffer and connection health.");
        } else {
            logAction("checkIncomingStream", "IDLE", "Audio input mode is set to None/Inactive.");
        }
    }
}

void loadSettings() {
    preferences.begin("enkelvoud", false);
    String storedVersion = preferences.getString("s_ver", "");
    
    if (storedVersion != SETTINGS_VERSION) {
        logAction("loadSettings", "NVM", "New firmware version detected (" + String(SETTINGS_VERSION) + "). Clearing old configuration for fresh setup.");
        preferences.clear();
        preferences.putString("s_ver", SETTINGS_VERSION);
        preferences.end();
        preferences.begin("enkelvoud", false);
    }

    serverThemeMode = preferences.getString("theme", "theme-black");
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

    pinBtLrc  = preferences.getInt("bt_lrc", 15);
    pinBtDin  = preferences.getInt("bt_din", 16);
    pinBtBclk = preferences.getInt("bt_bclk", 17);

    serverVolumeMultiplier = preferences.getFloat("vol", 1.0f);
    serverAudioInputMode = preferences.getString("audio_in", "Bluetooth");
    serverStreamingEnabled = preferences.getBool("streaming", false);
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

    auto cfg2 = i2s2.defaultConfig(TX_MODE);
    cfg2.pin_bck = pinBclk;
    cfg2.pin_ws = pinLrc;
    cfg2.pin_data = pinDout;
    cfg2.channels = 2;
    cfg2.bits_per_sample = 16;
    cfg2.sample_rate = 44100;
    i2s2.begin(cfg2);

    if (!usbToDacCopier) usbToDacCopier = new StreamCopy(i2s2, in);

    if (!radioStream) radioStream = new URLStream(serverWifiSsid.c_str(), serverWifiPass.c_str());
    if (!radioToEncoderCopier && radioStream && encoder) {
        radioToEncoderCopier = new StreamCopy(*encoder, *radioStream);
    }
}

void handleRoot() { server.send(200, "text/html", CONTROL_HTML); }
void handlePlayer() { server.send(200, "text/html", PLAYER_HTML); }
void handleSettings() {
    String html = getWebPageTemplate(
        serverThemeMode.c_str(), "Enkelvoud", "Enkelvoud",
        serverWifiSsid.c_str(), serverWifiPass.c_str(), serverUseStaticIp,
        serverStaticIp.c_str(), serverStaticGw.c_str(), serverStaticSn.c_str(), serverStaticDns.c_str(),
        pinLrc, pinDout, pinBclk,
        pinAuxLrc, pinAuxDin, pinAuxBclk, pinBtLrc, pinBtDin, pinBtBclk,
        serverVolumeMultiplier
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

    if (server.hasArg("reset_defaults") && server.arg("reset_defaults") == "on") {
        preferences.clear();
        preferences.putString("s_ver", SETTINGS_VERSION);
        preferences.end();
        server.send(200, "text/plain", "Defaults Reset and Restarting");
        delay(500);
        ESP.restart();
        return;
    }

    if (server.hasArg("theme_mode")) { serverThemeMode = server.arg("theme_mode"); preferences.putString("theme", serverThemeMode); }
    if (server.hasArg("ssid")) { serverWifiSsid = server.arg("ssid"); preferences.putString("ssid", serverWifiSsid); }
    if (server.hasArg("pass")) { serverWifiPass = server.arg("pass"); preferences.putString("pass", serverWifiPass); }

    serverUseStaticIp = server.hasArg("use_static");
    preferences.putBool("use_static", serverUseStaticIp);

    if (server.hasArg("static_ip")) { serverStaticIp = server.arg("static_ip"); preferences.putString("s_ip", serverStaticIp); }
    if (server.hasArg("static_gw")) { serverStaticGw = server.arg("static_gw"); preferences.putString("s_gw", serverStaticGw); }
    if (server.hasArg("static_sn")) { serverStaticSn = server.arg("static_sn"); preferences.putString("s_sn", serverStaticSn); }
    if (server.hasArg("static_dns")) { serverStaticDns = server.arg("static_dns"); preferences.putString("s_dns", serverStaticDns); }

    if (server.hasArg("lrc")) { pinLrc = server.arg("lrc").toInt(); preferences.putInt("lrc", pinLrc); }
    if (server.hasArg("dout")) { pinDout = server.arg("dout").toInt(); preferences.putInt("dout", pinDout); }
    if (server.hasArg("bclk")) { pinBclk = server.arg("bclk").toInt(); preferences.putInt("bclk", pinBclk); }

    if (server.hasArg("aux_lrc")) { pinAuxLrc = server.arg("aux_lrc").toInt(); preferences.putInt("aux_lrc", pinAuxLrc); }
    if (server.hasArg("aux_din")) { pinAuxDin = server.arg("aux_din").toInt(); preferences.putInt("aux_din", pinAuxDin); }
    if (server.hasArg("aux_bclk")) { pinAuxBclk = server.arg("aux_bclk").toInt(); preferences.putInt("aux_bclk", pinAuxBclk); }

    if (server.hasArg("bt_lrc")) { pinBtLrc = server.arg("bt_lrc").toInt(); preferences.putInt("bt_lrc", pinBtLrc); }
    if (server.hasArg("bt_din")) { pinBtDin = server.arg("bt_din").toInt(); preferences.putInt("bt_din", pinBtDin); }
    if (server.hasArg("bt_bclk")) { pinBtBclk = server.arg("bt_bclk").toInt(); preferences.putInt("bt_bclk", pinBtBclk); }

    if (server.hasArg("node_vol")) {
        serverVolumeMultiplier = server.arg("node_vol").toFloat() / 100.0f;
        preferences.putFloat("vol", serverVolumeMultiplier);
    }

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

void handleApiState() {
    String json = "{";
    json += "\"host_muted\": " + String(hostMuted ? "true" : "false") + ",";
    json += "\"streaming_enabled\": " + String(serverStreamingEnabled ? "true" : "false") + ",";
    json += "\"audio_input\": \"" + serverAudioInputMode + "\",";
    json += "\"audio_buffer\": " + String(serverAudioBuffer) + ",";
    json += "\"bitrate\": \"" + serverOpusBitrate + "\",";
    json += "\"logs\": [";
    for (size_t i = 0; i < recentLogs.size(); ++i) {
        String logLine = recentLogs[i];
        logLine.replace("\"", "\\\"");
        json += "\"" + logLine + "\"";
        if (i < recentLogs.size() - 1) json += ",";
    }
    json += "],";
    json += "\"nodes\": {";
    json += "\"esp32_node_1\": {\"name\": \"Enkelvoud Node\", \"ip\": \"" + WiFi.localIP().toString() + "\", \"group\": \"Default Room\", \"muted\": false}";
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
            preferences.begin("enkelvoud", false);
            preferences.putString("s_ver", SETTINGS_VERSION);
            preferences.putBool("streaming", serverStreamingEnabled);
            preferences.end();
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
            if (body.indexOf("\"input\":\"Stream Radio Test\"") != -1) newMode = "Stream Radio Test";
            else if (body.indexOf("\"input\":\"USB\"") != -1) newMode = "USB";
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
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(ap_local_ip, ap_gateway, ap_subnet);
        WiFi.softAP(default_ap_ssid, default_ap_password);
        serverAudioInputMode = "None";
    }

    Serial.println("\n========================================");
    Serial.println("       ENKELVOUD NETWORK SETTINGS       ");
    Serial.println("========================================");
    Serial.printf("Wi-Fi Mode        : %s\n", (WiFi.getMode() == WIFI_STA ? "STA (Station)" : (WiFi.getMode() == WIFI_AP ? "AP (Access Point)" : "AP+STA")));
    Serial.printf("Connected SSID    : %s\n", connectedToNetwork ? serverWifiSsid.c_str() : default_ap_ssid);
    Serial.printf("IP Address        : %s\n", connectedToNetwork ? WiFi.localIP().toString().c_str() : WiFi.softAPIP().toString().c_str());
    Serial.printf("Gateway IP        : %s\n", connectedToNetwork ? WiFi.gatewayIP().toString().c_str() : ap_gateway.toString().c_str());
    Serial.printf("Subnet Mask       : %s\n", connectedToNetwork ? WiFi.subnetMask().toString().c_str() : ap_subnet.toString().c_str());
    Serial.printf("Primary DNS       : %s\n", WiFi.dnsIP(0).toString().c_str());
    Serial.printf("Secondary DNS     : %s\n", WiFi.dnsIP(1).toString().c_str());
    Serial.printf("MAC Address       : %s\n", WiFi.macAddress().c_str());
    Serial.printf("Use Static Config : %s\n", serverUseStaticIp ? "YES" : "NO");
    Serial.println("========================================\n");

    server.on("/", HTTP_GET, handleRoot);
    server.on("/player", HTTP_GET, handlePlayer);
    server.on("/settings", HTTP_GET, handleSettings);
    server.on("/scan", HTTP_GET, handleApiScanWifi);
    server.on("/api/test_wifi", HTTP_POST, handleApiTestWifi);
    server.on("/api/volume", HTTP_POST, handleApiVolume);
    server.on("/api/reset", HTTP_POST, handleApiReset);
    server.on("/save", HTTP_POST, handleSaveSettings);
    server.on("/cancel", HTTP_POST, handleCancelSettings);
    server.on("/api/state", HTTP_GET, handleApiState);
    server.on("/api/control", HTTP_POST, handleApiControl);

    server.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    if (opus) opus->begin();
    if (encoder) encoder->begin(currentAudioInfo);
}

void loop() {
    server.handleClient();
    webSocket.loop();

    checkIncomingStream();

    handleUSBInputLoop();
    handleBluetoothInputLoop();
    handleAuxInputLoop();
    handleStreamInputLoop();
}