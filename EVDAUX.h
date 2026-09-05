#ifndef EVDAUX_H
#define EVDAUX_H

#include <Arduino.h>

extern String serverAudioInputMode;
extern bool hostMuted;
extern bool serverStreamingEnabled;
extern void logAction(const String& functionName, const String& eventType, const String& details);

namespace {
    unsigned long lastAuxCheckLogTime = 0;
}

inline void setupAuxInput() {
    logAction("handleAuxInputLoop", "AUX_OK", "AUX input module initialized.");
}

inline void handleAuxInputLoop() {
    if (serverAudioInputMode != "AUX in") return;

    unsigned long currentMillis = millis();
    if (currentMillis - lastAuxCheckLogTime > 30000) {
        lastAuxCheckLogTime = currentMillis;
        bool auxSignalPresent = true; 
        if (!auxSignalPresent) {
            logAction("handleAuxInputLoop", "AUX_WARN", "AUX input selected, but no valid audio input signal detected.");
        }
    }

    if (!hostMuted) {
        // Route AUX audio to DACs
    }

    if (serverStreamingEnabled && !hostMuted) {
        // Stream AUX audio to network nodes
    }
}

#endif