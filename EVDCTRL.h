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
    body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#101218;color:#e8eefc;margin:0;padding:20px}
    .card{max-width:760px;margin:0 auto;background:#171b26;border:1px solid #2a3142;border-radius:14px;padding:18px}
    h1{margin:0 0 6px;font-size:1.3rem}
    .muted{color:#a9b5d1;font-size:.95rem}
    .row{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}
    button{padding:11px 14px;border-radius:10px;border:1px solid #3f4c68;background:#20283a;color:#fff;font-weight:700;cursor:pointer}
    button:hover{filter:brightness(1.08)}
    .pill{display:inline-block;padding:5px 10px;border-radius:999px;background:#2e3952;border:1px solid #4a5e8b}
    pre{background:#0f131d;border:1px solid #2a3142;padding:10px;border-radius:8px;overflow:auto}
    a{color:#8db7ff}
  </style>
</head>
<body>
  <div class="card">
    <h1>EVDCTRL</h1>
    <div class="muted">Source control for upstream ESP32 receiver.</div>

    <p>Current Source: <span id="src" class="pill">...</span></p>

    <div class="row">
      <button onclick="setSource('BT')">Bluetooth</button>
      <button onclick="setSource('AUX')">AUX</button>
      <button onclick="setSource('USB')">USB</button>
    </div>

    <div class="row">
      <button onclick="sendCmd('prev')">Prev</button>
      <button onclick="sendCmd('next')">Next</button>
    </div>

    <div class="row">
      <button onclick="refreshStatus()">Refresh Status</button>
      <a href="/player">Open EVDPLR Player Page</a>
    </div>

    <h3>API Status</h3>
    <pre id="status">{}</pre>
  </div>

<script>
async function refreshStatus(){
  try{
    const r = await fetch('/api/status');
    const j = await r.json();
    document.getElementById('src').textContent = j.source || '?';
    document.getElementById('status').textContent = JSON.stringify(j,null,2);
  }catch(e){
    document.getElementById('status').textContent = 'status error: '+e;
  }
}

async function setSource(source){
  try{
    const r = await fetch('/api/source',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({source})
    });
    const j = await r.json();
    if(!j.ok) alert('Set source failed: '+JSON.stringify(j));
    await refreshStatus();
  }catch(e){ alert(e); }
}

async function sendCmd(cmd){
  try{
    const r = await fetch('/api/cmd',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({cmd})
    });
    const j = await r.json();
    if(!j.ok) alert('Command failed: '+JSON.stringify(j));
    await refreshStatus();
  }catch(e){ alert(e); }
}

refreshStatus();
setInterval(refreshStatus, 4000);
</script>
</body>
</html>
)HTML";