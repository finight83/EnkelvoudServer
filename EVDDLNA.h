#pragma once
// ============================================================================
//  EVDDLNA.h  -  DLNA/UPnP MediaRenderer over WAV/PCM
// ============================================================================
//  Advertises as urn:schemas-upnp-org:device:MediaRenderer:1 over SSDP,
//  serves the description/scpd/control endpoints, and (when the controller
//  calls SetAVTransportURI) opens an HTTP GET on the URI it supplies,
//  parses a RIFF/WAVE 16-bit stream, resamples to 48 kHz if needed, and
//  pushes RawChunk packets into the existing rawQueue.
//
//  ---- Interaction with the existing pipeline --------------------------------
//
//  A new enum value SRC_FROM_NETWORK is added to ServerInputSource. When it
//  is the active source, audioReadTask() sleeps (see EVDAudio.h) and the
//  fetch task below is the sole producer for rawQueue. When the DLNA
//  controller calls Stop, the previous source is restored.
//
//  ---- Threading -------------------------------------------------------------
//
//  dlnaMutex           guards dlnaCurrentUri / dlnaUuid / dlnaFriendlyName.
//                      The SOAP handlers run on the AsyncTCP task (Core 0).
//                      The fetch task runs on Core 1 (pri 2).
//  rawQueue            written by the fetch task; read by audioProcessingTask.
//                      FreeRTOS xQueue is already thread-safe across cores.
//  g_rateEstimatorNeedsReset is a static in EVDAudio.h; it is visible here
//                      because this TU is the same .ino file.
// ============================================================================

#include "EVDHelpers.h"
#include "EVDUsb.h"
#include <HTTPClient.h>

// ---- State -----------------------------------------------------------------
static SemaphoreHandle_t dlnaMutex = nullptr;
static String            dlnaUuid;
static String            dlnaFriendlyName = "Enkelvoud Server";
static String            dlnaCurrentUri;
static volatile bool     dlnaPlaying       = false;
static volatile bool     dlnaPaused        = false;
static volatile int      dlnaVolume        = 100;
static volatile bool     dlnaMuted         = false;
static volatile ServerInputSource dlnaPrevSource = SRC_FROM_I2S;

static WiFiUDP           dlnaSsdpUdp;
static TaskHandle_t      dlnaSsdpTaskHandle  = nullptr;
static TaskHandle_t      dlnaFetchTaskHandle = nullptr;
static volatile bool     dlnaFetchStop       = false;
static volatile bool     dlnaFetchRunning    = false;

// Independent resampler instance for the network path. Ratio 44.1k->48k is
// 1.08843; 48k->48k is 1.00000 (fast path is a memcpy).
static ResamplerToOpusRate dlnaResampler;

// ---- XML helpers -----------------------------------------------------------
inline String dlnaXmlGetTag(const String &xml, const char *tag) {
  String open  = String("<")  + tag + ">";
  String close = String("</") + tag + ">";
  int s = xml.indexOf(open);
  if (s < 0) return "";
  s += open.length();
  int e = xml.indexOf(close, s);
  if (e < 0) return "";
  String v = xml.substring(s, e);
  v.replace("&amp;","&"); v.replace("&lt;","<");
  v.replace("&gt;",">"); v.replace("&quot;","\"");
  v.replace("&apos;","'");
  return v;
}

inline String dlnaXmlEscape(const String &s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if      (c == '&')  o += "&amp;";
    else if (c == '<')  o += "&lt;";
    else if (c == '>')  o += "&gt;";
    else if (c == '"')  o += "&quot;";
    else if (c == '\'') o += "&apos;";
    else o += c;
  }
  return o;
}

// Action name is the first tag after <s:Body>. Handles both "u:Foo" and "Foo".
inline String dlnaSoapActionName(const String &xml) {
  int b = xml.indexOf(":Body>");
  if (b < 0) return "";
  b = xml.indexOf('>', b);
  if (b < 0) return "";
  b++;
  while (b < (int)xml.length() &&
         (xml[b] == ' ' || xml[b] == '\n' || xml[b] == '\r' || xml[b] == '\t')) b++;
  if (b >= (int)xml.length() || xml[b] != '<') return "";
  int e = xml.indexOf('>', b);
  if (e < 0) return "";
  String tag = xml.substring(b + 1, e);
  tag.trim();
  if (tag.endsWith("/")) tag = tag.substring(0, tag.length() - 1);
  int colon = tag.indexOf(':');
  if (colon >= 0) tag = tag.substring(colon + 1);
  int space = tag.indexOf(' ');
  if (space >= 0) tag = tag.substring(0, space);
  return tag;
}

