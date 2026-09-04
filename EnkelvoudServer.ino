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

Preferences preferences;

const char* default_ap_ssid = "Enkelvoud-Server";
const char* default_ap_password = "";

IPAddress ap_local_ip(192, 168, 4, 1);
IPAddress ap_gateway(192, 168, 4, 1);
IPAddress ap_subnet(255, 255, 255, 0);

WebServer server(80);
WebSocketsServer webSocket(8081);

AudioInfo currentAudioInfo(48000, 2, 16);
USBAudioStream in;
I2SStream i2s1;
I2SStream i2s2;

unsigned long totalOpusBytesStreamed = 0;
bool hostMuted = false;
bool serverStreamingEnabled = false;

String serverThemeMode = "theme-blue";
String serverWifiSsid = "";
String serverWifiPass = "";
bool serverUseStaticIp = false;
String serverStaticIp = "192.168.1.100";
String serverStaticGw = "192.168.1.1";
String serverStaticSn = "255.255.255.0";
String serverStaticDns = "192.168.1.1";

int pinLrc1  = 4;
int pinDout1 = 5;
int pinBclk1 = 6;

int pinLrc2  = 11;
int pinDout2 = 12;
int pinBclk2 = 13;

int pinAuxLrc = 15;
int pinAuxDin = 16;
int pinAuxBclk = 17;

int pinBtLrc = 18;
int pinBtDin = 23;
int pinBtBclk = 19;

float serverVolumeMultiplier = 1.0f;
String serverAudioInputMode = "USB";

std::vector<String> recentLogs;
const size_t maxLogs = 50;

WebSocketPrint* wsPrint = nullptr;
OpusAudioEncoder* opus = nullptr;
EncodedAudioStream* encoder = nullptr;
StreamCopy* copier = nullptr;
StreamCopy* usbToDacCopier1 = nullptr;
StreamCopy* usbToDacCopier2 = nullptr;

URLStream* radioStream = nullptr;
StreamCopy* radioToEncoderCopier = nullptr;

#include "EVDCTRL.H"
#include "EVDPLR.H"
#include "EVDSET.H"
#include "EVDUSB.H"
#include "EVDBT.H"
#include "EVDAUX.H"
#include "EVDSTR.H"

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

void loadSettings() {
    preferences.begin("enkelvoud", true);
    serverThemeMode = preferences.getString("theme", "theme-blue");
    serverWifiSsid = preferences.getString("ssid", "");
    serverWifiPass = preferences.getString("pass", "");
    serverUseStaticIp = preferences.getBool("use_static", false);
    serverStaticIp = preferences.getString("s_ip", "192.168.1.100");
    serverStaticGw = preferences.getString("s_gw", "192.168.1.1");
    serverStaticSn = preferences.getString("s_sn", "255.255.255.0");
    serverStaticDns = preferences.getString("s_dns", "192.168.1.1");

    pinLrc1  = preferences.getInt("lrc_1", 4);
    pinDout1 = preferences.getInt("dout_1", 5);
    pinBclk1 = preferences.getInt("bclk_1", 6);
    pinLrc2  = preferences.getInt("lrc_2", 11);
    pinDout2 = preferences.getInt("dout_2", 12);
    pinBclk2 = preferences.getInt("bclk_2", 13);

    pinAuxLrc = preferences.getInt("aux_lrc", 15);
    pinAuxDin = preferences.getInt("aux_din", 16);
    pinAuxBclk = preferences.getInt("aux_bclk", 17);

    pinBtLrc = preferences.getInt("bt_lrc", 18);
    pinBtDin = preferences.getInt("bt_din", 23);
    pinBtBclk = preferences.getInt("bt_bclk", 19);

    serverVolumeMultiplier = preferences.getFloat("vol", 1.0f);
    serverAudioInputMode = preferences.getString("audio_in", "USB");
    serverStreamingEnabled = preferences.getBool("streaming", false);
    preferences.end();
    logAction("loadSettings", "NVM", "Settings loaded successfully from NVM.");
}

