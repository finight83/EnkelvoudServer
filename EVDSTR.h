#ifndef EVDSTR_H
#define EVDSTR_H

#include "AudioTools.h"
#include "AudioTools/Communication/AudioHttp.h"

using namespace audio_tools;

extern String serverAudioInputMode;
extern String serverWifiSsid;
extern String serverWifiPass;
extern bool hostMuted;
extern bool serverStreamingEnabled;
extern void logAction(const String& functionName, const String& eventType, const String& details);

// Use extern pointers to defer initialization until after preferences are loaded
extern URLStream* radioStream;
extern StreamCopy* radioToEncoderCopier;

namespace {
    bool radioAttempted = false;
    unsigned long lastRadioPacketTime = 0;
    unsigned long lastRadioCheckLogTime = 0;
}

inline void setupStreamInput() {
    // Stream input initialization will be called from main setup() after preferences are loaded
}

inline void handleStreamInputLoop() {
    if (serverAudioInputMode != "Stream Radio Test") {
        if (radioStream && *radioStream) {
            radioStream->end();
            logAction("handleStreamInputLoop", "STREAM", "HTTP stream closed due to input mode change.");
        }
        radioAttempted = false;
        return;
    }

    unsigned long currentMillis = millis();

    if (!radioStream) {
        logAction("handleStreamInputLoop", "STREAM_ERR", "Radio stream not initialized in setup.");
        return;
    }

    if (!*radioStream && !radioAttempted) {
        logAction("handleStreamInputLoop", "STREAM", "Attempting connection to HTTP Audio Stream...");
        radioAttempted = true;
        bool started = radioStream->begin("http://stream.srg-ssr.ch/m/rsj/mp3_128", "audio/mp3");
        if (!started) {
            logAction("handleStreamInputLoop", "STREAM_ERR", "Failed to connect or open HTTP stream URL.");
        } else {
            logAction("handleStreamInputLoop", "STREAM", "HTTP audio stream connected successfully.");
        }
    }

    if (*radioStream) {
        if (!hostMuted) {
            // Route stream to DACs if applicable
        }

        if (serverStreamingEnabled && !hostMuted && radioToEncoderCopier) {
            size_t bytesCopied = radioToEncoderCopier->copy();
            if (bytesCopied > 0) {
                lastRadioPacketTime = currentMillis;
            }
        }
    }

    if (currentMillis - lastRadioCheckLogTime > 5000) {
        lastRadioCheckLogTime = currentMillis;
        if (*radioStream) {
            if (currentMillis - lastRadioPacketTime > 6000) {
                logAction("handleStreamInputLoop", "STREAM_WARN", "HTTP stream connected but no audio payload received recently.");
            } else {
                logAction("handleStreamInputLoop", "STREAM_OK", "HTTP stream active and receiving audio data.");
            }
        } else {
            logAction("handleStreamInputLoop", "STREAM_ERR", "HTTP stream is disconnected.");
        }
    }
}

#endif