#pragma once
// ============================================================================
//  EVDState.h - Mutable globals: state, queues, stats, mutexes, objects
// ============================================================================
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ESPAsyncWebServer.h>
#include "opus.h"

// ---- Network ---------------------------------------------------------------
IPAddress ap_local_ip(192, 168, 4, 1);
IPAddress ap_gateway (192, 168, 4, 1);
IPAddress ap_subnet  (255, 255, 255, 0);

String wifiStaSsid        = DEFAULT_WIFI_STA_SSID;
String wifiStaPassword    = DEFAULT_WIFI_STA_PASSWORD;
bool   wifiUseStaticIp    = false;
String wifiStaticIp       = "";
String wifiStaticGateway  = "";
String wifiStaticSubnet   = "255.255.255.0";
String wifiStaticDns      = "";
String serverName         = "Enkelvoud";
bool   accessPointActive  = false;

// ---- Runtime settings ------------------------------------------------------
volatile bool     masterMuted          = false;
volatile bool     quietModeEnabled     = false;
volatile uint8_t  masterVolume         = 100;
volatile uint8_t  potVolume            = 100;
// [latency-autotune-removed] Retained for /api/state and /api/stats schema
// compatibility with existing scrapers, but it is never set to a non-zero
// value, never transmitted to nodes, and never consulted by any code path.
// The single latency authority is audioBufferMs (per-mode preset, edited
// on the Advanced > Opus Encoding tab) which is pushed to every node via
// bufferMs in /api/player/register responses and host_audio_sync ctrl
// packets. Nodes must obey that value verbatim; they are not permitted to
// grow or shrink it locally based on their own late-frame histogram.
volatile uint16_t latencyAdjustmentMs  = 0;
volatile uint16_t audioBufferMs        = 0;

// [audio-buffer-range-single-tu]
// Single source of truth for the valid range of audioBufferMs. Enforced at
// every entry point:
//   - setup() boot fallback for a corrupted NVS value (EVDMain.h)
//   - handleApiConfig() direct-API range check (EVDPlayers.h)
//   - normalizeAudioBufferForMode() presets (all values >= MIN, by design)
//   - EVDCTRL.h dashboard slider min/max attributes
//
// The previous code had FOUR separate limits: 20->60 fallback in setup(),
// a hardcoded 80/120/150 per-mode preset, a direct-API check of "v < 100 ||
// v > 300", and a slider min="20" in the dashboard. That meant:
//   * POST {mode:"movie"} installed audioBufferMs = 80, but
//   * POST {audioBufferMs:80} was rejected as invalid, and
//   * dragging the dashboard slider to 50 produced a 400 error.
// 20 is a strict lower bound enforced at every entry point. The per-mode
// default presets are 120 / 150 / 80 (off / music / movie), all well above
// it; the dashboard slider's min attribute must be pinned to
// AUDIO_BUFFER_MIN_MS so a drag cannot produce a 400 on /api/config.
//
// IMPORTANT: if you change these, also change the dashboard slider in
// EVDCTRL.h (search for: id="buffer-latency") and regenerate
// EVDCTRL_GZ.h with gen_gz.py / gen_gz.bat.
#define AUDIO_BUFFER_MIN_MS 20
#define AUDIO_BUFFER_MAX_MS 400

// [cross-core-string-fix] CRIT-1: playbackMode / defaultBootSource were
// String objects shared between Core 0 (AsyncTCP config handlers) and
// Core 1 (broadcastNodeConfig, loop() stats). String::operator= is
// free-old + alloc-new + swap-pointer; a reader on Core 1 could walk a
// _buffer that Core 0 just freed — use-after-free. Encoded as a single-
// byte aligned index; accessors return static literals (never freed), and
// the load/store is atomic on ESP32-S3 with no mutex needed.
volatile uint8_t playbackModeIdx = 0;   // 0=off 1=music 2=movie
volatile uint8_t bootSourceIdx   = 0;   // 0=BT  1=AUX   2=USB

inline const char *playbackModeCStr() {
  static const char *k[] = { "off", "music", "movie" };
  return k[playbackModeIdx < 3 ? playbackModeIdx : 0];
}
inline const char *bootSourceCStr() {
  static const char *k[] = { "BT", "AUX", "USB" };
  return k[bootSourceIdx < 3 ? bootSourceIdx : 0];
}
inline uint8_t bootSourceNameToIdx(const String &s) {
  if (s == "AUX") return 1;
  if (s == "USB") return 2;
  return 0;
}

// Serial log verbosity. 3 = everything (boot), 1 = errors only (steady state).
volatile uint8_t gSerialLogLevel = 3;

