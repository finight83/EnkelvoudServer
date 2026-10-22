#pragma once
// ============================================================================
//  EVDUsb.h - USB Audio Class (UAC) device on the ESP32-S3 server
//
//  Wraps EspUsbDevice / EspUsbAudioFunction so the rest of the tree sees only
//  four functions (startServerUsbAudio, stopServerUsbAudio, readServerUsbAudio,
//  usbLastError). All library quirks stay in this file.
//
//  ---- Why Windows volume did nothing --------------------------------------
//
//  The library exposes the UAC Feature Unit as a state object plus an event
//  queue. When Windows moves the volume slider it sends SET_CUR on the
//  control endpoint; the library turns that into an EspUsbAudioEvent the
//  sketch polls. The library does NOT apply any gain to the PCM stream -
//  that is the sketch's job, because only the sketch knows what data path
//  the samples take. Without polling the events and scaling the buffer
//  ourselves, the host's volume control is silently discarded.
//
//  readServerUsbAudio() now polls the queue on every call, keeps the master
//  volume and mute, and scales the returned buffer in place. Everything
//  downstream (resampler, quiet filter, dashboard master volume, Opus) sees
//  an already-attenuated signal and is unaware of the USB Feature Unit.
//
//  Two globals are exposed for diagnostics:
//
//      gUsbHostVolumeDb256   last SET_CUR volume, 0 dB = 0, -90 dB = -23040
//      gUsbHostMuted         last SET_CUR mute state
//
//  Both are written from audioReadTask (the only caller of readServerUsbAudio)
//  so no lock is needed for the simple observation case.
//
//  ---- Wiring ---------------------------------------------------------------
//
//      USB D-  GPIO19
//      USB D+  GPIO20
//
//  On most dev boards this is the second USB-C port, sometimes labelled
//  "USB" or "OTG" to distinguish it from the "UART" port used for flashing.
// ============================================================================

#include "EVDHelpers.h"

