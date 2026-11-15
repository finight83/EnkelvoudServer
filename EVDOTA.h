#pragma once
// ============================================================================
//  EVDOTA.h - Over-the-air update manager for EnkelvoudServer
// ============================================================================
#include <Update.h>
#include <FS.h>
#include <LittleFS.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h> 
#include <MD5Builder.h>
#include <HardwareSerial.h>

// -- State -------------------------------------------------------------------
struct OtaFile {
  String name;
  String kind;
  String version;
  size_t size;
  String path;
};

struct OtaNodeVersion {
  String ip;
  String name;
  String version;
  bool   reachable;
};

static std::vector<OtaFile>        otaSpool;
static std::vector<OtaFile>        otaAllSpoolFiles; // NEW: For diagnostic UI
static std::vector<OtaNodeVersion> otaNodeVersions;
// [ota-mutex-single-tu] otaMutex is declared in EVDState.h and created in
// createSyncPrimitives() (EVDHelpers.h). Do not redeclare it here.
// [ota-extern-linkage] Removed 'static' so the extern declarations in
// EVDPlayers.h can link to these definitions properly.
volatile bool                      otaInProgress = false;
static volatile int                otaProgressPct = 0;
static String                      otaLastError;
static String                      otaLastTarget;
static volatile unsigned long otaJobStartedMs = 0;

// [ota-per-node-progress] Per-node outcome tracking, exposed by
// handleOtaStatus so the wizard can paint checkmarks on each finished node
// as soon as the job advances to the next one. Without this, the client
// had no way to tell "already updated" from "not started yet" once the
// next node's spinner replaced the previous node's, and every finished
// node reverted to "Waiting…". Mutated only by the otaJob task under
// otaMutex; snapshot-copied by handleOtaStatus under the same mutex.
static std::vector<String> otaCompletedNodes;
static std::vector<String> otaFailedNodes;

// [ota-status-string-lock] otaLastTarget / otaLastError are String objects
// written by the otaJob task's lambda and read by handleOtaStatus /
// handleOtaPlan on the AsyncTCP task. String assignment is
// free-old + alloc-new + pointer-swap; a concurrent reader can walk a
// _buffer pointing at freed memory. Serialise every access under
// otaMutex. The writes are tiny (a short String assign), so the lock is
// held for microseconds — no risk of blocking the status poller.
static void otaSetTarget(const String &t) {
  if (otaMutex && xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    otaLastTarget = t;
    xSemaphoreGive(otaMutex);
  }
}
static void otaSetError(const String &e) {
  if (otaMutex && xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    otaLastError = e;
    xSemaphoreGive(otaMutex);
  }
}

extern volatile bool otaPauseAudio;

// [ota-upload-flag-hoist]
// Single declaration. handleOtaApply() (defined earlier in this file) needs
// this flag, so it must live up here. Do NOT add a second copy further down.
// [ota-extern-linkage] Removed 'static' to match the extern in EVDPlayers.h.
volatile bool otaUploadInProgress = false;

// -- Helpers -----------------------------------------------------------------
static bool parseFwName(const String &name, OtaFile &out) {
  int us = name.indexOf('_');
  int dot = name.lastIndexOf(".bin");
  if (us <= 0 || dot <= us) return false;
  String kind = name.substring(0, us);
  kind.toLowerCase();
  if (kind != "server" && kind != "node" && kind != "source") return false;
  out.name    = name;
  out.kind    = kind;
  out.version = name.substring(us + 1, dot);
  return out.version.length() > 0;
}

static int cmpVersion(const String &a, const String &b) {
  int ai = 0, bi = 0;
  while (ai < (int)a.length() || bi < (int)b.length()) {
    int an = 0, bn = 0;
    while (ai < (int)a.length() && a[ai] != '.') an = an * 10 + (a[ai++] - '0');
    while (bi < (int)b.length() && b[bi] != '.') bn = bn * 10 + (b[bi++] - '0');
    if (a[ai] == '.') ai++;
    if (b[bi] == '.') bi++;
    if (an != bn) return an < bn ? -1 : 1;
  }
  return 0;
}

static void otaSpoolAdd(const OtaFile &f) {
  for (auto &e : otaSpool) {
    if (e.name == f.name) { e.size = f.size; e.path = f.path; return; }
  }
  otaSpool.push_back(f);
}

// ---------------------------------------------------------------------------
//  LittleFS init guard.
//
//  We only ever want to call LittleFS.begin() once per boot. begin(true)
//  attempts a format when the mount fails, and that call can block for tens
//  of milliseconds. handleOtaPlan() runs on the AsyncTCP task (Core 0), and
//  blocking that task stalls every in-flight HTTP response, which is what
//  made the dashboard show "Server firmware: Offline" even though the JSON
//  plan was being built correctly. begin(false) never formats, and the
//  otaFsInitTried flag ensures we do not retry on every /api/ota/plan poll.
// ---------------------------------------------------------------------------
static bool otaFsReady     = false;
static bool otaFsInitTried = false;

// [ota-fs-last-attempt-type] millis() is uint32_t; storing it in a bool
// truncates every value to 0 or 1, so (millis() - otaFsLastAttemptMs)
// was really "millis() - (0|1)" and the retry window was effectively
// "always retry in first 5 s, never after". uint32_t restores the
// documented 5 s retry-after-failure behaviour.
static uint32_t otaFsLastAttemptMs = 0;
static bool ensureOtaFs() {
  if (otaFsReady) return true;
  // [ota-fs-retry-once] First failure sets the "tried" flag; allow one
  // retry after 5 s in case the first attempt raced a concurrent LittleFS
  // write (OTA upload) and the partition was momentarily busy. After the
  // retry window, stop trying to avoid hammering a genuinely broken flash.
  if (otaFsInitTried && (millis() - otaFsLastAttemptMs) < 5000UL) return false;
  otaFsInitTried     = true;
  otaFsLastAttemptMs = millis();

  if (!LittleFS.begin(false)) {       // no format-on-fail
    LOGW("OTA: LittleFS not available (attempt at %lu ms)",
         (unsigned long)otaFsLastAttemptMs);
    return false;
  }
  otaFsReady = true;
  LOGI("OTA: LittleFS mounted");
  return true;
}

static void otaScanSpool() {
  otaSpool.clear();
  otaAllSpoolFiles.clear(); // Reset the diagnostic list
  
  if (!ensureOtaFs()) return;

  File root = LittleFS.open("/ota_spool");
  if (!root || !root.isDirectory()) return;

  struct ScannedFile {
    OtaFile info;
  };
  std::vector<ScannedFile> files;
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String full = String("/ota_spool/") + String(f.name());
      String base = String(f.name());
      int slash = base.lastIndexOf('/');
      if (slash >= 0) base = base.substring(slash + 1);
      
      // Record EVERY file for the UI diagnostic list
      OtaFile diagFile;
      diagFile.name = base;
      diagFile.size = f.size();
      otaAllSpoolFiles.push_back(diagFile);

      OtaFile of;
      if (parseFwName(base, of)) {
        of.size = f.size();
        of.path = full;
        files.push_back({of});
      }
    }
    f = root.openNextFile();
  }

  // [ota-spool-keep-all] Keep ALL valid parsed firmware files in the active
  // spool, not just the newest of each kind. This allows the dashboard to
  // list every available bin and let the operator explicitly choose which
  // one to flash (e.g. rolling back to an older version).
  for (auto &file : files) {
    otaSpoolAdd(file.info);
  }
}

