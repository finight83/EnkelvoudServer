#pragma once

/*
  EVDCTRL.h
  ---------
  Control panel page served at:
    - /
  Used to switch source (BT/AUX/USB), send next/prev commands, and show status.
*/

static const char EVDCTRL_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Enkelvoud Control</title>
  <style>
    :root{
      --bg:#06070b;--panel:#10131b;--panel-2:#171c27;--surface:#0b0f16;--surface-2:#141a24;
      --text:#eef3ff;--muted:#9aa6bf;--line:#252e40;--line-2:#334059;--accent:#7eb3ff;
      --accent-2:#4a88ff;--good:#2ed39b;--warn:#f2bd5d;--bad:#ff6c7d;--shadow:0 18px 48px rgba(0,0,0,.32);
    }
    body[data-theme="blue"]{
      --bg:#08111f;--panel:#101c31;--panel-2:#172741;--surface:#0d1526;--surface-2:#162238;
      --text:#f3f7ff;--muted:#a7b8d7;--line:#223350;--line-2:#385685;--accent:#8bc6ff;--accent-2:#4fa2ff;
    }
    body[data-theme="light"]{
      --bg:#edf2fb;--panel:#ffffff;--panel-2:#f5f8ff;--surface:#eef3fb;--surface-2:#f8faff;
      --text:#162033;--muted:#5b6882;--line:#d7e0ef;--line-2:#b9c8e2;--accent:#285ce6;--accent-2:#1d48c8;
      --good:#178f69;--warn:#b7791f;--bad:#d83b58;--shadow:0 18px 40px rgba(28,44,73,.12);
    }
    *{box-sizing:border-box}
    body{
      margin:0;padding:18px;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;color:var(--text);
      background:
        radial-gradient(circle at top,rgba(126,179,255,.16),transparent 24rem),
        linear-gradient(180deg,var(--bg),#030407 120%);
      min-height:100vh;
    }
    a{color:var(--accent);text-decoration:none}
    a:hover{text-decoration:underline}
    .app{max-width:1120px;margin:0 auto;display:grid;gap:14px}
    .card{
      background:linear-gradient(180deg,var(--panel),var(--panel-2));
      border:1px solid var(--line);border-radius:18px;box-shadow:var(--shadow);
    }
    .hero{padding:18px}
    .hero-top,.hero-actions,.toolbar,.themes,.chips,.button-row,.status-grid{display:flex;gap:10px;flex-wrap:wrap}
    .hero-top{justify-content:space-between;align-items:flex-start}
    .eyebrow{color:var(--accent);text-transform:uppercase;letter-spacing:.16em;font-size:.72rem;font-weight:700}
    h1{margin:8px 0 6px;font-size:clamp(1.5rem,3.8vw,2.3rem)}
    .sub{margin:0;color:var(--muted);max-width:48rem;line-height:1.45}
    .hero-actions{align-items:center;justify-content:space-between;margin-top:16px}
    .toolbar{align-items:center}
    .grid{display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(280px,1fr))}
    .pad{padding:16px}
    .title{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:12px}
    .title h2{margin:0;font-size:1.02rem}
    .muted{color:var(--muted)}
    .pill,.chip{
      display:inline-flex;align-items:center;gap:8px;padding:8px 12px;border-radius:999px;
      border:1px solid var(--line-2);background:rgba(255,255,255,.03);font-weight:700;font-size:.92rem;
    }
    .pill[data-tone="good"],.chip[data-tone="good"]{border-color:rgba(46,211,155,.42);color:var(--good)}
    .pill[data-tone="warn"],.chip[data-tone="warn"]{border-color:rgba(242,189,93,.42);color:var(--warn)}
    .pill[data-tone="bad"],.chip[data-tone="bad"]{border-color:rgba(255,108,125,.42);color:var(--bad)}
    .btn{
      appearance:none;border:1px solid var(--line-2);background:linear-gradient(180deg,var(--surface-2),var(--surface));
      color:var(--text);padding:11px 14px;border-radius:14px;font:inherit;font-weight:700;cursor:pointer;
      min-height:44px;transition:transform .12s ease,border-color .12s ease,filter .12s ease,background .12s ease;
    }
    .btn:hover{filter:brightness(1.08);border-color:var(--accent)}
    .btn:active{transform:translateY(1px)}
    .btn.active{background:linear-gradient(180deg,var(--accent),var(--accent-2));border-color:transparent;color:#fff}
    .btn.ghost{background:transparent}
    .btn.link{display:inline-flex;align-items:center;justify-content:center}
    .btn.small{min-height:36px;padding:8px 12px;border-radius:11px;font-size:.88rem}
    .surface{
      background:linear-gradient(180deg,rgba(255,255,255,.02),rgba(255,255,255,.01));
      border:1px solid var(--line);border-radius:16px;padding:12px;
    }
    .table{width:100%;border-collapse:collapse;font-size:.94rem}
    .table td{padding:10px 0;border-bottom:1px solid var(--line);vertical-align:top}
    .table tr:last-child td{border-bottom:none}
    .table td:last-child{text-align:right;font-weight:700;word-break:break-word}
    .activity{
      min-height:48px;padding:12px 14px;border-radius:14px;border:1px solid var(--line);
      background:linear-gradient(180deg,var(--surface-2),var(--surface));display:flex;align-items:center;justify-content:space-between;gap:10px
    }
    .activity strong{display:block;margin-bottom:2px}
    .log{
      margin:0;max-height:320px;overflow:auto;white-space:pre-wrap;word-break:break-word;
      font:600 .84rem/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;color:var(--text)
    }
    .dot{width:10px;height:10px;border-radius:50%;background:var(--muted);box-shadow:0 0 0 4px rgba(255,255,255,.03)}
    .dot[data-tone="good"]{background:var(--good)}
    .dot[data-tone="warn"]{background:var(--warn)}
    .dot[data-tone="bad"]{background:var(--bad)}
    .themes{justify-content:flex-end}
    @media (max-width:760px){
      body{padding:12px}
      .hero,.pad{padding:14px}
      .hero-top,.hero-actions,.title{align-items:flex-start}
      .themes{justify-content:flex-start}
      .table td:last-child{text-align:left}
    }
  </style>
</head>
<body data-theme="dark">
  <div class="app">
    <section class="card hero">
      <div class="hero-top">
        <div>
          <div class="eyebrow">Enkelvoud / Control</div>
          <h1>Receiver control panel</h1>
          <p class="sub">Switch inputs, send transport commands, and monitor bridge, Wi-Fi, and websocket health from the ESP32 web UI.</p>
        </div>
        <div class="themes">
          <button class="btn small ghost" type="button" data-theme-btn="dark">Dark</button>
          <button class="btn small ghost" type="button" data-theme-btn="blue">Blue</button>
          <button class="btn small ghost" type="button" data-theme-btn="light">Light</button>
        </div>
      </div>

      <div class="hero-actions">
        <div class="chips">
          <span id="srcPill" class="pill">Source: --</span>
          <span id="wifiPill" class="pill">Wi-Fi: --</span>
          <span id="bridgePill" class="pill">Bridge: --</span>
        </div>
        <div class="toolbar">
          <button id="refreshBtn" class="btn" type="button">Refresh</button>
          <a class="btn link ghost" href="/player">Open /player</a>
        </div>
      </div>
    </section>

    <div class="grid">
      <section class="card pad">
        <div class="title">
          <h2>Source selection</h2>
          <span class="muted">Send <code>/api/source</code></span>
        </div>
        <div class="button-row">
          <button class="btn" type="button" data-source="BT">Bluetooth</button>
          <button class="btn" type="button" data-source="AUX">AUX</button>
          <button class="btn" type="button" data-source="USB">USB</button>
        </div>
      </section>

      <section class="card pad">
        <div class="title">
          <h2>Transport commands</h2>
          <span class="muted">Send <code>/api/cmd</code></span>
        </div>
        <div class="button-row">
          <button class="btn" type="button" data-cmd="prev">Prev</button>
          <button class="btn" type="button" data-cmd="next">Next</button>
        </div>
      </section>
    </div>

    <section class="activity" id="activity">
      <div>
        <strong id="activityTitle">Ready</strong>
        <span id="activityText" class="muted">Waiting for status refresh.</span>
      </div>
      <div class="chip"><span class="dot" id="activityDot"></span><span id="updatedAt">Never</span></div>
    </section>

    <div class="grid">
      <section class="card pad">
        <div class="title">
          <h2>Network & bridge</h2>
          <span class="muted">Gracefully handles missing fields</span>
        </div>
        <div class="surface">
          <table class="table">
            <tr><td>Device IP</td><td id="ipValue">--</td></tr>
            <tr><td>Websocket URL</td><td id="wsUrlValue">--</td></tr>
            <tr><td>Wi-Fi RSSI</td><td id="wifiRssiValue">--</td></tr>
            <tr><td>Receiver host</td><td id="receiverHostValue">--</td></tr>
            <tr><td>Receiver port</td><td id="receiverPortValue">--</td></tr>
            <tr><td>Bridge HTTP</td><td id="bridgeHttpValue">--</td></tr>
            <tr><td>Last bridge message</td><td id="bridgeMsgValue">--</td></tr>
          </table>
        </div>
      </section>

      <section class="card pad">
        <div class="title">
          <h2>Live metrics</h2>
          <span class="muted">From <code>/api/status</code></span>
        </div>
        <div class="surface">
          <table class="table">
            <tr><td>WS clients</td><td id="wsClientsValue">--</td></tr>
            <tr><td>Average latency</td><td id="avgLatencyValue">--</td></tr>
            <tr><td>I2S bytes in</td><td id="i2sBytesValue">--</td></tr>
            <tr><td>I2S read errors</td><td id="i2sErrorsValue">--</td></tr>
            <tr><td>Raw dropped</td><td id="rawDroppedValue">--</td></tr>
            <tr><td>Opus encoded</td><td id="opusEncodedValue">--</td></tr>
            <tr><td>Opus dropped</td><td id="opusDroppedValue">--</td></tr>
            <tr><td>WS packets sent</td><td id="wsPacketsValue">--</td></tr>
            <tr><td>WS bytes sent</td><td id="wsBytesValue">--</td></tr>
          </table>
        </div>
      </section>
    </div>

    <section class="card pad">
      <div class="title">
        <h2>Status snapshot</h2>
        <span class="muted">Compact debug log</span>
      </div>
      <div class="surface">
        <pre id="statusLog" class="log">{}</pre>
      </div>
    </section>
  </div>

<script>
const els = {};
const state = { status:{}, theme:'dark' };
function $(id){ return els[id] || (els[id] = document.getElementById(id)); }
function valueOf(v){ return v === undefined || v === null || v === '' ? '--' : String(v); }
function numberOf(v,suffix){ return typeof v === 'number' && isFinite(v) ? String(v) + (suffix || '') : '--'; }
function stamp(){ return new Date().toLocaleTimeString([], {hour:'2-digit', minute:'2-digit', second:'2-digit'}); }
function pill(id, text, tone){ var el = $(id); el.textContent = text; el.setAttribute('data-tone', tone || ''); }
function setCell(id, text){ $(id).textContent = text; }
function setTheme(theme){
  state.theme = theme || 'dark';
  document.body.setAttribute('data-theme', state.theme);
  try{ localStorage.setItem('evdctrl-theme', state.theme); }catch(e){}
  Array.prototype.forEach.call(document.querySelectorAll('[data-theme-btn]'), function(btn){
    btn.classList.toggle('active', btn.getAttribute('data-theme-btn') === state.theme);
  });
}
function setActivity(title, text, tone){
  $('activityTitle').textContent = title;
  $('activityText').textContent = text;
  $('activityDot').setAttribute('data-tone', tone || '');
  $('updatedAt').textContent = stamp();
}
function render(status){
  state.status = status || {};
  var src = valueOf(status.source || status.bridgeLastSource);
  var wifi = status.wifiConnected === true ? 'Connected' : (status.wifiConnected === false ? 'Offline' : '--');
  var bridgeCode = typeof status.bridgeLastHttp === 'number' ? status.bridgeLastHttp : null;
  var bridgeTone = bridgeCode === null ? '' : (bridgeCode >= 200 && bridgeCode < 300 ? 'good' : (bridgeCode === 0 ? 'warn' : 'bad'));
  pill('srcPill', 'Source: ' + src, src === '--' ? '' : 'good');
  pill('wifiPill', 'Wi-Fi: ' + wifi, status.wifiConnected === true ? 'good' : (status.wifiConnected === false ? 'bad' : ''));
  pill('bridgePill', 'Bridge: ' + (bridgeCode === null ? '--' : String(bridgeCode)), bridgeTone);

  Array.prototype.forEach.call(document.querySelectorAll('[data-source]'), function(btn){
    btn.classList.toggle('active', btn.getAttribute('data-source') === src);
  });

  setCell('ipValue', valueOf(status.ip));
  setCell('wsUrlValue', valueOf(status.wsUrl));
  setCell('wifiRssiValue', typeof status.wifiRssi === 'number' ? String(status.wifiRssi) + ' dBm' : '--');
  setCell('receiverHostValue', valueOf(status.receiverHost));
  setCell('receiverPortValue', typeof status.receiverPort === 'number' ? String(status.receiverPort) : '--');
  setCell('bridgeHttpValue', bridgeCode === null ? '--' : String(bridgeCode));
  setCell('bridgeMsgValue', valueOf(status.bridgeLastMsg));
  setCell('wsClientsValue', typeof status.wsClients === 'number' ? String(status.wsClients) : '--');
  setCell('avgLatencyValue', typeof status.avgLatencyMs === 'number' ? String(status.avgLatencyMs) + ' ms' : '--');
  setCell('i2sBytesValue', typeof status.i2sBytesIn === 'number' ? String(status.i2sBytesIn) : '--');
  setCell('i2sErrorsValue', typeof status.i2sReadErrors === 'number' ? String(status.i2sReadErrors) : '--');
  setCell('rawDroppedValue', typeof status.rawDropped === 'number' ? String(status.rawDropped) : '--');
  setCell('opusEncodedValue', typeof status.opusEncoded === 'number' ? String(status.opusEncoded) : '--');
  setCell('opusDroppedValue', typeof status.opusDropped === 'number' ? String(status.opusDropped) : '--');
  setCell('wsPacketsValue', typeof status.wsPacketsSent === 'number' ? String(status.wsPacketsSent) : '--');
  setCell('wsBytesValue', typeof status.wsBytesSent === 'number' ? String(status.wsBytesSent) : '--');
  $('statusLog').textContent = JSON.stringify(status, null, 2);
}
async function postJson(url, payload){
  var r = await fetch(url, {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify(payload)
  });
  var text = await r.text();
  var data = {};
  try{ data = text ? JSON.parse(text) : {}; }catch(e){ data = { raw:text }; }
  if(!r.ok){
    var msg = data.error || data.message || data.raw || ('HTTP ' + r.status);
    throw new Error(msg);
  }
  return data;
}
async function refreshStatus(){
  $('refreshBtn').disabled = true;
  try{
    var r = await fetch('/api/status', { cache:'no-store' });
    if(!r.ok) throw new Error('HTTP ' + r.status);
    var data = await r.json();
    render(data);
    setActivity('Status refreshed', 'Latest device and bridge state loaded successfully.', 'good');
  }catch(e){
    setActivity('Refresh failed', String(e), 'bad');
  }finally{
    $('refreshBtn').disabled = false;
  }
}
async function setSource(source){
  setActivity('Switching source', 'Requesting ' + source + ' from the bridge.', 'warn');
  try{
    await postJson('/api/source', { source:source });
    await refreshStatus();
    setActivity('Source updated', 'Source request for ' + source + ' completed.', 'good');
  }catch(e){
    setActivity('Source update failed', String(e), 'bad');
  }
}
async function sendCmd(cmd){
  setActivity('Sending command', 'Dispatching "' + cmd + '" to the receiver bridge.', 'warn');
  try{
    await postJson('/api/cmd', { cmd:cmd });
    await refreshStatus();
    setActivity('Command sent', '"' + cmd + '" finished successfully.', 'good');
  }catch(e){
    setActivity('Command failed', String(e), 'bad');
  }
}
document.getElementById('refreshBtn').addEventListener('click', refreshStatus);
Array.prototype.forEach.call(document.querySelectorAll('[data-source]'), function(btn){
  btn.addEventListener('click', function(){ setSource(btn.getAttribute('data-source')); });
});
Array.prototype.forEach.call(document.querySelectorAll('[data-cmd]'), function(btn){
  btn.addEventListener('click', function(){ sendCmd(btn.getAttribute('data-cmd')); });
});
Array.prototype.forEach.call(document.querySelectorAll('[data-theme-btn]'), function(btn){
  btn.addEventListener('click', function(){ setTheme(btn.getAttribute('data-theme-btn')); });
});
try{ setTheme(localStorage.getItem('evdctrl-theme') || 'dark'); }catch(e){ setTheme('dark'); }
render({});
refreshStatus();
setInterval(refreshStatus, 4000);
</script>
</body>
</html>
)HTML";
