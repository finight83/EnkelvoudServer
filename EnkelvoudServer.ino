// ---- Versioning ------------------------------------------------------------
#ifndef EVD_FW_VERSION
#define EVD_FW_VERSION "3.9.1.6" // USB: 10% volume on connect, Windows slider polling
#endif
#define FW_VERSION EVD_FW_VERSION

static const char *SETTINGS_VERSION = "v2.0.7";

/*
  ============================================================================
  EnkelvoudServer (ESP32-S3)
  I2S / USB / NETWORK AUDIO -> Opus Encoder -> UDP unicast to player nodes

  Transport: Opus frames are fanned out over UDP unicast to each registered
  subscriber on audio :4202 and ctrl :4203. A separate request/response
  clock-sync exchange runs on :4204 (server receives sync_req, replies to
  the node's source port). Registration, heartbeats and node stats stay
  HTTP. The 30 s authoritative time_sync broadcast rides the ctrl channel
  (sendAuthoritativeTimeSync() -> sendCtrlToSubscriber()), not :4204.
  DLNA/UPnP MediaRenderer support adds a second audio ingress (HTTP WAV
  pulled from a controller), fed into the same rawQueue as the local
  I2S/USB paths.

  ============================ Project files ================================

  --- Inline headers (all in the same translation unit) ---------------------

  EVDState.h
      Every mutable global: ServerInputSource enum, audioBufferMs +
      per-mode ModeEncSettings presets, queues, stats counters (incl. the
      DLNA statNet* counters), all mutexes, source-log ring, LOG ring,
      Opus runtime variables (g_new* / g_current* / g_pendingOpusSampleRate).
      Included first because every other header references these symbols.

  EVDHelpers.h
      Cross-task helpers. PrefsGuard RAII for the NVS mutex; lockFor/
      unlockFor; find/prune/snapshot for the subscriber table and player
      list; String escaping (jsonEscape, htmlEscape, sanitizeServerName).
      Also forward-declares the OTA flags so EVDWiFi.h can gate on them
      before EVDOTA.h is reached.

      Every symbol forward-declared in EnkelvoudServer.ino is DEFINED
      inline in one of these headers; the inline keyword on the first
      declaration is load-bearing for multi-TU safety
      ([inline-forward-decls]).

  EVDWiFi.h
      WiFi bring-up. startSetupAccessPoint (AP fallback), setup_wifi_sta
      (STA with event handler, mDNS), UDP discovery responder, network-
      test task, WiFi scan endpoints, handleRestart. Also owns the per-mode
      encoder preset load/save to NVS and applyModeSettingsToLive() — the
      single point that copies a mode preset into the live encoder vars.

      Every mutating handler here is OTA-gated ([ota-gate-save],
      [ota-gate-scan], [ota-gate-restart]): an NVS write, a WiFi scan, or
      an ESP.restart() during Phase 1/2 of a node push stalls or resets
      the TCP upload socket.

  EVDUDP.h
      UDP transport. setup_udp_transport() creates ctrlMutex and binds
      the three sockets; udpSendTask paces opusQueue→subscribers (with the
      absolute frame schedule and stale-backlog handling); sendCtrlToSubscriber;
      handleClockSyncRequests + clockSyncTask (RTT sync responder); the
      30 s authoritative time_sync broadcast. Never touches I2S or HTTP.

  EVDAudio.h
      The audio pipeline. ResamplerToOpusRate (Catmull-Rom cubic with
      ratio-1 fast path), quiet-mode biquad + peak limiter, anti-alias
      24 kHz filter, setup_opus / handleEncoderRecreate. Owns the three
      tasks: audioReadTask (I2S DMA / USB / NETWORK-yield), audioProcessing-
      Task (rate estimator, two-stage resample, quiet, volume →
      encodeQueue), audioEncodeTask (opus_encode → opusQueue), plus
      setup_queues_and_tasks().

  EVDDLNA.h
      DLNA/UPnP MediaRenderer. dlnaSsdpTaskFn announces/responds on
      239.255.255.250:1900; description.xml + 3 SCPD XMLs; SOAP handlers
      for AVTransport / RenderingControl / ConnectionManager. When the
      controller calls SetAVTransportURI + Play, dlnaFetchTaskFn opens the
      URI as HTTP WAV, resamples to 48 kHz (via a second Resampler instance
      owned here), and pushes RawChunk into rawQueue. Sets
      serverInputSource = SRC_FROM_NETWORK on entry and restores
      dlnaPrevSource on Stop. Exposes dlnaStartFetch / dlnaStopFetch /
      initDlna, plus the dlnaPlaying / dlnaPaused / dlnaVolume (volatile
      int) flags that EVDPlayers.h and EVDMain.h reference.

  EVDUsb.h
      USB Audio Class (UAC1) device. startServerUsbAudio enumerates a
      48 kHz 16-bit stereo speaker via EspUsbDevice; readServerUsbAudio
      drains it; pollUsbAudioEvents mirrors Windows volume/mute into the
      gUsbHostVolumeDb256 / gUsbHostMuted diagnostics. stopServerUsbAudio
      uses gUsbLifecycleMutex to tear down cleanly against a blocked read.
      Compiled out (no-op stubs) when EVD_HAS_USB_DEVICE_LIB == 0.

  EVDZones.h
      Zone CRUD. serializeZones/loadZonesFromNVS (bar-encoded NVS string),
      saveZonesToNVS (defers to loop() via settingsDirty — no flash from
      the HTTP task), and the five HTTP handlers (create / delete / move /
      config / remove-node). All validate the alphanumeric-only zone name
      rule and re-broadcast config after every mutation.

  EVDPlayers.h
      Player registration + heartbeat (handlePlayerRegistration), receiver
      UART link parser (handleReceiverLink), setReceiverSource /
      bridgeSendReceiverCmd, per-node perf (updateNodePerf),
      sendZoneConfigToIp, broadcastNodeConfig, and
      the /api/config | /api/source | /api/cmd | /api/audio/encoding*
      handlers. Also defines handleNodeStats (/api/node_stats) and
      handleOtaResult (/api/player/ota_result). Calls dlnaStopFetch() at
      the top of handleApiSourceBridge so any manual source change cleanly
      hands the rawQueue back.

  EVDRoutes.h
      setup_http_server(). Single point of route registration for every
      HTTP endpoint: OTA (plan/status/apply/upload/delete), the new DLNA
      endpoints (/description.xml, /upnp/scpd/*.xml, /upnp/control/*,
      /upnp/event/*), the dashboard (gzip-compressed, served from
      EVDCTRL_GZ.h), network config, /api/state /api/stats /api/log/*,
      /api/player/*, /api/audio/encoding*, /api/zones/*, /api/source*,
      /api/bt/*, and /api/discover. Calls server.begin() as the last action.

  EVDMain.h
      setup() and loop() — non-inline (Arduino main.cpp needs external
      linkage). setup_serial logs the reset reason + dashboard gzip size.
      Boot sequencing is load-bearing: createSyncPrimitives → LittleFS
      format-once → loadWiFiSettings → loadZonesFromNVS → setup_wifi_sta
      → mDNS → setup_opus → configureQuietFilters → setupDiscoveryResponder
      → setup_udp_transport → setup_http_server → initDlna →
      clockSyncTask → setupReceiverLink → restore boot source →
      setup_i2s_in → 2 s resampler seed → setup_queues_and_tasks.
      loop() drives factory-reset pin, otaCheckStuckJob, serviceVolumePot,
      discovery, deferred NVS flush, rate-change
      broadcast, 15 s stats window (with DLNA counter reset), rate
      anomaly detector, and GET_BT_DEVICES poll.

  EVDOTA.h
      OTA manager. Spool scanner (LittleFS /ota_spool), otaPushToNode
      (HTTP multipart with per-chunk retry), otaPushToSource (UART
      OTA_BEGIN|DATA|END protocol with CRC-16 + MD5), the otaJob task
      (sequenced: source → nodes → server, then ESP.restart), the per-
      target completion tracking, the stuck-job watchdog
      otaCheckStuckJob, and the /api/ota/{plan,status,apply,upload,delete}
      handlers with all the fragmentation and lock-timeout guards.

  EVDSTS.h
      /api/state and /api/stats JSON endpoints. Both use a single
      beginResponse(200, ..., body) — NOT AsyncResponseStream — so the
      response is one Content-Length-framed body instead of ~50–70 small
      chunked writes. This is the biggest fixed-cost win on a heap that is
      already at 90 %. handleApiState skips the heavy JSON during OTA and
      returns only the scalar fields the dashboard still needs. Includes
      the CPU-delta wrap-safe statistics, heap / DMA / PSRAM pills, the
      per-task CPU breakdown, and the encoder-state passthrough fields.

  --- Generated headers (do not hand-edit) ----------------------------------

  EVDCTRL_GZ.h
      Gzip-compressed copy of the dashboard HTML. Generated from EVDCTRL.h
      by gen_gz.py / gen_gz.bat. Served by server.on("/") with
      Content-Encoding: gzip. STALE-FILE HAZARD: if you edit EVDCTRL.h and
      forget to regenerate this header, the build succeeds and the OLD
      dashboard is served with no warning. setup_serial() logs the size
      as a fingerprint — compare against gen_gz.py's printed length.

  --- Editable dashboard sources -------------------------------------------

  EVDCTRL.h
      The dashboard, as a PROGMEM string. Single file containing HTML +
      CSS + the entire <script> block that drives Zones / Network /
      Advanced (Status/Source/Encoding) / Update tabs, the OTA wizard,
      the diagnostics recorder, and the WiFi scan modal. EVDCTRL_GZ.h is
      derived from this file — this is the file you edit.

  EVDNTW.h
      Standalone network-setup page served at GET /settings when the
      AP fallback is active. Minimal HTML form + WiFi scan modal, self-
      contained. Independent of the main dashboard. Pulled in by
      EVDWiFi.h; defines EVDNTW_HTML_TEMPLATE and networkSettingsPage().

  --- Non-C++ project files ------------------------------------------------

  EnkelvoudServer.ino
      This file. Includes the headers in the load-bearing order below,
      defines the pin/audio constants, the POD structs (RawChunk,
      OpusPacket, EncodeFrame, Subscriber, PlayerNode, Zone, NodePerf),
      and the forward declarations. No logic lives here beyond the
      merged-in EVDProtocolCompat.h and EVDConfig.h blocks.

      RawChunk and OpusPacket sizing is deliberately 16 slots each
      ([raw-queue-16-measured] / [opus-queue-len-16]) so a future edit
      to one cannot silently mismatch the encoder drain rate against the
      pacer input rate.

  build_opt.h
      Build flags. FreeRTOS run-time statistics for the CPU pills;
      LWIP pool sizes (pbuf pool, sockets, TCP window); AsyncTCP
      pinning (core 0, pri 1); Arduino core selection.

  partitions.csv
      Flash layout: 2 × 4 MB OTA app partitions + 7.9 MB LittleFS (used
      as the OTA spool) + NVS + coredump + otadata. Changing these breaks
      existing firmware binary compatibility.

  ============================ Include order ================================

  The order below is load-bearing. Each header assumes the symbols
  introduced by everything above it in this list.

    EVDState.h      no deps; defines every shared global
    EVDHelpers.h    uses EVDState globals (mutexes, subscribers[], playerNodes)
    EVDWiFi.h       uses EVDHelpers (PrefsGuard, jsonEscape); also owns
                    the per-mode encoder preset load/save to NVS
    EVDNTW.h        pulled in by EVDWiFi.h; defines EVDNTW_HTML_TEMPLATE
                    and networkSettingsPage() served at GET /settings
    EVDUDP.h        uses EVDHelpers (lockFor/unlockFor, snapshotSubscribers);
                    defines ctrlMutex, setup_udp_transport, udpSendTask
    EVDAudio.h      uses EVDState + EVDHelpers + EVDUsb (USB read path);
                    defines ResamplerToOpusRate, rawQueue producers,
                    g_rateEstimatorNeedsReset
    EVDDLNA.h       uses EVDAudio (RawChunk, ResamplerToOpusRate, the rate-
                    estimator reset flag) + EVDState (rawQueue); exposes
                    dlnaStopFetch / dlnaPlaying / dlnaPaused / initDlna
    EVDZones.h      uses EVDHelpers (snapshot); forward-declares the
                    EVDPlayers config-push symbols it calls
    EVDPlayers.h    uses EVDDLNA (dlnaStopFetch/dlnaPlaying/dlnaPaused) +
                    EVDUsb (start/stop); defines handleApiConfig,
                    broadcastNodeConfig, updateNodePerf, handleNodeStats,
                    handleOtaResult
    EVDRoutes.h     uses EVDDLNA handlers + EVDOTA + EVDSTS + EVDCTRL_GZ;
                    calls server.begin()
    EVDMain.h       uses EVDRoutes (setup_http_server) + EVDDLNA (initDlna) +
                    EVDAudio (setup_opus, setup_i2s_in,
                    setup_queues_and_tasks); defines setup() + loop()

  ============================ Task layout ==================================

  Core 0:
    WiFi / lwIP      pri 18-23  system
    audioRead        pri 4      I2S DMA drain OR USB read;
                                yields 50 ms in NETWORK mode
    clockSync        pri 4      UDP clock sync responder
    udpSend          pri 3      Opus→subscribers pacer
    audioProc        pri 2      resample + quiet + volume → encodeQueue
    dlnaSsdp         pri 2      SSDP announce/respond
    AsyncTCP         pri 1      HTTP
    loop()           pri 1      factory reset, pot, discovery, NVS flush

  Core 1:
    audioEncode      pri 5      opus_encode() — sole occupant
    dlnaFetch        pri 2      HTTP WAV fetch → rawQueue
                                (spawned on Play, joined on Stop)

  ============================================================================
*/