inline String dlnaSoapResponse(const char *serviceType,
                               const char *action,
                               const String &body) {
  String x;
  x.reserve(512 + body.length());
  x  = "<?xml version=\"1.0\"?>\r\n";
  x += "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
       "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">";
  x += "<s:Body><u:"; x += action; x += "Response xmlns:u=\"";
  x += serviceType; x += "\">";
  x += body;
  x += "</u:"; x += action; x += "Response></s:Body></s:Envelope>";
  return x;
}

inline String dlnaSoapFault(int code, const char *desc) {
  char buf[512];
  snprintf(buf, sizeof(buf),
    "<?xml version=\"1.0\"?>\r\n"
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
    "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
    "<s:Body><s:Fault><faultcode>s:Client</faultcode>"
    "<faultstring>UPnPError</faultstring><detail>"
    "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">"
    "<errorCode>%d</errorCode><errorDescription>%s</errorDescription>"
    "</UPnPError></detail></s:Fault></s:Body></s:Envelope>",
    code, desc);
  return String(buf);
}

// ---- Audio fetcher --------------------------------------------------------
// Pushes RawChunk packets into rawQueue at the WAV's native rate, resampled
// to 48 kHz. If the stream stops (server closes, connection drops, or the
// controller calls Stop), the task exits cleanly and dlnaFetchRunning
// returns to false.
inline void dlnaFetchTaskFn(void *) {
  dlnaFetchRunning = true;
  dlnaFetchStop    = false;

  String uri;
  if (dlnaMutex && xSemaphoreTake(dlnaMutex, pdMS_TO_TICKS(100))) {
    uri = dlnaCurrentUri;
    xSemaphoreGive(dlnaMutex);
  }
  if (!uri.startsWith("http://")) {
    LOGE("DLNA: unsupported URI scheme: %s", uri.c_str());
    dlnaFetchRunning = false;
    dlnaFetchTaskHandle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  // Parse host:port/path manually to keep HTTPClient out of the picture.
  // HTTPClient's GET() blocks until the entire body is loaded on some
  // builds, which defeats streaming.
  String rest = uri.substring(7);          // strip "http://"
  int slash = rest.indexOf('/');
  String hostPort = (slash < 0) ? rest : rest.substring(0, slash);
  String path     = (slash < 0) ? "/"  : rest.substring(slash);
  int colon = hostPort.indexOf(':');
  String host = (colon < 0) ? hostPort : hostPort.substring(0, colon);
  int    port = (colon < 0) ? 80 : hostPort.substring(colon + 1).toInt();

  LOGI("DLNA: fetching http://%s:%d%s", host.c_str(), port, path.c_str());

  WiFiClient client;
  client.setTimeout(10);  // seconds
  if (!client.connect(host.c_str(), port)) {
    LOGE("DLNA: TCP connect to %s:%d failed", host.c_str(), port);
    dlnaFetchRunning = false;
    dlnaFetchTaskHandle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  client.print(String("GET ") + path + " HTTP/1.0\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: Enkelvoud/1.0\r\n" +
               "Accept: */*\r\n" +
               "Connection: close\r\n\r\n");

  // Read headers byte-by-byte until \r\n\r\n
  uint32_t hdrStart = millis();
  String  hdr;
  hdr.reserve(512);
  while (client.connected() && (millis() - hdrStart) < 8000) {
    if (client.available()) {
      char c = (char)client.read();
      hdr += c;
      if (hdr.endsWith("\r\n\r\n")) break;
    } else {
      vTaskDelay(1);
    }
  }
  int sc = 0;
  { int sp = hdr.indexOf(' '); if (sp > 0) sc = hdr.substring(sp + 1).toInt(); }
  if (sc != 200) {
    LOGE("DLNA: HTTP %d from %s", sc, host.c_str());
    client.stop();
    dlnaFetchRunning = false;
    dlnaFetchTaskHandle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  // Parse WAV header. Standard 44-byte RIFF/WAVE PCM header.
  uint8_t wh[44];
  size_t  whGot = 0;
  uint32_t whStart = millis();
  while (whGot < 44 && client.connected() && (millis() - whStart) < 8000) {
    if (client.available()) wh[whGot++] = (uint8_t)client.read();
    else vTaskDelay(1);
  }
  if (whGot < 44 ||
      memcmp(wh,     "RIFF", 4) != 0 ||
      memcmp(wh + 8, "WAVE", 4) != 0 ||
      memcmp(wh + 36, "data", 4) != 0) {
    LOGE("DLNA: stream is not RIFF/WAVE PCM");
    client.stop();
    dlnaFetchRunning = false;
    dlnaFetchTaskHandle = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  uint16_t chans = wh[22] | (wh[23] << 8);
  uint32_t rate  = wh[24] | (wh[25] << 8) | ((uint32_t)wh[26] << 16) | ((uint32_t)wh[27] << 24);
  uint16_t bits  = wh[34] | (wh[35] << 8);
  LOGI("DLNA: WAV %u Hz, %u ch, %u bits", (unsigned)rate, (unsigned)chans, (unsigned)bits);
  if (chans != 2 || bits != 16 || (rate != 44100 && rate != 48000)) {
    LOGE("DLNA: unsupported WAV format (need 16-bit stereo @ 44.1/48k)");
    client.stop();
    dlnaFetchRunning = false;
    dlnaFetchTaskHandle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  dlnaResampler.reset();
  // Resampler ratio is input samples consumed per output sample.
  dlnaResampler.setRatio((double)rate / 48000.0);

  // Stream raw PCM. We read into a small ingress buffer, feed the resampler,
  // and push 1024-byte RawChunks (256 stereo frames @ 16-bit) into rawQueue.
  static uint8_t  inBuf[2048];
  static int16_t  outBuf[2048];
  size_t          inFill = 0;
  uint32_t        outputTimelineStartMs = 0;
  uint64_t        outputFramesQueued = 0;
  bool            outputTimelineStarted = false;

  uint32_t lastActivity = millis();
  while (!dlnaFetchStop && client.connected()) {
    // Skip while OTA has the audio pipeline paused.
    if (otaPauseAudio || otaUploadInProgress || otaInProgress) {
      vTaskDelay(pdMS_TO_TICKS(50));
      lastActivity = millis();
      continue;
    }

    int avail = client.available();
    if (avail <= 0) {
      if (millis() - lastActivity > 15000) {
        LOGW("DLNA: stream idle for 15s, ending");
        break;
      }
      vTaskDelay(1);
      continue;
    }
    lastActivity = millis();

    size_t toRead = (size_t)avail;
    if (toRead > sizeof(inBuf) - inFill) toRead = sizeof(inBuf) - inFill;
    int got = client.read(inBuf + inFill, toRead);
    if (got <= 0) { vTaskDelay(1); continue; }
    inFill += (size_t)got;

    // Consume complete 16-bit stereo frames.
    size_t inFrames = inFill / 4;
    if (inFrames == 0) continue;

    uint32_t produced = dlnaResampler.process(
        (const int16_t *)inBuf, (uint32_t)inFrames,
        outBuf, sizeof(outBuf) / (2 * sizeof(int16_t)));

    // Shift any leftover incomplete frame to the start of inBuf.
    size_t consumedBytes = inFrames * 4;
    size_t leftover = inFill - consumedBytes;
    if (leftover) memmove(inBuf, inBuf + consumedBytes, leftover);
    inFill = leftover;

    // Push produced frames in RawChunk-sized units. If the queue is full
    // we simply drop (a full rawQueue means the pipeline is behind; the
    // node will absorb the gap via its playout buffer).
    size_t outConsumed = 0;
    const size_t CHUNK_FRAMES = I2S_READ_CHUNK_BYTES / (2 * sizeof(int16_t));
    while (outConsumed < produced) {
      size_t n = produced - outConsumed;
      if (n > CHUNK_FRAMES) n = CHUNK_FRAMES;
      RawChunk rc;
      memcpy(rc.data, outBuf + outConsumed * 2, n * 4);
      rc.len         = (uint32_t)(n * 4);
      if (!outputTimelineStarted) {
        outputTimelineStartMs = millis();
        outputFramesQueued = 0;
        outputTimelineStarted = true;
      }
      rc.captured_ms = audioSampleTimestampMs(
          outputTimelineStartMs, outputFramesQueued, 48000);

      // Pace the file stream against its output sample timeline. Without
      // this, a fast HTTP source fills rawQueue immediately and then drops
      // chunks while the encoder drains at the audio sample rate. Blocking
      // queue sends provide backpressure; a long stall reanchors the timeline
      // so playback does not burst stale chunks when the consumer recovers.
      bool queued = false;
      while (!dlnaFetchStop && !queued) {
        uint32_t nowMs = millis();
        int32_t lateMs = (int32_t)(nowMs - rc.captured_ms);
        if (lateMs > 100) {
          outputTimelineStartMs = nowMs;
          outputFramesQueued = 0;
          rc.captured_ms = nowMs;
        }
        int32_t untilMs = (int32_t)(rc.captured_ms - nowMs);
        if (untilMs > 0) {
          uint32_t waitMs = (uint32_t)untilMs;
          if (waitMs > 20) waitMs = 20;
          vTaskDelay(pdMS_TO_TICKS(waitMs));
          continue;
        }
        if (xQueueSend(rawQueue, &rc, pdMS_TO_TICKS(20)) == pdTRUE) {
          queued = true;
        } else {
          vTaskDelay(1);
        }
      }
      if (queued) {
        outputFramesQueued += n;
        portENTER_CRITICAL(&gStatsMux);
          statNetBytesIn += rc.len;
        portEXIT_CRITICAL(&gStatsMux);
      } else {
        statNetDropped++;
      }
      outConsumed += n;
    }
    vTaskDelay(1);   // yield to encodeTask on Core 1 (pri 5 > us)
  }

  client.stop();
  LOGI("DLNA: fetch stopped");
  dlnaFetchRunning    = false;
  dlnaFetchTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

inline void dlnaStartFetch() {
  if (dlnaFetchTaskHandle) {
    dlnaFetchStop = true;
    uint32_t t = millis();
    while (dlnaFetchRunning && millis() - t < 3000) vTaskDelay(10);
    dlnaFetchTaskHandle = nullptr;
  }
  dlnaFetchStop = false;
  g_rateEstimatorNeedsReset = true;
  // Core 1: alongside audioEncode (pri 5). We are pri 2, so encode always
  // wins. HTTP reads are blocking but yield via vTaskDelay(1) each loop.
  BaseType_t rc = xTaskCreatePinnedToCore(
      dlnaFetchTaskFn, "dlnaFetch", 6144, nullptr, 2,
      &dlnaFetchTaskHandle, 1);
  if (rc != pdPASS) {
    LOGE("DLNA: failed to start fetch task (rc=%d)", (int)rc);
    dlnaFetchTaskHandle = nullptr;
  }
}

inline void dlnaStopFetch() {
  if (!dlnaFetchTaskHandle) return;
  dlnaFetchStop = true;
  uint32_t t = millis();
  while (dlnaFetchRunning && millis() - t < 3000) vTaskDelay(10);
  dlnaFetchTaskHandle = nullptr;
  dlnaFetchStop = false;
}

// ---- SSDP -----------------------------------------------------------------
inline void dlnaSsdpSend(const IPAddress &dst, uint16_t port,
                         const char *st, bool isSearchResponse) {
  String x;
  x.reserve(512);
  if (isSearchResponse) {
    x  = "HTTP/1.1 200 OK\r\n";
    x += "CACHE-CONTROL: max-age=1800\r\n";
    x += "EXT:\r\n";
  } else {
    x  = "NOTIFY * HTTP/1.1\r\n";
    x += "HOST: 239.255.255.250:1900\r\n";
    x += "CACHE-CONTROL: max-age=1800\r\n";
  }
  IPAddress ip = activeIpAddress();
  x += "LOCATION: http://"; x += ip.toString(); x += "/description.xml\r\n";
  x += "SERVER: Linux/3.14 UPnP/1.0 Enkelvoud/1.0\r\n";
  x += "ST: "; x += st; x += "\r\n";
  x += "USN: "; x += dlnaUuid; x += "::"; x += st; x += "\r\n";
  x += "NT: "; x += st; x += "\r\n";       // for NOTIFY
  x += "NTS: ssdp:alive\r\n";
  x += "\r\n";
  dlnaSsdpUdp.beginPacket(dst, port);
  dlnaSsdpUdp.write((const uint8_t *)x.c_str(), x.length());
  dlnaSsdpUdp.endPacket();
}

inline void dlnaSsdpNotifyAll() {
  static const char *targets[] = {
    "upnp:rootdevice",
    "urn:schemas-upnp-org:device:MediaRenderer:1",
    "urn:schemas-upnp-org:service:AVTransport:1",
    "urn:schemas-upnp-org:service:RenderingControl:1",
    "urn:schemas-upnp-org:service:ConnectionManager:1",
  };
  for (auto *t : targets) {
    dlnaSsdpSend(IPAddress(239,255,255,250), 1900, t, false);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

inline void dlnaSsdpTaskFn(void *) {
  uint32_t lastNotify = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    uint32_t now = millis();
    if (now - lastNotify > 300000UL || lastNotify == 0) {  // 5 min
      lastNotify = now;
      dlnaSsdpNotifyAll();
    }

    int sz = dlnaSsdpUdp.parsePacket();
    if (sz <= 0) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    char buf[512];
    int  n = dlnaSsdpUdp.read(buf, sizeof(buf) - 1);
    if (n <= 0) continue;
    buf[n] = '\0';
    if (strncmp(buf, "M-SEARCH", 8) != 0) continue;

    // Pull the ST header to know what the searcher is looking for.
    String req(buf);
    String st;
    int stPos = req.indexOf("ST:");
    if (stPos >= 0) {
      int nl = req.indexOf('\r', stPos);
      if (nl < 0) nl = req.indexOf('\n', stPos);
      if (nl > stPos) st = req.substring(stPos + 3, nl);
    }
    st.trim();
    if (st.length() == 0 || st == "ssdp:all" ||
        st == "upnp:rootdevice" ||
        st == "urn:schemas-upnp-org:device:MediaRenderer:1" ||
        st.startsWith("urn:schemas-upnp-org:service:AVTransport") ||
        st.startsWith("urn:schemas-upnp-org:service:RenderingControl") ||
        st.startsWith("urn:schemas-upnp-org:service:ConnectionManager")) {
      IPAddress remote = dlnaSsdpUdp.remoteIP();
      uint16_t  rport  = dlnaSsdpUdp.remotePort();
      if (st.length() == 0 || st == "ssdp:all") {
        // Reply once per target type
        static const char *targets[] = {
          "upnp:rootdevice",
          "urn:schemas-upnp-org:device:MediaRenderer:1",
        };
        for (auto *t : targets) {
          dlnaSsdpSend(remote, rport, t, true);
          vTaskDelay(pdMS_TO_TICKS(15));
        }
      } else {
        dlnaSsdpSend(remote, rport, st.c_str(), true);
      }
    }
  }
}

// ---- Description + SCPD endpoints -----------------------------------------
// The UPnP device description. Must be served at LOCATION (see
// dlnaSsdpSend) for every controller that responds to an SSDP M-SEARCH.
// Pulls the service list, UDN and friendlyName from the SSDP/init state
// so a server rename takes effect on the next controller that re-reads
// the description (no reboot required).
inline String dlnaDescriptionXml() {
  String x;
  x.reserve(1280);
  x  = "<?xml version=\"1.0\"?>\r\n";
  x += "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">";
  x += "<specVersion><major>1</major><minor>0</minor></specVersion>";
  x += "<device>";
  x += "<deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>";
  x += "<friendlyName>"; x += dlnaXmlEscape(dlnaFriendlyName); x += "</friendlyName>";
  x += "<manufacturer>Enkelvoud</manufacturer>";
  x += "<modelDescription>Enkelvoud DLNA MediaRenderer (WAV/PCM)</modelDescription>";
  x += "<modelName>Enkelvoud Server</modelName>";
  x += "<modelNumber>1</modelNumber>";
  x += "<UDN>"; x += dlnaUuid; x += "</UDN>";
  x += "<serviceList>";
  x += "<service>"
       "<serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>"
       "<serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>"
       "<SCPDURL>/upnp/scpd/avt.xml</SCPDURL>"
       "<controlURL>/upnp/control/avt</controlURL>"
       "<eventSubURL>/upnp/event/avt</eventSubURL>"
       "</service>";
  x += "<service>"
       "<serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>"
       "<serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>"
       "<SCPDURL>/upnp/scpd/rcs.xml</SCPDURL>"
       "<controlURL>/upnp/control/rcs</controlURL>"
       "<eventSubURL>/upnp/event/rcs</eventSubURL>"
       "</service>";
  x += "<service>"
       "<serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>"
       "<serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>"
       "<SCPDURL>/upnp/scpd/cms.xml</SCPDURL>"
       "<controlURL>/upnp/control/cms</controlURL>"
       "<eventSubURL>/upnp/event/cms</eventSubURL>"
       "</service>";
  x += "</serviceList>";
  x += "</device></root>";
  return x;
}

inline void dlnaHandleDescription(AsyncWebServerRequest *request) {
  String x = dlnaDescriptionXml();
  AsyncWebServerResponse *r =
      request->beginResponse(200, "text/xml; charset=utf-8", x);
  r->addHeader("Cache-Control", "no-cache");
  request->send(r);
}

inline String dlnaScpdAvt() {
  String x;
  x.reserve(2048);
  x  = "<?xml version=\"1.0\"?>\r\n";
  x += "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">";
  x += "<specVersion><major>1</major><minor>0</minor></specVersion>";
  x += "<actionList>";
  auto act = [&x](const char *name, const char *arg) {
    x += "<action><name>"; x += name; x += "</name><argumentList>";
    if (arg) { x += "<argument><name>InstanceID</name><direction>in</direction>"
                    "<relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>"; }
    x += "</argumentList></action>";
  };
  act("SetAVTransportURI", "x");
  act("Play",              "x");
  act("Pause",             "x");
  act("Stop",              "x");
  act("Seek",              "x");
  act("GetTransportInfo",  "x");
  act("GetMediaInfo",      "x");
  act("GetPositionInfo",   "x");
  act("GetDeviceCapabilities", "x");
  x += "</actionList>";
  x += "<serviceStateTable>";
  x += "<stateVariable sendEvents=\"no\"><name>TransportState</name>"
       "<dataType>string</dataType><allowedValueList>"
       "<allowedValue>STOPPED</allowedValue><allowedValue>PLAYING</allowedValue>"
       "<allowedValue>PAUSED_PLAYBACK</allowedValue></allowedValueList>"
       "</stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>TransportStatus</name>"
       "<dataType>string</dataType></stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>AVTransportURI</name>"
       "<dataType>string</dataType></stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>AVTransportURIMetaData</name>"
       "<dataType>string</dataType></stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>CurrentTrack</name>"
       "<dataType>ui4</dataType></stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>CurrentTrackDuration</name>"
       "<dataType>string</dataType></stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>RelTime</name>"
       "<dataType>string</dataType></stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name>"
       "<dataType>ui4</dataType></stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>DeviceCapabilities</name>"
       "<dataType>string</dataType></stateVariable>";
  x += "</serviceStateTable></scpd>";
  return x;
}

inline String dlnaScpdRcs() {
  String x;
  x.reserve(1024);
  x  = "<?xml version=\"1.0\"?>\r\n";
  x += "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">";
  x += "<specVersion><major>1</major><minor>0</minor></specVersion>";
  x += "<actionList>";
  const char *names[] = { "GetVolume", "SetVolume", "GetMute", "SetMute" };
  for (auto *n : names) {
    x += "<action><name>"; x += n; x += "</name><argumentList>"
         "<argument><name>InstanceID</name><direction>in</direction>"
         "<relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>"
         "<argument><name>Channel</name><direction>in</direction>"
         "<relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>"
         "</argumentList></action>";
  }
  x += "</actionList>";
  x += "<serviceStateTable>";
  x += "<stateVariable sendEvents=\"no\"><name>Volume</name>"
       "<dataType>ui2</dataType></stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>Mute</name>"
       "<dataType>boolean</dataType></stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name>"
       "<dataType>ui4</dataType></stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Channel</name>"
       "<dataType>string</dataType></stateVariable>";
  x += "</serviceStateTable></scpd>";
  return x;
}

inline String dlnaScpdCms() {
  String x;
  x.reserve(768);
  x  = "<?xml version=\"1.0\"?>\r\n";
  x += "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">";
  x += "<specVersion><major>1</major><minor>0</minor></specVersion>";
  x += "<actionList>";
  const char *names[] = {
    "GetProtocolInfo", "GetCurrentConnectionIDs", "GetCurrentConnectionInfo"
  };
  for (auto *n : names) {
    x += "<action><name>"; x += n; x += "</name><argumentList/>";
    x += "</action>";
  }
  x += "</actionList>";
  x += "<serviceStateTable>";
  x += "<stateVariable sendEvents=\"no\"><name>SourceProtocolInfo</name>"
       "<dataType>string</dataType></stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>SinkProtocolInfo</name>"
       "<dataType>string</dataType></stateVariable>";
  x += "<stateVariable sendEvents=\"no\"><name>CurrentConnectionIDs</name>"
       "<dataType>string</dataType></stateVariable>";
  x += "</serviceStateTable></scpd>";
  return x;
}

// ---- SOAP action dispatch -------------------------------------------------
inline void dlnaReplySoap(AsyncWebServerRequest *request,
                          const char *serviceType,
                          const char *action,
                          const String &body) {
  String x = dlnaSoapResponse(serviceType, action, body);
  AsyncWebServerResponse *r =
      request->beginResponse(200, "text/xml; charset=utf-8", x);
  r->addHeader("EXT", "");
  request->send(r);
}

inline void dlnaHandleControlAvt(AsyncWebServerRequest *request,
                                 uint8_t *data, size_t len,
                                 size_t index, size_t total) {
  if (index != 0 || len != total) {
    if (index != 0) return;
    request->send(413, "text/xml", dlnaSoapFault(402, "Body fragmented"));
    return;
  }
  String xml((const char *)data, len);
  String action = dlnaSoapActionName(xml);
  LOGI("DLNA AVT action: %s", action.c_str());

  const char *svc = "urn:schemas-upnp-org:service:AVTransport:1";

  if (action == "SetAVTransportURI") {
    String uri = dlnaXmlGetTag(xml, "CurrentURI");
    if (uri.length() == 0) {
      request->send(500, "text/xml", dlnaSoapFault(402, "Missing CurrentURI"));
      return;
    }
    // Save previous source only on first entry into network mode.
    if (serverInputSource != SRC_FROM_NETWORK) {
      dlnaPrevSource = serverInputSource;
      if (serverInputSource == SRC_FROM_USB) stopServerUsbAudio();
      serverInputSource = SRC_FROM_NETWORK;
      g_rateEstimatorNeedsReset = true;
    }
    if (dlnaMutex && xSemaphoreTake(dlnaMutex, pdMS_TO_TICKS(100))) {
      dlnaCurrentUri = uri;
      xSemaphoreGive(dlnaMutex);
    }
    dlnaPlaying = false;
    dlnaPaused  = false;
    dlnaReplySoap(request, svc, "SetAVTransportURI", "");
    return;
  }

  if (action == "Play") {
    if (!dlnaPlaying) {
      dlnaPlaying = true;
      dlnaPaused  = false;
      dlnaStartFetch();
    }
    dlnaReplySoap(request, svc, "Play", "");
    return;
  }

  if (action == "Pause") {
    dlnaPaused  = true;
    dlnaPlaying = false;
    dlnaStopFetch();
    dlnaReplySoap(request, svc, "Pause", "");
    return;
  }

  if (action == "Stop") {
    dlnaPlaying = false;
    dlnaPaused  = false;
    dlnaStopFetch();
    // Restore the previous input source.
    if (serverInputSource == SRC_FROM_NETWORK) {
      serverInputSource = dlnaPrevSource;
      g_rateEstimatorNeedsReset = true;
    }
    dlnaReplySoap(request, svc, "Stop", "");
    return;
  }

  if (action == "GetTransportInfo") {
    const char *st = dlnaPlaying ? "PLAYING"
                   : dlnaPaused  ? "PAUSED_PLAYBACK"
                   :               "STOPPED";
    String body;
    body.reserve(128);
    body  = "<CurrentTransportState>";  body += st;  body += "</CurrentTransportState>";
    body += "<CurrentTransportStatus>OK</CurrentTransportStatus>";
    body += "<CurrentSpeed>1</CurrentSpeed>";
    dlnaReplySoap(request, svc, "GetTransportInfo", body);
    return;
  }

  if (action == "GetMediaInfo") {
    String uri;
    if (dlnaMutex && xSemaphoreTake(dlnaMutex, pdMS_TO_TICKS(50))) {
      uri = dlnaCurrentUri;
      xSemaphoreGive(dlnaMutex);
    }
    String body;
    body.reserve(384);
    body  = "<NrTracks>1</NrTracks>";
    body += "<MediaDuration>0:00:00</MediaDuration>";
    body += "<CurrentURI>"; body += dlnaXmlEscape(uri); body += "</CurrentURI>";
    body += "<CurrentURIMetaData></CurrentURIMetaData>";
    body += "<NextURI></NextURI><NextURIMetaData></NextURIMetaData>";
    body += "<PlayMedium>NETWORK</PlayMedium><RecordMedium>NOT_IMPLEMENTED</RecordMedium>";
    body += "<WriteStatus>NOT_IMPLEMENTED</WriteStatus>";
    dlnaReplySoap(request, svc, "GetMediaInfo", body);
    return;
  }

  if (action == "GetPositionInfo") {
    String body;
    body.reserve(256);
    body  = "<Track>1</Track><TrackDuration>0:00:00</TrackDuration>";
    body += "<TrackMetaData></TrackMetaData><TrackURI></TrackURI>";
    body += "<RelTime>0:00:00</RelTime><AbsTime>0:00:00</AbsTime>";
    body += "<RelCount>0</RelCount><AbsCount>0</AbsCount>";
    dlnaReplySoap(request, svc, "GetPositionInfo", body);
    return;
  }

  if (action == "GetDeviceCapabilities") {
    String body = "<PlayMedia>NETWORK</PlayMedia><RecMedia>NOT_IMPLEMENTED</RecMedia>"
                  "<RecQualityModes>NOT_IMPLEMENTED</RecQualityModes>";
    dlnaReplySoap(request, svc, "GetDeviceCapabilities", body);
    return;
  }

  request->send(500, "text/xml", dlnaSoapFault(401, "Invalid Action"));
}

inline void dlnaHandleControlRcs(AsyncWebServerRequest *request,
                                 uint8_t *data, size_t len,
                                 size_t index, size_t total) {
  if (index != 0 || len != total) {
    if (index != 0) return;
    request->send(413, "text/xml", dlnaSoapFault(402, "Body fragmented"));
    return;
  }
  String xml((const char *)data, len);
  String action = dlnaSoapActionName(xml);
  LOGI("DLNA RCS action: %s", action.c_str());
  const char *svc = "urn:schemas-upnp-org:service:RenderingControl:1";

  if (action == "GetVolume") {
    String body = "<CurrentVolume>"; body += String(dlnaVolume); body += "</CurrentVolume>";
    dlnaReplySoap(request, svc, "GetVolume", body);
    return;
  }
  if (action == "SetVolume") {
    int v = dlnaXmlGetTag(xml, "DesiredVolume").toInt();
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    dlnaVolume   = v;
    masterVolume = (uint8_t)v;
    broadcastNodeConfig();
    dlnaReplySoap(request, svc, "SetVolume", "");
    return;
  }
  if (action == "GetMute") {
    String body = "<CurrentMute>";
    body += (dlnaMuted ? "1" : "0");
    body += "</CurrentMute>";
    dlnaReplySoap(request, svc, "GetMute", body);
    return;
  }
  if (action == "SetMute") {
    String v = dlnaXmlGetTag(xml, "DesiredMute");
    dlnaMuted   = (v == "1" || v == "true");
    masterMuted = dlnaMuted;
    broadcastNodeConfig();
    dlnaReplySoap(request, svc, "SetMute", "");
    return;
  }
  request->send(500, "text/xml", dlnaSoapFault(401, "Invalid Action"));
}

inline void dlnaHandleControlCms(AsyncWebServerRequest *request,
                                 uint8_t *data, size_t len,
                                 size_t index, size_t total) {
  if (index != 0 || len != total) {
    if (index != 0) return;
    request->send(413, "text/xml", dlnaSoapFault(402, "Body fragmented"));
    return;
  }
  String xml((const char *)data, len);
  String action = dlnaSoapActionName(xml);
  LOGI("DLNA CMS action: %s", action.c_str());
  const char *svc = "urn:schemas-upnp-org:service:ConnectionManager:1";

  if (action == "GetProtocolInfo") {
    // Advertise WAV and raw PCM (what we can actually decode).
    String body;
    body.reserve(384);
    body  = "<Source></Source>";
    body += "<Sink>http-get:*:audio/wav:*,http-get:*:audio/x-wav:*,"
            "http-get:*:audio/wave:*,http-get:*:audio/L16:*,"
            "http-get:*:audio/L16;rate=44100;channels=2:*,"
            "http-get:*:audio/L16;rate=48000;channels=2:*</Sink>";
    dlnaReplySoap(request, svc, "GetProtocolInfo", body);
    return;
  }
  if (action == "GetCurrentConnectionIDs") {
    dlnaReplySoap(request, svc, "GetCurrentConnectionIDs",
                  "<ConnectionIDs>0</ConnectionIDs>");
    return;
  }
  if (action == "GetCurrentConnectionInfo") {
    String body;
    body  = "<RcsID>0</RcsID><AVTransportID>0</AVTransportID>";
    body += "<ProtocolInfo></ProtocolInfo>";
    body += "<PeerConnectionManager></PeerConnectionManager>";
    body += "<PeerConnectionID>-1</PeerConnectionID>";
    body += "<Direction>Input</Direction><Status>OK</Status>";
    dlnaReplySoap(request, svc, "GetCurrentConnectionInfo", body);
    return;
  }
  request->send(500, "text/xml", dlnaSoapFault(401, "Invalid Action"));
}

// ---- Init -----------------------------------------------------------------
inline void initDlna() {
  if (!dlnaMutex) dlnaMutex = xSemaphoreCreateMutex();

  // Build a stable UUID from the eFuse MAC.
  uint64_t mac = ESP.getEfuseMac();
  char buf[64];
  snprintf(buf, sizeof(buf),
           "uuid:456e6b6c-6f76-6f75-6400-%08x%04x",
           (unsigned)(mac >> 16), (unsigned)(mac & 0xFFFF));
  dlnaUuid = buf;
  dlnaFriendlyName = serverName;

  dlnaResampler.reset();
  dlnaResampler.setRatio(1.0);

  // Bind SSDP on 239.255.255.250:1900.
  if (!dlnaSsdpUdp.beginMulticast(IPAddress(239, 255, 255, 250), 1900)) {
    LOGE("DLNA: beginMulticast(239.255.255.250:1900) failed");
    return;
  }
  BaseType_t rc = xTaskCreatePinnedToCore(
      dlnaSsdpTaskFn, "dlnaSsdp", 3072, nullptr, 2,
      &dlnaSsdpTaskHandle, 0);
  if (rc != pdPASS) {
    LOGE("DLNA: failed to start SSDP task (rc=%d)", (int)rc);
    return;
  }
  LOGI("DLNA: MediaRenderer ready, UUID=%s, friendly=%s",
       dlnaUuid.c_str(), dlnaFriendlyName.c_str());
}