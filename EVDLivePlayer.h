#pragma once
// ============================================================================
//  EVDLivePlayer.h - WebSocket + MediaSource true-continuous Opus player
// ============================================================================
//  Serves a gapless continuous audio stream to every modern browser by
//  pushing each newly-encoded Opus frame over a persistent WebSocket as a
//  segment the browser feeds into a MediaSource SourceBuffer.
//
//  Two transports, one per format, because browser support differs:
//
//    /audio.webm  - WebM/Opus. Works on Chrome/Edge (desktop + Android),
//                   Firefox (desktop + Android), Samsung Internet, Brave,
//                   Edge Android, and every other Chromium-Android browser.
//                   This is the primary path.
//
//    /audio.ogg   - OGG/Opus. Works on desktop Chrome/Edge/Firefox ONLY.
//                   Android Chrome deliberately removed OGG from MSE to
//                   save battery (software Opus decode is costly there).
//                   Kept for Firefox because Firefox desktop still lacks
//                   WebM/Opus in MSE on some builds.
//
//  The /live page does MediaSource.isTypeSupported() checks and connects
//  to whichever endpoint the browser will accept. If neither works, the
//  page redirects to /player (the HTTP-refetch snapshot player).
//
//  Browser compatibility matrix (verified):
//    Chrome desktop          WebM ✓   OGG ✓
//    Edge desktop            WebM ✓   OGG ✓
//    Firefox desktop         WebM ✓*  OGG ✓
//    Chrome Android          WebM ✓   OGG ✗
//    Samsung Internet        WebM ✓   OGG ✗
//    Firefox Android         WebM ✓   OGG ✓
//    iOS Safari              WebM ✗   OGG ✗  (redirects to /player)
//
//    * Firefox desktop has historically been inconsistent about WebM in
//      MSE. The /live page tests isTypeSupported() at runtime, so this
//      doesn't matter in practice.
// ============================================================================

#include <ESPAsyncWebServer.h>
#include "EVDHelpers.h"

// ---- Configuration ----------------------------------------------------------
#define EVD_LIVE_WS_MAX_CLIENTS     8
// Cluster flush thresholds. Cluster is flushed to all WebM clients when
// EITHER of these is reached. 10 frames = 200 ms of audio, which keeps
// end-to-end latency low enough for live TV/radio use while still giving
// the WS queue something to work with. Larger clusters = fewer WS messages
// but higher latency; smaller = the opposite.
#define EVD_WEBM_CLUSTER_MAX_FRAMES 10
#define EVD_WEBM_CLUSTER_MAX_BYTES  4096
// Bytes reserved at the start of the cluster buffer for the Cluster header.
// Cluster ID (4) + size VINT (up to 4) + Timecode element (6) = 14 bytes
// worst case, so 16 gives us a small safety margin.
#define EVD_WEBM_HDR_RESERVE        16

// ---- Client state -----------------------------------------------------------
enum EvdLiveFormat : uint8_t {
  EVD_FMT_NONE = 0,
  EVD_FMT_OGG  = 1,
  EVD_FMT_WEBM = 2
};

struct EvdLiveClient {
  uint32_t clientId = 0;
  uint8_t  format   = EVD_FMT_NONE;
  // OGG per-client state. WebM state is global (see below) because all
  // WebM clients receive identical cluster bytes and just consume them at
  // their own pace.
  uint32_t pageSeq  = 2;
  uint64_t granule  = 0;
};

static EvdLiveClient       gLiveClients[EVD_LIVE_WS_MAX_CLIENTS];
static AsyncWebSocket      gLiveWsWebm("/audio.webm");
static AsyncWebSocket      gLiveWsOgg ("/audio.ogg");
static SemaphoreHandle_t   gLiveMutex = nullptr;

// ---- WebM cluster state (shared across all WebM clients) -------------------
// Layout:
//   [0 .. EVD_WEBM_HDR_RESERVE-1]  reserved for Cluster header at flush time
//   [EVD_WEBM_HDR_RESERVE .. ]     SimpleBlocks appended one per Opus frame
// On flush the header is written into the reserved prefix (right-aligned so
// it abuts the SimpleBlocks) and the whole thing is sent as one binary WS
// message per client.
static uint8_t  gWebmClusterBuf[EVD_WEBM_HDR_RESERVE +
                                EVD_WEBM_CLUSTER_MAX_BYTES + 512];