#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include "opus.h"
// [usb-lib-detect] Unconditional include of the UAC library so arduino-cli
// can resolve it (it only scans the sketch's own #include lines). The CODE in
// EVDUsb.h is still guarded on EVD_HAS_USB_DEVICE_LIB; this just makes the
// header visible when it exists. A machine without it still compiles the
// no-library fallback path.
#if defined(__has_include)
  #if __has_include(<EspUsbDevice.h>)
    #include <EspUsbDevice.h>
  #endif
#else
  #include <EspUsbDevice.h>
#endif

// ============================================================================
//  MERGED FROM EVDProtocolCompat.h
// ============================================================================
struct AudioCompatDecision {
  bool        compatible;
  int         negotiatedProtocolVersion;
  const char *reason;
};

inline AudioCompatDecision evaluateAudioCompatibility(int declaredProtocolVersion,
                                                       int serverProtocolVersion) {
  if (declaredProtocolVersion < 0) {
    return AudioCompatDecision{ false, 0, "no_protocol_declared" };
  }
  if (declaredProtocolVersion < serverProtocolVersion) {
    return AudioCompatDecision{ false, 0, "player_protocol_too_old" };
  }
  if (declaredProtocolVersion > serverProtocolVersion) {
    return AudioCompatDecision{ true, serverProtocolVersion, "negotiated_down" };
  }
  return AudioCompatDecision{ true, serverProtocolVersion, "ok" };
}

