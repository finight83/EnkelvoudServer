#ifndef EVDUSB_H
#define EVDUSB_H

#include <Arduino.h>
#include "AudioTools.h"
#include "AudioTools/Communication/USB/USBAudioStream.h"

using namespace audio_tools;

extern String serverAudioInputMode;
extern bool hostMuted;
extern bool serverStreamingEnabled;
extern void logAction(const String& functionName, const String& eventType, const String& details);

extern USBAudioStream in;
extern StreamCopy* copier;            

inline void setupUSBInput() {
    logAction("handleUSBInputLoop", "USB_OK", "USB input module initialized.");
}

inline void handleUSBInputLoop() {
    if (serverAudioInputMode != "USB") return; // Completely halts processing if another input is selected

    if (serverStreamingEnabled && !hostMuted) {
        if (copier) copier->copy();
    }
}

#endif