#if EVD_HAS_USB_DEVICE_LIB
  #include <EspUsbDevice.h>

  // ---- USB device objects --------------------------------------------------
  static EspUsbDevice              *gUsbDevice   = nullptr;
  static EspUsbAudioFunction       *gUsbAudio    = nullptr;
  static EspUsbAudioPlaybackStream *gUsbPlayback = nullptr;
  static volatile bool              gUsbRunning  = false;
  static const char                *gUsbLastError = "";
  inline const char *usbLastError() { return gUsbLastError; }

  // ---- Host volume / mute state -------------------------------------------
  // USB Audio 1.0 units: 1/256 dB. 0 = 0 dB (full scale),
  // -90 * 256 = -23040 = -90 dB (effectively silence). Windows sends 0 at the
  // top of the slider and the minimum at the bottom.
  //
  // [usb-host-volume-single-tu] Actual definitions live in EVDState.h so the
  // library branch and the no-library branch share one instance. Comments
  // stay here for context; the polling/apply logic below is unchanged.

  // ---- Event poll and gain application ------------------------------------
  // Called from readServerUsbAudio() on the audio task. Drains the library's
  // event queue and updates the two globals above. Cheap when there are no
  // events (one atomic load on the queue head).
  inline void pollUsbAudioEvents() {
    if (!gUsbAudio) return;
    EspUsbAudioEvent ev;
    // Cap the drain so a pathological flood cannot starve the read loop.
    for (int i = 0; i < 16 && gUsbAudio->pollEvent(ev); ++i) {
      // Events carry a target; capture-side changes must not affect the
      // playback buffer.
      if (ev.target == EspUsbAudioEventTarget::Capture) continue;

      switch (ev.type) {
        case EspUsbAudioEventType::VolumeChanged:
          if (ev.channel == 0) {
            gUsbHostVolumeDb256      = ev.volumeDb256;
            gUsbHostHasEverSetVolume = true;
            LOGI("USB host volume: %.2f dB (raw %d)",
                 (float)ev.volumeDb256 / 256.0f, (int)ev.volumeDb256);
          } else {
            LOGI("USB host volume (ch %u): %.2f dB (ignored for master gain)",
                 (unsigned)ev.channel, (float)ev.volumeDb256 / 256.0f);
          }
          break;
        case EspUsbAudioEventType::MuteChanged:
          if (ev.channel == 0) {
            gUsbHostMuted            = ev.muted;
            gUsbHostHasEverSetVolume = true;
            LOGI("USB host mute: %s", ev.muted ? "on" : "off");
          }
          break;
        case EspUsbAudioEventType::SampleRateChanged:
          // The library tracks this internally. We do not use it: the
          // resampler already handles 44100 <-> 48000 and the source rate
          // is fixed by the source board.
          break;
        case EspUsbAudioEventType::StreamStateChanged:
          LOGI("USB audio: stream %s (alt=%u)",
               ev.enabled ? "started" : "stopped",
               (unsigned)ev.alternateSetting);
          break;
      }
    }
  }

  // ---- startServerUsbAudio ------------------------------------------------
  // Enumerates us as a UAC1 speaker at SOURCE_SAMPLE_RATE (44100 Hz),
  // 16-bit, stereo. The rate matches the I2S path exactly so the resampler
  // ratio in EVDAudio.h stays valid - no pitch shift, no resample step.
  //
  // Returns true on success. On failure, all objects are cleaned up and
  // false is returned, leaving the previous source untouched.
  inline bool startServerUsbAudio() {
    if (gUsbRunning) return true;

    LOGI("USB audio: enumerating UAC1 speaker (%d Hz, %d-bit, %d ch)...",
         SOURCE_SAMPLE_RATE, 16, CHANNELS);
  #if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32P4)
    LOGI("USB audio: free heap %u B, VID:PID %04x:%04x", (unsigned)ESP.getFreeHeap(),
         (unsigned)EVD_USB_VID, (unsigned)EVD_USB_PID);
  #else
    LOGE("USB audio: this chip has no native USB-OTG (needs ESP32-S2/S3/P4)");
    gUsbLastError = "unsupported_target";
    return false;
  #endif
  #if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1
    LOGW("USB audio: ARDUINO_USB_MODE=1 (hardware CDC/JTAG); select 'USB-OTG (TinyUSB)' mode if begin() fails");
  #endif

    gUsbDevice = new (std::nothrow) EspUsbDevice();
    if (!gUsbDevice) {
      LOGE("USB audio: out of memory allocating EspUsbDevice");
      gUsbLastError = "alloc_failed";
      return false;
    }
    gUsbAudio = new (std::nothrow) EspUsbAudioFunction(*gUsbDevice);
    if (!gUsbAudio) {
      LOGE("USB audio: out of memory allocating EspUsbAudioFunction");
      delete gUsbDevice; gUsbDevice = nullptr;
      gUsbLastError = "alloc_failed";
      return false;
    }
    gUsbPlayback = &gUsbAudio->addPlaybackStream();

    // 44100 Hz to match the I2S input and the pipeline's nominal ratio.
    // Format: {sampleRate, channels, bytesPerSamplePerChannel, bitsPerSample}
    // The third field is UAC's bSubFrameSize - bytes per channel - so 2 for
    // 16-bit, NOT 4 (which would claim "32-bit samples, 16 bits used" and
    // make Windows negotiate a mismatch after enumeration).
    gUsbPlayback->addFormat({ (uint32_t)SOURCE_SAMPLE_RATE,
                              (uint8_t)CHANNELS,
                              (uint8_t)2,
                              (uint8_t)16 });

    EspUsbDeviceConfig cfg;
    cfg.vid          = EVD_USB_VID;
    cfg.pid          = EVD_USB_PID;
    cfg.manufacturer = "Enkelvoud";
    cfg.product      = "Enkelvoud USB";
    cfg.serialNumber = "enkelvoud-usb";

    if (!gUsbDevice->begin(cfg)) {
      LOGE("USB audio: device.begin() FAILED: %s (heap %u B, VID:PID %04x:%04x)",
           gUsbDevice->lastErrorName(), (unsigned)ESP.getFreeHeap(),
           (unsigned)EVD_USB_VID, (unsigned)EVD_USB_PID);
      LOGE("USB audio: check board is ESP32-S2/S3/P4, USB Mode = USB-OTG (TinyUSB), "
           "and that D-/D+ (GPIO19/20) are not used by CDC/JTAG");
      gUsbLastError = "begin_failed";
      gUsbPlayback = nullptr;
      delete gUsbAudio;  gUsbAudio  = nullptr;
      delete gUsbDevice; gUsbDevice = nullptr;
      return false;
    }

    // The audio function's graph is built inside device.begin(), so only now
    // are the Feature Unit accessors valid. Read the state Windows will see
    // on enumeration.
    gUsbHostVolumeDb256      = 0;
    gUsbHostMuted            = false;
    gUsbHostHasEverSetVolume = false;

    int16_t v = 0;
    bool    m = false;
    if (gUsbAudio->getVolume(v)) gUsbHostVolumeDb256 = v;
    if (gUsbAudio->getMute(m))   gUsbHostMuted       = m;

    EspUsbAudioVolumeRange range;
    if (gUsbAudio->getVolumeRange(range)) {
      LOGI("USB audio: Feature Unit volume range %d..%d (1/256 dB), res %d",
           (int)range.min, (int)range.max, (int)range.resolution);
    } else {
      LOGW("USB audio: Feature Unit does not expose a volume range - "
           "host volume control will be a no-op");
    }
    LOGI("USB audio: initial Feature Unit state - volume %.2f dB, mute %s",
         (float)gUsbHostVolumeDb256 / 256.0f, gUsbHostMuted ? "on" : "off");

    gUsbLastError = "";
    gUsbRunning = true;
    LOGI("USB audio: device enumerated, VID:PID %04x:%04x, serial=%s",
         (unsigned)EVD_USB_VID, (unsigned)EVD_USB_PID, cfg.serialNumber);
    LOGI("USB audio: heap after begin %u B, playback stream ready=%s",
         (unsigned)ESP.getFreeHeap(), gUsbPlayback ? "yes" : "no");
    LOGI("USB audio: speaker ready - plug the host into the USB-OTG port");
    return true;
  }

  // ---- stopServerUsbAudio -------------------------------------------------
  // Tears the device down. The host sees the device disappear, which is the
  // correct UX: while the source is BT/AUX, we are not a USB sound card.
  inline void stopServerUsbAudio() {
    if (!gUsbRunning) return;
    LOGI("USB audio: stopping device");
    gUsbRunning = false;
    delay(50);   // give any in-flight read() one more pass
    if (gUsbDevice) {
      gUsbDevice->end();
      gUsbPlayback = nullptr;   // owned by gUsbAudio (returned by reference)
      delete gUsbAudio;    gUsbAudio    = nullptr;
      delete gUsbDevice;   gUsbDevice   = nullptr;
    }
    // Reset tracked host state so the next start() begins from defaults
    // rather than whatever the previous session left behind.
    gUsbHostVolumeDb256      = 0;
    gUsbHostMuted            = false;
    gUsbHostHasEverSetVolume = false;
    LOGI("USB audio: device stopped");
  }

  // ---- readServerUsbAudio -------------------------------------------------
  // Pulls PCM from the UAC playback stream, then applies the host's current
  // volume and mute to the samples in place before returning. This is the
  // single point where Windows' slider actually changes the audio: every
  // sample that leaves this function has already had the Feature Unit's
  // gain applied.
  //
  // The caller (audioReadTask) treats 0 as "nothing right now" and sleeps.
  inline size_t readServerUsbAudio(uint8_t *buf, size_t maxBytes) {
    if (!gUsbRunning || !gUsbPlayback) return 0;

    // Drain the event queue for diagnostics, but do not apply the gain to
    // the PCM. Windows applies its own software gain to the stream before
    // sending it, and multiplies with our hardware gain if we also scale.
    // The observed result was double attenuation that made everything below
    // 100% inaudible. Passthrough is what actually matches the user's
    // expectation of the Windows slider.
    pollUsbAudioEvents();

    return gUsbPlayback->read(buf, maxBytes);
  }

#else  // EVD_HAS_USB_DEVICE_LIB == 0

  // ---- No-op fallbacks ----------------------------------------------------
  // Server compiles and runs normally without a UAC library. Selecting USB
  // on the dashboard produces a clear log line and a 502 to the browser.
  // [usb-host-volume-single-tu] The gUsbHost* globals are defined in
  // EVDState.h so this branch and the library branch share one instance.

  inline bool   startServerUsbAudio() {
    LOGE("USB audio unavailable: firmware built without the EspUsbDevice library");
    return false;
  }
  inline const char *usbLastError() { return "no_library"; }
  inline void   stopServerUsbAudio()  {}
  inline size_t readServerUsbAudio(uint8_t *, size_t) { return 0; }

#endif  // EVD_HAS_USB_DEVICE_LIB