// ============================================================================
//  MERGED FROM EVDConfig.h
// ============================================================================
// ---- Pin assignments -------------------------------------------------------
static const int pinBtLrc     = 15;
static const int pinBtDin     = 16;
static const int pinBtBclk    = 17;

static const int pinDacLrc    = 4;
static const int pinDacDout   = 5;
static const int pinDacBclk   = 6;

static const int pinVolumePot = 7;

static const int pinSrcUartTx = 9;
static const int pinSrcUartRx = 10;

// ---- Defaults --------------------------------------------------------------
const char *DEFAULT_WIFI_STA_SSID     = "";
const char *DEFAULT_WIFI_STA_PASSWORD = "";
const char *default_ap_ssid           = "Enkelvoud Server";
const char *default_ap_password       = "";

#define WIFI_CONNECT_TIMEOUT_MS 3000
#define WIFI_TEST_TIMEOUT_MS   15000

// ---- Factory reset ---------------------------------------------------------
#define PIN_FACTORY_RESET     21
#define FACTORY_RESET_HOLD_MS 3000

// ---- Audio format ----------------------------------------------------------
#define SOURCE_SAMPLE_RATE      48000
// [opus-sample-rate]
// Runtime-switchable between 48000 and 24000 Hz from the dashboard
// (Advanced > Encoding). Changing it re-creates the encoder on the audio
// task and tells every node to re-create its decoder at the matching rate
// via opusSampleRate in ctrl packets + registration responses.
// Frame duration stays 20 ms in both cases (960 samples @ 48k, 480 @ 24k).
#define OPUS_SAMPLE_RATE_48K    48000
#define OPUS_SAMPLE_RATE_24K    24000
#define OPUS_SAMPLE_RATE_DEFAULT OPUS_SAMPLE_RATE_48K
#define OPUS_FRAME_MS           20
#define CHANNELS                2
#define OPUS_BITRATE            96000
// [packet-cap-nodeside-alignment]
// The player firmwares cap their UDP receive buffer at MAX_OPUS_PACKET_LEN
// (1024 on the ESP32-S3 node as of the 1024/32 rebalance). A frame that
// exceeds that is silently dropped at udpAudioTask AND again in
// enqueueAudioPacket; the second drop path does not advance expected_seq,
// so the node's next real frame looks like a gap and PLC fires. That is
// the exact "single click on every loud transient" symptom operators
// report when they push the bitrate slider past ~150 kbps with VBR
// Constraint off. 1024 covers 192 kbps CBR (480 B/frame avg) plus 2.1x
// VBR peak headroom; higher bitrates need a coordinated node bump too.
#define OPUS_MAX_PACKET_BYTES 1024

