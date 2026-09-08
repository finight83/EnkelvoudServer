#ifndef EVDAUX_H
#define EVDAUX_H

#include <Arduino.h>
#include "EVDSRC.h"

extern String serverAudioInputMode;
extern void logAction(const String& functionName, const String& eventType, const String& details);

// AUX is captured on the source ESP32 and forwarded over the shared I2S link.
inline void setupAuxInput() {
    logAction("setupAuxInput", "AUX_OK",
              "AUX in is selected on the source ESP32. This board only receives the forwarded I2S stream.");
}

// AUX switching is handled by source-select commands, not a local ADC on the server.
inline void handleAuxInputLoop() {
    (void)serverAudioInputMode;
}

#endif
