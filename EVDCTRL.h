#pragma once
/*
  ============================================================================
  EVDCTRL.h — Enkelvoud Server "Control" web page
  ============================================================================
  Owns the markup/JS for the main control page (served at HTTP GET "/") and
  the function that wires that page up to the control-plane routes.

  The page's JS talks to the JSON API already implemented in
  EnkelvoudServer.ino:
    GET  /api/status         -> full state snapshot (volume, mute, stats...)
    POST /api/config         -> {masterVolume|masterMuted|latencyAdjustmentMs|
                                  audioBufferMs|serverName}
    POST /api/source         -> {source}            (bridged to receiver)
    POST /api/cmd            -> {cmd: next|prev|bt|aux|usb} (bridged to receiver)
    POST /api/restart        -> restarts the ESP32-S3
    GET  /settings, POST /save, GET /api/network-test -> Wi-Fi setup flow
        (registered separately in EnkelvoudServer.ino; unchanged by this file)

  This header does not duplicate that handler logic — it only forward-
  declares the handlers (defined in EnkelvoudServer.ino) and registers the
  routes that back the control page, via evdctrlRegisterRoutes().
  ============================================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// ----------------------------------------------------------------------------
// Handlers implemented in EnkelvoudServer.ino (definitions may appear later
// in the same translation unit; forward declarations are all that's needed
// here).
// ----------------------------------------------------------------------------
void handleNetworkSettings(AsyncWebServerRequest *request);
void handleApiStatus(AsyncWebServerRequest *request);
void handleRestart(AsyncWebServerRequest *request);
void handleApiConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleApiSourceBridge(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleApiCmdBridge(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

// ----------------------------------------------------------------------------
// Control page markup (adapted from template.html)
// ----------------------------------------------------------------------------
static const char EVDCTRL_HTML[] PROGMEM = R"HTMLPAGE(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Enkelvoud Server</title>
<style>
:root {
  --bg: #050507;
  --panel: #111116;
  --panel2: #1a1a21;
  --line: #32323c;
  --text: #f7f7fa;
  --muted: #aaaab6;
  --accent: #a982ff;
  --accenttext: #180d2e;
  --good: #37dba0;
  --danger: #ff7188
}
body[data-theme="white"] {
  --bg: #edf0f5;
  --panel: #fff;
  --panel2: #f7f8fb;
  --line: #cbd1dc;
  --text: #171923;
  --muted: #606878;
  --accent: #6740ce;
  --accenttext: #fff
}
body[data-theme="blue"] {
  --bg: #07111f;
  --panel: #0e1b2c;
  --panel2: #142842;
  --line: #294968;
  --accent: #69b5ff;
  --accenttext: #041627
}
* {
  box-sizing: border-box
}
body {
  margin: 0;
  background: radial-gradient(circle at top right, #291b50, transparent 34rem), var(--bg);
  color: var(--text);
  font: 15px system-ui, sans-serif
}
.app {
  max-width: 1080px;
  margin: auto;
  padding: 24px;
  display: grid;
  gap: 18px
}
.card {
  background: linear-gradient(145deg, var(--panel2), var(--panel));
  border: 1px solid var(--line);
  border-radius: 18px;
  padding: 22px;
  box-shadow: 0 18px 42px #0005
}
.top {
  display: grid;
  gap: 22px
}
.head, .row {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap
}
.head {
  justify-content: space-between
}
.brand {
  display: flex;
  align-items: center;
  gap: 12px
}
.dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: var(--good);
  box-shadow: 0 0 14px var(--good)
}
h1, summary, b {
  user-select: none
}
h1 {
  font-size: clamp(1.5rem, 5vw, 2.35rem);
  letter-spacing: -0.05em;
  margin: 0;
  cursor: pointer
}
h1:hover {
  color: var(--accent)
}
.btn, select, input {
  font: inherit;
  color: var(--text);
  border: 1px solid var(--line);
  background: var(--bg);
  border-radius: 11px;
  padding: 10px 12px
}
.btn {
  font-weight: 750;
  cursor: pointer
}
.btn:hover {
  border-color: var(--accent)
}
.btn.primary {
  background: var(--accent);
  color: var(--accenttext);
  border-color: var(--accent)
}
.btn.active {
  background: #553889;
  border-color: var(--accent)
}
.btn.danger {
  color: var(--danger)
}
select {
  min-width: 145px
}
.range {
  appearance: none;
  width: 100%;
  height: 8px;
  border: 0;
  border-radius: 99px;
  padding: 0;
  background: linear-gradient(90deg, var(--accent) 0%, var(--accent) var(--p, 0%), #3b3b46 var(--p, 0%), #3b3b46 100%);
  touch-action: none;
  cursor: pointer;
  --p: 0%
}
.range::-webkit-slider-thumb {
  appearance: none;
  width: 21px;
  height: 21px;
  border-radius: 50%;
  background: #fff;
  border: 3px solid var(--accent);
  cursor: grab
}
.range:active::-webkit-slider-thumb {
  cursor: grabbing
}
.range::-moz-range-thumb {
  width: 15px;
  height: 15px;
  border-radius: 50%;
  background: #fff;
  border: 3px solid var(--accent);
  cursor: grab
}
.range-row {
  display: grid;
  grid-template-columns: 1fr auto;
  gap: 14px;
  align-items: center
}
.range-row .range {
  grid-column: 1/-1
}
.section {
  padding: 0;
  overflow: hidden
}
.section summary {
  padding: 20px 22px;
  cursor: pointer;
  font-size: 1.05rem;
  font-weight: 800;
  list-style: none
}
.section summary::-webkit-details-marker {
  display: none
}
.section summary:after {
  content: '+';
  float: right;
  color: var(--accent);
  font-size: 1.4rem
}
.section[open] summary:after {
  content: '−'
}
.section-body {
  border-top: 1px solid var(--line);
  padding: 22px;
  display: grid;
  gap: 22px
}
.room {
  border: 1px dashed #4d4d5a;
  border-radius: 13px;
  padding: 16px;
  margin-top: 14px
}
.room-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px
}
.muted {
  color: var(--muted)
}
.stat {
  display: grid;
  grid-template-columns: 1fr auto;
  padding: 11px 0;
  border-bottom: 1px solid var(--line)
}
.stat:last-child {
  border: 0
}
.modal {
  position: fixed;
  inset: 0;
  background: #000a;
  display: grid;
  place-items: center;
  padding: 20px
}
.modal[hidden] {
  display: none
}
.dialog {
  width: min(100%, 400px);
  padding: 22px;
  background: var(--panel2);
  border: 1px solid var(--line);
  border-radius: 18px
}
.dialog input {
  width: 100%;
  margin: 10px 0 16px
}
.network {
  display: grid;
  gap: 16px
}
.network label {
  font-weight: 700;
  cursor: text
}
.network input {
  display: block;
  width: 100%;
  margin-top: 8px
}
.check {
  display: flex;
  align-items: center;
  gap: 9px
}
.check input {
  width: auto;
  margin: 0
}
.hidden {
  display: none
}
@media(max-width: 560px) {
  .app {
    padding: 12px;
    gap: 12px
  }
  .card {
    padding: 16px
  }
  .head {
    align-items: flex-start
  }
  .head > .row {
    width: 100%
  }
  .head > .row > * {
    flex: 1
  }
  .section summary {
    padding: 16px
  }
  .section-body {
    padding: 16px
  }
}
</style>
</head>
<body>
<main class="app">
<section class="card top">
<div class="head">
<div class="brand">
<i class="dot"></i>
<h1 id="name" title="Rename server">Enkelvoud Server</h1>
</div>
<div class="row">
<select id="source">
<option value="BT">Bluetooth</option>
</select>
<select id="theme">
<option value="black">Black</option>
<option value="white">White</option>
<option value="blue">Blue</option>
</select>
<button class="btn" id="mute">Mute</button>
<button class="btn danger" id="restart">Restart</button>
<a class="btn" href="/player">Player</a>
</div>
</div>
<div class="range-row">
<b>Master volume</b>
<b id="volume-label">100%</b>
<input class="range" id="volume" type="range" min="0" max="100" value="100">
</div>
</section>

<section class="card">
<div class="row">
<b>Playback controls</b>
<span class="muted">Previous / Next on the upstream receiver</span>
<button class="btn" data-cmd="prev">Previous</button>
<button class="btn" data-cmd="next">Next</button>
</div>
</section>

<details class="card section" open>
<summary>Rooms</summary>
<div class="section-body">
<div class="row">
<span class="muted">Players initially join Master Room.</span>
<button class="btn primary" id="add-room">+ New Group</button>
</div>
<div id="rooms"></div>
</div>
</details>

<details class="card section">
<summary>Settings</summary>
<div class="section-body">
<div class="range-row">
<b>Latency adjustment</b>
<b id="latency-label">0 ms</b>
<input class="range" id="latency" type="range" min="0" max="200" value="0">
</div>
<div class="range-row">
<b>Audio buffer</b>
<b id="buffer-label">0 ms</b>
<input class="range" id="buffer" type="range" min="0" max="200" value="0">
</div>
<form class="network" id="network">
<b>Network</b>
<label>Server name<input id="network-name" name="server_name" pattern="[A-Za-z0-9]+" required></label>
<label>Wi-Fi SSID<input id="ssid" name="ssid" required></label>
<label>Wi-Fi password<input id="password" name="password" type="password"></label>
<label class="check">
<input id="static" name="use_static_ip" type="checkbox"> Use static IP
</label>
<div id="static-fields" class="hidden">
<label>IP address<input id="ip" name="ip"></label>
<label>Gateway<input id="gateway" name="gateway"></label>
<label>Subnet mask<input id="subnet" name="subnet" value="255.255.255.0"></label>
<label>DNS server<input id="dns" name="dns"></label>
</div>
<button class="btn primary">Save</button>
</form>
</div>
</details>

<details class="card section">
<summary>Server status</summary>
<div class="section-body">
<div id="stats"></div>
</div>
</details>
</main>

<div class="modal" id="modal" hidden>
<div class="dialog" id="dialog"></div>
</div>

<script>
const $ = x => document.getElementById(x);
const rooms = $('rooms');
let state = {};
let groups = [{name: 'Master Room', muted: false, volume: 100}];
let editingInputs = new Set();

function esc(s) {
  return String(s ?? '').replace(/[&<>"']/g, c => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;'
  }[c]));
}

function modal(x) {
  $('dialog').innerHTML = x;
  $('modal').hidden = false;
}

function closeModal() {
  $('modal').hidden = true;
}

function paintSlider(e, v) {
  let fill = ((Number(v) - Number(e.min)) / (Number(e.max) - Number(e.min)) * 100);
  e.style.setProperty('--p', fill + '%');
  e.style.background = 'linear-gradient(90deg, var(--accent) 0%, var(--accent) ' + fill + '%, #3b3b46 ' + fill + '%, #3b3b46 100%)';
}

function slider(id, v) {
  let e = $(id);
  e.value = v;
  paintSlider(e, v);
}

async function api(url, data) {
  let r = await fetch(url, {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(data)
  });
  let j = await r.json();
  if (!r.ok) throw Error(j.error || 'Request failed');
  return j;
}

function renderRooms() {
  let nodes = (state.players || []).map(n => 
    `<div class="room"><div class="room-head"><b>${esc(n.name)}</b><a href="http://${esc(n.ip)}/" target="_blank">${esc(n.ip)}</a></div></div>`
  ).join('');
  
  rooms.innerHTML = groups.map((g, i) => 
    `<div class="room">
      <div class="room-head">
        <b>${esc(g.name)}</b>
        <button class="btn ${g.muted ? 'active' : ''}" data-m="${i}">${g.muted ? 'Muted' : 'Mute'}</button>
      </div>
      <div class="range-row">
        <span class="muted">Room volume</span>
        <b>${g.volume}%</b>
        <input class="range" data-v="${i}" type="range" min="0" max="100" value="${g.volume}">
      </div>
      ${i === 0 ? nodes : ''}
    </div>`
  ).join('');
  
  document.querySelectorAll('[data-m]').forEach(x => 
    x.onclick = () => {
      groups[x.dataset.m].muted = !groups[x.dataset.m].muted;
      renderRooms();
    }
  );
  
  document.querySelectorAll('[data-v]').forEach(x => {
    paintSlider(x, x.value);
    x.oninput = () => {
      let v = x.value;
      groups[x.dataset.v].volume = v;
      paintSlider(x, v);
      x.previousElementSibling.textContent = v + '%';
    };
    x.onchange = renderRooms;
  });
  
  document.querySelectorAll('.range').forEach(x => paintSlider(x, x.value));
}

function setIfInactive(id, value, label, suffix) {
  if (document.activeElement !== $(id) && !editingInputs.has(id)) {
    slider(id, value);
    $(label).textContent = value + suffix;
  }
}

function setInputIfInactive(id, value) {
  if (document.activeElement !== $(id) && !editingInputs.has(id)) {
    $(id).value = value;
  }
}

function render() {
  console.log('Rendering state:', state);
  
  let n = state.serverName || 'Enkelvoud';
  let stream = state.streaming || {};
  
  $('name').textContent = n + ' Server';
  document.title = n + ' Server';
  
  setInputIfInactive('network-name', n);
  setInputIfInactive('ssid', state.wifiSsid || '');
  setInputIfInactive('password', state.wifiPassword || '');
  $('static').checked = !!state.wifiUseStaticIp;
  $('static-fields').classList.toggle('hidden', !state.wifiUseStaticIp);
  setInputIfInactive('ip', state.wifiStaticIp || '');
  setInputIfInactive('gateway', state.wifiStaticGateway || '');
  setInputIfInactive('subnet', state.wifiStaticSubnet || '255.255.255.0');
  setInputIfInactive('dns', state.wifiStaticDns || '');
  
  $('mute').textContent = state.masterMuted ? 'Unmute' : 'Mute';
  $('mute').classList.toggle('active', !!state.masterMuted);
  $('source').value = 'BT';
  
  setIfInactive('volume', state.masterVolume ?? 100, 'volume-label', '%');
  setIfInactive('latency', state.latencyAdjustmentMs ?? 0, 'latency-label', ' ms');
  setIfInactive('buffer', state.audioBufferMs ?? 0, 'buffer-label', ' ms');
  
  $('stats').innerHTML = [
    ['Address', state.ip],
    ['Wi-Fi', state.wifiConnected ? 'Connected' : 'AP mode'],
    ['Streaming input', stream.i2sBytesLast5s > 0 ? 'Receiving I2S audio' : 'No I2S audio received'],
    ['I2S received (last 5 s)', stream.i2sBytesLast5s ?? '--'],
    ['I2S received total', stream.i2sBytesTotal ?? '--'],
    ['Raw chunks dropped (last 5 s)', stream.rawDroppedLast5s ?? '--'],
    ['Opus frames encoded (last 5 s)', stream.opusEncodedLast5s ?? '--'],
    ['Opus frames dropped (last 5 s)', stream.opusDroppedLast5s ?? '--'],
    ['WebSocket clients', state.wsClients],
    ['WebSocket packets (last 5 s)', stream.wsPacketsLast5s ?? '--'],
    ['WebSocket bytes (last 5 s)', stream.wsBytesLast5s ?? '--'],
    ['Audio latency', state.avgLatencyMs + ' ms']
  ].map(x => `<div class="stat"><span>${x[0]}</span><b>${esc(x[1])}</b></div>`).join('');
  
  if (document.activeElement?.classList.contains('range') === false) {
    renderRooms();
  }
}

async function refresh() {
  try {
    const response = await fetch('/api/status');
    if (!response.ok) {
      console.error('API status request failed:', response.status);
      return;
    }
    state = await response.json();
    render();
    document.querySelectorAll('.range').forEach(x => paintSlider(x, x.value));
  } catch (e) {
    console.error('Error fetching status:', e);
  }
}

function config(id, key, label, suffix) {
  $(id).oninput = () => {
    slider(id, $(id).value);
    $(label).textContent = $(id).value + suffix;
  };
  $(id).onchange = () => api('/api/config', {[key]: +$(id).value});
}

$('source').onchange = () => 
  api('/api/source', {source: $('source').value}).then(refresh);

$('theme').onchange = () => {
  document.body.dataset.theme = $('theme').value === 'black' ? '' : $('theme').value;
  localStorage.setItem('theme', $('theme').value);
};
$('theme').value = localStorage.getItem('theme') || 'black';
$('theme').dispatchEvent(new Event('change'));

$('mute').onclick = () => {
  const newMutedState = !state.masterMuted;
  $('mute').textContent = newMutedState ? 'Unmute' : 'Mute';
  $('mute').classList.toggle('active', newMutedState);
  
  api('/api/config', {masterMuted: newMutedState})
    .then(() => {
      state.masterMuted = newMutedState;
      refresh();
    })
    .catch(e => {
      $('mute').textContent = state.masterMuted ? 'Unmute' : 'Mute';
      $('mute').classList.toggle('active', state.masterMuted);
      alert('Failed to toggle mute: ' + e.message);
    });
};

$('restart').onclick = () => {
  if (confirm('Restart the server?')) {
    fetch('/api/restart', {method: 'POST'})
      .then(response => {
        if (response.ok) {
          alert('Server is restarting...');
        } else {
          alert('Failed to restart: Server returned error');
        }
      })
      .catch(e => alert('Failed to restart: ' + e.message));
  }
};

document.querySelectorAll('[data-cmd]').forEach(x => {
  x.onclick = () => {
    const cmd = x.dataset.cmd;
    api('/api/cmd', {cmd: cmd})
      .then(() => {
        console.log('Command sent successfully:', cmd);
        refresh();
      })
      .catch(e => {
        console.error('Command failed:', cmd, e);
        alert('Command failed: ' + e.message);
      });
  };
});

config('volume', 'masterVolume', 'volume-label', '%');
config('latency', 'latencyAdjustmentMs', 'latency-label', ' ms');
config('buffer', 'audioBufferMs', 'buffer-label', ' ms');

$('name').onclick = () => 
  modal(`<h2>Rename server</h2>
    <input id="rename" value="${esc(state.serverName || 'Enkelvoud')}">
    <button class="btn primary" id="rename-save">Save & restart</button>
    <button class="btn" onclick="closeModal()">Cancel</button>`);

document.addEventListener('click', e => {
  if (e.target.id === 'rename-save') {
    let n = $('rename').value;
    if (!/^[A-Za-z0-9]+$/.test(n)) return alert('Use letters and numbers only.');
    api('/api/config', {serverName: n})
      .then(() => fetch('/api/restart', {method: 'POST'}))
      .catch(err => alert('Failed to rename: ' + err.message));
  }
});

$('add-room').onclick = () => {
  let n = prompt('Group / room name');
  if (n) {
    groups.push({name: n, muted: false, volume: 100});
    renderRooms();
  }
};

$('static').onchange = () => 
  $('static-fields').classList.toggle('hidden', !$('static').checked);

['network-name', 'ssid', 'password', 'ip', 'gateway', 'subnet', 'dns'].forEach(id => {
  let el = $(id);
  el.onfocus = () => editingInputs.add(id);
  el.onblur = () => editingInputs.delete(id);
  el.oninput = () => editingInputs.add(id);
});

$('ip').oninput = () => {
  let p = $('ip').value.split('.');
  if (p.length === 4) {
    let g = p.slice(0, 3).join('.') + '.1';
    $('gateway').value = g;
    $('dns').value = g;
    $('subnet').value = '255.255.255.0';
  }
};

$('network').onsubmit = async e => {
  e.preventDefault();
  modal('<div class="spinner"></div><h2>Testing connection</h2><p>Checking your network settings...</p>');
  try {
    let r = await fetch('/save', {
      method: 'POST',
      body: new URLSearchParams(new FormData(e.target))
    });
    let j = await r.json();
    if (!r.ok) throw Error(j.error);
    
    let s;
    do {
      await new Promise(x => setTimeout(x, 500));
      s = await (await fetch('/api/network-test')).json();
    } while (s.state === 'running');
    
    if (s.state !== 'success') throw Error(s.error);
    
    $('dialog').innerHTML = '<h2>Success</h2><p>Connected to ' + esc(s.ip) + '. Restarting…</p>';
    await fetch('/api/restart', {method: 'POST'});
    setTimeout(() => location.href = 'http://' + $('network-name').value + '.local/', 3000);
  } catch (x) {
    $('dialog').innerHTML = '<h2>Unable to connect</h2><p>' + esc(x.message) + '</p><button class="btn primary" id="close-modal">Close</button>';
  }
};

document.addEventListener('click', e => {
  if (e.target.id === 'close-modal') $('modal').hidden = true;
});

refresh();
setInterval(refresh, 4000);
</script>
</body>
</html>)HTMLPAGE";

// ----------------------------------------------------------------------------
// Route registration — call once from setup_websocket_server().
// ----------------------------------------------------------------------------
/**
 * Registers the "/" control page and the control-plane API routes it drives
 * (status, config, source, cmd, restart) on the given AsyncWebServer.
 *
 * Falls back to the captive Wi-Fi setup page (handleNetworkSettings) at "/"
 * whenever the device isn't connected to a router yet, same as before.
 */
inline void evdctrlRegisterRoutes(AsyncWebServer &srv) {
  srv.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (WiFi.status() != WL_CONNECTED) {
      handleNetworkSettings(request);
      return;
    }
    request->send(200, "text/html; charset=utf-8", EVDCTRL_HTML);
  });

  srv.on("/api/status", HTTP_GET, handleApiStatus);

  srv.on("/api/config", HTTP_POST,
         [](AsyncWebServerRequest *request) {},
         nullptr,
         handleApiConfig);

  srv.on("/api/source", HTTP_POST,
         [](AsyncWebServerRequest *request) {},
         nullptr,
         handleApiSourceBridge);

  srv.on("/api/cmd", HTTP_POST,
         [](AsyncWebServerRequest *request) {},
         nullptr,
         handleApiCmdBridge);

  srv.on("/api/restart", HTTP_POST, handleRestart);
}