// ---- Input source routing --------------------------------------------------
// Which physical input feeds the encoder pipeline right now.
//   SRC_FROM_I2S     - the source board is providing PCM over I2S (BT or AUX)
//   SRC_FROM_USB     - the USB UAC device is receiving PCM straight from the host
//   SRC_FROM_NETWORK - the DLNA fetch task (EVDDLNA.h) is producing PCM
//
// The audioReadTask (EVDAudio.h) branches on this every chunk. It is
// volatile because it is written by the HTTP handler (AsyncWebServer task,
// Core 0) and read by the audio task (Core 1). On a 32-bit word this is a
// single aligned store/load, so no mutex is needed - a one-chunk stale read
// is harmless (worst case: one ~23 ms chunk goes down the wrong path).
enum ServerInputSource : uint8_t {
  SRC_FROM_I2S     = 0,
  SRC_FROM_USB     = 1,
  SRC_FROM_NETWORK = 2,   // NEW: DLNA/UPnP WAV stream (EVDDLNA.h)
};
volatile ServerInputSource serverInputSource = SRC_FROM_I2S;

// Timestamp of the last /api/source/info request. Set by the handler in
// EVDRoutes.h, read by loop() (EVDMain.h) to decide whether the dashboard's
// Source tab is currently open — and therefore whether the periodic
// GET_BT_DEVICES poll is worth the UART burst it triggers. 0 means "never".
volatile unsigned long gLastSourceInfoRequestMs = 0;

// ---- Receiver (upstream BT/aux module over UART) ---------------------------
String   receiverSource     = "BT";
String   receiverState      = "unknown";
uint32_t receiverSampleRate = 0;
uint8_t  receiverChannels   = 0;
uint8_t  receiverBits       = 0;
uint32_t receiverBytes      = 0;
uint32_t receiverFrames     = 0;
uint32_t receiverErrors     = 0;
String   lastReceiverSource = "BT";
HardwareSerial receiverUart(2);

// Mirrored copies of source-side diagnostics, updated whenever the source
// pushes a status line over UART. Read by /api/source/info (see EVDRoutes.h).
String   currentConnectedMacCache = "";
uint8_t  currentSourceBtVolume    = 0;
bool     avrcConnectedCache       = false;
uint32_t diagA2dpConnecting       = 0;
uint32_t diagA2dpConnected        = 0;
uint32_t diagA2dpDisconnected     = 0;
uint32_t diagVolumeEvents         = 0;
uint32_t diagUartRxLines          = 0;
uint32_t lastHeartbeatMsCache     = 0;

// Extended source-board health, mirrored from the SRCSTAT line that the
// source sends with every GET_BT_DEVICES reply. Read by /api/stats and
// the 60 s diagnostic recorder; never read by any control path.
String   currentSourceFwVersion           = "";
uint32_t currentSourceLoopTicks           = 0;
uint32_t currentSourceHeap                = 0;
uint32_t currentSourceUptime              = 0;
uint32_t currentSourceChanges             = 0;
uint32_t currentSourceVolumeApplied       = 0;
uint32_t currentSourceUartBytes           = 0;
uint32_t currentSourceUartOverflow        = 0;
uint32_t currentSourceUartTxDropped       = 0;
uint32_t currentSourceA2dpConnecting      = 0;
uint32_t currentSourceA2dpDisconnecting   = 0;

// ---- NVS save state --------------------------------------------------------
// Written from AsyncWebServer handlers (Core 0) and from handlePlayerRegistration
// (Core 0), read from loop() (Core 1). volatile is required so the compiler
// re-reads them every iteration instead of caching; alignment on the ESP32
// makes the 8-bit and 32-bit stores/loads atomic, so no lock is needed.
volatile bool          settingsDirty      = false;
volatile unsigned long settingsDirtySince = 0;

// [arm-settings-flush] Arm the deferred NVS writer from any core. Always
// re-stamps settingsDirtySince so the MAX_DEFER_MS deadline in loop() runs
// from the LAST change, and always re-asserts the flag (a flush that is in
// flight clears it between the arm and the next tick). Every settings writer
// should call this instead of assigning the two globals by hand.
inline void armSettingsFlush() {
  settingsDirtySince = millis();
  settingsDirty      = true;
}

// [audio-buffer-deferred-nvs] Set by /api/audio/encoding when the live buffer
// changes. The handler used to call saveEncoderModeSettings() directly from
// the AsyncTCP task, which is a flash erase inside a slider drag. Instead it
// records WHICH mode the change belongs to (the mode current at request time,
// not at flush time — that was the original lost-setting bug) and loop()
// persists it through the same deferred writer as everything else.
volatile bool          g_bufferDirtyValid = false;
volatile uint8_t       g_bufferDirtyMode  = 0;
volatile unsigned long g_bufferDirtyAtMs  = 0;

