#ifndef EVDBT_H
#define EVDBT_H

#include <Arduino.h>
#include "EVDSRC.h"

extern String serverAudioInputMode;
extern void logAction(const String& functionName, const String& eventType, const String& details);

// Bluetooth is received on the source ESP32, not on this ESP32-S3.
inline void setupBluetoothInput() {
    logAction("setupBluetoothInput", "BT_OK",
              "Bluetooth is selected on the source ESP32. This board only receives I2S on pins 15/16/17.");
}

// No local Bluetooth stack: the source board is commanded when the input mode changes.
inline void handleBluetoothInputLoop() {
    (void)serverAudioInputMode;
}

#endif