// ---- Buffers ---------------------------------------------------------------
#define I2S_PORT_IN           I2S_NUM_0
#define I2S_PORT_OUT          I2S_NUM_1
#define I2S_READ_CHUNK_BYTES  1024

// [raw-queue-16-measured]
// Was 32 (= 640 ms at 50 fps). Field recording 2026-11-13 shows
// rawQueueDepth: 0 in every one of 310 samples across 558 s. The
// encoder drains at exactly 50 fps (audioProc produces, audioEncode
// consumes), so 32 slots is 6x the natural dwell. 16 slots = 320 ms is
// still above every preemption window the S3 can produce (longest
// observed i2s_read() stall: 42 ms at the 8×256 DMA ring depth).
// Saves 16 slots × 1036 bytes ≈ 16.6 KB of internal DRAM — the single
// largest safe heap recovery available on a build already at 92 %.
// If a future >40 % CPU WiFi storm ever overflows it, the source is
// the WiFi task priority, not this queue depth.
#define RAW_QUEUE_LEN   16
// N16R8: 16 (was 8). 16 slots = 320 ms of buffering at 50 Hz, giving
// the pacer room to absorb a slow UDP-burst window (AsyncTCP contention
// on Core 0 during an HTTP poll) without dropping an encoded frame.
// At OPUS_MAX_PACKET_BYTES=1024 the queue is 16 × 1036 = ~16.6 KB DRAM;
// on N16R8 that is free. A single opusDroppedLastWin > 0 is now only
// possible when the encoder itself overruns, never when the pacer is
// merely behind schedule.
#define OPUS_QUEUE_LEN  16

