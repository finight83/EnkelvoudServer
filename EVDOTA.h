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
static std::vector<OtaNodeVersion> otaNodeVersions;
static SemaphoreHandle_t           otaMutex = nullptr;
static volatile bool               otaInProgress = false;
static volatile int                otaProgressPct = 0;
static String                      otaLastError;
static String                      otaLastTarget;
static volatile unsigned long otaJobStartedMs = 0;

extern volatile bool otaPauseAudio;

// -- Helpers -----------------------------------------------------------------
static bool parseFwName(const String &name, OtaFile &out) {
  int us = name.indexOf('_');
  int dot = name.lastIndexOf(".bin");
  if (us <= 0 || dot <= us) return false;
  String kind = name.substring(0, us);
  kind.toLowerCase();
  if (kind != "server" && kind != "node") return false;
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

static bool ensureOtaFs() {
  if (otaFsReady)     return true;
  if (otaFsInitTried) return false;   // already failed once; do not retry
  otaFsInitTried = true;

  if (!LittleFS.begin(false)) {       // no format-on-fail
    LOGW("OTA: LittleFS not available (no spiffs partition?)");
    return false;
  }
  otaFsReady = true;
  LOGI("OTA: LittleFS mounted");
  return true;
}

static void otaScanSpool() {
  otaSpool.clear();
  if (!ensureOtaFs()) return;

  File root = LittleFS.open("/ota_spool");
  if (!root || !root.isDirectory()) return;

  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String full = String("/ota_spool/") + String(f.name());
      String base = String(f.name());
      int slash = base.lastIndexOf('/');
      if (slash >= 0) base = base.substring(slash + 1);
      OtaFile of;
      if (parseFwName(base, of)) {
        of.size = f.size();
        of.path = full;
        otaSpoolAdd(of);
      }
    }
    f = root.openNextFile();
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

  WiFiClient raw;
  // [ota-preflight] Connect with a short explicit timeout so a black-holed
  // node cannot stall the OTA job.
  if (!raw.connect(ip.c_str(), 80, 4000)) {
    f.close();
    // Secondary probe: is the node answering ARP at all?
    IPAddress probe;
    bool ipOk = probe.fromString(ip);
    bool sameSubnet = ipOk &&
      ((uint32_t)probe & (uint32_t)WiFi.subnetMask()) ==
      ((uint32_t)WiFi.localIP() & (uint32_t)WiFi.subnetMask());
    if (!ipOk) {
      errOut = "invalid ip";
    } else if (!sameSubnet) {
      errOut = "node on different subnet than server";
    } else {
      errOut = "tcp connect timed out (node offline, rebooting, or AP isolates clients)";
    }
    return false;
  }

  // [ota-client-timeout]
  // MUST be called AFTER connect. WiFiClient::setTimeout() only touches
  // SO_RCVTIMEO and SO_SNDTIMEO when fd() >= 0; called before connect it
  // sets the Stream-level timeout and nothing else. The socket then runs
  // with LwIP's defaults, which are infinite send/receive timeouts. The
  // write loop completes but the very next call (raw.print(tail)) blocks
  // forever in send() if the node's receive window is closed while it is
  // busy flushing its own flash on Update.end(). That is the exact stall
  // that leaves the dashboard on "Updating node X — 100%" indefinitely.
  //
  // 4 s is generous: on a healthy LAN the node's reply comes back in
  // well under 100 ms. Anything longer than this is a stuck peer and we
  // want the job to move on, mark the node as failed, and let the
  // browser close the wizard.
  raw.setTimeout(4);

  String hdr = "POST /api/ota HTTP/1.1\r\n"
               "Host: " + ip + "\r\n"
               "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n"
               "Content-Length: " + String(contentLen) + "\r\n"
               "Connection: close\r\n\r\n" + head;
  raw.print(hdr);

  uint8_t buf[1024];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    raw.write(buf, n);
    otaProgressPct = (int)((f.position() * 100) / (total ? total : 1));
  }
  raw.print(tail);

  unsigned long t0 = millis();
  while (raw.connected() && millis() - t0 < 8000 && !raw.available()) delay(20);
  String resp = raw.readString();
  f.close();

  bool ok = resp.indexOf("\"ok\":true") >= 0;
  if (!ok) errOut = "node replied: " + resp.substring(0, 120);
  raw.stop();
  return ok;}