// [core-split-flags] Cross-core request flags. Set by handleEncoderRecreate()
// (core 0), drained by audioProcessingTask() (core 1) at the top of its loop.
// Volatile bool: single aligned byte store/load, atomic on ESP32-S3.
volatile bool g_rateChangeFlushPending   = false;  // flush pendingBuf + reset stage-2 resampler
volatile bool g_quietFiltersNeedReconfig = false;  // re-run configureQuietFilters()

// ---- BT cache --------------------------------------------------------------
String receiverBtJson = "{\"devices\":[]}";

// ---- Source-board log ring -------------------------------------------------
// Source board forwards its own logLine() output over UART as "LOG|..."
// lines (see the source's EVDBTHelpers.h). handleReceiverLink() in
// EVDPlayers.h pushes each line into this ring; /api/source/info in
// EVDRoutes.h drains it for the dashboard's Source tab log viewer.
// Guarded by uartMutex.
// [n16r8-source-log-ring-50]
// N16R8: 50 (was 20). ~600 B of String headers — nothing on N16R8.
// 50 lines gives the Source tab meaningful history across BT
// connect/disconnect sequences and AVRCP renegotiations; 20 was
// enough for one poll cycle but not for the follow-up investigation.
#define SOURCE_LOG_RING_LEN 50
String sourceLogRing[SOURCE_LOG_RING_LEN];
int    sourceLogRingHead  = 0;
int    sourceLogRingCount = 0;

// ---- OTA pause flag (extern in EVDOTA.h) -----------------------------------
volatile bool otaPauseAudio = false;

// ---- Multi-server election (P3: servers decide, see EVDPeers.h) ------------
// g_peerIsLeader is read by udpSendTask (Core 0), the discovery responder,
// the registration handler and the HTTP task; it is written by peerEval()
// from loop() (Core 1), hence volatile.
// [servers-decide] Starts FALSE: a booting board is a standby until the first
// election tick promotes it (single-server installs promote within ~1 s).
// Starting true made every boot briefly answer discovery and claim mDNS
// while the PC was already the leader.
volatile bool g_peerIsLeader = false;
int           g_peerPriority = 2;   // 1 = highest; PC defaults to 1

// ---- Objects ---------------------------------------------------------------
AsyncWebServer server(80);
OpusEncoder   *opusEncoder = nullptr;
Preferences    preferences;

// ---------------------------------------------------------------------------
//  Opus encoder complexity. These live here (not in EVDPlayers.h) because
//  audioProcessingTask() in EVDAudio.h must be the ONLY task that ever calls
//  opus_encoder_ctl(). libopus is not thread-safe per encoder instance:
//  opus_encoder_ctl(OPUS_SET_COMPLEXITY) writing internal state while
//  opus_encode() reads it corrupts the SILK/CELT predictors, which produces
//  exactly the "grainy, wrong-sounding but still decodes" artefact this
//  codebase has been chasing. The tuner (loop, Core 1) and a mode change
//  (AsyncWebServer, Core 0) both write g_pendingComplexity instead; the
//  audio task drains it and applies it, serialised with opus_encode().
//    currentOpusComplexity - last value actually pushed into the encoder,
//                            written ONLY by audioProcessingTask.
//    g_pendingComplexity   - -1 means no change pending, otherwise the
//                            absolute complexity value to install.
// ---------------------------------------------------------------------------
volatile int currentOpusComplexity = 8;
volatile int g_pendingComplexity   = -1;

// ---- Runtime encoder settings (via /api/audio/encoding) --------------------
// Same thread-safety reasoning as complexity: /api/audio/encoding runs on
// the AsyncWebServer task (Core 0), opus_encode() runs on audioProcessingTask
// (Core 1). The handler therefore only writes the g_new* variables and
// sets g_encoderSettingsDirty. audioProcessingTask drains them and calls
// opus_encoder_ctl() serialised with opus_encode().
//
// Application mode (audio/voip/lowdelay) can only be set at
// opus_encoder_create() time. A change to it requires a reboot, so the
// runtime handler stores it but it only takes effect on the next boot.
volatile int  g_currentBitrate       = OPUS_BITRATE;
volatile int  g_currentVbr           = 1;
volatile int  g_currentVbrConstraint = 1;
volatile int  g_currentSignal        = OPUS_SIGNAL_MUSIC;
volatile int  g_currentApplication   = OPUS_APPLICATION_AUDIO;

