/*
  ============================================================================
  EnkelvoudServer (ESP32-S3)
  I2S AUDIO INPUT -> Opus Encoder -> UDP Multicast Server (+ EVD pages)

  Transport: Opus frames are fanned out via UDP multicast to 239.255.42.99:4202.
  Config pushes go to 239.255.42.99:4203. Registration + node stats stay HTTP.

    EVDState.h      - mutable globals (state, queues, stats, mutexes)
    EVDHelpers.h    - lock helpers, snapshots, string escaping, OGG CRC
    EVDWiFi.h       - AP/STA setup, discovery responder, WiFi scan, net test
    EVDUDP.h        - udpSendTask, ctrl push, heartbeat, OGG test stream
    EVDAudio.h      - I2S in/out, resampler, quiet filters, Opus, tasks
    EVDZones.h      - zone CRUD + HTTP handlers
    EVDPlayers.h    - player registration, receiver link, per-node perf,
                      adaptive buffer engine, /api/config|source|cmd
    EVDRoutes.h     - AsyncWebServer route table
    EVDMain.h       - setup(), loop(), factory-reset pin handling

  Unchanged headers kept as-is:
    EVDCTRL.h, EVDNTW.h, EVDOTA.h, EVDSTS.h
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

// ============================================================================
//  MERGED FROM EVDProtocolCompat.h
// ============================================================================
struct AudioCompatDecision {
  bool        compatible;                  // false => withhold audio entirely
  int         negotiatedProtocolVersion;    // 0 when compatible == false
  const char *reason;                       // short, stable machine-readable code
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

// ---- Versioning ------------------------------------------------------------
#ifndef EVD_FW_VERSION
#define EVD_FW_VERSION "3.1.5"
#endif
#define FW_VERSION EVD_FW_VERSION

static const char *SETTINGS_VERSION = "v2.0.7";

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
#define SOURCE_SAMPLE_RATE    44100
#define OPUS_SAMPLE_RATE      48000
#define CHANNELS              2
#define OPUS_FRAME_SAMPLES    960
// 96 kbps is the practical transparency floor for stereo music in Opus CBR.
// 64 kbps (the previous value) audibly quantizes cymbals, strings and vocals
// and is the largest single contributor to the "grainy" complaint. The S3
// has ample CPU to encode this rate at complexity 8-10 in 20 ms frames.
#define OPUS_BITRATE          96000
#define OPUS_MAX_PACKET_BYTES 1500

// ---- Buffers ---------------------------------------------------------------
#define OGG_TEST_PACKET_COUNT     50
#define OGG_TEST_MAX_PACKET_BYTES 256

#define I2S_PORT_IN           I2S_NUM_0
#define I2S_PORT_OUT          I2S_NUM_1
#define I2S_READ_CHUNK_BYTES  1024

#define RAW_QUEUE_LEN   32
#define OPUS_QUEUE_LEN  16

// ---- UDP transport ---------------------------------------------------------
#define AUDIO_UDP_PORT      4202
#define CTRL_UDP_PORT       4203
#define CLOCK_SYNC_UDP_PORT 4204
#define MAX_UDP_SUBSCRIBERS 32
#define MAX_SEND_FAILURES_BEFORE_SKIP 10

#define PENDING_MAX_FRAMES 4096

// ---- USB Audio Class -------------------------------------------------------
// Native USB Audio on ESP32-S2/S3/P4 requires a UAC-capable library.
// Detected by __has_include so the whole USB path compiles to no-ops on a
// build without the library (server still runs, dashboard USB option just
// logs a clear "not available" message instead of failing to build).
//
// Tested with tanakamasayuki/EspUsbDevice (Arduino Library Manager).
// If you use a different UAC library, replace the four wrapper functions
// in EVDUsb.h - nothing else in the tree touches the USB API directly.
#if __has_include(<EspUsbDevice.h>)
  #define EVD_HAS_USB_DEVICE_LIB 1
#else
  #define EVD_HAS_USB_DEVICE_LIB 0
  #pragma message("EspUsbDevice.h not found: USB audio source is disabled in this build")
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
// Runtime-gated. 3=verbose(boot) 2=warn+error 1=error only 0=silent.
// setup() starts at 3 so the serial monitor gets one complete boot trace,
// then drops to 1 at the end of setup(). Dashboard can raise it via
// POST /api/log/level.
#define LOG_BAUD 115200
extern volatile uint8_t gSerialLogLevel;
// Level gate is unchanged; only the emission path changes. Each macro now
// calls evdLogPrintf() (defined in EVDState.h) which prints to Serial AND
// appends the line to the in-RAM ring the dashboard's Status tab reads.
// See the ring-buffer block at the bottom of EVDState.h for the details
// and the RAM/CPU cost.
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
static const unsigned long PLAYER_STALE_TIMEOUT_MS = 45000;
// [v1.1.1-cleanup] DRIFT_WINDOW_MS removed: defined but never referenced.

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

struct OggTestPacket {
  uint8_t  data[OGG_TEST_MAX_PACKET_BYTES];
  uint16_t len;
};

struct Subscriber {
  String   ip;
  String   name;
  unsigned long lastSeenMs;
  uint8_t  consecutiveSendFailures = 0;
  int  declaredAudioProtocolVersion   = -1;
  int  negotiatedAudioProtocolVersion = 0;
  bool audioCompatible                = false;
  bool hasCompletedClockSync          = false;
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
void loadZonesFromNVS();
void configureQuietFilters();
void applyQuietFilter(int16_t *buf, uint32_t frames);
void broadcastNodeConfig();
void sendZoneConfigToIp(const String &ip, int zvol, bool zmute);
void updateNodePerf(const String &ip, uint32_t latencyMs, uint32_t queueDepth,
                    uint32_t target, uint16_t rebuffers, unsigned long now);
std::vector<Zone>      snapshotZones();
std::vector<NodePerf>  snapshotPerf();
void handleZonesCreate(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleZonesDelete(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleZonesMove  (AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleZonesConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

void udpSendTask(void *param);
void setup_udp_transport();
void sendCtrlToSubscriber(const String &ip, const String &json);
void handleNodeStats(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleClockSyncRequests();
void evdLiveBroadcastPacket(const OpusPacket &packet);

// ============================================================================
//  Remaining headers
// ============================================================================
#include "EVDState.h"
#include "EVDHelpers.h"
#include "EVDWiFi.h"
#include "EVDUDP.h"
#include "EVDAudio.h"
#include "EVDZones.h"
#include "EVDPlayers.h"
#include "EVDRoutes.h"
#include "EVDMain.h"