static void otaProbeNodes() {
  otaNodeVersions.clear();

  std::vector<PlayerNode> players = snapshotPlayers(millis());
  auto lookupName = [&players](const String &ip) -> String {
    for (auto &p : players) if (p.ip == ip) return p.name;
    return ip;
  };
  auto lookupVer = [&players](const String &ip) -> String {
    for (auto &p : players) if (p.ip == ip) return p.version;
    return String("unknown");
  };

  std::vector<String> seen;

  // 1. Players that have hit /api/player/register recently -> reachable.
  for (auto &p : players) {
    OtaNodeVersion nv;
    nv.ip        = p.ip;
    nv.name      = p.name;
    nv.version   = p.version.length() ? p.version : String("unknown");  // NEW: real version
    nv.reachable = true;
    otaNodeVersions.push_back(nv);
    seen.push_back(p.ip);
  }

  // 2. Subscribers not in the player list (heartbeat through the same route
  //    may have dropped them but they are still receiving audio).
  if (subsMutex && xSemaphoreTake(subsMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    for (int i = 0; i < subscriberTableCount; i++) {
      String ip = subscribers[i].ip;
      bool dup = false;
      for (auto &s : seen) if (s == ip) { dup = true; break; }
      if (dup) continue;
      OtaNodeVersion nv;
      nv.ip        = ip;
      nv.name      = lookupName(ip);
      nv.version   = lookupVer(ip);
      nv.reachable = true;
      otaNodeVersions.push_back(nv);
    }
    xSemaphoreGive(subsMutex);
  }

  LOGI("OTA plan: %u players, %u subscriber slots, %u nodes listed",
       (unsigned)players.size(), (unsigned)subscriberTableCount,
       (unsigned)otaNodeVersions.size());
  for (auto &nv : otaNodeVersions) {
    LOGI("  node  %-15s name=%-16s version=%-8s reachable=%s",
         nv.ip.c_str(), nv.name.c_str(),
         nv.version.length() ? nv.version.c_str() : "?",
         nv.reachable ? "yes" : "no");
  }
}

// -- Push to a node ----------------------------------------------------------
static bool otaPushToNode(const String &ip, const OtaFile &file, String &errOut) {
  // [ota-skip-dead-node] Fast reachability gate.
  {
    std::vector<PlayerNode> recent = snapshotPlayers(millis());
    bool alive = false;
    unsigned long ageMs = 0xFFFFFFFFUL;
    for (auto &p : recent) {
      if (p.ip == ip) { ageMs = millis() - p.lastSeenMs; break; }
    }
    if (ageMs == 0xFFFFFFFFUL || ageMs > 60000UL) {
      char buf[96];
      snprintf(buf, sizeof(buf),
               "no recent heartbeat (age %lu s); skipped",
               ageMs == 0xFFFFFFFFUL ? 0UL : (ageMs / 1000UL));
      errOut = buf;
      return false;
    }
  }

  if (file.path.length() == 0) { errOut = "spool file missing"; return false; }
  File f = LittleFS.open(file.path, "r");
  if (!f) { errOut = "cannot open " + file.path; return false; }
  size_t total = f.size();

  const String boundary = "----EVDOTA";
  String head = "--" + boundary + "\r\n"
                "Content-Disposition: form-data; name=\"fw\"; filename=\"" +
                file.name + "\"\r\n"
                "Content-Type: application/octet-stream\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";
  size_t contentLen = head.length() + total + tail.length();

  const int MAX_RETRIES = 3;
  const unsigned long WRITE_STALL_MS = 60000UL;  // 60 seconds: covers flash erase
  const size_t CHUNK_SIZE = 512;                 // smaller chunks ease node TCP buffer

  for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    if (attempt > 1) {
      f.seek(0);
      LOGI("OTA: retry %d/%d for node %s", attempt, MAX_RETRIES, ip.c_str());
    }

    WiFiClient raw;
    if (!raw.connect(ip.c_str(), 80, 2000)) {
      IPAddress probe;
      bool ipOk = probe.fromString(ip);
      bool sameSubnet = ipOk &&
        ((uint32_t)probe & (uint32_t)WiFi.subnetMask()) ==
        ((uint32_t)WiFi.localIP() & (uint32_t)WiFi.subnetMask());
      if (!ipOk) { errOut = "invalid ip"; }
      else if (!sameSubnet) { errOut = "node on different subnet than server"; }
      else { errOut = "tcp connect timed out (node offline, rebooting, or AP isolates clients)"; }
      if (attempt == MAX_RETRIES) { f.close(); return false; }
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    raw.setTimeout(60000);       // wait up to 60s for the node to ACK the first chunk
    raw.setNoDelay(true);        // send headers + body immediately

    String hdr = "POST /api/ota HTTP/1.1\r\n"
                 "Host: " + ip + "\r\n"
                 "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n"
                 "Content-Length: " + String(contentLen) + "\r\n"
                 "Connection: close\r\n\r\n" + head;

    size_t hdrLen = hdr.length();
    if (raw.print(hdr) != hdrLen) {
      raw.stop();
      errOut = "short write of HTTP headers";
      if (attempt == MAX_RETRIES) { f.close(); return false; }
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    uint8_t buf[CHUNK_SIZE];
    bool writeFailed = false;
    String failReason;

    while (f.available()) {
      size_t n = f.read(buf, sizeof(buf));
      if (n == 0) continue;

      const uint8_t *wp = buf;
      size_t remaining = n;
      unsigned long writeStart = millis();

      while (remaining > 0) {
        if (!raw.connected()) {
          writeFailed = true;
          failReason = "connection dropped mid-write";
          break;
        }
        size_t w = raw.write(wp, remaining);
        if (w > 0) {
          wp += w;
          remaining -= w;
          writeStart = millis();
          continue;
        }
        if (millis() - writeStart > WRITE_STALL_MS) {
          writeFailed = true;
          failReason = String("write stalled at chunk offset ") +
                       String((unsigned)(n - remaining)) + "/" + String((unsigned)n);
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
      }
      if (writeFailed) break;
      otaProgressPct = (int)((f.position() * 100) / (total ? total : 1));
      g_otaLastProgressMs = millis();
      vTaskDelay(1);
    }

    if (writeFailed) {
      raw.stop();
      errOut = failReason;
      if (attempt == MAX_RETRIES) { f.close(); return false; }
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    if (raw.print(tail) == 0) {
      raw.stop();
      errOut = "failed to send HTTP trailer";
      if (attempt == MAX_RETRIES) { f.close(); return false; }
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    unsigned long t0 = millis();
    while (raw.connected() && millis() - t0 < 8000 && !raw.available()) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }

    String resp = "";
    while (raw.available()) {
      resp += (char)raw.read();
    }

    raw.stop();
    g_otaLastProgressMs = millis();

    bool ok = resp.indexOf("\"ok\":true") >= 0;
    if (!ok) {
      if (resp.length() == 0) {
        errOut = "node did not reply (timeout after upload)";
      } else {
        errOut = "node replied: " + resp.substring(0, 120);
      }
      if (attempt == MAX_RETRIES) { f.close(); return false; }
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    f.close();
    return true;
  }

  f.close();
  return false;
}

// ============================================================================
//  Source board OTA (UART) - matches the source's EVDBTOta.h wire format
// ============================================================================
//   S->Src: OTA_BEGIN|<ver>|<size>|<md5_hex>
//           OTA_DATA|<seq>|<offset_hex>|<hex>|<crc16_hex>
//           OTA_END|<md5_hex>
//   Src->S: OTA_READY / OTA_ERR|<r>
//           OTA_ACK|<seq> / OTA_NAK|<seq>|<r>
//           OTA_OK / OTA_FAIL|<r>
// CRC16-CCITT/XModem (poly 0x1021, init 0x0000). Payload capped at 512 B
// because the source's otaHexDecode() uses a static uint8_t[512] scratch.
//
// [ota-source-uart-exclusive]
// srcUart is owned by otaPushToSource() while otaInProgress==true. The
// loop()-time source-link parser MUST early-return on otaInProgress, or
// it eats the OTA_ACK bytes and every chunk times out.
// [ota-source-uart-name] Source-link UART is `receiverUart` (see
// setupReceiverLink() in EVDPlayers.h). Definition lives in EVDState.h.
extern HardwareSerial receiverUart;

static uint16_t otaCrc16Local(const uint8_t *data, size_t len) {
  uint16_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) {
      if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
      else              crc = (uint16_t)(crc << 1);
    }
  }
  return crc;
}

static void otaHexEncode(const uint8_t *src, size_t len, char *dst) {
  static const char hx[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    dst[i*2]     = hx[src[i] >> 4];
    dst[i*2 + 1] = hx[src[i] & 0x0F];
  }
  dst[len*2] = '\0';
}

// Fixed-buffer line reader (no String churn during OTA).
static bool otaReadSourceLine(char *out, size_t outCap, uint32_t timeoutMs) {
  size_t lineLen = 0;
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (receiverUart.available()) {
      int ci = receiverUart.read();
      if (ci < 0) break;
      char c = (char)ci;
      if (c == '\r') continue;
      if (c == '\n') {
        if (lineLen > 0) { out[lineLen] = '\0'; return true; }
        continue;
      }
      if (lineLen < outCap - 1) out[lineLen++] = c;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  out[0] = '\0';
  return false;
}

// Skips non-OTA lines (LOG|, SRCSTAT:, etc.) until one starts with `want`.
static bool otaWaitSourcePrefix(const char *wantPrefix, char *out,
                                size_t outCap, uint32_t timeoutMs) {
  size_t wlen = strlen(wantPrefix);
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    uint32_t remaining = timeoutMs - (millis() - start);
    if (remaining == 0) break;
    if (!otaReadSourceLine(out, outCap, remaining)) { out[0] = '\0'; return false; }
    if (strncmp(out, wantPrefix, wlen) == 0) return true;
  }
  out[0] = '\0';
  return false;
}

// [ota-source-static-buffers] Source OTA uses 512 B chunks (source's
// otaHexDecode() scratch buffer is uint8_t[512]). The four large buffers
// live in .bss, not on the otaJob task stack: static-lived arrays in
// otaPushToSource() would peak at ~4.2 KB (resp 1500 + buf 512 + hex
// 1025 + cmd 1200) plus call frames — over OTA_JOB_STACK (4096). Source
// OTA is serialised by otaInProgress, so static is safe here.
static constexpr size_t OTA_SRC_CHUNK = 512;
static char    otaSrcRespBuf[1500];
static uint8_t otaSrcDataBuf[OTA_SRC_CHUNK];
static char    otaSrcHexBuf[OTA_SRC_CHUNK * 2 + 1];
static char    otaSrcCmdBuf[1200];
static char    otaSrcCmdBeginBuf[160];
static char    otaSrcCmdEndBuf[64];

static bool otaPushToSource(const OtaFile &file, String &errOut) {
  if (file.path.length() == 0) { errOut = "source spool file missing"; return false; }
  File f = LittleFS.open(file.path, "r");
  if (!f) { errOut = "cannot open " + file.path; return false; }

  size_t total = f.size();
  if (total < 100 * 1024 || total > 1900 * 1024) {
    f.close();
    errOut = String("size out of range: ") + String((unsigned)total);
    return false;
  }

  MD5Builder md5;
  md5.begin();
  {
    uint8_t mb[1024];
    while (f.available()) {
      size_t n = f.read(mb, sizeof(mb));
      if (n == 0) break;
      md5.add(mb, n);
    }
  }
  md5.calculate();
  String md5Hex = md5.toString();
  if (md5Hex.length() != 32) { f.close(); errOut = "md5 compute failed"; return false; }
  f.seek(0);

  char *resp = otaSrcRespBuf;

  // ---- OTA_BEGIN ----------------------------------------------------
  {
    char *cmd = otaSrcCmdBeginBuf;
    int n = snprintf(cmd, sizeof(otaSrcCmdBeginBuf), "OTA_BEGIN|%s|%u|%s",
                     file.version.c_str(), (unsigned)total, md5Hex.c_str());
    if (n <= 0 || n >= (int)sizeof(otaSrcCmdBeginBuf)) { f.close(); errOut = "begin line overflow"; return false; }
    receiverUart.println(cmd);
    LOGI("OTA source: -> %s", cmd);
  }
  if (!otaWaitSourcePrefix("OTA_", resp, sizeof(otaSrcRespBuf), 15000)) {
    f.close(); errOut = "no reply to OTA_BEGIN (timeout)"; return false;
  }
  if (strncmp(resp, "OTA_READY", 9) == 0) {
    LOGI("OTA source: <- OTA_READY");
  } else if (strncmp(resp, "OTA_ERR|", 8) == 0) {
    errOut = String("source rejected begin: ") + (resp + 8);
    f.close(); return false;
  } else {
    errOut = String("unexpected begin reply: ") + resp;
    f.close(); return false;
  }

  // ---- OTA_DATA -----------------------------------------------------
  constexpr size_t CHUNK = OTA_SRC_CHUNK;
  uint8_t *buf = otaSrcDataBuf;
  char    *hex = otaSrcHexBuf;
  uint32_t seq = 0, offset = 0;

  while (offset < total) {
    size_t want = total - offset;
    if (want > CHUNK) want = CHUNK;
    size_t got = f.read(buf, want);
    if (got != want) { f.close(); errOut = String("short read at ") + String((unsigned)offset); return false; }

    uint16_t crc = otaCrc16Local(buf, got);
    otaHexEncode(buf, got, hex);

    char *cmd = otaSrcCmdBuf;
    int n = snprintf(cmd, sizeof(otaSrcCmdBuf), "OTA_DATA|%u|%08x|%s|%04x",
                     (unsigned)seq, (unsigned)offset, hex, (unsigned)crc);
    if (n <= 0 || n >= (int)sizeof(otaSrcCmdBuf)) { f.close(); errOut = "data line overflow"; return false; }

    // Source treats seq==expectedSeq-1 as idempotent re-ACK, so a lost ACK
    // can be retried safely without duplicating a write.
    bool   chunkOk = false;
    String chunkErr;
    for (int attempt = 1; attempt <= 2 && !chunkOk; attempt++) {
      if (attempt > 1) {
        LOGW("OTA source: retry chunk seq=%u (attempt %d)", (unsigned)seq, attempt);
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      receiverUart.println(cmd);
      if (!otaWaitSourcePrefix("OTA_", resp, sizeof(otaSrcRespBuf), 30000)) {
        chunkErr = String("chunk seq=") + String((unsigned)seq) + " timeout";
        continue;
      }
      if (strncmp(resp, "OTA_ACK|", 8) == 0) {
        long gotSeq = atol(resp + 8);
        if (gotSeq == (long)seq) chunkOk = true;
        else chunkErr = String("ack seq mismatch: sent ") + String((unsigned)seq) + " got " + String(gotSeq);
      } else if (strncmp(resp, "OTA_NAK|", 8) == 0) {
        chunkErr = String("chunk NAK: ") + (resp + 8);
      } else {
        chunkErr = String("unexpected reply: ") + resp;
      }
    }
    if (!chunkOk) { f.close(); errOut = chunkErr; return false; }

    offset += got;
    seq++;
    otaProgressPct = (int)((offset * 100) / total);
    g_otaLastProgressMs = millis();
    vTaskDelay(1);   // yield so AsyncTCP can serve /api/ota/status
  }

  // ---- OTA_END ------------------------------------------------------
  {
    char *cmd = otaSrcCmdEndBuf;
    int n = snprintf(cmd, sizeof(otaSrcCmdEndBuf), "OTA_END|%s", md5Hex.c_str());
    if (n <= 0 || n >= (int)sizeof(otaSrcCmdEndBuf)) { f.close(); errOut = "end line overflow"; return false; }
    receiverUart.println(cmd);
    LOGI("OTA source: -> %s", cmd);
  }

  bool ok = false;
  if (!otaWaitSourcePrefix("OTA_", resp, sizeof(otaSrcRespBuf), 30000)) {
    errOut = "no reply to OTA_END (timeout; source may still be committing)";
  } else if (strncmp(resp, "OTA_OK", 6) == 0) {
    LOGI("OTA source: <- OTA_OK (commit accepted; source will reboot)");
    ok = true;
  } else if (strncmp(resp, "OTA_FAIL|", 9) == 0) {
    errOut = String("source commit failed: ") + (resp + 9);
  } else {
    errOut = String("unexpected end reply: ") + resp;
  }
  f.close();
  return ok;
}


// -- Apply -------------------------------------------------------------------
// [ota-apply-chunk-accumulator]
// /api/ota/apply is registered as the 5-arg onBody form. The body handler
// fires once per TCP chunk. The apply JSON is < 200 B in practice and
// never fragments, but the previous code parsed the FIRST chunk only and
// ignored index/total — a fragmented body parsed garbage and returned 400
// mid-stream. Accumulate chunks into a small static buffer and only run
// the parse on the final chunk. Single AsyncTCP worker, so the statics
// are safe without a mutex.
static char   otaApplyBody[512];
static size_t otaApplyBodyLen = 0;

static void handleOtaApply(AsyncWebServerRequest *request, uint8_t *data,
                           size_t len, size_t index, size_t total) {
  if (index == 0) otaApplyBodyLen = 0;
  if (otaApplyBodyLen + len > sizeof(otaApplyBody)) {
    otaApplyBodyLen = 0;
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"apply body too large\"}");
    return;
  }
  if (data && len > 0) {
    memcpy(otaApplyBody + otaApplyBodyLen, data, len);
    otaApplyBodyLen += len;
  }
  if (index + len != total) return;    // wait for the final chunk
  size_t bodyLen  = otaApplyBodyLen;
  otaApplyBodyLen = 0;

  LOGW("OTA: /api/ota/apply entered (bodyLen=%u total=%u inProgress=%d heap=%u maxBlk=%u)",
       (unsigned)bodyLen, (unsigned)total, (int)otaInProgress,
       (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  // [ota-apply-lock-timeout] 500 ms, not 200 ms. handleOtaPlan (refreshPlan
  // from the wizard's Step 3 entry) holds otaMutex for up to ~500 ms during
  // otaScanSpool()'s directory walk + per-file String allocations. The old
  // 200 ms value returned 503 to ~5 % of "Start update" clicks where the
  // plan fetch and the apply POST landed within the same second. 500 ms
  // exceeds any legitimate plan scan and still fails fast if the mutex is
  // genuinely wedged.
  if (!otaMutex ||
      xSemaphoreTake(otaMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"ota state lock busy\"}");
    return;
  }
  bool _alreadyRunning = otaInProgress || otaUploadInProgress;
  if (!_alreadyRunning) {
    otaInProgress = true;   // commit early so concurrent /api/ota/apply
                            // and /api/ota/upload both observe it
  }
  xSemaphoreGive(otaMutex);
  if (_alreadyRunning) {
    request->send(409, "application/json",
                  "{\"ok\":false,\"error\":\"update/upload already running\"}");
    return;
  }
  // [ota-apply-abort-guard] Every early return below must release the
  // provisional otaInProgress set above. Without this, a malformed body or
  // a stale UI tab leaves the server permanently in "update running" state
  // with no otaJob task to clear it — every subsequent apply/upload/status/
  // log-tail 409s or 503s until reboot. RAII covers all six early-return
  // paths (parse fail, 4 file-availability checks, job alloc fail) plus
  // the eventual success path where the otaJob task takes ownership.
  struct _OtaApplyAbortGuard {
    bool commit = false;
    ~_OtaApplyAbortGuard() {
      if (commit) return;
      if (otaMutex && xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        otaInProgress = false;
        otaPauseAudio = false;
        xSemaphoreGive(otaMutex);
      } else {
        // Best-effort: single word stores, safe without the lock
        otaInProgress = false;
        otaPauseAudio = false;
      }
    }
  } _applyGuard;
  DynamicJsonDocument in(512);
  if (deserializeJson(in, otaApplyBody, bodyLen)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }

  bool doServer = in["server"] | false;
  bool doSource = in["source"] | false;                       // NEW
  std::vector<String> doNodes;
  if (in["nodes"].is<JsonArray>()) {
    for (JsonVariant v : in["nodes"].as<JsonArray>()) doNodes.push_back(v.as<String>());
  }

  // [ota-explicit-file-select]
  // Optional nodeFile / serverFile string fields let the UI pin which
  // exact bin to flash when the spool holds more than one of a kind.
  //   - field absent or empty  -> fall back to newest version (old behaviour,
  //                               so an old client still works)
  //   - field present but not in the spool -> 400, so a stale UI tab that
  //     was pointed at a now-deleted file cannot silently flash a different
  //     one than the operator saw on screen.
  String pickNodeFile   = in["nodeFile"]   | "";
  String pickServerFile = in["serverFile"] | "";
  String pickSourceFile = in["sourceFile"] | "";             // NEW

  OtaFile serverFile; bool haveServerFile = false;
  OtaFile nodeFile;   bool haveNodeFile   = false;
  OtaFile sourceFile; bool haveSourceFile = false;           // NEW

  // [ota-spool-read-locked] otaSpool is mutated by otaScanSpool() (from
  // handleOtaPlan, under otaMutex). Re-take the mutex for the read. The
  // early 503 return is safe — _applyGuard will clear the provisional
  // otaInProgress that was set above.
  if (!otaMutex || xSemaphoreTake(otaMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"ota state lock busy\"}");
    return;
  }
  for (auto &f : otaSpool) {
    if (f.kind == "server") {
      if (pickServerFile.length()) {
        if (f.name == pickServerFile) { serverFile = f; haveServerFile = true; }
      } else if (!haveServerFile || cmpVersion(f.version, serverFile.version) > 0) {
        serverFile = f; haveServerFile = true;
      }
    }
    if (f.kind == "node") {
      if (pickNodeFile.length()) {
        if (f.name == pickNodeFile) { nodeFile = f; haveNodeFile = true; }
      } else if (!haveNodeFile || cmpVersion(f.version, nodeFile.version) > 0) {
        nodeFile = f; haveNodeFile = true;
      }
    }
    if (f.kind == "source") {                                // NEW
      if (pickSourceFile.length()) {
        if (f.name == pickSourceFile) { sourceFile = f; haveSourceFile = true; }
      } else if (!haveSourceFile || cmpVersion(f.version, sourceFile.version) > 0) {
        sourceFile = f; haveSourceFile = true;
      }
    }
  }
  xSemaphoreGive(otaMutex);

  // Reject only when the CLIENT named a file that no longer exists.
  if (doServer && pickServerFile.length() && !haveServerFile) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"selected server bin not in spool\"}");
    return;
  }
  if (!doNodes.empty() && pickNodeFile.length() && !haveNodeFile) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"selected node bin not in spool\"}");
    return;
  }
  // Then the original "nothing at all of that kind in spool" checks.
  if (doServer && !haveServerFile) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"no server bin in spool\"}");
    return;
  }
  if (!doNodes.empty() && !haveNodeFile) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"no node bin in spool\"}");
    return;
  }

  if (doSource && pickSourceFile.length() && !haveSourceFile) {          // NEW
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"selected source bin not in spool\"}");
    return;
  }
  if (doSource && !haveSourceFile) {                                     // NEW
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"no source bin in spool\"}");
    return;
  }

  // [ota-pause-race] Set g_otaLastProgressMs BEFORE otaPauseAudio. The
  // loop()-side watchdog otaCheckStuckJob() reads both flags outside any
  // mutex:
  //     if (!otaPauseAudio || g_otaLastProgressMs == 0) return;
  //     if ((millis() - g_otaLastProgressMs) <= 120000UL) return;
  // If otaPauseAudio is set first while g_otaLastProgressMs is still stale
  // from a prior job (>120s old — any second-or-later OTA after a short
  // pause), loop() releases the pause we just armed before the 1500 ms
  // settle starts. The otaJob task then races the not-yet-drained WiFi TX
  // ring and its first raw.write() sees a zero window.
  //
  // Ordering progress-first means the elapsed check inside otaCheckStuckJob
  // sees ~0 ms and returns without touching otaPauseAudio. The subsequent
  // g_otaLastProgressMs = millis() below (right before xTaskCreate) is now
  // a redundant refresh; harmless, kept for clarity.
  g_otaLastProgressMs = millis();
  otaPauseAudio   = true;
  // [ota-error-mutex] Route through otaSetError() so a concurrent
  // handleOtaStatus() (holding otaMutex) never sees a String mid-rebuild.
  otaSetError("");
  otaProgressPct  = 0;
  otaJobStartedMs = millis();

  // [ota-first-target-sync] Publish the initial target AND reset per-node
  // tracking while still inside the apply handler, so the first
  // /api/ota/status poll (fired ~1 s later by the wizard) already sees a
  // target and paints the spinner. Previously the target stayed "" until
  // the otaJob task finished its 250 ms flush delay — long enough for one
  // poll to render "Waiting…" across the whole node list on every job.
  if (otaMutex && xSemaphoreTake(otaMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    otaCompletedNodes.clear();
    otaFailedNodes.clear();
    // Order matches otaJob's actual execution: source, then nodes, then server.
    if      (doSource)         otaLastTarget = "source";
    else if (!doNodes.empty()) otaLastTarget = "node " + doNodes[0];
    else if (doServer)         otaLastTarget = "server";
    else                       otaLastTarget = "";
    xSemaphoreGive(otaMutex);
  }

  // [ota-apply-response-ordering]
  // The 202 used to be sent here, BEFORE the job task was created. On a
  // freshly-uploaded heap the xTaskCreatePinnedToCore below can take a
  // noticeable time to find a contiguous 4 KB stack block, and until it
  // returns the AsyncTCP task is still inside this handler -- so the
  // 202 sits in the socket write queue and the browser's fetch never
  // resolves. That is the exact "Applying update -> Starting..." hang.
  //
  // Send the 202 AFTER task creation, and send a 500 on the rare heap
  // path where creation actually fails. Either way the browser always
  // gets exactly one response, immediately, and never sits on a
  // half-written reply.

  struct Job {
    std::vector<String> nodes;
    OtaFile nodeFile; bool haveNodeFile;
    OtaFile serverFile; bool haveServerFile;
    OtaFile sourceFile; bool haveSourceFile;   // NEW
    bool doServer;
    bool doSource;                             // NEW
  };
  Job *job = new (std::nothrow) Job();
  if (!job) {
    otaInProgress = false;
    otaPauseAudio = false;
    // [ota-error-mutex] IMP-1: every other write to otaLastError goes
    // through otaSetError() which takes otaMutex. handleOtaStatus reads
    // otaLastError under that lock; a naked String::operator= here races
    // a concurrent read of a freed _buffer on the exact path where the
    // heap is already exhausted.
    otaSetError("out of heap allocating OTA job");
    otaSetTarget("");
    request->send(500, "application/json",
                  "{\"ok\":false,\"error\":\"out of heap allocating OTA job\"}");
    return;
  }
  job->nodes = doNodes;
  job->nodeFile = nodeFile; job->haveNodeFile = haveNodeFile;
  job->serverFile = serverFile; job->haveServerFile = haveServerFile;
  job->sourceFile = sourceFile; job->haveSourceFile = haveSourceFile;   // NEW
  job->doServer = doServer;
  job->doSource = doSource;                                             // NEW

  // [ota-job-stack]
  // 4096 is enough for the node path (1 KB read buffer) and the server
  // path (1 KB write buffer), but NOT for the source path if its four
  // read/encode/frame buffers were stack-allocated — see
  // [ota-source-static-buffers] in otaPushToSource(). Those buffers are
  // now in .bss, so the source path also fits under 4096.
  constexpr uint32_t OTA_JOB_STACK = 4096;

  // [ota-pause-watchdog] Mark the start of a real flash job; the loop()
  // watchdog uses this to distinguish "active OTA" from "hung pause".
  g_otaLastProgressMs = millis();

  LOGI("OTA: pre-task heap free=%u  maxBlock=%u  dma_cap_free=%u  dma_cap_block=%u",
       (unsigned)ESP.getFreeHeap(),
       (unsigned)ESP.getMaxAllocHeap(),
       (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
        BaseType_t otaRc = xTaskCreatePinnedToCore([](void *arg){
          Job *j = (Job*)arg;
          
          // [ota-pause-settle] Was 250 ms. The otaPauseAudio flag stops
          // all four audio tasks within ~50 ms, but the WiFi driver TX
          // ring still holds up to 32 queued packets (~48 KB with 16
          // subscribers). lwIP must drain that backlog before the TCP
          // upload socket can accept the first 1 KB — otherwise the very
          // first raw.write() sees a zero window and returns 0/1024.
          // 1500 ms = four OPUS_QUEUE_LEN cycles + ring drain; the 202
          // is flushed in the first ~50 ms so the browser is unaffected.
          vTaskDelay(pdMS_TO_TICKS(1500));
          
          LOGI("OTA: Applying update. %u nodes, server=%d", (unsigned)j->nodes.size(), j->doServer);
          



          // ---- NEW: source board OTA over UART ------------------------
          if (j->doSource) {
            otaSetTarget("source");
            otaProgressPct = 0;
            g_otaLastProgressMs = millis();
            String err;
            LOGI("OTA: -> Starting source board (UART) ...");
            bool ok = otaPushToSource(j->sourceFile, err);
            if (!ok) {
              otaSetError("source: " + err);
              LOGE("OTA source FAILED: %s", err.c_str());
            } else {
              LOGW("OTA source SUCCESS (updated to %s)",
                   j->sourceFile.version.c_str());
            }
            if (otaMutex && xSemaphoreTake(otaMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
              if (ok) otaCompletedNodes.push_back("source");
              else    otaFailedNodes.push_back("source");
              xSemaphoreGive(otaMutex);
            }
            g_otaLastProgressMs = millis();
            vTaskDelay(pdMS_TO_TICKS(500));
          }





          for (auto &ip : j->nodes) {
            otaSetTarget("node " + ip);
            otaProgressPct = 0;
            g_otaLastProgressMs = millis();
            String err;
            LOGI("OTA: -> Starting node %s ...", ip.c_str());

            bool ok = otaPushToNode(ip, j->nodeFile, err);
            if (!ok) {
              otaSetError("node " + ip + ": " + err);
              LOGE("OTA node %s FAILED: %s", ip.c_str(), err.c_str());
            } else {
              LOGW("OTA node %s SUCCESS (updated to %s)",
                   ip.c_str(), j->nodeFile.version.c_str());
            }

            // [ota-per-node-progress] Record the outcome under otaMutex so
            // handleOtaStatus() can publish it as a completed/failed array.
            // The wizard uses this to paint ✔ / ✖ on the finished node the
            // instant it moves on to the next one — which is the entire
            // reason "Waiting…" no longer reverts on completed nodes.
            if (otaMutex && xSemaphoreTake(otaMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
              if (ok) otaCompletedNodes.push_back(ip);
              else    otaFailedNodes.push_back(ip);
              xSemaphoreGive(otaMutex);
            }

            g_otaLastProgressMs = millis();
            vTaskDelay(pdMS_TO_TICKS(500));
          }
          
          if (j->doServer) {
            otaSetTarget("server");
            otaProgressPct = 0;
            LOGI("OTA: -> Starting server flash ...");
            File f = LittleFS.open(j->serverFile.path, "r");


        bool serverOk = false;
        if (!f) {
          otaSetError("cannot open server bin");
        } else {
          size_t sz = f.size();
          if (!Update.begin(sz)) {
            otaSetError(String("Update.begin: ") + Update.errorString());
          } else {
            // Manual write loop instead of Update.writeStream(), so
            // otaProgressPct advances for the server path too. Without
            // this the dashboard's progress bar stayed at 0 % for the
            // entire flash even though Update.writeStream() was making
            // steady progress, which is what made a working update look
            // "stuck at Starting…".  1 KB per step is ~0.1 % per tick on
            // a 1 MB image, which is plenty of resolution for a bar.
            uint8_t wbuf[1024];
            size_t written = 0;
            bool   writeFail = false;
            while (f.available()) {
              size_t n = f.read(wbuf, sizeof(wbuf));
              if (n == 0) break;
              if (Update.write(wbuf, n) != n) {
                writeFail = true;
                break;
              }
              written += n;
              otaProgressPct = (int)((written * 100) / (sz ? sz : 1));
              // Yield so AsyncTCP can answer /api/ota/status polls while
              // the flash is being written. Without it a single
              // Update.write() burst of ~1 MB monopolises the CPU and
              // the browser's poll times out.
              vTaskDelay(1);
            }
            if (writeFail) {
              otaSetError(String("Update.write: ") + Update.errorString());
            } else if (written != sz) {
              otaSetError("short write " + String(written) + "/" + String(sz));
            } else if (!Update.end(true)) {
              otaSetError(String("Update.end: ") + Update.errorString());
            } else {
              serverOk = true;
            }
          }
          f.close();
        }
        if (serverOk) {
          LittleFS.remove(j->serverFile.path);
          LOGI("Server OTA complete, server bin cleared, rebooting...");
          delay(500);
          ESP.restart();
      }
    }
    delete j;
    // [ota-job-epilogue-mutex] CRIT-3: handleOtaStatus (Core 0) reads
    // otaInProgress / otaPauseAudio under otaMutex. A naked write here can
    // land mid-snapshot and produce a half-cleared state that leaves one
    // of /api/stats, /api/log/tail, /api/source/info gated forever.
    if (otaMutex && xSemaphoreTake(otaMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      otaInProgress = false;
      otaPauseAudio = false;
      xSemaphoreGive(otaMutex);
    } else {
      // Best-effort: single aligned byte stores are atomic on S3; the
      // mutex is only needed to prevent a mid-snapshot interleave, which
      // the lock-timeout path cannot guarantee but the flag values still
      // converge on next boot.
      otaInProgress = false;
      otaPauseAudio = false;
    }
    vTaskDelete(nullptr);
  }, "otaJob", OTA_JOB_STACK, job, 2, nullptr, 0);

  if (otaRc != pdPASS) {
    // Task never ran - unwind everything so the pipeline resumes, and
    // tell the browser we could not start. 500 rather than 202 so the
    // wizard's `if (!r.ok)` path fires and shows the reason.
    delete job;
    otaInProgress = false;
    otaPauseAudio = false;
    // [ota-target-reset] IMP-6: otaLastTarget was set to "node <ip>" (or
    // "server") *before* task creation. Without this reset, handleOtaStatus
    // reports target:"node <ip>" with inProgress:false — the operator sees
    // a phantom target for a job that never ran.
    otaSetError("otaJob task create failed (out of heap?)");
    otaSetTarget("");
    LOGE("OTA: xTaskCreatePinnedToCore failed rc=%d", (int)otaRc);
    request->send(500, "application/json",
                  "{\"ok\":false,\"error\":\"could not start OTA job (out of heap?)\"}");
    return;
  }

  // [ota-apply-guard-commit]
  // CRITICAL: disarm the abort guard BEFORE responding. The guard lives on
  // the stack and its destructor runs on function return. Without this
  // line, the destructor fires immediately after the 202 is queued and
  // clears otaInProgress + otaPauseAudio while the otaJob task is already
  // running (it wakes from its 1.5 s vTaskDelay with the flags reporting
  // "no OTA"). That makes /api/ota/status report inProgress=false during
  // the flash (wizard times out with "all failed"), disables the log-tail
  // and upload gates, and lets a second /api/ota/apply slip past the
  // _alreadyRunning check — a second otaJob then races Update.begin()
  // against the first on the active partition.
  //
  // Order matters: commit BEFORE send. request->send() is synchronous on
  // ESPAsyncWebServer for the non-stream form, but if a future change
  // makes it async a post-send commit would race the caller reading the
  // flags. Committing first leaves flags true from the moment the 202 is
  // queued until the otaJob task itself clears them at the end (or fails
  // to start, in which case the fail path clears them explicitly).
  _applyGuard.commit = true;

  // Task is running. Only now does the browser learn about it.
  request->send(202, "application/json", "{\"ok\":true,\"state\":\"running\"}");
}

// -- Plan --------------------------------------------------------------------
static void handleOtaPlan(AsyncWebServerRequest *request) {
  // [ota-mutex-single-tu] otaMutex is created in createSyncPrimitives()
  // (EVDHelpers.h) on the boot path, never here.
  //
  // [ota-plan-mutex-held-across-scan] Reverted from the short-lock variant.
  // Running otaScanSpool()/otaProbeNodes() outside the mutex causes their
  // LittleFS directory I/O + per-file String allocations to contend with an
  // in-flight /api/ota/upload chunk write on the same flash bus. On a
  // fragmented heap (a 1 MB .bin just absorbed, WiFi PBUF pool warm), the
  // scan's allocations fail and the WiFi ACK loop misses enough segments
  // that lwIP RSTs the upload socket — the browser reports
  // "Upload failed: network error" and the operator is pointed at WiFi
  // instead of at flash/heap contention. Holding the mutex across the
  // whole scan means a concurrent /api/ota/status poll blocks and returns
  // scalars-only, leaving the flash bus and heap to the upload. This is
  // the working behaviour; do not "optimise" it back.
  if (!otaMutex || xSemaphoreTake(otaMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    request->send(503, "application/json", "{\"ok\":false,\"error\":\"busy\"}");
    return;
  }
  otaScanSpool();
  otaProbeNodes();
  String   lastTargetCopy = otaLastTarget;
  String   lastErrorCopy  = otaLastError;
  uint32_t progressCopy   = otaProgressPct;
  bool     inProgressCopy = otaInProgress;
  xSemaphoreGive(otaMutex);

  // ---------------------------------------------------------------------
  // Build the entire JSON body in a heap String and hand it to
  // request->send() as a single body. Do NOT use AsyncResponseStream here.
  // (See the previous revision's rationale — per-chunk allocs can be
  // dropped on a loaded S3, truncating the body and making the browser's
  // r.json() throw.)
  //
  // [ota-plan-comma-fix]
  // The previous revision emitted a double comma at two points:
  //     ..."serverVersion":"3.5.6",,"inProgress":...
  //     ..."lastTarget":"...",,"lastError":...
  // because the value-append lines ended with `",` (trailing comma) while
  // the following key-append lines started with `,"`. The result is
  // syntactically invalid JSON: the browser's refreshPlan() sees a 200
  // response, its r.json() throws, and it falls through to the catch
  // path that sets the version label to "Offline". That, in turn,
  // leaves `plan` null so render() bails early and neither the spooled
  // bin list nor the player-node list ever populates on the Update tab.
  //
  // Fix: every key-append starts with `,` and every value-append ends
  // with a bare `"` — the same shape used by handleNetworkState() in
  // EVDWiFi.h. Do NOT reintroduce a trailing `,` on any value line.
  // ---------------------------------------------------------------------
  String body;
  body.reserve(1024);
  body  = "{\"ok\":true";
  body += ",\"serverVersion\":\""; body += EVD_FW_VERSION; body += "\"";
  body += ",\"inProgress\":";      body += (inProgressCopy ? "true" : "false");
  body += ",\"progressPct\":";     body += String(progressCopy);
  body += ",\"lastTarget\":\"";    body += jsonEscape(lastTargetCopy); body += "\"";
  body += ",\"lastError\":\"";     body += jsonEscape(lastErrorCopy);  body += "\"";
  
  // NEW: Add filesystem capacity and ALL spool files for diagnostic UI
  body += ",\"totalBytes\":";      body += String((unsigned)LittleFS.totalBytes());
  body += ",\"usedBytes\":";       body += String((unsigned)LittleFS.usedBytes());
  body += ",\"allSpoolFiles\":[";
  for (size_t i = 0; i < otaAllSpoolFiles.size(); ++i) {
    if (i) body += ",";
    body += "{\"name\":\""; body += jsonEscape(otaAllSpoolFiles[i].name); body += "\",";
    body += "\"size\":";    body += String((unsigned)otaAllSpoolFiles[i].size); body += "}";
  }
  body += "],\"files\":["; 
  for (size_t i = 0; i < otaSpool.size(); ++i) {
    if (i) body += ",";
    body += "{\"name\":\"";    body += jsonEscape(otaSpool[i].name);    body += "\",";
    body += "\"kind\":\"";     body += otaSpool[i].kind;                 body += "\",";
    body += "\"version\":\"";  body += jsonEscape(otaSpool[i].version);  body += "\",";
    body += "\"size\":";       body += String((unsigned)otaSpool[i].size);
    body += "}";
  }
  body += "],\"nodes\":[";
  for (size_t i = 0; i < otaNodeVersions.size(); ++i) {
    if (i) body += ",";
    body += "{\"ip\":\"";       body += jsonEscape(otaNodeVersions[i].ip);      body += "\",";
    body += "\"name\":\"";      body += jsonEscape(otaNodeVersions[i].name);    body += "\",";
    body += "\"version\":\"";   body += jsonEscape(otaNodeVersions[i].version); body += "\",";
    body += "\"reachable\":";   body += (otaNodeVersions[i].reachable ? "true" : "false");
    body += "}";
  }
  body += "]}";

  AsyncWebServerResponse *resp =
      request->beginResponse(200, "application/json", body);
  resp->addHeader("Cache-Control", "no-store, max-age=0");
  request->send(resp);
}

// -- Status ------------------------------------------------------------------
static void handleOtaStatus(AsyncWebServerRequest *request) {
  bool lockHeld = (otaMutex &&
                   xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE);

  // [ota-status-read-only] handleOtaStatus is a pure reader now. It does
  // NOT clear otaInProgress or otaPauseAudio — the otaJob task may still
  // be inside raw.write() to a dead node, and clearing otaInProgress would
  // let a second /api/ota/apply start a competing job that races Update.write()
  // on the same partition. The ONLY stall watchdog is otaCheckStuckJob()
  // (loop(), 120 s): it releases otaPauseAudio so audio resumes. Recovery
  // from a truly stuck otaJob requires an explicit reboot.
  //
  // The stall is still REPORTED here (so the wizard can show a banner),
  // via the "stalled" field below. Nothing in this function mutates state.

  String body;
  body.reserve(384);
  const uint32_t _nowMs = millis();
  const bool _stalled = otaInProgress &&
                        g_otaLastProgressMs > 0 &&
                        (_nowMs - g_otaLastProgressMs) > 120000UL;
  body  = "{\"ok\":true,";
  body += "\"inProgress\":";   body += (otaInProgress ? "true" : "false");
  body += ",\"stalled\":";     body += (_stalled ? "true" : "false");
  body += ",\"stalledAgeMs\":"; body += (_stalled
                                         ? String((unsigned long)(_nowMs - g_otaLastProgressMs))
                                         : String("0"));
  body += ",\"progressPct\":"; body += String(otaProgressPct);
  // [ota-status-snapshot-cache]
  // If we could not take otaMutex within 100 ms (a concurrent
  // /api/ota/plan is holding it during its LittleFS spool scan), return
  // the last successful snapshot with stale=true so the wizard does NOT
  // revert its ✔ / ✖ marks. The cache is process-global static, written
  // only while otaMutex is held, so it is coherent with the otaJob
  // task's writes.
  // [ota-status-cache-single-worker] Static cache is safe ONLY because the
  // default ESPAsyncWebServer is single-worker. If CONFIG_ASYNC_TCP_...
  // is ever changed to >1 worker, guard these with otaMutex or replace the
  // String members with fixed char[N] + portMUX.
  static String     s_cachedTarget;
  static String     s_cachedError;
  static String     s_cachedCompletedJson;   // pre-rendered JSON array
  static String     s_cachedFailedJson;
  static bool       s_cacheValid = false;

  if (lockHeld) {
    String tgt = otaLastTarget;
    String err = otaLastError;
    String cmpl = "[";
    for (size_t i = 0; i < otaCompletedNodes.size(); ++i) {
      if (i) cmpl += ",";
      cmpl += "\"";
      cmpl += jsonEscape(otaCompletedNodes[i]);
      cmpl += "\"";
    }
    cmpl += "]";
    String fail = "[";
    for (size_t i = 0; i < otaFailedNodes.size(); ++i) {
      if (i) fail += ",";
      fail += "\"";
      fail += jsonEscape(otaFailedNodes[i]);
      fail += "\"";
    }
    fail += "]";
    xSemaphoreGive(otaMutex);
    // Cache the snapshot so a subsequent contention poll can serve it.
    s_cachedTarget        = tgt;
    s_cachedError         = err;
    s_cachedCompletedJson = cmpl;
    s_cachedFailedJson    = fail;
    s_cacheValid          = true;

    body += ",\"target\":\"";   body += jsonEscape(tgt); body += "\"";
    body += ",\"error\":\"";    body += jsonEscape(err); body += "\"";
    body += ",\"completed\":";  body += cmpl;
    body += ",\"failed\":";     body += fail;
  } else if (s_cacheValid) {
    body += ",\"target\":\"";   body += jsonEscape(s_cachedTarget); body += "\"";
    body += ",\"error\":\"";    body += jsonEscape(s_cachedError);  body += "\"";
    body += ",\"completed\":";  body += s_cachedCompletedJson;
    body += ",\"failed\":";     body += s_cachedFailedJson;
    body += ",\"stale\":true";
  } else {
    body += ",\"target\":\"\",\"error\":\"\",\"completed\":[],\"failed\":[]";
  }
  body += "}";
  request->send(200, "application/json", body);
}

// [ota-stuck-job-loop-watchdog]
// handleOtaStatus already force-clears a stalled job, but only while the
// dashboard wizard is polling /api/ota/status. If the browser tab was
// closed mid-update (or the network dropped), a job that never returns
// leaves otaInProgress == true forever, which 503s /api/log/tail,
// /api/source/info, /api/ota/plan and /api/ota/apply until reboot.
// loop() calls this so the lockout self-heals within ~2 min.
// [ota-stuck-job-loop-watchdog]
// Clears ONLY otaPauseAudio. otaInProgress is deliberately left set:
// the otaJob task may be parked inside raw.write() to a dead node and
// will eventually resume (TCP RTO) and finish. Clearing otaInProgress
// would allow a second /api/ota/apply to start a competing task that
// races Update.write() / Update.begin() on the same partition — that
// can brick the server. Audio resumes; the operator must reboot to
// start a fresh OTA (wizard shows the stall error in the log).

// [ota-upload-statics-hoist] otaCheckStuckJob() below references these to
// reclaim a stalled upload. They used to be defined AFTER this function
// (near handleOtaUploadFinal), which caused "'otaUploadFile' was not
// declared in this scope" at compile time. Hoisted here; the original
// definition sites further down are deleted to avoid re-definition.
static File                   otaUploadFile;
static String                 otaUploadName;
static AsyncWebServerRequest *otaUploadOwnerReq = nullptr;

static inline void otaCheckStuckJob() {
  // [ota-stuck-watchdog-covers-uploads] The check must NOT require
  // otaInProgress — a stuck UPLOAD sets otaPauseAudio but leaves
  // otaInProgress false. Without this extension, a backgrounded tab or a
  // network cut that never fires onDisconnect parks the audio pipeline
  // forever. Behaviour branches on which job class stalled:
  //
  //   flash (otaInProgress):  audio released ONLY. Clearing otaInProgress
  //                           would let a second Update.begin() race
  //                           Update.write() on the active partition —
  //                           brick risk. Reboot required for recovery.
  //
  //   upload (otaUploadInProgress): clear BOTH. The chunk handler may
  //                           still be parked inside otaUploadFile.write();
  //                           any arriving chunks continue writing to that
  //                           file (the eventual onDisconnect cleans it up).
  //                           The next upload's index-0 handler closes the
  //                           old handle and opens a fresh one. Safe.
  //
  //   pause without a job:    defensive fallback — just release audio.
  if (!otaPauseAudio || g_otaLastProgressMs == 0) return;
  if ((millis() - g_otaLastProgressMs) <= 120000UL) return;
  if (!otaMutex) return;
  if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

  if (otaPauseAudio && otaInProgress) {
    otaPauseAudio = false;
    if (otaLastError.length() == 0) {
      otaLastError = "OTA flash stalled (no progress for 120s); "
                     "audio resumed — REBOOT server to clear stuck job";
    }
    LOGE("OTA flash stall: audio released, otaInProgress held. "
         "REBOOT required. target=%s", otaLastTarget.c_str());
  } else if (otaPauseAudio && otaUploadInProgress) {
    otaUploadInProgress = false;
    otaPauseAudio       = false;
    // [ota-upload-stall-reclaim] Do NOT rely on onDisconnect. If the peer
    // vanished (power-cut laptop, network partition) ESPAsyncWebServer
    // may never fire it, and the handle + partial spool file sit open
    // until the NEXT upload's index==0 closes them — every subsequent
    // delete/plan/list call sees a phantom spool entry and can be told
    // "file exists" for a write that will silently fail because another
    // File handle has the same path open on LittleFS. Close and remove
    // here so recovery is deterministic.
    if (otaUploadFile) otaUploadFile.close();
    if (otaUploadName.length() > 0) {
      char stalledPath[96];
      snprintf(stalledPath, sizeof(stalledPath),
               "/ota_spool/%s", otaUploadName.c_str());
      LittleFS.remove(stalledPath);
      otaUploadName = "";
    }
    otaUploadOwnerReq = nullptr;
    LOGE("OTA upload stall: audio released, otaUploadInProgress cleared, "
         "stale spool file reclaimed.");
  } else if (otaPauseAudio) {
    otaPauseAudio = false;
    LOGW("OTA pause released (120s silence, no active job flag)");
  }

  xSemaphoreGive(otaMutex);
}

// -- Delete spooled file -----------------------------------------------------
// [ota-delete-chunk-accumulator] Same 3-arg chunk form as handleOtaApply().
// Body is tiny (<128 B) and never fragments in practice, but the pattern
// matches /api/ota/apply so a fragmented body does not parse garbage.
static char   otaDeleteBody[192];
static size_t otaDeleteBodyLen = 0;

static void handleOtaDelete(AsyncWebServerRequest *request, uint8_t *data,
                            size_t len, size_t index, size_t total) {
  if (index == 0) otaDeleteBodyLen = 0;
  if (otaDeleteBodyLen + len > sizeof(otaDeleteBody)) {
    otaDeleteBodyLen = 0;
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"delete body too large\"}");
    return;
  }
  if (data && len > 0) {
    memcpy(otaDeleteBody + otaDeleteBodyLen, data, len);
    otaDeleteBodyLen += len;
  }
  if (index + len != total) return;
  size_t bodyLen = otaDeleteBodyLen;
  otaDeleteBodyLen = 0;

  // [ota-delete-mutex-guard] Refuse while a flash or upload is running:
  // the running job holds the file open, and LittleFS.remove() while a
  // File handle is active either fails or corrupts metadata. The wizard
  // already blocks the button visually, but a stale browser tab could
  // POST anyway — reject server-side too.
  if (otaInProgress || otaUploadInProgress) {
    request->send(409, "application/json",
                  "{\"ok\":false,\"error\":\"update/upload in progress\"}");
    return;
  }

  DynamicJsonDocument in(192);
  if (deserializeJson(in, otaDeleteBody, bodyLen)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }

  String name = in["name"] | "";
  // [ota-delete-sanitize] Only accept a bare filename. Reject path
  // separators and ".." so a crafted POST cannot escape /ota_spool.
  // [ota-delete-length-guard] The path[] buffer below is 96 bytes and the
  // "/ota_spool/" prefix alone is 11, so any name >84 chars truncates the
  // snprintf output and the delete targets the wrong (or a non-existent)
  // path. Real firmware names are ~16 chars; reject anything over 80 to
  // leave headroom for future prefix growth.
  if (!name.length() ||
      name.length() > 80 ||
      name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 ||
      name.indexOf("..") >= 0) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"invalid filename\"}");
    return;
  }

  if (!ensureOtaFs()) {
    request->send(500, "application/json",
                  "{\"ok\":false,\"error\":\"filesystem unavailable\"}");
    return;
  }

  char path[96];
  snprintf(path, sizeof(path), "/ota_spool/%s", name.c_str());
  if (!LittleFS.exists(path)) {
    request->send(404, "application/json",
                  "{\"ok\":false,\"error\":\"file not found\"}");
    return;
  }
  if (!LittleFS.remove(path)) {
    request->send(500, "application/json",
                  "{\"ok\":false,\"error\":\"delete failed\"}");
    return;
  }
  LOGI("OTA: deleted spool file %s", path);
  request->send(200, "application/json", "{\"ok\":true}");
}
// [upload-pause-audio]
// Set for the whole duration of an upload, from the first chunk to the
// final one. The audio tasks (audioRead/audioProc/udpSend) block the
// same Core 1 that AsyncTCP runs on, and the flash write on every
// multipart fragment blocks the SPI bus. Running both at once produces
// the mid-upload TCP reset that shows up in the browser as
// "Upload failed: network error". Pausing the audio tasks while the
// upload is in flight removes the CPU contention; the flash-write
// contention is handled by the yield below.
//
// [ota-upload-flag-hoist] The otaUploadInProgress definition has been
// hoisted to the top of this file (see the note there) because
// handleOtaApply() reads the flag before this block appears in the
// translation unit. Do NOT re-declare it here with the initializer: a
// second `static volatile bool otaUploadInProgress = false;` would be a
// redefinition error. The definition above is the only one.

// [ota-upload-error-state] Declared here so both the Final and the Chunk
// handlers, plus the onDisconnect lambda, can see it. Must be declared
// BEFORE handleOtaUploadFinal()/handleOtaUploadChunk() (which reference it).
static volatile bool  otaUploadHadError  = false;
static String         otaUploadErrorMsg;


// [ota-upload-owner] Tracks the request that actually owns otaUploadFile.
// A second/third concurrent upload arrives while otaUploadInProgress is
// true; its Chunk handler returns without setting the owner, so its Final
// handler sees owner mismatch and 409s. Prevents a rejected Final from
// closing the active uploader's file (silent truncation) and returning
// 200 for a body it never accepted.
static void handleOtaUploadFinal(AsyncWebServerRequest *request) {
  // [ota-upload-final-mutex] CRIT-2: otaCheckStuckJob() (Core 1, loop) also
  // closes otaUploadFile / removes the spool path under otaMutex. Every
  // upload-side close must take the SAME lock, or File::close() can be
  // invoked twice on the same File handle (undefined on some LittleFS
  // builds) or a write can land on a closed handle.
  //
  // [ota-upload-final-must-bail-if-locked] This is the LAST step of the
  // upload. It is fine to wait a full second for the mutex here (worst
  // case: a concurrent /api/ota/plan scan). What is NOT fine is falling
  // through to the cleanup with lockOk == false — that runs close() and
  // LittleFS.remove() unprotected and reintroduces the exact race this
  // fix exists to eliminate. Bail and let onDisconnect / otaCheckStuckJob
  // clean up.
  if (!otaMutex) {
    otaUploadOwnerReq = nullptr;   // atomic pointer store, safe
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"ota mutex unavailable\"}");
    return;
  }
  if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    request->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"ota state lock busy; retry\"}");
    return;
  }
  if (otaUploadOwnerReq != request) {
    xSemaphoreGive(otaMutex);
    request->send(409, "application/json",
                  "{\"ok\":false,\"error\":\"another OTA upload is already in progress\"}");
    return;
  }
  if (otaUploadFile) otaUploadFile.close();
  otaUploadOwnerReq   = nullptr;
  otaUploadInProgress = false;
  otaPauseAudio       = false;
  // [IMP-2] Clear the name too so a stale /ota_spool/<name> path is not
  // resurrected by the next handleOtaUploadChunk index==0 open.
  otaUploadName       = "";
  xSemaphoreGive(otaMutex);

  if (otaUploadHadError) {
    String msg = String("{\"ok\":false,\"error\":\"") +
                 jsonEscape(otaUploadErrorMsg) + "\"}";
    otaUploadHadError = false;
    otaUploadErrorMsg = "";
    request->send(500, "application/json", msg);
    return;
  }
  otaUploadErrorMsg = "";
  request->send(200, "application/json", "{\"ok\":true}");
}