static size_t   gWebmClusterLen          = EVD_WEBM_HDR_RESERVE;
static uint32_t gWebmClusterBaseTimecode = 0;
static size_t   gWebmClusterFrameCount   = 0;

// ============================================================================
//  OGG/Opus muxing (unchanged from the previous implementation)
// ============================================================================

static const uint8_t kOpusHead[] = {
  'O','p','u','s','H','e','a','d',
  1,                     // version
  2,                     // channel count
  0, 0,                  // pre-skip
  0x80, 0xBB, 0, 0,      // input sample rate 48000 LE
  0, 0,                  // output gain
  0                      // channel mapping family
};
static const uint8_t kOpusTags[] = {
  'O','p','u','s','T','a','g','s',
  0, 0, 0, 0,            // vendor string length = 0
  0, 0, 0, 0             // user comment count = 0
};

static inline size_t evdBuildOggPage(uint8_t *out, size_t outCap,
                                     const uint8_t *packet, size_t packetLen,
                                     uint32_t serial, uint32_t seq,
                                     uint64_t granule, uint8_t headerType) {
  const size_t segmentCount = (packetLen / 255) + 1;
  const size_t pageLen      = 27 + segmentCount + packetLen;
  if (pageLen > outCap) return 0;

  memcpy(out, "OggS", 4);
  out[4] = 0;
  out[5] = headerType;
  for (uint8_t i = 0; i < 8; ++i) out[6  + i] = (granule >> (i * 8)) & 0xFF;
  for (uint8_t i = 0; i < 4; ++i) out[14 + i] = (serial  >> (i * 8)) & 0xFF;
  for (uint8_t i = 0; i < 4; ++i) out[18 + i] = (seq     >> (i * 8)) & 0xFF;
  memset(out + 22, 0, 4);
  out[26] = (uint8_t)segmentCount;

  size_t remaining = packetLen;
  for (size_t i = 0; i < segmentCount; ++i) {
    out[27 + i] = remaining >= 255 ? 255 : (uint8_t)remaining;
    remaining -= out[27 + i];
  }
  memcpy(out + 27 + segmentCount, packet, packetLen);

  uint32_t crc = oggCrc(out, pageLen);
  for (uint8_t i = 0; i < 4; ++i) out[22 + i] = (crc >> (i * 8)) & 0xFF;
  return pageLen;
}

// ============================================================================
//  WebM/Opus muxing
// ============================================================================

// ---- EBML VINT size writer --------------------------------------------------
// Writes an EBML variable-length size field into `out` and returns its
// length in bytes. Handles 1- to 4-byte VINTs, which is more than enough
// for our cluster sizes (max ~4.5 KB → 2-byte VINT).
static inline size_t evdWriteVintSize(uint8_t *out, size_t value) {
  if (value <= 0x7E) {
    out[0] = (uint8_t)(0x80 | value);
    return 1;
  }
  if (value <= 0x3FFE) {
    out[0] = (uint8_t)(0x40 | (value >> 8));
    out[1] = (uint8_t)(value & 0xFF);
    return 2;
  }
  if (value <= 0x1FFFFE) {
    out[0] = (uint8_t)(0x20 | (value >> 16));
    out[1] = (uint8_t)((value >> 8) & 0xFF);
    out[2] = (uint8_t)(value & 0xFF);
    return 3;
  }
  out[0] = (uint8_t)(0x10 | ((value >> 24) & 0x0F));
  out[1] = (uint8_t)((value >> 16) & 0xFF);
  out[2] = (uint8_t)((value >> 8)  & 0xFF);
  out[3] = (uint8_t)( value        & 0xFF);
  return 4;
}

