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
    // AUX input initialization stub
}

inline void handleAuxInputLoop() {
    if (serverAudioInputMode != "AUX in") return;

    unsigned long currentMillis = millis();
    if (currentMillis - lastAuxCheckLogTime > 5000) {
        lastAuxCheckLogTime = currentMillis;
        bool auxSignalPresent = true; 
        if (!auxSignalPresent) {
            logAction("handleAuxInputLoop", "AUX_WARN", "AUX input selected, but no valid audio input signal detected on pins.");
        } else {
            logAction("handleAuxInputLoop", "AUX_OK", "AUX input connected and receiving signal.");
        }
    }

    if (!hostMuted) {
        // Route AUX audio to DACs
    }

    if (serverStreamingEnabled && !hostMuted) {
        // Opus encode & stream AUX audio to WiFi nodes
    }
}

#endif