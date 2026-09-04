#ifndef EVDSTR_H
#define EVDSTR_H

#include <Arduino.h>
#include <WiFi.h>
#include "AudioTools.h"
#include "AudioTools/Communication/AudioHttp.h"

using namespace audio_tools;

extern String serverAudioInputMode;
extern String serverWifiSsid;
extern String serverWifiPass;
extern bool hostMuted;
extern bool serverStreamingEnabled;
extern void logAction(const String& functionName, const String& eventType, const String& details);

extern URLStream* radioStream;
extern StreamCopy* radioToEncoderCopier;

namespace {
    bool radioAttempted = false;
    unsigned long lastRadioRetryAttempt = 0;
    // Increased cooldown to 30 seconds so it doesn't hammer DNS or starve the web server
    const unsigned long RADIO_RETRY_INTERVAL = 30000; 
}

inline void setupStreamInput() {
    // Initialization stub
}

inline void handleStreamInputLoop() {
    if (serverAudioInputMode != "Stream Radio Test") {
        if (radioStream && *radioStream) {
            radioStream->end();
            logAction("handleStreamInputLoop", "STREAM", "HTTP stream closed due to mode change.");
        }
        radioAttempted = false;
        return; 
    }

    if (!radioStream) return;

    // If Wi-Fi is down, don't attempt anything
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    // Only attempt a connection if we haven't tried yet, or if 30 seconds have passed since the last failure
    if (!*radioStream && (!radioAttempted || (millis() - lastRadioRetryAttempt > RADIO_RETRY_INTERVAL))) {
        logAction("handleStreamInputLoop", "STREAM", "Attempting connection to HTTP Audio Stream...");
        radioAttempted = true;
        lastRadioRetryAttempt = millis();
        
        bool started = radioStream->begin("http://stream.srg-ssr.ch/m/rsj/mp3_128", "audio/mp3");
        if (!started) {
            logAction("handleStreamInputLoop", "STREAM_ERR", "Stream connection failed. Backing off for 30s to keep web server responsive.");
        } else {
            logAction("handleStreamInputLoop", "STREAM", "HTTP audio stream connected successfully.");
        }
    }

    if (*radioStream) {
        if (serverStreamingEnabled && !hostMuted && radioToEncoderCopier) {
            radioToEncoderCopier->copy();
        }
    }
}

#endif