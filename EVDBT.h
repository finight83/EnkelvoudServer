#ifndef EVDBT_H
#define EVDBT_H

#include <Arduino.h>
#include "AudioTools.h"

// External references from main sketch
extern String serverAudioInputMode;
extern bool hostMuted;
extern bool serverStreamingEnabled;
extern float serverVolumeMultiplier;
extern void logAction(const String& functionName, const String& eventType, const String& details);

extern int pinBtBclk;
extern int pinBtLrc;
extern int pinBtDin;

// AudioTools processing streams for Bluetooth input
namespace {
    audio_tools::I2SStream btI2SStream;
    // FormatConverterStream handles 32-bit to 16-bit sample alignment to prevent static/distortion
    audio_tools::FormatConverterStream btConverter(btI2SStream);
    audio_tools::VolumeStream btVolumeStream(btConverter);
    audio_tools::StreamCopy* btToEncoderCopier = nullptr;
    audio_tools::StreamCopy* btToDacCopier1 = nullptr;
    bool btInitialized = false;
    unsigned long lastBtCheckLogTime = 0;
}

inline void setupBluetoothInput() {
    // Configure ESP32-S3 as an I2S SLAVE. Many BT modules output 32-bit slots.
    auto cfg = btI2SStream.defaultConfig(RX_MODE);
    cfg.pin_bck = pinBtBclk;     
    cfg.pin_ws = pinBtLrc;      
    cfg.pin_data = pinBtDin;    
    cfg.channels = 2;
    cfg.bits_per_sample = 32; // Set to 32 to safely ingest master frames without alignment static (converted downstream)
    cfg.sample_rate = 44100;    
    cfg.is_master = false;      
    
    // Robust buffer settings to eliminate I2S jitter stuttering
    cfg.buffer_count = 12;
    cfg.buffer_size = 1024;

    btI2SStream.begin(cfg);
    btConverter.begin(cfg);
    
    auto volCfg = cfg;
    volCfg.bits_per_sample = 16; // Output standard 16-bit to volume and copy pipelines
    btVolumeStream.begin(volCfg);
    
    logAction("setupBluetoothInput", "BT_OK", "Bluetooth I2S slave input module initialized with 32-bit safety alignment.");
}

inline void handleBluetoothInputLoop() {
    if (serverAudioInputMode != "Bluetooth") {
        if (btInitialized) {
            delete btToEncoderCopier; btToEncoderCopier = nullptr;
            delete btToDacCopier1; btToDacCopier1 = nullptr;
            btInitialized = false;
            logAction("handleBluetoothInputLoop", "BT_INFO", "Bluetooth stream pipelines deactivated.");
        }
        return; 
    }

    if (!btInitialized) {
        setupBluetoothInput();
        extern EncodedAudioStream* encoder;
        extern I2SStream i2s1;
        
        if (encoder) btToEncoderCopier = new StreamCopy(*encoder, btVolumeStream);
        btToDacCopier1 = new StreamCopy(i2s1, btVolumeStream);
        btInitialized = true;
    }

    btVolumeStream.setVolume(serverVolumeMultiplier);

    unsigned long currentMillis = millis();
    if (currentMillis - lastBtCheckLogTime > 30000) {
        lastBtCheckLogTime = currentMillis;
        logAction("handleBluetoothInputLoop", "BT_INFO", "Bluetooth audio stream active.");
    }

    if (!hostMuted) {
        if (serverStreamingEnabled && btToEncoderCopier) {
            btToEncoderCopier->copy();
        } else if (btToDacCopier1) {
            btToDacCopier1->copy();
        }
    }
}

#endif