static void handleOtaUploadChunk(AsyncWebServerRequest *request, String filename,
                                 size_t index, uint8_t *data, size_t len, bool final) {
  if (!filename.length()) return;

  // [ota-upload-foreign-chunk-guard]
  // CRITICAL: discard every chunk from a request that never became
  // otaUploadOwnerReq. Without this, a second concurrent upload's bytes
  // reach otaUploadFile.write() (which is still the FIRST upload's open
  // handle, with a non-empty otaUploadName), silently corrupting the
  // spool file being written by the first upload. The Final handler's
  // owner-mismatch check runs too late — by then the damage is done.
  //
  // On a legitimate request: index 0 sets otaUploadOwnerReq = this
  // request BEFORE any write, so every subsequent chunk matches.
  // On a rejected request: index 0 hit the otaUploadInProgress guard
  // below and returned WITHOUT setting the owner, so every subsequent
  // chunk fails this check and is dropped.
  if (index != 0 && otaUploadOwnerReq != request) {
    return;
  }

  g_otaLastProgressMs = millis();

  static size_t lastLogBytes = 0;

  if (index == 0) {
    lastLogBytes = 0;

    if (otaUploadInProgress) {
      LOGW("OTA upload: rejecting '%s' — '%s' already in progress",
           filename.c_str(), otaUploadName.c_str());
      request->send(409, "application/json",
                    "{\"ok\":false,\"error\":\"upload already in progress\"}");
      return;
    }

    if (otaInProgress) {
      // [upload-rejected-no-state-touch] Send 409 and return WITHOUT setting
      // otaUploadOwnerReq, otaUploadHadError, or registering an onDisconnect.
      // Previously this request's Final handler (server always calls the
      // onRequest lambda after the body) would see otaUploadOwnerReq==this,
      // then close otaUploadFile and clear otaUploadInProgress/otaPauseAudio
      // — which on a flash-in-progress path means:
      //   * otaUploadFile is nullptr (flash, not upload) → close skipped;
      //   * otaUploadInProgress is false → no-op;
      //   * otaPauseAudio is TRUE (set by handleOtaApply) → un-paused
      //     audio while Update.write() is running. Silent flash-cache
      //     contention, exactly what the pause exists to prevent.
      // After this fix, the Final handler sees owner mismatch, sends a
      // second 409 which ESPAsyncWebServer discards because the socket is
      // already closed by the first send. No global state touched.
      LOGW("OTA upload: rejecting '%s' — flash already in progress", filename.c_str());
      request->send(409, "application/json",
                    "{\"ok\":false,\"error\":\"flash already in progress\"}");
      return;
    }

    {
      uint32_t maxBlk = (uint32_t)ESP.getMaxAllocHeap();
      uint32_t freeH  = (uint32_t)ESP.getFreeHeap();
      LOGI("OTA upload precheck: heap free=%u maxBlk=%u", freeH, maxBlk);
      if (maxBlk < 4096) {
        otaUploadOwnerReq = request;
        otaUploadHadError = true;
        otaUploadErrorMsg = String("heap too fragmented (") + String(maxBlk) + " B max block, need 4096)";
        AsyncWebServerRequest *thisReq = request;
        request->onDisconnect([thisReq]() {
          if (otaUploadOwnerReq == thisReq) {
            otaUploadOwnerReq = nullptr;
            otaUploadHadError = false;
            otaUploadErrorMsg = "";
          }
        });
        return;
      }
    }

    otaUploadInProgress = true;
    otaPauseAudio       = true;
    otaUploadOwnerReq   = request;
    otaUploadHadError   = false;
    otaUploadErrorMsg   = "";
    if (otaUploadFile) otaUploadFile.close();

    // [ota-disconnect-mutex] CRIT-2: otaCheckStuckJob() also closes the file
    // and removes the spool path under otaMutex. Both paths now serialise.
    // The LittleFS.remove is intentionally INSIDE the mutex so a subsequent
    // upload cannot recreate the same filename between close() and remove()
    // and have it silently deleted by our stale cleanup.
    //
    // [ota-disconnect-must-bail-if-locked] Same rule as Final: if we cannot
    // get the mutex, DO NOT touch the file or the flags. Leave the cleanup
    // to otaCheckStuckJob (fires at 120 s) — the safe behaviour is worse
    // UX (one stuck upload until reboot), not a corrupted spool.
    request->onDisconnect([request]() {
      if (!otaMutex) return;
      if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        LOGW("OTA upload onDisconnect: mutex busy; deferring cleanup to otaCheckStuckJob");
        return;
      }
      if (otaUploadOwnerReq != request) {
        xSemaphoreGive(otaMutex);
        return;
      }
      if (otaUploadFile) otaUploadFile.close();
      if (otaUploadName.length() > 0) {
        char badPath[96];
        snprintf(badPath, sizeof(badPath), "/ota_spool/%s", otaUploadName.c_str());
        LittleFS.remove(badPath);
      }
      otaUploadName       = "";
      otaUploadOwnerReq   = nullptr;
      otaUploadInProgress = false;
      otaPauseAudio       = false;
      xSemaphoreGive(otaMutex);
      LOGW("OTA upload: client disconnected; audio pipeline resumed");
    });

    // [upload-filename-length-guard] The snprintf path buffers below are
    // 96 bytes and "/ota_spool/" + NUL is 12, so the name must fit in
    // ≤ 80 chars. Real firmware names are ~16 (server_3.7.5.8.bin). A
    // longer name truncates, creates an orphan file at a different path
    // than the subsequent remove/delete handlers construct, and the
    // operator sees a spool entry they cannot delete.
    // [upload-path-traversal] Reject the same shapes handleOtaDelete()
    // already rejects. filename comes straight from Content-Disposition;
    // without this, "../" escapes /ota_spool on builds where the VFS
    // does not harden traversal.
    if (filename.length() > 80 ||
        filename.indexOf('/') >= 0 || filename.indexOf('\\') >= 0 ||
        filename.indexOf("..") >= 0) {
      otaUploadHadError = true;
      otaUploadErrorMsg = String("invalid filename: ") + filename;
      return;
    }
    OtaFile probe;
    if (!parseFwName(filename, probe)) {
      otaUploadHadError = true;
      otaUploadErrorMsg = String("invalid firmware filename: ") + filename;
      return;
    }

    if (!ensureOtaFs()) {
      otaUploadHadError = true;
      otaUploadErrorMsg = String("filesystem unavailable, cannot store ") + filename;
      return;
    }

    LittleFS.mkdir("/ota_spool");

    // [ota-spool-keep-all] Do NOT wipe existing files. Rolling back to an
    // older bin requires keeping more than one file in the spool (the
    // dashboard's radio-select lists every valid bin). Only remove a file
    // whose exact name is being overwritten — same name = re-upload of the
    // same version, safe to replace.
    char sameNamePath[96];
    snprintf(sameNamePath, sizeof(sameNamePath),
             "/ota_spool/%s", filename.c_str());
    if (LittleFS.exists(sameNamePath)) {
      LittleFS.remove(sameNamePath);
    }

    size_t totalSpace = LittleFS.totalBytes();
    size_t usedSpace  = LittleFS.usedBytes();
    if (totalSpace - usedSpace < 2 * 1024 * 1024) {
      LOGE("OTA upload: LittleFS full (%u free, need 2MB)",
           (unsigned)(totalSpace - usedSpace));
      otaUploadHadError = true;
      otaUploadErrorMsg = "LittleFS full; free space and retry";
      return;
    }

    otaUploadName = filename;
    // FIX: Avoid String allocation entirely to reduce heap fragmentation
    char pathBuf[96];
    snprintf(pathBuf, sizeof(pathBuf), "/ota_spool/%s", otaUploadName.c_str());
    
    otaUploadFile = LittleFS.open(pathBuf, "w");
    if (!otaUploadFile) {
      LOGE("OTA upload: cannot open %s", pathBuf);
      otaUploadHadError = true;
      otaUploadErrorMsg = String("cannot open spool file ") + String(pathBuf);
      return;
    }

    LOGI("OTA upload: receiving %s", filename.c_str());
  }

  if (otaUploadHadError || !otaUploadFile || otaUploadName.length() == 0) {
    if (final && otaUploadFile) {
      otaUploadFile.close();
      otaUploadName = "";
    }
    return;
  }

  if (len > 0) {
    size_t wrote = otaUploadFile.write(data, len);
    if (wrote != len) {
      LOGE("OTA upload: short write %u/%u on %s",
           (unsigned)wrote, (unsigned)len, otaUploadName.c_str());
      otaUploadHadError = true;
      otaUploadErrorMsg = String("short write ") + String((unsigned)wrote) +
                          "/" + String((unsigned)len);
      // [ota-chunk-error-mutex] CRIT-2: serialise the close/remove against
      // otaCheckStuckJob's identical sequence.
      bool lockOk = (otaMutex &&
                     xSemaphoreTake(otaMutex, pdMS_TO_TICKS(200)) == pdTRUE);
      if (otaUploadFile) otaUploadFile.close();
      if (otaUploadName.length() > 0) {
        char badPath[96];
        snprintf(badPath, sizeof(badPath), "/ota_spool/%s", otaUploadName.c_str());
        LittleFS.remove(badPath);
      }
      otaUploadName = "";
      if (lockOk) xSemaphoreGive(otaMutex);
      return;
    }

    // FIX: Yield 5ms. When LittleFS blocks the flash cache, AsyncTCP cannot ACK packets.
    // 1ms is not enough to drain the 45KB TCP window during a block write.
    vTaskDelay(pdMS_TO_TICKS(5));

    if (lastLogBytes == 0 || (index - lastLogBytes) >= (64 * 1024)) {
      lastLogBytes = index;
      LOGI("OTA upload: %s progress %u bytes",
           otaUploadName.c_str(), (unsigned)index);
    }
  }

  if (final && otaUploadFile) {
    otaUploadFile.close();
    LOGI("OTA upload: stored %s", otaUploadName.c_str());
    otaUploadName = "";
  }
}