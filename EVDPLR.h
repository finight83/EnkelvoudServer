#pragma once

/*
  EVDPLR.h
  --------
  Player page served at:
    - /player
  This page is HTTP-served and can be used as a companion control/status page
  for the receiver node. (Actual Opus playback is typically performed against
  the ESP32-S3 websocket endpoint in your EnkelvoudPlayer flow.)
*/

static const char EVDPLR_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Enkelvoud Player</title>
  <style>
    body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#0c1118;color:#eaf0ff;margin:0;padding:20px}
    .card{max-width:760px;margin:0 auto;background:#131b28;border:1px solid #26354e;border-radius:14px;padding:18px}
    h1{margin:0 0 6px;font-size:1.3rem}
    .muted{color:#a8badc}
    .box{margin-top:12px;padding:12px;border:1px solid #2b3d5a;border-radius:10px;background:#0d1421}
    .pill{display:inline-block;padding:5px 10px;border-radius:999px;background:#1b2a40;border:1px solid #39547c}
    a{color:#9ec1ff}
    code{background:#0b1220;padding:2px 6px;border-radius:6px}
  </style>
</head>
<body>
  <div class="card">
    <h1>EVDPLR</h1>
    <div class="muted">Receiver-side player/status page</div>

    <div class="box">
      <p>Selected Source: <span id="src" class="pill">...</span></p>
      <p>Receiver IP: <span id="ip">...</span></p>
      <p>Open Control: <a href="/">/</a></p>
      <p>Expected S3 websocket endpoint (on your server node): <code>ws://&lt;s3-ip&gt;/audio</code></p>
    </div>

    <div class="box">
      <pre id="status">{}</pre>
    </div>
  </div>

<script>
async function poll(){
  try{
    const r = await fetch('/api/status');
    const j = await r.json();
    document.getElementById('src').textContent = j.source || '?';
    document.getElementById('ip').textContent = j.ip || '?';
    document.getElementById('status').textContent = JSON.stringify(j,null,2);
  }catch(e){
    document.getElementById('status').textContent = 'error: ' + e;
  }
}
poll();
setInterval(poll, 3000);
</script>
</body>
</html>
)HTML";