volatile bool g_encoderSettingsDirty = false;
volatile int  g_newBitrate           = OPUS_BITRATE;
volatile int  g_newVbr               = 1;
volatile int  g_newVbrConstraint     = 1;
volatile int  g_newSignal            = OPUS_SIGNAL_MUSIC;
volatile int  g_newApplication       = OPUS_APPLICATION_AUDIO;

// ---- Opus sample rate (runtime-switchable) --------------------------------
// [opus-sample-rate]
// g_opusSampleRate is the rate the encoder is CURRENTLY created at.
// g_opusFrameSamples is derived from it (rate / 50 for a 20 ms frame).
// g_pendingOpusSampleRate is a request from the dashboard that the audio
// task will drain on its next iteration via handleEncoderRecreate()
// (EVDAudio.h). 0 = no pending change.
// g_opusRateChangeNeedsBroadcast is set by the audio task after a
// successful re-create; loop() (EVDMain.h) consumes it to push the new
// opusSampleRate value to every subscriber.
volatile uint32_t g_opusSampleRate              = OPUS_SAMPLE_RATE_DEFAULT;
volatile uint16_t g_opusFrameSamples            = OPUS_SAMPLE_RATE_DEFAULT / (1000 / OPUS_FRAME_MS);
volatile uint32_t g_pendingOpusSampleRate       = 0;
volatile bool     g_opusRateChangeNeedsBroadcast = false;

// ---- Per-mode encoder presets ---------------------------------------------
// [per-mode-encoder-settings]
// The Encoding tab's Save button persists the current live encoder settings
// under the current playback mode. Off / Music / Movie each carry their own
// six-value preset; switching modes swaps all six at once.
//
// g_modeSettings is the RAM copy. Written only from the AsyncWebServer task
// (EVDPlayers.h) and from loadWiFiSettings() during setup (single-threaded).
// No mutex needed.
//
// NVS keys: enc_<mode>_br / _cx / _vbr / _vc / _sig / _app, one set per mode.
struct ModeEncSettings {
  int32_t bitrate;        // bps, 6000..510000
  int32_t complexity;     // 0..10
  bool    vbr;
  bool    vbrConstraint;
  int32_t signal;         // OPUS_SIGNAL_MUSIC / VOICE / OPUS_AUTO
  int32_t application;    // OPUS_APPLICATION_AUDIO / VOIP / RESTRICTED_LOWDELAY
  // [opus-sample-rate] Per-mode sample rate. OPUS_SAMPLE_RATE_48K or
  // OPUS_SAMPLE_RATE_24K. Loading a mode preset with a different rate
  // than g_opusSampleRate sets g_pendingOpusSampleRate so the audio task
  // re-creates the encoder on the next frame.
  int32_t sampleRate;
  // [audio-buffer-per-mode]
  // audioBufferMs used to be a single global, force-overwritten to a
  // hardcoded value (80/150/120) by normalizeAudioBufferForMode() on boot
  // and again by handleApiConfig() on every mode change. That made the
  // "Target Buffer Latency" slider appear to be persisted but silently
  // discard the user's value at the next mode switch or reboot. Made it
  // part of the per-mode preset so the slider behaves like the other six
  // encoder values: Save writes it to NVS for the current mode, Revert
  // restores it, and a mode switch loads the destination mode's saved
  // bufferMs.
  int32_t bufferMs;       // AUDIO_BUFFER_MIN_MS..AUDIO_BUFFER_MAX_MS
};

enum ModeIdx : uint8_t { MODE_OFF = 0, MODE_MUSIC = 1, MODE_MOVIE = 2, MODE_COUNT = 3 };

// Field order: bitrate, complexity, vbr, vbrConstraint, signal, application,
// sampleRate, bufferMs  (see ModeEncSettings declaration above).
ModeEncSettings g_modeSettings[MODE_COUNT] = {
  { OPUS_BITRATE, 4, true, true, OPUS_SIGNAL_MUSIC, OPUS_APPLICATION_AUDIO, OPUS_SAMPLE_RATE_48K, 120 }, // Off
  { OPUS_BITRATE, 4, true, true, OPUS_SIGNAL_MUSIC, OPUS_APPLICATION_AUDIO, OPUS_SAMPLE_RATE_48K, 150 }, // Music
  { OPUS_BITRATE, 4, true, true, OPUS_SIGNAL_MUSIC, OPUS_APPLICATION_AUDIO, OPUS_SAMPLE_RATE_48K,  80 }, // Movie
};

inline ModeIdx modeNameToIdx(const String &m) {
  if (m == "music") return MODE_MUSIC;
  if (m == "movie") return MODE_MOVIE;
  return MODE_OFF;
}
inline const char *modeIdxToName(ModeIdx i) {
  return i == MODE_MUSIC ? "music" : i == MODE_MOVIE ? "movie" : "off";
}