// [core-split-encode-queue] 4 slots (80 ms of 20 ms frames). AudioProc
// (core 1) produces one frame per ~20 ms; audioEncode (core 0) drains it.
// 4 gives 80 ms of preemption slack for the encoder against WiFi bursts.
#define ENCODE_QUEUE_LEN 4

// ---- UDP transport ---------------------------------------------------------
#define AUDIO_UDP_PORT      4202
#define CTRL_UDP_PORT       4203
#define CLOCK_SYNC_UDP_PORT 4204
// [ram-save-subscribers] Was 32. 16 is well above every deployment in
// the field. Hard cap: nodes beyond this are rejected in
// findOrCreateSubscriberLocked(). Saves ~1.3 KB DRAM in the subscriber
// table plus ~0.3 KB in udpSendTask's snapBuf.
#define MAX_UDP_SUBSCRIBERS 16
#define MAX_SEND_FAILURES_BEFORE_SKIP 10

// [pending-max-frames-measured]
// Empirically 4096 was a guess, 8192 was a guess, and the recorded
// session (310 samples over 558 s) showed pendingBufPeakDepthLastSec
// peak at 1170 frames — 28 % of 4096, 14 % of 8192 — with
// pendingBufDroppedTotal = 0 across the entire window. Neither value
// is justified by measurement; 4096 is the smaller of the two and
// leaves 16 KB DRAM to the heap that is currently at 92 % / maxAlloc
// 45 KB on this exact hardware. Reverted to 4096.
//
// If a real >4 % source-rate mismatch is ever observed in production
// (pendingBufDroppedTotal > 0 in /api/stats), the correct fix is to
// widen the adaptive resampler's fast-commit threshold, NOT to grow
// this buffer. Growing a buffer to absorb a debounce gap is an
// unbounded DRAM tax on every boot; shortening the debounce is
// bounded. See updateInputRateEstimate() in EVDAudio.h for the
// fast-commit path.
//
// If a future board has headroom AND a measured peak > 3000, add a
// runtime diagnostic via /api/stats (pendingBufPeakDepthLastSec is
// already there) and raise only after seeing that number cross 3000
// in a real recording.
#define PENDING_MAX_FRAMES 4096

