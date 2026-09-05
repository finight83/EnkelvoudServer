#ifndef EVDBT_H
#define EVDBT_H

#include <Arduino.h>
#include "AudioTools.h"

extern String serverAudioInputMode;
extern bool hostMuted;
extern bool serverStreamingEnabled;
extern float serverVolumeMultiplier;
extern void logAction(const String& functionName, const String& eventType, const String& details);

extern int pinBtBclk;
extern int pinBtLrc;
extern int pinBtDin;

namespace {
    audio_tools::I2SStream btI2SStream;
    audio_tools::StreamCopy* btToEncoderCopier = nullptr;
    audio_tools::StreamCopy* btToDacCopier = nullptr;
    bool btInitialized = false;
}

inline void setupBluetoothInput() {
    auto cfg = btI2SStream.defaultConfig(RX_MODE);
    cfg.pin_bck = pinBtBclk;
    cfg.pin_ws = pinBtLrc;
    cfg.pin_data = pinBtDin;
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    cfg.sample_rate = 48000;
    cfg.is_master = false;

    btI2SStream.begin(cfg);
    logAction("setupBluetoothInput", "BT_OK", "Bluetooth I2S slave input module initialized at 48kHz.");
}

inline void handleBluetoothInputLoop() {
    if (serverAudioInputMode != "Bluetooth") {
        if (btInitialized) {
            delete btToEncoderCopier; btToEncoderCopier = nullptr;
            delete btToDacCopier; btToDacCopier = nullptr;
            btI2SStream.end();
            btInitialized = false;
            logAction("handleBluetoothInputLoop", "BT_INFO", "Bluetooth stream pipelines deactivated.");
        }
        return; 
    }

    if (!btInitialized) {
        setupBluetoothInput();
        btInitialized = true;
        extern EncodedAudioStream* encoder;
        extern I2SStream i2s;
        
        if (encoder) btToEncoderCopier = new StreamCopy(*encoder, btI2SStream);
        btToDacCopier = new StreamCopy(i2s, btI2SStream);
    }

    if (btInitialized && btI2SStream) {
        extern EncodedAudioStream* encoder;
        if (serverStreamingEnabled && !hostMuted && btToEncoderCopier && encoder) {
            btToEncoderCopier->copy();
        }
        if (btToDacCopier) {
            btToDacCopier->copy();
        }
    }
}

#endif // EVDBT_H