// ---- UDP transport objects -------------------------------------------------
WiFiUDP audioUdp;
WiFiUDP ctrlUdp;
WiFiUDP clockSyncUdp;
IPAddress multicastGroup(239, 255, 42, 99);

// ---- Session identity ------------------------------------------------------
uint32_t serverEpoch = 0;

// ---- Queues ----------------------------------------------------------------
QueueHandle_t rawQueue    = nullptr;
QueueHandle_t opusQueue   = nullptr;
QueueHandle_t encodeQueue = nullptr;   // [core-split] core 1 -> core 0

// ---- Stats -----------------------------------------------------------------
volatile uint32_t statI2sBytesIn             = 0;
volatile uint32_t statI2sReadErrors          = 0;

// [avg-encode-us-single-tu]
// Exponential moving average of opus_encode() wall-clock time. Written by
// audioProcessingTask() (EVDAudio.h) after each opus_encode() call.
// Readers:
//   - EVDSTS.h  /api/stats  ("avgEncodeUs")
//   - EVDMain.h loop()      verbose stats dump (level 3 only)
// Consolidated to a single definition here so the value exists before any
// translation unit that reads it, regardless of include order.
//
// NOT read by the adaptive tuner any more: the per-mode complexity preset
// (g_modeSettings[].complexity -> g_pendingComplexity, applied by
// applyModeSettingsToLive() in EVDWiFi.h) replaced the old
// encode-time-driven autoTuneOpusComplexity() and this counter is now
// diagnostics-only.
volatile uint32_t gAvgEncodeUs               = 0;
volatile uint32_t statRawDropped             = 0;
volatile uint32_t statOpusEncoded            = 0;
volatile uint32_t statOpusDropped            = 0;
volatile uint32_t statUdpPacketsSent         = 0;
volatile uint32_t statUdpBytesSent           = 0;
volatile uint32_t statI2sBytesLast5s         = 0;
volatile uint32_t statRawDroppedLast5s       = 0;
volatile uint32_t statOpusEncodedLast5s      = 0;
volatile uint32_t statOpusDroppedLast5s      = 0;
volatile uint32_t statUdpPacketsLast5s       = 0;
volatile uint32_t statUdpBytesLast5s         = 0;
volatile uint64_t statI2sBytesTotal          = 0;
// 32-bit latency accumulator, guarded by gStatsMux. A 64-bit cross-core
// read tears on a 32-bit MCU; the sum never exceeds 2^32 at the rates this
// server runs (500 ms max per packet × 50 packets/sec × 15 s window
// ≈ 375 000), so a 32-bit word is sufficient and remains atomic under the
// critical section. Writers and readers MUST use gStatsMux.
volatile uint32_t statLatencySumMs           = 0;
volatile uint32_t statLatencyCount           = 0;
volatile int      subscriberCount            = 0;
volatile uint32_t statUdpErrors              = 0;
// [uart-line-drops] Lines discarded by handleReceiverLink() because they did
// not fit RX_LINE_MAX. Non-zero means the source is emitting something longer
// than this build expects (BT_JSON is the only line that can) and the
// dashboard will show a stale device list until the buffers are re-matched.
volatile uint32_t statUartLineDrops          = 0;
volatile int32_t  statDriftPpm               = 0;
volatile float    statMeasuredInputHz  = 0.0f;
volatile uint32_t statSyncRequestsServed      = 0;
volatile uint32_t statSyncRequestErrors       = 0;
volatile uint32_t statSyncRequestsThrottled   = 0;
volatile uint32_t statAudioIncompatibleSkips  = 0;
// [stat-audio-sync-pending]
// Frames withheld because the subscriber had not yet completed its first
// RTT clock-sync exchange. Was previously folded into statSyncRequests-
// Throttled, which is a UDP rate-limit counter and reported the wrong
// thing. Separating the two makes /api/stats honest about *why* a new
// node is silent for the first ~2 s.
volatile uint32_t statAudioSyncPending        = 0;
// [stat-audio-provisional-skips]
// Frames withheld from a PROVISIONAL subscriber slot — one created by
// handleClockSyncRequests()'s [sync-before-heartbeat-provisional-slot]
// branch, which has audioCompatible=false and declaredAudioProtocolVersion<0
// until the node's first heartbeat upgrades it. Those frames were previously
// counted in statAudioIncompatibleSkips, which made the dashboard report
// "audioIncompatibleSkips: 222" on a deployment where all three nodes were
// fully compatible. The two counters are now distinct so the operator can
// tell "old firmware is being excluded" (real incompatibility) from
// "a node's first-heartbeat handshake is still in progress" (transient).
volatile uint32_t statAudioProvisionalSkips   = 0;
// [sub-stall-cooldown] Frames not even attempted because the subscriber
// was inside its post-failure cooldown. Expected to climb when a node is
// powered off; it is the count that used to be paid for in stutter on
// every OTHER node.
volatile uint32_t statAudioCooldownSkips      = 0;
// [udp-send-liveness] Incremented once per udpSendTask loop iteration.
// Healthy value climbs ~50/s when audio flows. If it is static while
// statOpusDropped climbs, udpSendTask is stuck (see the recording-2 stall).
// g_udpSendStallResets counts packets dropped while draining stale backlog.
// [udp-send-liveness-defs] These are written by udpSendTask() in EVDUDP.h and
// exposed by handleApiStats() in EVDSTS.h. Single-TU: defined here so
// udpSendTask, /api/stats, and future split TUs all see one instance.
volatile uint32_t g_udpSendLoopCount   = 0;   // udpSendTask iterations
volatile uint32_t g_udpSendStallResets = 0;   // stale backlog packets dropped
// [udp-stall-split] Legacy "ahead" field is retained for API compatibility;
// the pacer now uses an absolute deadline and cannot accumulate an ahead
// deadline. Both active counters identify drops during stale-backlog drain.
volatile uint32_t g_udpPacerAheadReset  = 0;  // legacy; no longer incremented
volatile uint32_t g_udpPacerBehindReset = 0;  // stale backlog packet dropped
volatile uint32_t g_udpPacerDriftReset  = 0;  // stale backlog packet dropped
// [udp-stage-counters] Written by udpSendTask in EVDUDP.h; read by
// handleApiStats in EVDSTS.h. Poll /api/stats twice and diff to find
// the stage where the task is blocked. A frozen counter = block is
// inside the NEXT stage's function call.
volatile uint32_t g_udpStage_afterRecv  = 0;   // past xQueueReceive
volatile uint32_t g_udpStage_afterStore = 0;   // past master-mute gate