// ---- WebM init segment ------------------------------------------------------
// Byte-for-byte construction of:
//   EBML Header
//   Segment (unknown size)
//     Info { TimecodeScale = 1000000 ns (i.e. 1 ms per timecode unit) }
//     Tracks { TrackEntry { TrackNumber=1, TrackUID=1, TrackType=2 (audio),
//                           CodecID="A_OPUS",
//                           CodecPrivate=<19-byte OpusHead>,
//                           Audio { SamplingFrequency=48000.0,
//                                   Channels=2 } } }
//
// Sent once per WebM client, immediately after the WebSocket opens.
static const uint8_t kWebmInitSegment[] = {
  // ---- EBML Header (36 bytes) ----
  0x1A, 0x45, 0xDF, 0xA3, 0x9F,                          // EBML, size=31
    0x42, 0x86, 0x81, 0x01,                              // EBMLVersion = 1
    0x42, 0xF7, 0x81, 0x01,                              // EBMLReadVersion = 1
    0x42, 0xF2, 0x81, 0x04,                              // EBMLMaxIDLength = 4
    0x42, 0xF3, 0x81, 0x08,                              // EBMLMaxSizeLength = 8
    0x42, 0x82, 0x84, 'w','e','b','m',                   // DocType = "webm"
    0x42, 0x87, 0x81, 0x02,                              // DocTypeVersion = 2
    0x42, 0x85, 0x81, 0x02,                              // DocTypeReadVersion = 2
  // ---- Segment (unknown size, 12 bytes) ----
  0x18, 0x53, 0x80, 0x67,
    0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,      // unknown size (8-byte VINT)
  // ---- Info (12 bytes) ----
  0x15, 0x49, 0xA9, 0x66, 0x87,
    0x2A, 0xD7, 0xB1, 0x83, 0x0F, 0x42, 0x40,            // TimecodeScale = 1000000
  // ---- Tracks (61 bytes) ----
  //
  // Sizes below are VINT-encoded and were previously wrong (0xBB / 0xB9
  // claimed Tracks=59, TrackEntry=57). The TrackEntry payload is exactly
  // 54 bytes:
  //     TrackNumber          D7 81 01                 -> 3
  //     TrackUID             73 C5 81 01              -> 4
  //     TrackType            83 81 02                 -> 3
  //     CodecID              86 85 "A_OPUS"           -> 7
  //     CodecPrivate         63 A2 93 + 19 bytes      -> 22
  //     Audio                E1 8D + 13 bytes         -> 15
  //                                                    ----
  //                                                      54
  // VINT(54) = 0xB6. TrackEntry element (0xAE + 0xB6 + 54) = 56 bytes, so
  // the Tracks payload is 56 bytes and VINT(56) = 0xB8.
  //
  // Chromium's WebM demuxer tolerates the old mismatch and plays anyway;
  // Firefox's validates the declared size and rejects the init segment,
  // leaving /live silent with no error in either console.
  0x16, 0x54, 0xAE, 0x6B, 0xB8,                          // Tracks, size=56
    0xAE, 0xB6,                                          // TrackEntry, size=54
      0xD7, 0x81, 0x01,                                  // TrackNumber = 1
      0x73, 0xC5, 0x81, 0x01,                            // TrackUID = 1
      0x83, 0x81, 0x02,                                  // TrackType = 2 (audio)
      0x86, 0x85, 'A','_','O','P','U','S',               // CodecID = "A_OPUS"
      0x63, 0xA2, 0x93,                                  // CodecPrivate, 19 bytes
      'O','p','u','s','H','e','a','d',
      0x01, 0x02, 0x00, 0x00, 0x80, 0xBB, 0x00, 0x00, 0x00, 0x00,
      0x00,                                              // channel-mapping-family [v1.1.1-webm-head]
      0xE1, 0x8D,                                        // Audio, size=13
      0xB5, 0x88,                                        // SamplingFrequency, 8 bytes
      0x40, 0xE5, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00,   // 48000.0 as BE double
      0x9F, 0x81, 0x02                                  // Channels = 2
};

