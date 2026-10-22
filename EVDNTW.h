#pragma once

static const char EVDNTW_HTML_TEMPLATE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name='viewport' content='width=device-width,initial-scale=1'>
  <title>Enkelvoud Network Setup</title>
  <style>
    :root {
      --bg: #050507; --panel: #111116; --panel2: #1a1a21; --line: #32323c; 
      --text: #f6f6f8; --muted: #a1a1ad; --accent: #9d7bff; --accenttext: #140b2c; --good: #37dba0;
    }
    body {
      font: 15px system-ui, sans-serif; margin: 0; padding: 24px; color: var(--text);
      background: radial-gradient(circle at top right, #2a1b52, transparent 28rem), var(--bg);
    }
    form {
      max-width: 34rem; margin: 5vh auto; padding: 26px; border: 1px solid var(--line);
      border-radius: 20px; background: linear-gradient(145deg, var(--panel2), var(--panel)); box-shadow: 0 24px 60px #0008;
    }
    h1, p { max-width: 34rem; margin: 0 auto; }
    h1 { font-size: 2rem; letter-spacing: -.05em; }
    p { margin-top: 10px; color: var(--muted); line-height: 1.5; }
    label, input, button { display: block; width: 100%; box-sizing: border-box; }
    label { margin-top: 1rem; font-weight: 700; }
    input, button { padding: 0.8rem; font-size: 1rem; border-radius: 11px; }
    input { margin-top: 7px; color: var(--text); background: #09090c; border: 1px solid #343440; }
    input:focus { outline: none; border-color: var(--accent); }
    button { margin-top: 1.5rem; border: 0; background: var(--accent); color: var(--accenttext); font-weight: 800; cursor: pointer; }
    button:hover { opacity: 0.9; }
    small { color: var(--muted); display: block; margin-top: 6px; }
    .checkbox { display: flex; gap: 0.6rem; align-items: center; margin-top: 1rem; }
    .checkbox input { width: auto; margin: 0; }
    .hidden { display: none !important; }

    /* SSID row with inline Scan button */
    .ssid-row { display: flex; gap: 8px; align-items: stretch; margin-top: 7px; }
    .ssid-row input { margin-top: 0; flex: 1 1 auto; min-width: 0; }
    .scan-btn {
      margin-top: 0; flex: 0 0 auto; width: auto; padding: 0 18px;
      background: var(--panel2); color: var(--text);
      border: 1px solid var(--line); font-weight: 700;
    }
    .scan-btn:disabled { opacity: 0.6; cursor: default; }

    /* Wi-Fi picker modal */
    .modal-overlay {
      position: fixed; inset: 0; z-index: 50;
      display: flex; align-items: center; justify-content: center;
      background: rgba(0,0,0,0.7); padding: 20px;
    }
    .modal-box {
      width: 100%; max-width: 32rem; max-height: 80vh;
      background: var(--panel); border: 1px solid var(--line);
      border-radius: 18px; overflow: hidden; display: flex; flex-direction: column;
      box-shadow: 0 24px 60px #000a;
    }
    .modal-header {
      display: flex; justify-content: space-between; align-items: center;
      padding: 16px 20px; border-bottom: 1px solid var(--line);
    }
    .modal-close {
      margin: 0; padding: 6px 14px; width: auto;
      background: var(--panel2); color: var(--text);
      border: 1px solid var(--line); font-weight: 700;
    }
    #wifi-list { overflow-y: auto; padding: 4px 0; }
    .net {
      display: flex; justify-content: space-between; align-items: center;
      gap: 12px; width: 100%; text-align: left;
      padding: 14px 20px; margin: 0; border-radius: 0;
      background: none; border: 0; border-bottom: 1px solid var(--line);
      color: var(--text); font: inherit; font-weight: 500; cursor: pointer;
    }
    .net:last-child { border-bottom: 0; }
    .net:hover { background: var(--panel2); }
    .net .rssi { color: var(--muted); font-size: 0.82rem; flex: 0 0 auto; }
    .scan-status { padding: 18px 20px; color: var(--muted); text-align: center; }
  </style>
</head>
<body>

  <h1>Enkelvoud Network Setup</h1>
  <p>Connect the server to your Wi-Fi network. The access point is available at 192.168.4.1 while setup is required.</p>
  
  <form id="network-settings" onsubmit="return false;">
    
    <label for="server-name">Server name</label>
    <input id="server-name" name="server_name"
           pattern="[A-Za-z0-9]+" required maxlength="32"
           autocomplete="off" autocapitalize="off" spellcheck="false"
           value="{{SERVER_NAME}}"
           onbeforeinput="if (event.data && /[^A-Za-z0-9]/.test(event.data)) event.preventDefault()">
    <small>Letters and numbers only; this becomes the <strong>.local</strong> address.</small>
    
    <label for="ssid">Wi-Fi SSID</label>
    <div class="ssid-row">
      <input id="ssid" name="ssid" required value="{{WIFI_SSID}}">
      <button id="scan-button" type="button" class="scan-btn">Scan</button>
    </div>
    
    <label for="password">Wi-Fi password</label>
    <input id="password" name="password" type="password" value="{{WIFI_PASS}}">
    
    <label class="checkbox">
      <input id="static-ip" name="use_static_ip" type="checkbox" onchange="document.getElementById('static-settings').className=this.checked ? '' : 'hidden'" {{STATIC_CHECKED}}> 
      Use a static IP for this Wi-Fi network
    </label>
    
    <div id="static-settings" class="{{STATIC_CLASS}}">
      <label for="ip">IP address</label>
      <input id="ip" name="ip" value="{{STATIC_IP}}">
      <label for="gateway">Gateway</label>
      <input id="gateway" name="gateway" value="{{STATIC_GW}}">
      <label for="subnet">Subnet mask</label>
      <input id="subnet" name="subnet" value="{{STATIC_SUBNET}}">
      <label for="dns">DNS server</label>
      <input id="dns" name="dns" value="{{STATIC_DNS}}">
    </div>
    
    <button id="save-button" type="button">Save</button>
  </form>

  <!-- Wi-Fi picker -->
  <div id="wifi-modal" class="modal-overlay hidden" role="dialog" aria-modal="true" aria-label="Available networks">
    <div class="modal-box">
      <div class="modal-header">
        <strong>Available networks</strong>
        <button id="modal-close" type="button" class="modal-close">Close</button>
      </div>
      <div id="wifi-list"><div class="scan-status">Scanning…</div></div>
    </div>
  </div>

  <script>
    const form = document.getElementById('network-settings');
    const button = document.getElementById('save-button');

    function suggest() {
      const p = document.getElementById('ip').value.trim().split('.');
      if (p.length !== 4 || p.some(v => !/^\d+$/.test(v) || +v > 255)) return;
      const base = p.slice(0, 3).join('.') + '.1';
      document.getElementById('gateway').value = base;
      document.getElementById('dns').value = base;
      document.getElementById('subnet').value = '255.255.255.0';
    }
    
    document.getElementById('ip').addEventListener('input', suggest);
    
    // On load, if the IP field was populated from saved settings but the
    // auxiliary fields are blank, fill them from the IP's /24. This keeps
    // the form in sync with what the backend will actually save, and makes
    // the empty-DNS case obvious before the user hits Save.
    (function () {
      const ipEl = document.getElementById('ip');
      if (!ipEl || !ipEl.value) return;
      const p = ipEl.value.trim().split('.');
      if (p.length !== 4 || p.some(v => !/^\d+$/.test(v) || +v > 255)) return;
      const base = p.slice(0, 3).join('.') + '.1';
      if (!document.getElementById('gateway').value) document.getElementById('gateway').value = base;
      if (!document.getElementById('dns').value)     document.getElementById('dns').value     = base;
      if (!document.getElementById('subnet').value)  document.getElementById('subnet').value  = '255.255.255.0';
    })();    

    // ---- Wi-Fi scan ---------------------------------------------------------
    (function () {
      const scanBtn   = document.getElementById('scan-button');
      const modal     = document.getElementById('wifi-modal');
      const closeBtn  = document.getElementById('modal-close');
      const list      = document.getElementById('wifi-list');
      const ssidInput = document.getElementById('ssid');

      function closeModal() { modal.classList.add('hidden'); }

      closeBtn.addEventListener('click', closeModal);
      modal.addEventListener('click', (e) => { if (e.target === modal) closeModal(); });

      scanBtn.addEventListener('click', async () => {
        modal.classList.remove('hidden');
        list.innerHTML = '<div class="scan-status">Scanning…</div>';
        scanBtn.disabled = true;

        try {
          await fetch('/api/wifi/scan/start', { cache: 'no-store' });

          let done = false;
          for (let attempt = 0; attempt < 40 && !done; attempt++) {
            await new Promise(r => setTimeout(r, 500));
            const res  = await fetch('/api/wifi/scan/status', { cache: 'no-store' });
            const data = await res.json();

            if (data.state === 'running') continue;

            if (data.state === 'failed') {
              // Genuine failure: the server always sends "networks":[]
              // alongside it. Any other shape is a truncated body — retry.
              if (Array.isArray(data.networks)) {
                done = true;
                list.innerHTML = '<div class="scan-status">No networks found. Try again.</div>';
                return;
              }
              continue;
            }
            if (!data.networks) continue;    // truncated: retry

            done = true;
            if (!data.networks.length) {
              list.innerHTML = '<div class="scan-status">No networks found. Try again.</div>';
              return;
            }

            data.networks.sort((a, b) => b.rssi - a.rssi);
            list.innerHTML = '';
            data.networks.forEach(n => {
              const b = document.createElement('button');
              b.type = 'button';
              b.className = 'net';
              const s = document.createElement('span');
              s.textContent = n.ssid || '(hidden)';
              const r = document.createElement('span');
              r.className = 'rssi';
              r.textContent = n.rssi + ' dBm';
              b.appendChild(s); b.appendChild(r);
              b.addEventListener('click', () => {
                ssidInput.value = n.ssid || '';
                closeModal();
                document.getElementById('password').focus();
              });
              list.appendChild(b);
            });
          }
          if (!done) list.innerHTML = '<div class="scan-status">Scan timed out.</div>';
        } catch (err) {
          list.innerHTML = '<div class="scan-status">Scan failed. The server may be busy.</div>';
        } finally {
          scanBtn.disabled = false;
        }
      });
    })();

    // ---- Save / test --------------------------------------------------------
    button.addEventListener('click', async () => {
      if (!form.checkValidity()) { form.reportValidity(); return; }
      
      button.disabled = true;
      button.textContent = 'Testing connection...';
      
      try {
        const r = await fetch('/save', { method: 'POST', body: new URLSearchParams(new FormData(form)) });
        const d = await r.json();
        if (!r.ok || !d.ok) throw new Error(d.error || 'Unable to connect');
        
        let s;
        do {
          await new Promise(done => setTimeout(done, 500));
          s = await (await fetch('/api/network-test')).json();
        } while (s.state === 'running');
        
        if (s.state !== 'success') throw new Error(s.error || 'Unable to connect');
        
        const hostName = document.getElementById('server-name').value;
        document.body.innerHTML = `
          <div style="text-align:center; margin-top:10vh;">
            <div style="display:inline-block; width:60px; height:60px; background:var(--good); border-radius:50%; margin-bottom:20px; box-shadow: 0 0 20px var(--good);"></div>
            <h1>Success!</h1>
            <p style="color:var(--text); font-size:1.1rem;">Connected to <b>${s.ip}</b>.</p>
            <p style="color:var(--muted);">The server is restarting. Your device should reconnect to your home Wi-Fi shortly.</p>
            <p style="margin-top:20px;">Redirecting to <b style="color:var(--accent);">http://${hostName}.local/</b> in 10 seconds...</p>
          </div>
        `;
        
        await fetch('/api/restart', { method: 'POST' });
        setTimeout(() => location.href = 'http://' + hostName + '.local/', 10000);
        
      } catch (error) {
        alert('Unable to connect\n\n' + error.message + '\n\nCheck the SSID, password, and static IP settings, then try again.');
        button.disabled = false;
        button.textContent = 'Save';
      }
    });
  </script>
</body>
</html>
)HTML";

String networkSettingsPage() {
  String page = FPSTR(EVDNTW_HTML_TEMPLATE);
  page.replace("{{SERVER_NAME}}", htmlEscape(serverName));
  page.replace("{{WIFI_SSID}}", htmlEscape(wifiStaSsid));
  page.replace("{{WIFI_PASS}}", htmlEscape(wifiStaPassword));
  page.replace("{{STATIC_CHECKED}}", wifiUseStaticIp ? "checked" : "");
  page.replace("{{STATIC_CLASS}}", wifiUseStaticIp ? "" : "hidden");
  page.replace("{{STATIC_IP}}", htmlEscape(wifiStaticIp));
  page.replace("{{STATIC_GW}}", htmlEscape(wifiStaticGateway));
  page.replace("{{STATIC_SUBNET}}", htmlEscape(wifiStaticSubnet));
  page.replace("{{STATIC_DNS}}", htmlEscape(wifiStaticDns));
  return page;
}