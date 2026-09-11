#ifndef EVDUSB_H
#define EVDUSB_H

#include <Arduino.h>
#include "EVDSRC.h"

extern String serverAudioInputMode;
extern void logAction(const String& functionName, const String& eventType, const String& details);

// USB speaker capture lives on the source ESP32; this server never enumerates as USB audio.
inline void setupUSBInput() {
    logAction("setupUSBInput", "USB_OK",
              "USB is selected on the source ESP32. This board only receives the forwarded I2S stream.");
}

// USB switching is handled by source-select commands, not a local USB gadget on the server.
inline void handleUSBInputLoop() {
    (void)serverAudioInputMode;
}

#endif