// [ota-pause-watchdog] Wall-clock ms of the last OTA upload-chunk or
// otaJob activity. Written by EVDOTA.h; read by loop() to decide whether
// a stuck OTA pause should be force-cleared. 0 = no OTA has ever run.
volatile unsigned long g_otaLastProgressMs = 0;

// Counts ARDUINO_EVENT_WIFI_STA_DISCONNECTED occurrences since boot.
// Surfaced in /api/stats as "wifiDisconnects". If this counter increments
// once per observed ~1-second audio gap, the gap is WiFi link-layer loss on
// the server side (AP beacon miss, DTIM stall, background scan) and NOT the
// audio pipeline. If it stays put while the gap occurs, the loss is
// downstream of the S3 (client WiFi) or inside udpSendTask.
volatile uint32_t statWifiDisconnects         = 0;

// ---- USB audio stats -------------------------------------------------------
// statUsbBytesIn accumulates over the 15 s window in loop(); the value is
// copied into statUsbBytesLast5s and then reset alongside the I2S counters.
volatile uint32_t statUsbBytesIn     = 0;
volatile uint32_t statUsbBytesLast5s = 0;

// ---- DLNA network-audio stats (EVDDLNA.h) ----------------------------------
// statNetBytesIn   - incremented by dlnaFetchTaskFn() per RawChunk enqueued.
// statNetDropped   - produced RawChunks discarded when fetch is stopped.
// All three are diagnostics only; the snapshot-and-reset lives in
// loop() (EVDMain.h) next to the USB/I2S counters.
volatile uint32_t statNetBytesIn     = 0;
volatile uint32_t statNetBytesLast5s = 0;
volatile uint32_t statNetDropped     = 0;

// [usb-host-volume-single-tu]
// USB UAC Feature Unit state. Previously defined `static` in BOTH branches
// of EVDUsb.h (with and without EspUsbDevice). That is fine in a single-TU
// Arduino sketch but produces duplicate-definition link errors the moment
// anything is split into a second translation unit. Defined here so both
// branches of EVDUsb.h share exactly one instance.
//
// Written ONLY by pollUsbAudioEvents() on the audio task. Reads from
// diagnostics are plain aligned scalar loads and do not need a lock.
volatile int16_t gUsbHostVolumeDb256      = 0;
volatile bool    gUsbHostMuted            = false;
volatile bool    gUsbHostHasEverSetVolume = false;