void initializeAudioObjects() {
    if (!wsPrint) {
        wsPrint = new WebSocketPrint();
        logAction("initializeAudioObjects", "INIT", "WebSocketPrint initialized.");
    }

    if (!opus) {
        opus = new OpusAudioEncoder();
        logAction("initializeAudioObjects", "INIT", "OpusAudioEncoder initialized.");
    }

    if (!encoder) {
        encoder = new EncodedAudioStream(wsPrint, opus);
        logAction("initializeAudioObjects", "INIT", "EncodedAudioStream initialized.");
    }

    if (!copier) {
        copier = new StreamCopy(*encoder, in);
        logAction("initializeAudioObjects", "INIT", "Main encoder StreamCopy initialized.");
    }

    if (!usbToDacCopier1) {
        usbToDacCopier1 = new StreamCopy(i2s1, in);
        logAction("initializeAudioObjects", "INIT", "USB to DAC1 StreamCopy initialized.");
    }

    if (!usbToDacCopier2) {
        usbToDacCopier2 = new StreamCopy(i2s2, in);
        logAction("initializeAudioObjects", "INIT", "USB to DAC2 StreamCopy initialized.");
    }

    if (!radioStream) {
        radioStream = new URLStream(serverWifiSsid.c_str(), serverWifiPass.c_str());
        logAction("initializeAudioObjects", "INIT", "Radio URLStream initialized.");
    }

    if (!radioToEncoderCopier && radioStream && encoder) {
        radioToEncoderCopier = new StreamCopy(*encoder, *radioStream);
        logAction("initializeAudioObjects", "INIT", "Radio to encoder StreamCopy initialized.");
    }
}

void cleanupAudioObjects() {
    if (radioToEncoderCopier) { delete radioToEncoderCopier; radioToEncoderCopier = nullptr; }
    if (radioStream) { delete radioStream; radioStream = nullptr; }
    if (usbToDacCopier2) { delete usbToDacCopier2; usbToDacCopier2 = nullptr; }
    if (usbToDacCopier1) { delete usbToDacCopier1; usbToDacCopier1 = nullptr; }
    if (copier) { delete copier; copier = nullptr; }
    if (encoder) { delete encoder; encoder = nullptr; }
    if (opus) { delete opus; opus = nullptr; }
    if (wsPrint) { delete wsPrint; wsPrint = nullptr; }
}

void handleRoot() {
    logAction("handleRoot", "HTTP_REQ", "Serving Enkelvoud Control Panel HTML.");
    server.send(200, "text/html", CONTROL_HTML);
}

void handlePlayer() {
    logAction("handlePlayer", "HTTP_REQ", "Serving Audio Player HTML.");
    server.send(200, "text/html", PLAYER_HTML);
}

void handleSettings() {
    logAction("handleSettings", "HTTP_REQ", "Serving Enkelvoud Server Settings page.");
    String html = getWebPageTemplate(
        serverThemeMode.c_str(), "Enkelvoud", "Enkelvoud",
        serverWifiSsid.c_str(), serverWifiPass.c_str(), serverUseStaticIp,
        serverStaticIp.c_str(), serverStaticGw.c_str(), serverStaticSn.c_str(), serverStaticDns.c_str(),
        pinLrc1, pinDout1, pinBclk1, pinLrc2, pinDout2, pinBclk2,
        pinAuxLrc, pinAuxDin, pinAuxBclk, pinBtLrc, pinBtDin, pinBtBclk,
        serverVolumeMultiplier
    );
    server.send(200, "text/html", html);
}

void handleApiScanWifi() {
    logAction("handleApiScanWifi", "WIFI", "Scanning available Wi-Fi networks...");
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
        logAction("handleApiTestWifi", "WIFI", "Testing credentials connectivity...");

        String testSsid = "";
        String testPass = "";

        int ssidIdx = body.indexOf("\"ssid\":\"");
        if (ssidIdx != -1) {
            ssidIdx += 8;
            int endIdx = body.indexOf("\"", ssidIdx);
            if (endIdx != -1) {
                testSsid = body.substring(ssidIdx, endIdx);
            }
        }

        int passIdx = body.indexOf("\"pass\":\"");
        if (passIdx != -1) {
            passIdx += 8;
            int endIdx = body.indexOf("\"", passIdx);
            if (endIdx != -1) {
                testPass = body.substring(passIdx, endIdx);
            }
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

        if (connected) {
            logAction("handleApiTestWifi", "WIFI", "Test connection successful for SSID: " + testSsid);
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Connected successfully\"}");
        } else {
            logAction("handleApiTestWifi", "WIFI", "Test connection failed for SSID: " + testSsid);
            server.send(200, "application/json", "{\"success\":false,\"message\":\"Connection timed out or incorrect password\"}");
        }

        if (WiFi.status() == WL_CONNECTED) {
            WiFi.enableSTA(true);
            WiFi.enableAP(false);
        } else {
            WiFi.mode(WIFI_AP);
            WiFi.softAPConfig(ap_local_ip, ap_gateway, ap_subnet);
            WiFi.softAP(default_ap_ssid, default_ap_password);
        }
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
                preferences.putFloat("vol", serverVolumeMultiplier);
                preferences.end();
                logAction("handleApiVolume", "STATE_CHANGE", "Global volume multiplier set to: " + String(serverVolumeMultiplier));
            }
        }
    }
    server.send(200, "application/json", "{\"status\":\"ok\",\"volume\":" + String(serverVolumeMultiplier) + "}");
}