// ---- WebM frame append ------------------------------------------------------
// Append one Opus frame to the current cluster as a SimpleBlock. The caller
// is responsible for having taken gLiveMutex.
static inline void evdWebmAppendFrame(const OpusPacket &pkt) {
  // First frame in cluster sets the cluster base timecode. Subsequent frames
  // are stored as signed int16 deltas relative to that base. captured_ms is
  // the server's millis() at the moment of capture, so deltas grow by ~20 ms
  // per frame and never exceed the int16 range within a cluster.
  if (gWebmClusterFrameCount == 0) {
    gWebmClusterBaseTimecode = pkt.captured_ms;
  }

  int32_t delta = (int32_t)(pkt.captured_ms - gWebmClusterBaseTimecode);
  if (delta >  32767) delta =  32767;
  if (delta < -32768) delta = -32768;

  // SimpleBlock layout:
  //   A3                  EBML ID
  //   <VINT size>         payload length
  //   81                  TrackNumber = 1 (1-byte VINT)
  //   <int16 BE>          timestamp delta vs cluster base
  //   80                  flags: keyframe, no lacing
  //   <payload>           Opus frame bytes
  const size_t sbContentLen = 4 + pkt.len;
  uint8_t sbSizeVint[4];
  const size_t sbSizeVintLen = evdWriteVintSize(sbSizeVint, sbContentLen);
  const size_t sbTotalLen    = 1 + sbSizeVintLen + sbContentLen;

  if (gWebmClusterLen + sbTotalLen > sizeof(gWebmClusterBuf)) return;

  uint8_t *p = gWebmClusterBuf + gWebmClusterLen;
  *p++ = 0xA3;
  memcpy(p, sbSizeVint, sbSizeVintLen);
  p += sbSizeVintLen;
  *p++ = 0x81;
  *p++ = (uint8_t)((delta >> 8) & 0xFF);
  *p++ = (uint8_t)( delta       & 0xFF);
  *p++ = 0x80;
  memcpy(p, pkt.data, pkt.len);
  p += pkt.len;

  gWebmClusterLen = (size_t)(p - gWebmClusterBuf);
  gWebmClusterFrameCount++;
}

// ---- WebM cluster flush -----------------------------------------------------
// Wrap the accumulated SimpleBlocks in a Cluster element and broadcast to
// every connected WebM client as a single binary WS message (MSE requires
// one complete top-level EBML element per appendBuffer call, so the whole
// cluster must fit in one message).
static inline void evdWebmFlushCluster() {
  if (gWebmClusterFrameCount == 0) return;

  const size_t sbLen = gWebmClusterLen - EVD_WEBM_HDR_RESERVE;

  // Timecode element:
  //   E7 84 <uint32 BE>   Cluster base timecode in ms
  const size_t tcElemLen = 6;

  const size_t clusterContentLen = tcElemLen + sbLen;
  uint8_t clusterSizeVint[4];
  const size_t clusterSizeVintLen =
      evdWriteVintSize(clusterSizeVint, clusterContentLen);

  const size_t clusterHdrLen = 4 + clusterSizeVintLen + tcElemLen;
  if (clusterHdrLen > EVD_WEBM_HDR_RESERVE) return;   // should never happen

  // Write the header into the reserved prefix, right-aligned so it abuts
  // the SimpleBlocks. Everything before the header is discarded.
  const size_t hdrStart = EVD_WEBM_HDR_RESERVE - clusterHdrLen;
  uint8_t *h = gWebmClusterBuf + hdrStart;
  *h++ = 0x1F; *h++ = 0x43; *h++ = 0xB6; *h++ = 0x75;   // Cluster ID
  memcpy(h, clusterSizeVint, clusterSizeVintLen);
  h += clusterSizeVintLen;
  *h++ = 0xE7;
  *h++ = 0x84;
  *h++ = (uint8_t)((gWebmClusterBaseTimecode >> 24) & 0xFF);
  *h++ = (uint8_t)((gWebmClusterBaseTimecode >> 16) & 0xFF);
  *h++ = (uint8_t)((gWebmClusterBaseTimecode >>  8) & 0xFF);
  *h++ = (uint8_t)( gWebmClusterBaseTimecode        & 0xFF);

  const uint8_t *msg  = gWebmClusterBuf + hdrStart;
  const size_t   msgLen = clusterHdrLen + sbLen;

  // Fan out to every WebM client. Clients that have gone away are released
  // from their slot so a later connection can reuse it.
  for (int i = 0; i < EVD_LIVE_WS_MAX_CLIENTS; i++) {
    if (gLiveClients[i].format != EVD_FMT_WEBM) continue;
    AsyncWebSocketClient *c = gLiveWsWebm.client(gLiveClients[i].clientId);
    if (!c || c->status() != WS_CONNECTED) {
      gLiveClients[i].clientId = 0;
      gLiveClients[i].format   = EVD_FMT_NONE;
      continue;
    }
    c->binary((uint8_t *)msg, msgLen);
  }

  // Reset for the next cluster.
  gWebmClusterLen        = EVD_WEBM_HDR_RESERVE;
  gWebmClusterFrameCount = 0;
}