// [stutter-visible-counters]
// Server-side diagnostics. The 10 fields below are the ONLY new globals
// the server needs. They are written by audioEncodeTask (overrun counters,
// peak encode time) and audioProcessingTask (pendingBuf drop counters,
// peak fill depth), rolled up once per second by audioEncodeTask, and
// exposed via /api/stats in EVDSTS.h.
//
// Why these and not the node-side capturedMsGap / correctorFrames fields:
// the SERVER generates captured_ms; it cannot measure "gap from previous
// captured_ms" because it never receives its own audio. Those counters
// live only on the node (EVDPState.h) where they work correctly and
// already report the gaps the user is hearing.
//
// On the server the three observable "green but stuttering" causes are:
//   1. statEncodeOverruns*  — opus_encode() took >20 ms (WiFi preemption
//                             of the pri-10 encoder by pri-23 WiFi task).
//                             Each overrun = one frame the encoder falls
//                             behind = one packet the pacer sends late.
//                             This is the direct cause of the node's
//                             captured_ms gap.
//   2. statEncodePeakUsLastSec — worst single encode. avgEncodeUs is
//                             always ~13 ms; the peak is what matters.
//                             Values >20000 confirm #1 in a second where
//                             no individual overrun happened to fire.
//   3. statPendingBufDropped* — resampler output exceeded PENDING_MAX_FRAMES
//                             and dropped the OLDEST samples before encode.
//                             Not the current symptom (rate is locked at
//                             48k) but essential for catching a future
//                             source-rate mismatch early.
//
// All volatile uint32_t (single-word atomic on this MCU).
volatile uint32_t statPendingBufDroppedTotal       = 0;
volatile uint32_t statPendingBufDroppedThisSec     = 0;
volatile uint32_t statPendingBufDroppedLastSec     = 0;
volatile uint32_t statPendingBufPeakDepth          = 0;
volatile uint32_t statPendingBufPeakDepthLastSec   = 0;

volatile uint32_t statEncodeOverrunsTotal          = 0;
volatile uint32_t statEncodeOverrunsThisSec        = 0;
volatile uint32_t statEncodeOverrunsLastSec        = 0;
volatile uint32_t statEncodePeakUsThisSec          = 0;
volatile uint32_t statEncodePeakUsLastSec          = 0;

// [v1.1.1-atomic-window]
// Guards the snapshot-and-reset of the byte/packet counters in loop() so a
// chunk or datagram that lands between read and reset is not lost.
portMUX_TYPE gStatsMux = portMUX_INITIALIZER_UNLOCKED;

Subscriber subscribers[MAX_UDP_SUBSCRIBERS];
int        subscriberTableCount = 0;

// ---- Network-test state ----------------------------------------------------
enum NetworkTestState { NETWORK_TEST_IDLE, NETWORK_TEST_RUNNING, NETWORK_TEST_SUCCESS, NETWORK_TEST_FAILED };
volatile NetworkTestState networkTestState = NETWORK_TEST_IDLE;
String networkTestIp;
String networkTestError;
String pendingSsid, pendingServerName, pendingPassword;
bool   pendingUseStaticIp = false;
String pendingStaticIp, pendingStaticGateway, pendingStaticSubnet, pendingStaticDns;

// ---- Collections -----------------------------------------------------------
std::vector<PlayerNode> playerNodes;
std::vector<Zone>       zones;
std::vector<NodePerf>   nodePerf;

// ---- Mutexes ---------------------------------------------------------------
SemaphoreHandle_t playersMutex = nullptr;
SemaphoreHandle_t uartMutex    = nullptr;
SemaphoreHandle_t subsMutex    = nullptr;
SemaphoreHandle_t zonesMutex   = nullptr;
SemaphoreHandle_t perfMutex    = nullptr;
SemaphoreHandle_t prefsMutex   = nullptr;
SemaphoreHandle_t logTailMutex = nullptr;   // guards /api/log/tail static snap
// [ota-mutex-single-tu]
// otaMutex used to be defined `static` in EVDOTA.h and lazily created inside
// handleOtaPlan(). Declared here and created in createSyncPrimitives() like
// every other mutex.
// [ota-mutex-single-tu] See the block comment above.
SemaphoreHandle_t otaMutex        = nullptr;

// ---- Discovery UDP ---------------------------------------------------------
WiFiUDP discoveryUdp;

// [resampler-forward-decl]
// The class definition lives in EVDAudio.h, which is included AFTER this
// file from EnkelvoudServer.ino. Use a proper class forward declaration
// followed by a plain extern so this header does not rely on the
// elaborated-type-specifier side effect of "extern class X name;", which
// silently forward-declares X as a helper. That made an include-order
// change produce an "invalid use of incomplete type" error at a line that
// looked unrelated to the change.
class ResamplerToOpusRate;
extern ResamplerToOpusRate resampler;

