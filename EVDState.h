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
volatile uint8_t  masterVolume         = 100;
volatile uint8_t  potVolume            = 100;
volatile uint16_t latencyAdjustmentMs  = 0;
volatile uint16_t audioBufferMs        = 0;
String            playbackMode         = "off";   // String cannot be volatile; accessed via snapshots
// Boot default source. Persisted. Applied once at setup() (see EVDMain.h)
// and updated by POST /api/config {defaultSource:"BT"|"AUX"|"USB"} after a
// dashboard source change (debounced in JS, see EVDCTRL.h).
String   defaultBootSource    = "BT";

// Serial log verbosity. 3 = everything (boot), 1 = errors only (steady state).
volatile uint8_t gSerialLogLevel = 3;

// ---- Input source routing --------------------------------------------------
// Which physical input feeds the encoder pipeline right now.
//   SRC_FROM_I2S - the source board is providing PCM over I2S (BT or AUX)
//   SRC_FROM_USB - the USB UAC device is receiving PCM straight from the host
//
// The audioReadTask (EVDAudio.h) branches on this every chunk. It is
// volatile because it is written by the HTTP handler (AsyncWebServer task,
// Core 0) and read by the audio task (Core 1). On a 32-bit word this is a
// single aligned store/load, so no mutex is needed - a one-chunk stale read
// is harmless (worst case: one ~23 ms chunk goes down the wrong path).
enum ServerInputSource : uint8_t {
  SRC_FROM_I2S = 0,
  SRC_FROM_USB = 1,
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

// ---- NVS save state --------------------------------------------------------
// Written from AsyncWebServer handlers (Core 0) and from handlePlayerRegistration
// (Core 0), read from loop() (Core 1). volatile is required so the compiler
// re-reads them every iteration instead of caching; alignment on the ESP32
// makes the 8-bit and 32-bit stores/loads atomic, so no lock is needed.
volatile bool          settingsDirty      = false;
volatile unsigned long settingsDirtySince = 0;

// ---- BT cache --------------------------------------------------------------
String receiverBtJson = "{\"devices\":[]}";

// ---- Source-board log ring -------------------------------------------------
// Source board forwards its own logLine() output over UART as "LOG|..."
// lines (see the source's EVDBTHelpers.h). handleReceiverLink() in
// EVDPlayers.h pushes each line into this ring; /api/source/info in
// EVDRoutes.h drains it for the dashboard's Source tab log viewer.
// Guarded by uartMutex.
#define SOURCE_LOG_RING_LEN 50
String sourceLogRing[SOURCE_LOG_RING_LEN];
int    sourceLogRingHead  = 0;
int    sourceLogRingCount = 0;

// ---- OTA pause flag (extern in EVDOTA.h) -----------------------------------
volatile bool otaPauseAudio = false;

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

// ---- UDP transport objects -------------------------------------------------
WiFiUDP audioUdp;
WiFiUDP ctrlUdp;
WiFiUDP clockSyncUdp;
IPAddress multicastGroup(239, 255, 42, 99);

// ---- Session identity ------------------------------------------------------
uint32_t serverEpoch = 0;

// ---- Queues ----------------------------------------------------------------
QueueHandle_t rawQueue  = nullptr;
QueueHandle_t opusQueue = nullptr;

// ---- OGG test history ------------------------------------------------------
EXT_RAM_BSS_ATTR OggTestPacket oggTestPackets[OGG_TEST_PACKET_COUNT];
uint8_t       oggTestWriteIndex   = 0;
uint8_t       oggTestPacketCount  = 0;
portMUX_TYPE  oggTestMux          = portMUX_INITIALIZER_UNLOCKED;

// ---- Stats -----------------------------------------------------------------
volatile uint32_t statI2sBytesIn             = 0;
volatile uint32_t statI2sReadErrors          = 0;

// [avg-encode-us-single-tu]
// Exponential moving average of opus_encode() wall-clock time. Previously
// declared extern here and defined in EVDAudio.h. Consolidated to a single
// definition alongside the other stats so it survives a future multi-TU
// split, and so the value is initialized before any reader (EVDPlayers.h's
// autoTuneOpusComplexity, EVDSTS.h's /api/stats, EVDMain.h's boot log) can
// observe it, regardless of translation-unit order.
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

// ---- Discovery UDP ---------------------------------------------------------
WiFiUDP discoveryUdp;

extern class ResamplerToOpusRate resampler;

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
#define EVD_LOG_RING_LEN  32
#define EVD_LOG_LINE_MAX 200

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