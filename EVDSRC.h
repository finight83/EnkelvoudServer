#ifndef EVDSRC_H
#define EVDSRC_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <HardwareSerial.h>

extern String serverAudioInputMode;
extern String serverSourceHost;
extern int pinSrcUartTx;
extern int pinSrcUartRx;
extern void logAction(const String& functionName, const String& eventType, const String& details);

static HardwareSerial sourceUart(2);
static bool sourceUartReady = false;
static String pendingSourceCommand;
static volatile bool sourceCommandQueued = false;

// Maps the control-panel input label to the compact UART / HTTP source token.
inline String sourceModeToken(const String& mode) {
    if (mode == "USB") return "USB";
    if (mode == "AUX in") return "AUX";
    if (mode == "None") return "NONE";
    return "BT";
}

// Starts the UART used to tell the source ESP32 which stream to forward.
inline void setupSourceLink() {
    sourceUart.begin(115200, SERIAL_8N1, pinSrcUartRx, pinSrcUartTx);
    sourceUartReady = true;
    logAction("setupSourceLink", "SRC",
              "Source UART TX=" + String(pinSrcUartTx) + " RX=" + String(pinSrcUartRx));
}

// Queues a source-select command so the audio tasks never block on HTTP.
inline void queueSourceSelectCommand(const String& mode) {
    pendingSourceCommand = sourceModeToken(mode);
    sourceCommandQueued = true;
}

// Sends SET_SOURCE over UART, then repeats it over HTTP if WiFi is up.
inline void sendSourceSelectCommand(const String& token) {
    String line = "SET_SOURCE " + token + "\n";
    if (sourceUartReady) {
        sourceUart.print(line);
    }

    if (WiFi.status() == WL_CONNECTED && serverSourceHost.length() > 0) {
        HTTPClient http;
        String url = "http://" + serverSourceHost + "/api/source";
        http.setTimeout(1500);
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        String body = "{\"input\":\"" + token + "\"}";
        int code = http.POST(body);
        http.end();
        logAction("sendSourceSelectCommand", "SRC",
                  "Told source " + serverSourceHost + " to use " + token + " (HTTP " + String(code) + ")");
    } else {
        logAction("sendSourceSelectCommand", "SRC", "UART command sent: " + token);
    }
}

// Flushes one queued source-select command from the main loop.
inline void handleSourceCommandLoop() {
    if (!sourceCommandQueued) return;
    sourceCommandQueued = false;
    String token = pendingSourceCommand;
    sendSourceSelectCommand(token);
}

#endif