void handleSaveSettings() {
    logAction("handleSaveSettings", "NVM", "Saving configuration parameters to NVM...");
    preferences.begin("enkelvoud", false);

    if (server.hasArg("reset_defaults") && server.arg("reset_defaults") == "on") {
        logAction("handleSaveSettings", "NVM", "Factory reset requested. Clearing preferences...");
        preferences.clear();
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

    if (server.hasArg("node_vol")) {
        serverVolumeMultiplier = server.arg("node_vol").toFloat() / 100.0f;
        preferences.putFloat("vol", serverVolumeMultiplier);
    }

    if (server.hasArg("lrc_1")) { pinLrc1 = server.arg("lrc_1").toInt(); preferences.putInt("lrc_1", pinLrc1); }
    if (server.hasArg("dout_1")) { pinDout1 = server.arg("dout_1").toInt(); preferences.putInt("dout_1", pinDout1); }
    if (server.hasArg("bclk_1")) { pinBclk1 = server.arg("bclk_1").toInt(); preferences.putInt("bclk_1", pinBclk1); }
    if (server.hasArg("lrc_2")) { pinLrc2 = server.arg("lrc_2").toInt(); preferences.putInt("lrc_2", pinLrc2); }
    if (server.hasArg("dout_2")) { pinDout2 = server.arg("dout_2").toInt(); preferences.putInt("dout_2", pinDout2); }
    if (server.hasArg("bclk_2")) { pinBclk2 = server.arg("bclk_2").toInt(); preferences.putInt("bclk_2", pinBclk2); }

    if (server.hasArg("aux_lrc")) { pinAuxLrc = server.arg("aux_lrc").toInt(); preferences.putInt("aux_lrc", pinAuxLrc); }
    if (server.hasArg("aux_din")) { pinAuxDin = server.arg("aux_din").toInt(); preferences.putInt("aux_din", pinAuxDin); }
    if (server.hasArg("aux_bclk")) { pinAuxBclk = server.arg("aux_bclk").toInt(); preferences.putInt("aux_bclk", pinAuxBclk); }

    if (server.hasArg("bt_lrc")) { pinBtLrc = server.arg("bt_lrc").toInt(); preferences.putInt("bt_lrc", pinBtLrc); }
    if (server.hasArg("bt_din")) { pinBtDin = server.arg("bt_din").toInt(); preferences.putInt("bt_din", pinBtDin); }
    if (server.hasArg("bt_bclk")) { pinBtBclk = server.arg("bt_bclk").toInt(); preferences.putInt("bt_bclk", pinBtBclk); }

    preferences.end();

    server.send(200, "text/plain", "Saved and Restarting");
    delay(500);
    ESP.restart();
}

void handleCancelSettings() {
    logAction("handleCancelSettings", "HTTP_REQ", "Settings change reverted.");
    server.send(200, "text/plain", "Cancelled");
}

void handleApiState() {
    String json = "{";
    json += "\"host_muted\": " + String(hostMuted ? "true" : "false") + ",";
    json += "\"streaming_enabled\": " + String(serverStreamingEnabled ? "true" : "false") + ",";
    json += "\"audio_input\": \"" + serverAudioInputMode + "\",";
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
        logAction("handleApiControl", "API_POST", "Received control action: " + body);

        if (body.indexOf("toggle_host_mute") != -1) {
            hostMuted = !hostMuted;
            logAction("handleApiControl", "STATE_CHANGE", "Host Mute toggled to: " + String(hostMuted ? "MUTED" : "UNMUTED"));
        }
        else if (body.indexOf("toggle_streaming") != -1) {
            serverStreamingEnabled = !serverStreamingEnabled;
            preferences.begin("enkelvoud", false);
            preferences.putBool("streaming", serverStreamingEnabled);
            preferences.end();
            logAction("handleApiControl", "STATE_CHANGE", "Streaming toggled to: " + String(serverStreamingEnabled ? "ON" : "OFF"));
        }
        else if (body.indexOf("set_audio_input") != -1) {
            String newMode = serverAudioInputMode;

            if (body.indexOf("Stream Radio Test") != -1) newMode = "Stream Radio Test";
            else if (body.indexOf("USB") != -1) newMode = "USB";
            else if (body.indexOf("Bluetooth") != -1) newMode = "Bluetooth";
            else if (body.indexOf("AUX in") != -1) newMode = "AUX in";

            if (newMode != serverAudioInputMode) {
                serverAudioInputMode = newMode;
                preferences.begin("enkelvoud", false);
                preferences.putString("audio_in", serverAudioInputMode);
                preferences.end();
                logAction("handleApiControl", "STATE_CHANGE", "Audio input changed to: " + serverAudioInputMode);
            }
        }
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            logAction("webSocketEvent", "DISCONNECT", "Client #" + String(num) + " disconnected.");
            break;
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            logAction("webSocketEvent", "CONNECT", "Client #" + String(num) + " connected from IP: " + ip.toString());
            break;
        }
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    logAction("setup", "INIT", "Booting Enkelvoud Audio Server...");

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
            logAction("setup", "WIFI", "Configured user static IP: " + serverStaticIp);
        } else {
            WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
            logAction("setup", "WIFI", "Configured router DHCP mode.");
        }

        logAction("setup", "WIFI", "Connecting to saved SSID: " + serverWifiSsid);
        WiFi.begin(serverWifiSsid.c_str(), serverWifiPass.c_str());

        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            connectedToNetwork = true;
            logAction("setup", "WIFI", "Successfully connected to network! IP: " + WiFi.localIP().toString());
        } else {
            logAction("setup", "WIFI", "Failed to connect to saved network. Falling back to AP mode.");
        }
    }

    if (!connectedToNetwork) {
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(ap_local_ip, ap_gateway, ap_subnet);
        WiFi.softAP(default_ap_ssid, default_ap_password);
        logAction("setup", "WIFI", "AP Mode Active (192.168.4.1). IP: " + WiFi.softAPIP().toString());
    } else {
        WiFi.enableSTA(true);
        WiFi.enableAP(false);
        logAction("setup", "WIFI", "AP Mode disabled. Running purely in STA mode.");
    }

    Serial.println("\n--------------------------------------------------");
    Serial.printf("HTTP Server active! Access via browser at:\n");
    if (connectedToNetwork) {
        Serial.printf("-> http://%s/\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("-> http://192.168.4.1/ (AP Mode)\n");
    }
    Serial.println("--------------------------------------------------\n");

    server.on("/", HTTP_GET, handleRoot);
    server.on("/player", HTTP_GET, handlePlayer);
    server.on("/settings", HTTP_GET, handleSettings);
    server.on("/Settings", HTTP_GET, handleSettings);
    server.on("/control", HTTP_GET, handleRoot);
    server.on("/control.html", HTTP_GET, handleRoot);

    server.on("/scan", HTTP_GET, handleApiScanWifi);
    server.on("/api/test_wifi", HTTP_POST, handleApiTestWifi);
    server.on("/api/volume", HTTP_POST, handleApiVolume);
    server.on("/save", HTTP_POST, handleSaveSettings);
    server.on("/cancel", HTTP_POST, handleCancelSettings);
    server.on("/api/state", HTTP_GET, handleApiState);
    server.on("/api/control", HTTP_POST, handleApiControl);

    server.begin();
    logAction("setup", "HTTP", "HTTP Server active on port 80");

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    logAction("setup", "WEBSOCKET", "WebSocket server active on port 8081");

    setupUSBInput();
    setupBluetoothInput();
    setupAuxInput();
    setupStreamInput();

    if (opus) opus->begin();
    if (encoder) encoder->begin(currentAudioInfo);

    logAction("setup", "READY", "System ready and streaming initialized.");
}

void loop() {
    server.handleClient();
    webSocket.loop();

    handleUSBInputLoop();
    handleBluetoothInputLoop();
    handleAuxInputLoop();
    handleStreamInputLoop();
}