// ---- USB Audio Class -------------------------------------------------------
// [usb-lib-detect] Was a literal `#if 1`, so the fallback branch below was
// dead code and the no-library stubs in EVDUsb.h could never be selected. A
// build on a machine without EspUsbDevice therefore failed at LINK time
// instead of degrading to "USB disabled, everything else works".
#if defined(__has_include)
  #if __has_include(<EspUsbDevice.h>)
    #define EVD_HAS_USB_DEVICE_LIB 1
  #else
    #define EVD_HAS_USB_DEVICE_LIB 0
    #pragma message("EspUsbDevice.h not found: USB audio source is disabled in this build")
  #endif
#else
  #define EVD_HAS_USB_DEVICE_LIB 1
#endif

#define EVD_USB_VID 0x303a
#define EVD_USB_PID 0x40e0

// ---------------------------------------------------------------------------
//  Audio wire protocol (unchanged - see original comments for full docs)
// ---------------------------------------------------------------------------
#define AUDIO_PROTO_MAGIC   0xE1D0
#define AUDIO_PROTO_VERSION 2
#define AUDIO_HDR_MAGIC_BYTES       2
#define AUDIO_HDR_VERSION_BYTES     1
#define AUDIO_HDR_FLAGS_BYTES       1
#define AUDIO_HDR_SEQ_BYTES         4
#define AUDIO_HDR_CAPTURED_MS_BYTES 4
#define AUDIO_HEADER_BYTES (AUDIO_HDR_MAGIC_BYTES + AUDIO_HDR_VERSION_BYTES + \
                            AUDIO_HDR_FLAGS_BYTES + AUDIO_HDR_SEQ_BYTES +   \
                            AUDIO_HDR_CAPTURED_MS_BYTES)
static_assert(AUDIO_HEADER_BYTES == 12,
              "AUDIO_HEADER_BYTES must stay in sync with udpSendTask() byte packing");

// ---- Logging ---------------------------------------------------------------
#define LOG_BAUD 115200
extern volatile uint8_t gSerialLogLevel;
inline void evdLogPrintf(char level, const char *fmt, ...);
#define LOGI(fmt, ...) do { if (gSerialLogLevel >= 3) evdLogPrintf('I', fmt, ##__VA_ARGS__); } while (0)
#define LOGW(fmt, ...) do { if (gSerialLogLevel >= 2) evdLogPrintf('W', fmt, ##__VA_ARGS__); } while (0)
#define LOGE(fmt, ...) do { if (gSerialLogLevel >= 1) evdLogPrintf('E', fmt, ##__VA_ARGS__); } while (0)

// ---- UDP discovery ---------------------------------------------------------
#define DISCOVERY_PORT      4201
#define DISCOVERY_MAGIC_REQ "ENKELVOUDP/1 DISCOVER"
#define DISCOVERY_MAGIC_RPY "ENKELVOUDP/1 SERVER"

// ---- Misc ------------------------------------------------------------------
static const char *MAIN_ZONE_ID          = "main";
static const unsigned long NVM_SAVE_DELAY_MS       = 5000;
static const unsigned long PLAYER_STALE_TIMEOUT_MS = 70000;

// ---- Shared POD structs ----------------------------------------------------
struct RawChunk {
  uint8_t  data[I2S_READ_CHUNK_BYTES];
  uint32_t len;
  uint32_t captured_ms;
};

struct OpusPacket {
  uint8_t  data[OPUS_MAX_PACKET_BYTES];
  int      len;
  uint32_t captured_ms;
  uint32_t seq;
};

