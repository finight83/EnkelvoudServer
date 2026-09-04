#ifndef EVDBT_H
#define EVDBT_H

#include <Arduino.h>

extern String serverAudioInputMode;
extern bool hostMuted;
extern bool serverStreamingEnabled;
extern void logAction(const String& functionName, const String& eventType, const String& details);

namespace {
    unsigned long lastBtCheckLogTime = 0;
}

inline void setupBluetoothInput() {
    // Bluetooth initialization stub
}

inline void handleBluetoothInputLoop() {
    if (serverAudioInputMode != "Bluetooth") return;

    unsigned long currentMillis = millis();
    if (currentMillis - lastBtCheckLogTime > 5000) {
        lastBtCheckLogTime = currentMillis;
        bool btConnected = false; 
        if (!btConnected) {
            logAction("handleBluetoothInputLoop", "BT_WARN", "Bluetooth input selected, but no active device connection or audio stream detected.");
        } else {
            logAction("handleBluetoothInputLoop", "BT_OK", "Bluetooth audio stream active.");
        }
    }

    if (!hostMuted) {
        // Route Bluetooth audio to DACs
    }

    if (serverStreamingEnabled && !hostMuted) {
        // Opus encode & stream Bluetooth audio to WiFi nodes
    }
}

#endif