// -- Apply -------------------------------------------------------------------
static void handleOtaApply(AsyncWebServerRequest *request, uint8_t *data,
                           size_t len, size_t index, size_t total) {
  (void)index; (void)total;

  LOGW("OTA: /api/ota/apply entered (len=%u total=%u inProgress=%d heap=%u maxBlk=%u)",
       (unsigned)len, (unsigned)total, (int)otaInProgress,
       (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  if (otaInProgress) {
    request->send(409, "application/json", "{\"ok\":false,\"error\":\"update already running\"}");
    return;
  }
  DynamicJsonDocument in(512);
  if (deserializeJson(in, data, len)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }

  bool doServer = in["server"] | false;
  std::vector<String> doNodes;
  if (in["nodes"].is<JsonArray>()) {
    for (JsonVariant v : in["nodes"].as<JsonArray>()) doNodes.push_back(v.as<String>());
  }

  OtaFile serverFile; bool haveServerFile = false;
  OtaFile nodeFile;   bool haveNodeFile   = false;
  for (auto &f : otaSpool) {
    if (f.kind == "server" && (!haveServerFile || cmpVersion(f.version, serverFile.version) > 0)) {
      serverFile = f; haveServerFile = true;
    }
    if (f.kind == "node" && (!haveNodeFile || cmpVersion(f.version, nodeFile.version) > 0)) {
      nodeFile = f; haveNodeFile = true;
    }
  }
  if (doServer && !haveServerFile) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"no server bin in spool\"}");
    return;
  }
  if (!doNodes.empty() && !haveNodeFile) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"no node bin in spool\"}");
    return;
  }

  otaInProgress = true;
  otaPauseAudio = true;
  otaLastError = "";
  otaProgressPct = 0;
  otaJobStartedMs = millis();

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
    bool doServer;
  };
  Job *job = new (std::nothrow) Job();
  if (!job) {
    otaInProgress = false;
    otaPauseAudio = false;
    otaLastError  = "out of heap allocating OTA job";
    request->send(500, "application/json",
                  "{\"ok\":false,\"error\":\"out of heap allocating OTA job\"}");
    return;
  }
  job->nodes = doNodes;
  job->nodeFile = nodeFile; job->haveNodeFile = haveNodeFile;
  job->serverFile = serverFile; job->haveServerFile = haveServerFile;
  job->doServer = doServer;

  // [ota-job-stack]
  // Was 8192. On an S3 that has just absorbed a 1 MB multipart upload and is
  // still holding the WiFi PBUF pool (CONFIG_LWIP_PBUF_POOL_SIZE=256) plus
  // four audio tasks, an 8 KB contiguous internal-DRAM allocation can fail
  // even with tens of KB free overall. The job body does:
  //   * one 1024-byte read buffer in otaPushToNode
  //   * a handful of short String concatenations per node
  //   * for the server path, Update.writeStream() with its own tiny buffers
  // 4096 is comfortably above the observed high-water mark and is small
  // enough to fit through a fragmented heap.
  constexpr uint32_t OTA_JOB_STACK = 4096;

  LOGI("OTA: pre-task heap free=%u  maxBlock=%u  dma_cap_free=%u  dma_cap_block=%u",
       (unsigned)ESP.getFreeHeap(),
       (unsigned)ESP.getMaxAllocHeap(),
       (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));

  BaseType_t otaRc = xTaskCreatePinnedToCore([](void *arg){
    Job *j = (Job*)arg;
    
    // [ota-job-delay] Yield to the AsyncTCP task so the 202 Accepted response
    // is fully flushed to the client before flash operations begin blocking
    // Core 0. Without this, an immediate Update.writeStream() can starve the
    // WiFi task and the browser hangs at "Starting..." until the 30s timeout.
    vTaskDelay(pdMS_TO_TICKS(250));
    
    for (auto &ip : j->nodes) {
      otaLastTarget = "node " + ip;
      String err;
      if (!otaPushToNode(ip, j->nodeFile, err)) {
        otaLastError = "node " + ip + ": " + err;
        LOGE("OTA node %s failed: %s", ip.c_str(), err.c_str());
      } else {
        LOGW("OTA node %s updated to %s", ip.c_str(), j->nodeFile.version.c_str());
      }
    }
      if (j->doServer) {
        otaLastTarget = "server";
        File f = LittleFS.open(j->serverFile.path, "r");
        bool serverOk = false;
        if (!f) {
          otaLastError = "cannot open server bin";
        } else {
          size_t sz = f.size();
          if (!Update.begin(sz)) {
            otaLastError = String("Update.begin: ") + Update.errorString();
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
              otaLastError = String("Update.write: ") + Update.errorString();
            } else if (written != sz) {
              otaLastError = "short write " + String(written) + "/" + String(sz);
            } else if (!Update.end(true)) {
              otaLastError = String("Update.end: ") + Update.errorString();
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
    otaInProgress = false;
    otaPauseAudio = false;
    vTaskDelete(nullptr);
  }, "otaJob", OTA_JOB_STACK, job, 2, nullptr, 0);

  if (otaRc != pdPASS) {
    // Task never ran - unwind everything so the pipeline resumes, and
    // tell the browser we could not start. 500 rather than 202 so the
    // wizard's `if (!r.ok)` path fires and shows the reason.
    delete job;
    otaInProgress = false;
    otaPauseAudio = false;
    otaLastError  = "otaJob task create failed (out of heap?)";
    LOGE("OTA: xTaskCreatePinnedToCore failed rc=%d", (int)otaRc);
    request->send(500, "application/json",
                  "{\"ok\":false,\"error\":\"could not start OTA job (out of heap?)\"}");
    return;
  }

  // Task is running. Only now does the browser learn about it.
  request->send(202, "application/json", "{\"ok\":true,\"state\":\"running\"}");
}

// -- Plan --------------------------------------------------------------------
static void handleOtaPlan(AsyncWebServerRequest *request) {
  if (!otaMutex) otaMutex = xSemaphoreCreateMutex();
  if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    request->send(503, "application/json", "{\"ok\":false,\"error\":\"busy\"}");
    return;
  }
  otaScanSpool();
  otaProbeNodes();
  xSemaphoreGive(otaMutex);

  // ---------------------------------------------------------------------
  // Build the entire JSON body in a heap String and hand it to
  // request->send() as a single body. Do NOT use AsyncResponseStream here.
  //
  // AsyncResponseStream sends its payload in chunks. On an ESP32-S3 that
  // is already running WiFi + Opus + UDP, one of those per-chunk allocs
  // can silently fail; the response then reaches the browser truncated,
  // the client's r.json() throws, and the Update tab falls through to
  // its "Offline" error path even though the server logged a successful
  // plan build. A single String body goes out with a proper
  // Content-Length and arrives intact.
  //
  // The blob is on the order of a few hundred bytes, so heap cost is nil.
  // ---------------------------------------------------------------------
  String body;
  body.reserve(512);
  body  = "{\"ok\":true,";
  body += "\"serverVersion\":\""; body += EVD_FW_VERSION; body += "\",";
  body += "\"inProgress\":";       body += (otaInProgress ? "true" : "false");
  body += ",\"progressPct\":";     body += String(otaProgressPct);
  body += ",\"lastTarget\":\"";    body += jsonEscape(otaLastTarget); body += "\",";
  body += "\"lastError\":\"";      body += jsonEscape(otaLastError);  body += "\",";
  body += "\"files\":[";
  for (size_t i = 0; i < otaSpool.size(); ++i) {
    if (i) body += ",";
    body += "{\"name\":\"";    body += jsonEscape(otaSpool[i].name);    body += "\",";
    body += "\"kind\":\"";     body += otaSpool[i].kind;                 body += "\",";
    body += "\"version\":\"";  body += otaSpool[i].version;              body += "\",";
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
  // [ota-watchdog] Force-clear the in-progress flag if the job has been
  // running for longer than any legitimate OTA can take. 120 s is
  // generous: even 32 nodes at a worst-case 3 s handshake each is under
  // 100 s. If we get past that, the job task is stuck in a socket call
  // or has been killed without cleaning up, and the wizard must be told
  // the job ended so the user is not stranded.
  if (otaInProgress && otaJobStartedMs > 0 &&
      (millis() - otaJobStartedMs) > 120000UL) {
    otaInProgress = false;
    otaPauseAudio = false;
    if (otaLastError.length() == 0) {
      otaLastError = "OTA job timed out after 120 s; check serial log";
    }
    LOGE("OTA: watchdog fired, force-clearing inProgress (last target: %s)",
         otaLastTarget.c_str());
  }

  String body;
  body.reserve(160);
  body  = "{\"ok\":true,";
  body += "\"inProgress\":";   body += (otaInProgress ? "true" : "false");
  body += ",\"progressPct\":"; body += String(otaProgressPct);
  body += ",\"target\":\"";    body += jsonEscape(otaLastTarget); body += "\",";
  body += ",\"error\":\"";     body += jsonEscape(otaLastError);  body += "\"}";
  request->send(200, "application/json", body);
}

// -- Upload ------------------------------------------------------------------
static File otaUploadFile;
static String otaUploadName;
// [upload-pause-audio]
// Set for the whole duration of an upload, from the first chunk to the
// final one. The audio tasks (audioRead/audioProc/udpSend) block the
// same Core 1 that AsyncTCP runs on, and the flash write on every
// multipart fragment blocks the SPI bus. Running both at once produces
// the mid-upload TCP reset that shows up in the browser as
// "Upload failed: network error". Pausing the audio tasks while the
// upload is in flight removes the CPU contention; the flash-write
// contention is handled by the yield below.
static volatile bool otaUploadInProgress = false;

// [ota-upload-error-state]
// Set by handleOtaUploadChunk() on a short write (disk full, LittleFS
// failure). handleOtaUploadFinal() checks it and returns 500 instead of
// 200 so the wizard's upload progress bar reports the failure at the
// upload step, not at the later /api/ota/apply step with a confusing
// "cannot open server bin" message. Cleared at index == 0 of every new
// upload.
static volatile bool otaUploadHadError = false;
static String        otaUploadErrorMsg;

static void handleOtaUploadFinal(AsyncWebServerRequest *request) {
  // Request-complete handler: fires ONLY after a full body has been received.
  // The abort path is covered by the onDisconnect callback registered in
  // handleOtaUploadChunk() at index == 0. If you find this handler in the
  // hot path of a stuck-audio report, the abort path is the one that fired.
  if (otaUploadFile) otaUploadFile.close();
  otaUploadInProgress = false;
  otaPauseAudio       = false;

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
  if (index == 0) {
    otaUploadInProgress = true;
    otaPauseAudio       = true;
    otaUploadHadError   = false;      // [ota-upload-error-state]
    otaUploadErrorMsg   = "";         // [ota-upload-error-state]
    if (otaUploadFile) otaUploadFile.close();

    // Register a disconnect handler for the lifetime of this request.
    // The request-complete handler above and the final=true branch below
    // BOTH only fire when a completed body arrives. If the browser closes
    // the tab, drops Wi-Fi, or the TCP connection resets before the final
    // chunk, neither branch runs and the audio pipeline stays parked in
    // vTaskDelay(50) forever. onDisconnect fires on socket close in every
    // case (normal and aborted); on the normal path it is a no-op because
    // otaUploadName was already cleared by the final=true branch below.
    request->onDisconnect([]() {
      if (otaUploadFile) otaUploadFile.close();
      if (otaUploadName.length() > 0) {
        // Aborted mid-upload: delete the partial file so a later attempt
        // is not fooled by a truncated bin sitting in the spool.
        String bad = String("/ota_spool/") + otaUploadName;
        LittleFS.remove(bad);
      }
      otaUploadName       = "";
      otaUploadInProgress = false;
      otaPauseAudio       = false;
      LOGW("OTA upload: client disconnected; audio pipeline resumed");
    });

    OtaFile probe;
    if (!parseFwName(filename, probe)) {
      LOGW("OTA upload: ignoring '%s'", filename.c_str());
      otaUploadName = ""; return;
    }
    if (!ensureOtaFs()) {
      LOGW("OTA upload: LittleFS unavailable, cannot store %s", filename.c_str());
      otaUploadName = ""; return;
    }
    LittleFS.mkdir("/ota_spool");

    // Keep at most one spool file per kind: a node bin survives so nodes
    // can be re-flashed without a re-upload; the server bin is deleted
    // after it applies. Without this the spool fills LittleFS across
    // successive uploads.
    {
      File root = LittleFS.open("/ota_spool");
      if (root && root.isDirectory()) {
        std::vector<String> victims;
        File f = root.openNextFile();
        while (f) {
          if (!f.isDirectory()) {
            String base = String(f.name());
            int slash = base.lastIndexOf('/');
            if (slash >= 0) base = base.substring(slash + 1);
            OtaFile existing;
            if (parseFwName(base, existing) && existing.kind == probe.kind) {
              victims.push_back(String("/ota_spool/") + base);
            }
          }
          f = root.openNextFile();
        }
        for (auto &p : victims) {
          LittleFS.remove(p);
          LOGI("OTA upload: removed old %s", p.c_str());
        }
      }
    }

    otaUploadName = filename;
    String path = "/ota_spool/" + filename;
    otaUploadFile = LittleFS.open(path, "w");
    if (!otaUploadFile) {
      LOGE("OTA upload: cannot open %s", path.c_str());
      otaUploadName = ""; return;
    }
    LOGI("OTA upload: receiving %s", filename.c_str());
  }

  if (otaUploadFile && otaUploadName.length()) {
    // [v1.1.1-ota-write-check]
    size_t wrote = otaUploadFile.write(data, len);
    if (wrote != len) {
      LOGE("OTA upload: short write %u/%u on %s (disk full?)",
           (unsigned)wrote, (unsigned)len, otaUploadName.c_str());
      otaUploadHadError = true;                                          // [ota-upload-error-state]
      otaUploadErrorMsg = String("short write ") + String((unsigned)wrote) +
                          "/" + String((unsigned)len) +
                          " on " + otaUploadName;                        // [ota-upload-error-state]
      otaUploadFile.close();
      String bad = String("/ota_spool/") + otaUploadName;
      LittleFS.remove(bad);
      otaUploadName = "";
      return;
    }
  }

  if (final && otaUploadFile) {
    otaUploadFile.close();
    LOGI("OTA upload: stored %s", otaUploadName.c_str());
    otaUploadInProgress = false;
    otaPauseAudio       = false;
    // Clear otaUploadName so the onDisconnect callback registered above
    // (which fires after this branch on the normal path) does not try to
    // delete the successfully uploaded file.
    otaUploadName       = "";
  }

  // One tick per chunk. Without it, ~1 MB of near-continuous flash activity
  // starves the WiFi task on the same SPI bus, the AsyncTCP task cannot ACK,
  // and the browser tears down the connection mid-upload. ~300 ms added over
  // a 1.2 MB upload in exchange for keeping the connection alive.
  vTaskDelay(1);
}