// [dynamic-source-rate]
// Live measured source rate (I2S clock in Hz). Seeded from the 2 s
// I2S-clock average in setup() (EVDMain.h) and refined every 3 s window
// by updateInputRateEstimate() (EVDAudio.h). The resampler stage-1 ratio
// is derived from this — NOT from a compile-time SOURCE_SAMPLE_RATE
// assumption — so when the source board switches between 44.1 kHz and
// 48 kHz (BT codec renegotiation, AUX hot-plug to a different sample rate),
// the encoder keeps being fed exactly OPUS_SAMPLE_RATE samples per second.
// Reported in /api/state and /api/stats so the dashboard Transport section
// can display the actual rate instead of a hardcoded 44,100 Hz.
volatile uint32_t g_sourceSampleRate = SOURCE_SAMPLE_RATE;
// [rate-change-broadcast-flag]
// Set by updateInputRateEstimate() (audioProcessingTask, Core 1) whenever
// the measured source rate moves by more than 3 %. Consumed by loop()
// (EVDMain.h), which calls broadcastNodeConfig() and clears it. Carries
// the new Hz value so loop() can log it without re-reading the smoothed
// estimate. Single-word aligned write/read, atomic on ESP32-S3.
volatile uint32_t g_rateChangeBroadcastHz = 0;
// ============================================================================
//  Serial log capture ring
// ============================================================================
//  The LOGI/LOGW/LOGE macros in EVDConfig.h route through evdLogPrintf() below.
//  That function does two things:
//    1. Appends the formatted line to gLogRing so the dashboard's Status tab
//       can display it over HTTP while the tab is open.
//    2. Writes the same line to Serial (as it did before).
//
//  Both happens only when the runtime level gate in the macro lets the call
//  through, so at gSerialLogLevel == 1 (steady state) only LOGE lines reach
//  this code, and at level 3 (verbose) everything does.
//
//  The ring holds the most recent EVD_LOG_RING_LEN lines. Older ones are
//  overwritten; nothing is persisted across a reboot.
//
//  This is deliberately the smallest capture structure that is still useful
//  for on-dashboard diagnosis. Cost:
//    RAM:  EVD_LOG_RING_LEN * sizeof(LogRingEntry)  (see below)
//    CPU:  one short portMUX critical section per emitted line (a few
//          hundred ns at level 1, more only while verbose is on)
//  It is not a general-purpose logger and should not be treated as one.
// [n16r8-log-ring-32]
// N16R8: 32 (was 16). 32 × 128 = 4 KB gLogRing + 4.2 KB snap[] in
// /api/log/tail = ~8 KB DRAM. Doubles the dashboard's log history
// from 8 s to 16 s at steady state, which is the difference between
// "I see the error" and "the error scrolled off before I opened the
// Status tab". On N16R8, 8 KB is free.
#define EVD_LOG_RING_LEN  32
#define EVD_LOG_LINE_MAX 128

struct LogRingEntry {
  uint32_t seq;                    // monotonically increasing
  uint32_t ms;                     // millis() at time of log
  char     level;                  // 'I', 'W', 'E'
  char     text[EVD_LOG_LINE_MAX]; // formatted message, no prefix, no newline
};

static LogRingEntry   gLogRing[EVD_LOG_RING_LEN];
static volatile uint32_t gLogRingWriteIdx = 0;
static volatile uint32_t gLogRingSeq      = 0;
static portMUX_TYPE    gLogRingMux = portMUX_INITIALIZER_UNLOCKED;

// Format fmt+... into a local buffer, copy it into the ring under a short
// critical section, then print to Serial outside the critical section.
//
// The split matters: Serial.write can block for milliseconds on a slow UART
// and must NOT be called while holding a spinlock. The critical section here
// is only long enough to memcpy ~200 bytes.
inline void evdLogPrintf(char level, const char *fmt, ...) {
  char text[EVD_LOG_LINE_MAX];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n >= (int)sizeof(text)) n = (int)sizeof(text) - 1;
  text[n] = '\0';

  uint32_t now = millis();

  portENTER_CRITICAL(&gLogRingMux);
  uint32_t idx = gLogRingWriteIdx;
  gLogRing[idx].seq   = ++gLogRingSeq;
  gLogRing[idx].ms    = now;
  gLogRing[idx].level = level;
  memcpy(gLogRing[idx].text, text, (size_t)n + 1);
  gLogRingWriteIdx = (idx + 1) % EVD_LOG_RING_LEN;
  portEXIT_CRITICAL(&gLogRingMux);

  // Same wire format the previous macros emitted: "[LEVEL] <ms> | <text>".
  // Spaces inside the tag keep the columns aligned in the serial monitor.
  Serial.printf("[%s] %8lu ms | %s\n",
                level == 'E' ? "ERROR" :
                level == 'W' ? "WARN " : "INFO ",
                (unsigned long)now, text);
}