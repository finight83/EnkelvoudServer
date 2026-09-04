#ifndef EVDUSB_H
#define EVDUSB_H

#include "AudioTools.h"
#include "AudioTools/Communication/USB/USBAudioStream.h"

using namespace audio_tools;

extern AudioInfo currentAudioInfo;
extern USBAudioStream in;
extern String serverAudioInputMode;
extern bool hostMuted;
extern bool serverStreamingEnabled;

extern int pinLrc1;
extern int pinDout1;
extern int pinBclk1;
extern int pinLrc2;
extern int pinDout2;
extern int pinBclk2;

extern I2SStream i2s1; 
extern I2SStream i2s2; 
extern StreamCopy* usbToDacCopier1;
extern StreamCopy* usbToDacCopier2;
extern StreamCopy* encoderCopier;

extern void logAction(const String& functionName, const String& eventType, const String& details);

namespace {
    unsigned long lastUsbPacketTime = 0;
    unsigned long lastUsbCheckLogTime = 0;
}

inline bool setupUSBInput() {
#ifdef TinyUSBDevice
    if (!TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.begin(0);
        logAction("setupUSBInput", "USB", "TinyUSB Device initialized.");
    }
#endif

    auto config = in.defaultConfig(TX_MODE); 
    config.copyFrom(currentAudioInfo);
    
    if (!in.begin(config)) {
        logAction("setupUSBInput", "USB_ERR", "Failed to initialize USB audio stream");
        return false;
    }

#ifdef TinyUSBDevice
    if (TinyUSBDevice.mounted()) {
        TinyUSBDevice.detach();
        delay(10);
        TinyUSBDevice.attach();
        logAction("setupUSBInput", "USB", "TinyUSB Device re-attached successfully.");
    } else {
        logAction("setupUSBInput", "USB", "TinyUSB Device not currently mounted to a host PC.");
    }
#endif
    
    return true;
}

inline void handleUSBInputLoop() {
    if (serverAudioInputMode != "USB") return;

    unsigned long currentMillis = millis();

    AudioInfo activeInfo = in.audioInfo();
    if (activeInfo.sample_rate > 0 && 
        (activeInfo.sample_rate != currentAudioInfo.sample_rate || 
         activeInfo.channels != currentAudioInfo.channels || 
         activeInfo.bits_per_sample != currentAudioInfo.bits_per_sample)) {
        
        currentAudioInfo = activeInfo;
        
        auto cfg1 = i2s1.defaultConfig(TX_MODE);
        cfg1.copyFrom(currentAudioInfo);
        cfg1.pin_bck = pinBclk1;
        cfg1.pin_ws = pinLrc1;
        cfg1.pin_data = pinDout1;
        
        if (!i2s1.begin(cfg1)) {
            logAction("handleUSBInputLoop", "USB_ERR", "Failed to reconfigure I2S1");
            return;
        }

        auto cfg2 = i2s2.defaultConfig(TX_MODE);
        cfg2.copyFrom(currentAudioInfo);
        cfg2.pin_bck = pinBclk2;
        cfg2.pin_ws = pinLrc2;
        cfg2.pin_data = pinDout2;
        
        if (!i2s2.begin(cfg2)) {
            logAction("handleUSBInputLoop", "USB_ERR", "Failed to reconfigure I2S2");
            return;
        }

        logAction("handleUSBInputLoop", "USB_CONFIG", String("USB Audio format updated: ") + currentAudioInfo.sample_rate + "Hz");
    }

    int availableBytes = in.available();
    if (availableBytes > 0) {
        lastUsbPacketTime = currentMillis;
        
        // Always play to the 2 DACs when not muted, regardless of streaming toggle state
        if (!hostMuted) {
            if (usbToDacCopier1) usbToDacCopier1->copy();
            if (usbToDacCopier2) usbToDacCopier2->copy();
        }

        // Toggled ON: additionally Opus encode and stream to WiFi nodes
        if (serverStreamingEnabled && !hostMuted) {
            if (encoderCopier) encoderCopier->copy();
        }
    }

    if (currentMillis - lastUsbCheckLogTime > 5000) {
        lastUsbCheckLogTime = currentMillis;
#ifdef TinyUSBDevice
        bool mounted = TinyUSBDevice.mounted();
#else
        bool mounted = true;
#endif
        if (!mounted) {
            logAction("handleUSBInputLoop", "USB_WARN", "USB device is not mounted to a host computer.");
        } else if (currentMillis - lastUsbPacketTime > 4000) {
            logAction("handleUSBInputLoop", "USB_WARN", "USB connected, but no audio stream data packets are being received from host.");
        } else {
            logAction("handleUSBInputLoop", "USB_OK", "USB audio stream connected and actively receiving data.");
        }
    }
}

#endif