// ============================================================================
//  Client slot management
// ============================================================================

static inline int evdLiveFindSlot(uint32_t id) {
  for (int i = 0; i < EVD_LIVE_WS_MAX_CLIENTS; i++)
    if (gLiveClients[i].clientId == id) return i;
  return -1;
}

static inline int evdLiveAllocSlot(uint32_t id, uint8_t fmt) {
  for (int i = 0; i < EVD_LIVE_WS_MAX_CLIENTS; i++) {
    if (gLiveClients[i].clientId == 0) {
      gLiveClients[i].clientId = id;
      gLiveClients[i].format   = fmt;
      gLiveClients[i].pageSeq  = 2;
      gLiveClients[i].granule  = 0;
      return i;
    }
  }
  return -1;
}

static inline void evdLiveReleaseSlot(uint32_t id) {
  int slot = evdLiveFindSlot(id);
  if (slot >= 0) {
    gLiveClients[slot].clientId = 0;
    gLiveClients[slot].format   = EVD_FMT_NONE;
    gLiveClients[slot].pageSeq  = 2;
    gLiveClients[slot].granule  = 0;
  }
}

// ============================================================================
//  Broadcast (called from udpSendTask on Core 1)
// ============================================================================

inline void evdLiveBroadcastPacket(const OpusPacket &pkt) {
  if (!gLiveMutex) return;
  if (gLiveWsWebm.count() == 0 && gLiveWsOgg.count() == 0) return;
  // [v1.1.1-live-frame-ceiling]
  // Was OGG_TEST_MAX_PACKET_BYTES (256). Silent drop the moment Opus VBR or
  // a higher bitrate pushes a frame past that; the OGG page builder already
  // allocates for OPUS_MAX_PACKET_BYTES so this costs nothing.
  if (pkt.len <= 0 || pkt.len > OPUS_MAX_PACKET_BYTES) {
    LOGW("Live WS: dropping %d-byte Opus frame (max %d)",
         pkt.len, OPUS_MAX_PACKET_BYTES);
    return;
  }

  if (xSemaphoreTake(gLiveMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

  // ---- OGG: one page per frame, per-client page numbering ----
  if (gLiveWsOgg.count() > 0) {
    for (int i = 0; i < EVD_LIVE_WS_MAX_CLIENTS; i++) {
      if (gLiveClients[i].format != EVD_FMT_OGG) continue;
      AsyncWebSocketClient *c = gLiveWsOgg.client(gLiveClients[i].clientId);
      if (!c || c->status() != WS_CONNECTED) {
        gLiveClients[i].clientId = 0;
        gLiveClients[i].format   = EVD_FMT_NONE;
        continue;
      }
      gLiveClients[i].granule += OPUS_FRAME_SAMPLES;

      uint8_t page[27 + (OGG_TEST_MAX_PACKET_BYTES / 255) + 2 +
                   OGG_TEST_MAX_PACKET_BYTES];
      size_t n = evdBuildOggPage(page, sizeof(page),
                                 pkt.data, pkt.len, 0x454E4B4C,
                                 gLiveClients[i].pageSeq++,
                                 gLiveClients[i].granule, 0x00);
      if (n) c->binary(page, n);
    }
  }

  // ---- WebM: append to shared cluster, flush when full enough ----
  if (gLiveWsWebm.count() > 0) {
    const size_t before = gWebmClusterLen;
    evdWebmAppendFrame(pkt);
    // [v1.1.1-webm-cluster-stall]
    // If the append was rejected for size (gWebmClusterLen unchanged with
    // frames already queued) and neither threshold is tripped, flush what
    // we already have so the buffer cannot deadlock on an oversized frame.
    const bool appendRejected =
        (gWebmClusterLen == before) && (gWebmClusterFrameCount > 0);
    if (gWebmClusterFrameCount >= EVD_WEBM_CLUSTER_MAX_FRAMES ||
        (gWebmClusterLen - EVD_WEBM_HDR_RESERVE) >= EVD_WEBM_CLUSTER_MAX_BYTES ||
        appendRejected) {
      evdWebmFlushCluster();
    }
  }

  xSemaphoreGive(gLiveMutex);
}

// ============================================================================
//  WebSocket event handlers
// ============================================================================

static void evdWsEventWebm(AsyncWebSocket *srv, AsyncWebSocketClient *client,
                           AwsEventType type, void *arg,
                           uint8_t *data, size_t len) {
  (void)srv; (void)arg; (void)data; (void)len;
  if (type == WS_EVT_CONNECT) {
    if (xSemaphoreTake(gLiveMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      client->close(); return;
    }
    int slot = evdLiveAllocSlot(client->id(), EVD_FMT_WEBM);
    xSemaphoreGive(gLiveMutex);
    if (slot < 0) {
      LOGW("Live WS WebM: no free slot for client %u", client->id());
      client->close();
      return;
    }
    LOGI("Live WS WebM: client %u connected (slot %d)", client->id(), slot);
    client->binary((uint8_t *)kWebmInitSegment, sizeof(kWebmInitSegment));
  } else if (type == WS_EVT_DISCONNECT) {
    if (xSemaphoreTake(gLiveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      evdLiveReleaseSlot(client->id());
      xSemaphoreGive(gLiveMutex);
    }
    LOGI("Live WS WebM: client %u disconnected", client->id());
  }
}

static void evdWsEventOgg(AsyncWebSocket *srv, AsyncWebSocketClient *client,
                          AwsEventType type, void *arg,
                          uint8_t *data, size_t len) {
  (void)srv; (void)arg; (void)data; (void)len;
  if (type == WS_EVT_CONNECT) {
    if (xSemaphoreTake(gLiveMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      client->close(); return;
    }
    int slot = evdLiveAllocSlot(client->id(), EVD_FMT_OGG);
    xSemaphoreGive(gLiveMutex);
    if (slot < 0) {
      LOGW("Live WS OGG: no free slot for client %u", client->id());
      client->close();
      return;
    }
    LOGI("Live WS OGG: client %u connected (slot %d)", client->id(), slot);

    // Send OpusHead + OpusTags as one binary message so the demuxer sees
    // the complete init segment in a single appendBuffer() call.
    uint8_t buf[512];
    size_t off = 0;
    size_t n;
    n = evdBuildOggPage(buf, sizeof(buf), kOpusHead, sizeof(kOpusHead),
                        0x454E4B4C, 0, 0, 0x02);
    if (n) off = n;
    n = evdBuildOggPage(buf + off, sizeof(buf) - off, kOpusTags, sizeof(kOpusTags),
                        0x454E4B4C, 1, 0, 0x00);
    if (n) off += n;
    if (off) client->binary(buf, off);
  } else if (type == WS_EVT_DISCONNECT) {
    if (xSemaphoreTake(gLiveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      evdLiveReleaseSlot(client->id());
      xSemaphoreGive(gLiveMutex);
    }
    LOGI("Live WS OGG: client %u disconnected", client->id());
  }
}

// ============================================================================
//  Setup
// ============================================================================

static inline void setup_live_player() {
  if (!gLiveMutex) gLiveMutex = xSemaphoreCreateMutex();
  gLiveWsWebm.onEvent(evdWsEventWebm);
  gLiveWsOgg .onEvent(evdWsEventOgg);
  LOGI("Live player: /live  ->  ws://.../audio.webm (primary) "
       "or ws://.../audio.ogg (desktop Firefox fallback)");
}

static inline void add_live_player_routes(AsyncWebServer &server) {
  server.addHandler(&gLiveWsWebm);
  server.addHandler(&gLiveWsOgg);
}

// ============================================================================
//  Browser page
// ============================================================================

static const char EVDLIVEPLAYER_HTML[] PROGMEM = R"PLYR(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Enkelvoud Live Player</title>
<style>
  body{
    margin:0;min-height:100vh;
    background:radial-gradient(circle at top right,#2a1b52,transparent 28rem),#050510;
    color:#f6f6f8;font:16px system-ui,sans-serif;
    display:flex;align-items:center;justify-content:center;
    flex-direction:column;gap:20px;padding:20px;text-align:center;
  }
  h1{font-size:1.25rem;font-weight:700;color:#00d2ff;margin:0;letter-spacing:.3px}
  audio{width:min(600px,90vw)}
  .status{color:#a1a1ad;font-size:.85rem;max-width:34rem;line-height:1.5}
  button{
    background:linear-gradient(135deg,#00d2ff,#8e2de2);
    border:none;color:#fff;padding:12px 28px;border-radius:10px;
    font:inherit;font-weight:600;cursor:pointer;min-height:44px;
  }
  .warn{color:#ef4444;font-weight:600}
  .ok{color:#10b981}
  a{color:#00d2ff}
</style>
</head>
<body>
  <h1>Enkelvoud Live Player</h1>
  <audio id="p" controls playsinline></audio>
  <div class="status" id="status">Checking browser support…</div>
  <button id="startBtn" style="display:none">Start streaming</button>

<script>
(function(){
  "use strict";
  var audio  = document.getElementById('p');
  var status = document.getElementById('status');
  var button = document.getElementById('startBtn');

  var MIME_WEBM = 'audio/webm; codecs=opus';
  var MIME_OGG  = 'audio/ogg; codecs=opus';

  if (typeof MediaSource === 'undefined') {
    status.innerHTML = '<span class="warn">MediaSource unsupported.</span> Redirecting…';
    setTimeout(function(){ location.replace('/player'); }, 250);
    return;
  }

  // Prefer WebM: it works on Android Chrome and every Chromium-family
  // browser, plus recent Firefox. Fall back to OGG where WebM is missing
  // (rare, but Firefox desktop history has produced such builds). If neither
  // works, use the HTTP-refetch snapshot player.
  var mime, wsUrl;
  if (MediaSource.isTypeSupported(MIME_WEBM)) {
    mime  = MIME_WEBM;
    wsUrl = '/audio.webm';
    status.textContent = 'Connected via WebM/Opus.';
  } else if (MediaSource.isTypeSupported(MIME_OGG)) {
    mime  = MIME_OGG;
    wsUrl = '/audio.ogg';
    status.textContent = 'Connected via OGG/Opus.';
  } else {
    status.innerHTML =
      '<span class="warn">No supported live Opus transport.</span> ' +
      'Redirecting to the snapshot player…';
    setTimeout(function(){ location.replace('/player'); }, 400);
    return;
  }

 
  var mediaSource = null;
  var sourceBuffer = null;
  var ws = null;
  var queue = [];
  var reconnectDelay = 500;
  var started = false;
  // Set to true the instant the MediaSource starts closing. Any queued
  // appendBuffer() calls made after this flag is set are dropped silently
  // instead of throwing, because the SourceBuffer is already detached.
  var mseClosing = false;

  function teardownMSE(){
    mseClosing = true;
    if (sourceBuffer) {
      try { sourceBuffer.removeEventListener('updateend', drain); } catch (_) {}
    }
    if (mediaSource && mediaSource.readyState === 'open') {
      try { mediaSource.endOfStream(); } catch (_) {}
    }
    sourceBuffer = null;
    mediaSource = null;
    queue = [];
    started = false;
    try { URL.revokeObjectURL(audio.src); } catch (_) {}
  }

  function startMSE(){
    return new Promise(function(resolve, reject){
      // Always tear down any previous MSE session before creating a new one.
      // Without this, the previous SourceBuffer stays alive until GC and a
      // late drain() call can still try to append to it.
      teardownMSE();
      mseClosing = false;

      mediaSource = new MediaSource();
      audio.src = URL.createObjectURL(mediaSource);

      mediaSource.addEventListener('sourceopen', function h(){
        mediaSource.removeEventListener('sourceopen', h);
        if (mseClosing) return;               // teardown raced us
        try {
          sourceBuffer = mediaSource.addSourceBuffer(mime);
          sourceBuffer.addEventListener('updateend', drain);
          resolve();
        } catch (e) {
          status.textContent = 'addSourceBuffer failed: ' + e.message;
          reject(e);
        }
      });

      // Fires when the MediaSource object's readyState transitions to
      // "closed" — either because the <audio> element was detached, a load
      // was triggered, or the browser reclaimed it. Clearing our references
      // here is the only reliable way to prevent subsequent drain() calls
      // from touching a dead SourceBuffer.
      mediaSource.addEventListener('sourceclose', function(){
        mseClosing = true;
        sourceBuffer = null;
      });
    });
  }

  function drain(){
    // Guard chain, in order of cheapest to most expensive:
    //   1. If we know MSE is closing, drop everything immediately.
    //   2. No buffer or MediaSource reference means we're between sessions.
    //   3. MediaSource must be in the "open" state to accept an append.
    //   4. SourceBuffer.updating must be false (a prior append must finish).
    //   5. There must actually be something queued.
    if (mseClosing) return;
    if (!sourceBuffer || !mediaSource) return;
    if (mediaSource.readyState !== 'open') return;
    if (sourceBuffer.updating) return;
    if (queue.length === 0) return;

    var chunk = queue.shift();
    try {
      sourceBuffer.appendBuffer(chunk);
    } catch (e) {
      if (e.name === 'QuotaExceededError') {
        // Browser ran out of MSE buffer. Evict the oldest 5 seconds, then
        // re-queue the chunk so the next updateend fires drain() again.
        try {
          sourceBuffer.remove(0, Math.max(0, audio.currentTime - 5));
        } catch (_) { /* remove() can also throw if the buffer just closed */ }
        queue.unshift(chunk);
      } else if (e.name === 'InvalidStateError') {
        // The SourceBuffer was detached between our readyState check and
        // the appendBuffer() call. Discard this chunk and let the reconnect
        // path rebuild the pipeline from scratch on the next message.
        sourceBuffer = null;
      } else {
        status.textContent = 'appendBuffer error: ' + e.message;
      }
    }
  }

  function connect(){
    startMSE().then(function(){
      var proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
      ws = new WebSocket(proto + location.host + wsUrl);
      ws.binaryType = 'arraybuffer';

      ws.onopen = function(){
        reconnectDelay = 500;
        status.innerHTML = '<span class="ok">Streaming.</span> ' +
                           (mime.indexOf('webm') >= 0 ? 'WebM/Opus.' : 'OGG/Opus.');
      };

      ws.onmessage = function(ev){
        if (!(ev.data instanceof ArrayBuffer)) return;
        // Drop packets that arrive during the brief window between the
        // MediaSource closing and the new one's sourceopen firing. They
        // belong to the old timeline anyway; the new init segment resets
        // the decode clock cleanly.
        if (mseClosing) return;
        queue.push(ev.data);
        drain();
        if (!started && audio.buffered.length && audio.buffered.end(0) > 0.3) {
          started = true;
          audio.play().catch(function(err){
            button.style.display = '';
            status.innerHTML = 'Autoplay blocked (' + err.name + '). Tap below.';
          });
        }
      };

      ws.onclose = function(){
        status.textContent = 'Disconnected. Reconnecting in ' +
                             (reconnectDelay/1000).toFixed(1) + ' s…';
        // Tear down MSE immediately so any in-flight drain() stops appending
        // to a buffer whose audio element is about to be re-pointed.
        teardownMSE();
        setTimeout(function(){
          reconnectDelay = Math.min(reconnectDelay * 2, 5000);
          connect();
        }, reconnectDelay);
      };

      ws.onerror = function(){
        status.innerHTML = '<span class="warn">WebSocket error.</span> ' +
                           'Will retry.';
      };
    }).catch(function(){ /* startMSE already reported it */ });
  }


  button.addEventListener('click', function(){
    button.style.display = 'none';
    audio.play().catch(function(err){
      status.textContent = 'Playback failed: ' + err.message;
    });
  });

  connect();
})();
</script>
</body>
</html>
)PLYR";