// [core-split-encode-frame] One fully pre-processed PCM frame travelling
// from audioProcTask (core 1) to audioEncodeTask (core 0). pcm holds up to
// 960 samples × 2 ch = 1920 int16 (max is 48 kHz / 20 ms). `samples` carries
// the actual frame size so the encode task knows whether the frame came from
// before or after a rate change (480 vs 960) and drops stale ones.
struct EncodeFrame {
  int16_t  pcm[OPUS_SAMPLE_RATE_48K / (1000 / OPUS_FRAME_MS) * CHANNELS];
  uint32_t captured_ms;
  uint32_t seq;
  uint16_t samples;
};

struct Subscriber {
  String   ip;
  String   name;
  unsigned long lastSeenMs = 0;   // [subscriber-init] was uninitialised;
                                  // any pre-heartbeat read was UB
  uint8_t  consecutiveSendFailures = 0;
  int  declaredAudioProtocolVersion   = -1;
  int  negotiatedAudioProtocolVersion = 0;
  bool audioCompatible                = false;
  bool hasCompletedClockSync          = false;
  // [provisional-classification] True only while this slot was created by
  // handleClockSyncRequests()'s sync-before-heartbeat path and has not yet
  // been upgraded by a real /api/player/register heartbeat. Cleared on the
  // first real registration. Used by udpSendTask() to distinguish a
  // transient handshake from a genuinely old/incompatible firmware.
  bool provisional                    = false;
  // [sub-stall-cooldown] millis() until which this slot must NOT be sent to
  // again after a failed send. 0 = no cooldown. See udpSendTask() in
  // EVDUDP.h: retrying a node whose ARP entry is incomplete costs the pacer
  // a full ARP timeout (~2 s) PER ATTEMPT, which is what makes every other
  // node stutter when one node is switched off at the wall.
  uint32_t cooldownUntilMs            = 0;
};

struct PlayerNode {
  String name;
  String ip;
  String version;
  unsigned long lastSeenMs;
  int    declaredAudioProtocolVersion   = -1;
  int    negotiatedAudioProtocolVersion = 0;
  bool   audioCompatible                = false;
  String audioCompatReason              = "no_protocol_declared";
};

struct Zone {
  String id;
  String name;
  int volume;
  bool muted;
  std::vector<String> nodeIps;
};

struct NodePerf {
  String ip;
  uint32_t latencyMs;
  uint32_t queueDepth;
  uint32_t target;
  uint16_t rebuffers;
  unsigned long updatedMs;
  bool hasData;
};

// ---- Forward declarations --------------------------------------------------
// [inline-forward-decls] Every symbol below is DEFINED `inline` in one of
// the .h files further down this TU. C++ requires the inline specifier on
// the FIRST declaration in the translation unit; a non-inline declaration
// followed by an inline definition is ill-formed (gcc warns, clang errors
// under -Werror=undefined-inline). Single-TU sketches work today only
// because the compiler happens to tolerate the ordering; the moment a .h
// is split into its own .cpp the declarations in this file and the
// definitions in that file link-mismatch. Mark them inline here so the
// sketch is safely multi-TU.
inline void loadZonesFromNVS();
inline void configureQuietFilters();
inline void applyQuietFilter(int16_t *buf, uint32_t frames);
inline void broadcastNodeConfig();
inline void sendZoneConfigToIp(const String &ip, int zvol, bool zmute);
inline void updateNodePerf(const String &ip, uint32_t latencyMs, uint32_t queueDepth,
                           uint32_t target, uint16_t rebuffers, unsigned long now);
inline std::vector<Zone>      snapshotZones();
inline std::vector<NodePerf>  snapshotPerf();
inline void handleZonesCreate(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
inline void handleZonesDelete(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
inline void handleZonesMove  (AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
inline void handleZonesConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
inline void handleZonesRemoveNode(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

inline void udpSendTask(void *param);
inline void setup_udp_transport();
inline void sendCtrlToSubscriber(const String &ip, const String &json);
inline void handleNodeStats(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
inline void handleClockSyncRequests();

// ============================================================================
//  Remaining headers
// ============================================================================
#include "EVDState.h"
#include "EVDHelpers.h"
#include "EVDWiFi.h"
#include "EVDUDP.h"
#include "EVDAudio.h"
#include "EVDDLNA.h"
#include "EVDZones.h"
#include "EVDPlayers.h"
#include "EVDRoutes.h"
#include "EVDMain.h"
