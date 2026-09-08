#ifndef EVDCTRL_H
#define EVDCTRL_H

/*
  EVDCTRL.h - Control panel page (served at "/" and "/control").
  Pure front-end: returns one big HTML/CSS/JS string (getControlPageTemplate)
  that polls GET /api/state and posts actions to POST /api/control, /save,
  etc. All request handling lives in EnkelvoudServer.ino - this file has no
  server-side logic of its own.
*/

#include <Arduino.h>

inline String getControlPageTemplate(const char* friendly_name) {
    String html = String(R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)rawliteral") + friendly_name + R"rawliteral( Control Panel</title>
    <style>
        :root {
            --bg-color: #000000;
            --card-bg: #121212;
            --text-color: #f8fafc;
            --text-muted: #94a3b8;
            --accent-color: #38bdf8;
            --accent-hover: #0ea5e9;
            --danger-color: #f43f5e;
            --success-color: #22c55e;
            --border-color: #27272a;
            --group-bg: rgba(18, 18, 18, 0.6);
        }

        body.theme-black {
            --bg-color: #000000;
            --card-bg: #121212;
            --text-color: #f8fafc;
            --text-muted: #94a3b8;
            --accent-color: #38bdf8;
            --accent-hover: #0ea5e9;
            --danger-color: #f43f5e;
            --success-color: #22c55e;
            --border-color: #27272a;
            --group-bg: rgba(18, 18, 18, 0.6);
        }

        body.theme-light {
            --bg-color: #f1f5f9;
            --card-bg: #ffffff;
            --text-color: #0f172a;
            --text-muted: #64748b;
            --accent-color: #0284c7;
            --accent-hover: #0369a1;
            --danger-color: #e11d48;
            --success-color: #16a34a;
            --border-color: #cbd5e1;
            --group-bg: rgba(241, 245, 249, 0.8);
        }

        body.theme-blue {
            --bg-color: #090d16;
            --card-bg: #111c30;
            --text-color: #e2e8f0;
            --text-muted: #93c5fd;
            --accent-color: #3b82f6;
            --accent-hover: #2563eb;
            --danger-color: #ef4444;
            --success-color: #22c55e;
            --border-color: #1e3a8a;
            --group-bg: rgba(17, 28, 48, 0.6);
        }

        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-color);
            margin: 0;
            padding: 20px;
            display: flex;
            justify-content: center;
            transition: background-color 0.3s, color 0.3s;
        }

        .container {
            width: 100%;
            max-width: 900px;
            display: flex;
            flex-direction: column;
            gap: 20px;
        }

        .header-actions {
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
            gap: 15px;
        }

        .header-controls {
            display: flex;
            align-items: center;
            gap: 10px;
            flex-wrap: wrap;
        }

        .server-title-clickable {
            background: transparent;
            border: none;
            color: var(--text-color);
            font-size: 1.5rem;
            font-weight: 700;
            padding: 0;
            cursor: pointer;
            text-align: left;
            transition: color 0.2s;
        }

        .server-title-clickable:hover {
            color: var(--accent-color);
            text-decoration: underline;
        }

        .btn-primary {
            background-color: var(--accent-color);
            color: #ffffff;
            border: none;
            border-radius: 6px;
            padding: 8px 14px;
            font-size: 0.85rem;
            font-weight: 600;
            cursor: pointer;
            transition: background-color 0.2s;
        }

        .btn-primary:hover {
            background-color: var(--accent-hover);
        }

        .btn-toggle {
            background-color: var(--danger-color);
            color: #ffffff;
            border: none;
            border-radius: 6px;
            padding: 8px 14px;
            font-size: 0.85rem;
            font-weight: 600;
            cursor: pointer;
            transition: background-color 0.2s;
        }

        .btn-toggle.active {
            background-color: var(--success-color);
        }

        select.theme-select, select.input-select {
            background-color: var(--card-bg);
            color: var(--text-color);
            border: 1px solid var(--border-color);
            border-radius: 6px;
            padding: 7px 10px;
            font-size: 0.85rem;
            cursor: pointer;
        }

        .card {
            background-color: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: 12px;
            padding: 20px;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1), 0 2px 4px -1px rgba(0, 0, 0, 0.06);
            transition: background-color 0.3s, border-color 0.3s;
        }

        .group-container {
            background-color: var(--group-bg);
            border: 1px solid var(--border-color);
            border-radius: 10px;
            padding: 14px;
            margin-bottom: 14px;
            display: flex;
            flex-direction: column;
            gap: 12px;
            transition: border-color 0.2s, background-color 0.2s;
        }
        
        .group-container.drag-over {
            border: 2px dashed var(--accent-color);
            background-color: rgba(56, 189, 248, 0.05);
        }

        .group-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid var(--border-color);
            padding-bottom: 8px;
            flex-wrap: wrap;
            gap: 10px;
        }

        .group-title-wrapper {
            display: flex;
            align-items: center;
            gap: 8px;
        }

        .group-title-clickable {
            background: transparent;
            border: none;
            color: var(--accent-color);
            font-weight: 700;
            font-size: 1rem;
            padding: 2px 4px;
            cursor: pointer;
            text-align: left;
            border-radius: 4px;
            transition: background-color 0.2s;
        }

        .group-title-clickable:hover {
            background-color: var(--border-color);
            text-decoration: underline;
        }

        .group-controls {
            display: flex;
            align-items: center;
            gap: 12px;
            font-size: 0.85rem;
        }

        .group-controls input[type="range"] {
            accent-color: var(--accent-color);
            cursor: pointer;
            width: 100px;
        }

        .node-list {
            display: flex;
            flex-direction: column;
            gap: 10px;
            padding-left: 10px;
            min-height: 40px;
        }

        .node-item {
            background-color: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 10px 14px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
            gap: 10px;
            cursor: grab;
            transition: transform 0.15s, opacity 0.15s;
        }
        
        .node-item:active {
            cursor: grabbing;
        }
        
        .node-item.dragging {
            opacity: 0.4;
        }

        .node-info-inline {
            display: flex;
            align-items: center;
            gap: 12px;
            flex-wrap: wrap;
            font-size: 0.85rem;
            color: var(--text-muted);
            width: 100%;
        }

        .node-name {
            font-weight: 600;
            font-size: 0.9rem;
            color: var(--text-color);
        }

        .node-ip-container {
            margin-left: auto;
            display: flex;
            align-items: center;
            gap: 10px;
        }

        .node-ip-link {
            color: var(--accent-color);
            text-decoration: none;
            cursor: pointer;
        }

        .node-ip-link:hover {
            text-decoration: underline;
        }

        .node-ip-text {
            color: var(--text-muted);
        }

        .status-badge {
            font-size: 0.75rem;
            padding: 2px 6px;
            border-radius: 4px;
            font-weight: 600;
        }
        .status-badge.connected {
            background-color: rgba(34, 197, 94, 0.2);
            color: var(--success-color);
        }
        .status-badge.disconnected {
            background-color: rgba(244, 63, 94, 0.2);
            color: var(--danger-color);
        }

        button.action-btn {
            background-color: var(--border-color);
            color: var(--text-color);
            border: none;
            border-radius: 6px;
            padding: 6px 10px;
            font-size: 0.85rem;
            cursor: pointer;
            display: inline-flex;
            align-items: center;
            justify-content: center;
            text-decoration: none;
            transition: background-color 0.2s;
        }
        button.action-btn:hover, a.action-btn:hover {
            background-color: var(--accent-hover);
        }
        button.action-btn.muted {
            background-color: var(--danger-color);
        }
        button.action-btn.danger {
            background-color: var(--danger-color);
        }

        .log-window {
            background-color: var(--bg-color);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 10px;
            height: 150px;
            overflow-y: auto;
            font-family: monospace;
            font-size: 0.75rem;
            color: var(--text-muted);
            display: flex;
            flex-direction: column;
            gap: 4px;
        }

        .modal-overlay {
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background-color: rgba(0, 0, 0, 0.7);
            display: flex;
            justify-content: center;
            align-items: center;
            z-index: 1000;
            opacity: 0;
            pointer-events: none;
            transition: opacity 0.2s ease;
        }

        .modal-overlay.active {
            opacity: 1;
            pointer-events: auto;
        }

        .modal-dialog {
            background-color: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: 12px;
            padding: 20px;
            width: 100%;
            max-width: 450px;
            box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.3);
            display: flex;
            flex-direction: column;
            gap: 15px;
            box-sizing: border-box;
        }

        .modal-title {
            font-size: 1.1rem;
            font-weight: 600;
            margin: 0;
            color: var(--text-color);
        }

        .modal-input {
            background-color: var(--bg-color);
            color: var(--text-color);
            border: 1px solid var(--border-color);
            border-radius: 6px;
            padding: 10px;
            font-size: 1rem;
            outline: none;
            width: 100%;
            box-sizing: border-box;
        }

        .modal-input:focus {
            border-color: var(--accent-color);
        }

        .modal-buttons {
            display: flex;
            justify-content: flex-end;
            gap: 10px;
        }

        .gear-btn {
            background: transparent;
            border: 1px solid var(--border-color);
            color: var(--text-color);
            border-radius: 6px;
            padding: 7px 10px;
            font-size: 1rem;
            cursor: pointer;
            display: inline-flex;
            align-items: center;
            justify-content: center;
            transition: background-color 0.2s, border-color 0.2s;
        }
        .gear-btn:hover {
            background-color: var(--border-color);
            border-color: var(--accent-color);
        }

        .row-group {
            display: flex;
            gap: 10px;
            align-items: center;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header-actions">
            <div style="display: flex; align-items: center; gap: 15px; flex-wrap: wrap;">
                <button class="server-title-clickable" id="serverTitleBtn" onclick="openServerRenameModal()" title="Click to change server name">)rawliteral" + String(friendly_name) + R"rawliteral(</button>
                <button class="action-btn" id="globalMuteBtn" onclick="toggleGlobalMute()">Mute Server</button>
                <button class="btn-toggle" id="streamToggleBtn" onclick="toggleStreaming()">Stream</button>
                <select class="input-select" id="audioInputSelect" onchange="changeAudioInput(this.value)">
                    <option value="USB">USB</option>
                    <option value="Bluetooth">Bluetooth</option>
                    <option value="AUX in">AUX in</option>
                </select>
            </div>
            <div class="header-controls">
                <button class="action-btn danger" id="restartTopBtn" onclick="restartDevice()" title="Restart ESP32-S3">🔄 Restart</button>
                <select class="theme-select" id="themeSelector" onchange="changeTheme(this.value)">
                    <option value="black">Black (Default)</option>
                    <option value="light">Light</option>
                    <option value="blue">Blue</option>
                </select>
                <a href="/player" class="action-btn" title="Audio Player" style="text-decoration: none;">🔊 Player</a>
                <button class="gear-btn" onclick="openAdvancedModal()" title="Advanced Settings">⚙️</button>
            </div>
        </div>

        <div class="card">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px;">
                <h2 style="font-size: 1.2rem; margin: 0;">Rooms & Nodes</h2>
                <button class="btn-primary" onclick="createNewGroup()">+ New Group</button>
            </div>
            <div id="groupList">
                <div style="color: var(--text-muted); font-size: 0.85rem;" id="scanningMessage">Loading nodes from configuration...</div>
            </div>
        </div>

        <div class="card">
            <h3 style="font-size: 1rem; margin-top: 0; margin-bottom: 10px; color: var(--text-color);">Event & Error Log</h3>
            <div class="log-window" id="logWindow">
                <div>Waiting for logs from server...</div>
            </div>
        </div>
    </div>

    <!-- Advanced Settings Modal -->
    <div class="modal-overlay" id="advancedModal">
        <div class="modal-dialog" style="max-width: 550px; max-height: 90vh; overflow-y: auto;">
            <h3 class="modal-title">Advanced Settings</h3>
            <div style="display: flex; flex-direction: column; gap: 15px;">
                <div style="display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 10px;">
                    <button class="btn-primary" id="bitrateBtn" onclick="cycleBitrate()">Bitrate: mid</button>
                    <div style="display: flex; align-items: center; gap: 8px;">
                        <label style="font-size: 0.85rem; font-weight: 600;">Buffer:</label>
                        <input type="range" id="bufferSlider" min="0" max="200" value="0" style="accent-color: var(--accent-color); width: 110px; cursor: pointer;" oninput="document.getElementById('bufferLabel').innerText = this.value" onchange="changeBuffer(this.value)">
                        <span style="font-size: 0.85rem; min-width: 35px;"><span id="bufferLabel">0</span>ms</span>
                    </div>
                </div>

                <hr style="border: 0; border-top: 1px solid var(--border-color); margin: 5px 0;">

                <div>
                    <label style="font-size: 0.9rem; font-weight: 600; margin-bottom: 8px; display: block;">Network Settings</label>
                    <label style="font-size: 0.85rem;">Wi-Fi SSID:</label>
                    <div class="row-group" style="margin-top: 4px;">
                        <input type="text" id="ssidInput" name="ssid" class="modal-input" placeholder="Enter SSID">
                        <button type="button" class="btn-primary" onclick="openWifiModal()" style="white-space: nowrap;">Wi-Fi Search</button>
                    </div>
                    <label style="font-size: 0.85rem; margin-top: 8px;">Wi-Fi Password:</label>
                    <div class="row-group" style="margin-top: 4px;">
                        <input type="password" id="passInput" name="pass" class="modal-input">
                        <button type="button" class="btn-primary" id="testBtn" onclick="testWifiConnection()" style="white-space: nowrap; background-color: var(--text-muted);">Test Connection</button>
                    </div>
                    <div id="wifiTestResult" style="font-size: 0.85rem; margin-top: 6px;"></div>
                    <label style="display: flex; align-items: center; gap: 10px; margin-top: 10px; cursor: pointer; font-size: 0.85rem;">
                        <input type="checkbox" id="staticCheck" name="use_static" onchange="toggleStaticIp()" style="width: 16px; height: 16px; accent-color: var(--accent-color);"> Use Static IP Configuration
                    </label>
                    <div id="staticIpFields" style="display: none; margin-top: 8px;">
                        <label style="font-size: 0.85rem;">Static IP Address:</label>
                        <input type="text" id="staticIpInput" name="static_ip" class="modal-input" style="margin-top: 4px;">
                        <label style="font-size: 0.85rem; margin-top: 8px;">Gateway IP:</label>
                        <input type="text" id="gatewayInput" name="static_gw" class="modal-input" style="margin-top: 4px;">
                        <label style="font-size: 0.85rem; margin-top: 8px;">Subnet Mask:</label>
                        <input type="text" id="subnetInput" name="static_sn" class="modal-input" style="margin-top: 4px;">
                        <label style="font-size: 0.85rem; margin-top: 8px;">DNS Server:</label>
                        <input type="text" id="dnsInput" name="static_dns" class="modal-input" style="margin-top: 4px;">
                    </div>
                </div>

                <hr style="border: 0; border-top: 1px solid var(--border-color); margin: 5px 0;">

                <div>
                    <label style="font-size: 0.9rem; font-weight: 600; margin-bottom: 8px; display: block;">Receiver ESP32</label>
                    <label style="font-size: 0.85rem;">Receiver IP or Hostname:</label>
                    <input type="text" id="receiverHostInput" name="receiver_host" class="modal-input" style="margin-top: 4px;" placeholder="e.g. 192.168.1.60 or enkelvoud-receiver.local">
                    <label style="font-size: 0.75rem; color: var(--text-muted); margin-top: 4px; display: block;">The BT/AUX/USB source-select board this server sends input-switch commands to.</label>
                </div>
            </div>
            <div class="modal-buttons" style="margin-top: 10px;">
                <button class="action-btn" onclick="closeAdvancedModal()">Cancel</button>
                <button class="btn-primary" onclick="saveAdvancedSettingsModal()">Save</button>
            </div>
        </div>
    </div>

    <div class="modal-overlay" id="renameModal">
        <div class="modal-dialog">
            <h3 class="modal-title">Rename Room</h3>
            <input type="text" class="modal-input" id="renameInput" placeholder="Enter new room name">
            <div class="modal-buttons">
                <button class="action-btn" onclick="closeRenameModal()">Cancel</button>
                <button class="btn-primary" onclick="submitRenameGroup()">Save</button>
            </div>
        </div>
    </div>

    <div class="modal-overlay" id="serverRenameModal">
        <div class="modal-dialog">
            <h3 class="modal-title">Change Friendly Server Name</h3>
            <input type="text" class="modal-input" id="serverRenameInput" placeholder="Enter new server name" pattern="[a-zA-Z0-9]+" title="Alphanumeric characters only (no spaces)">
            <div class="modal-buttons">
                <button class="action-btn" onclick="closeServerRenameModal()">Cancel</button>
                <button class="btn-primary" onclick="submitServerRename()">Save</button>
            </div>
        </div>
    </div>

    <div class="modal-overlay" id="wifiModal">
        <div class="modal-dialog">
            <h3 class="modal-title">Available Wi-Fi Networks</h3>
            <div id="wifiNetworksList" style="max-height: 200px; overflow-y: auto; display: flex; flex-direction: column; gap: 8px; margin: 10px 0;">
                Scanning...
            </div>
            <div class="modal-buttons">
                <button class="action-btn" onclick="closeWifiModal()">Close</button>
            </div>
        </div>
    </div>

    <script>
        const groupList = document.getElementById('groupList');
        const logWindow = document.getElementById('logWindow');
        let currentNodesData = {};
        const groupVolumes = {};
        let activeSliderGroup = null;
        let currentEditingGroup = null;
        let isSlidingBuffer = false;
        let currentBitrate = 'mid';
        const bitrates = ['low', 'mid', 'high'];
        let saveTimeout = null;

        function triggerNvmAutoSave() {
            if (saveTimeout) clearTimeout(saveTimeout);
            saveTimeout = setTimeout(async () => {
                const formData = new URLSearchParams();
                formData.append('ssid', document.getElementById('ssidInput').value);
                formData.append('pass', document.getElementById('passInput').value);
                if (document.getElementById('staticCheck').checked) {
                    formData.append('use_static', 'on');
                    formData.append('static_ip', document.getElementById('staticIpInput').value);
                    formData.append('static_gw', document.getElementById('gatewayInput').value);
                    formData.append('static_sn', document.getElementById('subnetInput').value);
                    formData.append('static_dns', document.getElementById('dnsInput').value);
                }
                formData.append('friendly_name', document.getElementById('serverTitleBtn').textContent.trim());
                try {
                    await fetch('/save', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                        body: formData
                    });
                } catch (e) {}
            }, 15000);
        }

        function openServerRenameModal() {
            const btn = document.getElementById('serverTitleBtn');
            const input = document.getElementById('serverRenameInput');
            input.value = btn ? btn.textContent.trim() : '';
            document.getElementById('serverRenameModal').classList.add('active');
            setTimeout(() => { input.focus(); input.select(); }, 50);
        }

        function closeServerRenameModal() {
            document.getElementById('serverRenameModal').classList.remove('active');
        }

        async function submitServerRename() {
            const inputEl = document.getElementById('serverRenameInput');
            const newName = inputEl ? inputEl.value.trim() : '';
            if (!newName) {
                closeServerRenameModal();
                return;
            }
            closeServerRenameModal();
            try {
                await fetch('/api/server_name', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ name: newName })
                });
                triggerNvmAutoSave();
                alert('Server name updated successfully.');
                window.location.reload();
            } catch (e) {
                alert('Failed to update server name.');
            }
        }

        document.getElementById('serverRenameInput').addEventListener('keydown', (e) => {
            if (e.key === 'Enter') submitServerRename();
        });

        function openAdvancedModal() {
            document.getElementById('advancedModal').classList.add('active');
        }

        function closeAdvancedModal() {
            document.getElementById('advancedModal').classList.remove('active');
        }

        function toggleStaticIp() {
            const isChecked = document.getElementById('staticCheck').checked;
            document.getElementById('staticIpFields').style.display = isChecked ? 'block' : 'none';
            triggerNvmAutoSave();
        }

        async function saveAdvancedSettingsModal() {
            const formData = new URLSearchParams();
            formData.append('ssid', document.getElementById('ssidInput').value);
            formData.append('pass', document.getElementById('passInput').value);
            if (document.getElementById('staticCheck').checked) {
                formData.append('use_static', 'on');
                formData.append('static_ip', document.getElementById('staticIpInput').value);
                formData.append('static_gw', document.getElementById('gatewayInput').value);
                formData.append('static_sn', document.getElementById('subnetInput').value);
                formData.append('static_dns', document.getElementById('dnsInput').value);
            }

            try {
                await fetch('/save', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: formData
                });
                alert('Network settings saved. Restarting server...');
                setTimeout(() => { window.location.reload(); }, 4000);
            } catch (e) {
                alert('Failed to save settings.');
            }
        }

        async function openWifiModal() {
            document.getElementById('wifiModal').classList.add('active');
            const listEl = document.getElementById('wifiNetworksList');
            listEl.innerHTML = '<div>Scanning networks, please wait...</div>';
            try {
                const res = await fetch('/scan');
                const nets = await res.json();
                if (nets.length === 0) {
                    listEl.innerHTML = '<div>No networks found.</div>';
                    return;
                }
                let html = '';
                nets.forEach(n => {
                    html += `<div style="padding: 8px; background: var(--bg-color); border: 1px solid var(--border-color); border-radius: 6px; cursor: pointer; display: flex; justify-content: space-between;" onclick="selectNetwork('${n.ssid}')"><span>${escapeHtml(n.ssid)}</span><span>${n.rssi} dBm</span></div>`;
                });
                listEl.innerHTML = html;
            } catch (e) {
                listEl.innerHTML = '<div>Failed to scan networks.</div>';
            }
        }

        function closeWifiModal() {
            document.getElementById('wifiModal').classList.remove('active');
        }

        function selectNetwork(ssid) {
            document.getElementById('ssidInput').value = ssid;
            closeWifiModal();
            triggerNvmAutoSave();
        }

        async function testWifiConnection() {
            const ssid = document.getElementById('ssidInput').value;
            const pass = document.getElementById('passInput').value;
            const resEl = document.getElementById('wifiTestResult');
            resEl.textContent = 'Testing connection...';
            resEl.style.color = 'var(--text-muted)';
            try {
                const res = await fetch('/api/test_wifi', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ssid, pass })
                });
                const data = await res.json();
                resEl.textContent = data.message;
                resEl.style.color = data.success ? 'var(--success-color)' : 'var(--danger-color)';
            } catch (e) {
                resEl.textContent = 'Connection test failed.';
                resEl.style.color = 'var(--danger-color)';
            }
        }

        async function cycleBitrate() {
            let currentIndex = bitrates.indexOf(currentBitrate);
            currentIndex = (currentIndex + 1) % bitrates.length;
            currentBitrate = bitrates[currentIndex];
            
            const btn = document.getElementById('bitrateBtn');
            if (btn) btn.textContent = `Bitrate: ${currentBitrate}`;

            await fetch('/api/control', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ action: 'set_bitrate', bitrate: currentBitrate })
            });
            triggerNvmAutoSave();
            fetchState();
        }

        function changeTheme(theme) {
            document.body.className = '';
            if (theme === 'black') {
                document.body.classList.add('theme-black');
            } else if (theme === 'light') {
                document.body.classList.add('theme-light');
            } else if (theme === 'blue') {
                document.body.classList.add('theme-blue');
            }
            localStorage.setItem('enkelvoud_theme', theme);
        }

        const savedTheme = localStorage.getItem('enkelvoud_theme') || 'black';
        document.getElementById('themeSelector').value = savedTheme;
        changeTheme(savedTheme);

        function escapeHtml(str) {
            return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        }

        async function restartDevice() {
            if (!confirm("Restart server?")) return;
            try {
                await fetch('/api/reset', { method: 'POST' });
            } catch (e) {}
            alert('Server is restarting...');
            setTimeout(() => { window.location.reload(); }, 4000);
        }

        async function fetchState() {
            if (document.getElementById('renameModal').classList.contains('active')) return;
            if (document.getElementById('serverRenameModal').classList.contains('active')) return;
            if (document.getElementById('advancedModal').classList.contains('active')) return;
            if (activeSliderGroup !== null) return;

            try {
                const res = await fetch('/api/state?_=' + new Date().getTime(), { cache: 'no-store' });
                if (!res.ok) throw new Error('Network response was not ok');
                const data = await res.json();
                currentNodesData = data.nodes || {};
                
                if (data.server_name) {
                    const sBtn = document.getElementById('serverTitleBtn');
                    if (sBtn && sBtn.textContent !== data.server_name && document.activeElement !== document.getElementById('serverRenameInput')) {
                        sBtn.textContent = data.server_name;
                    }
                }

                if (data.bitrate) {
                    currentBitrate = data.bitrate;
                    const btn = document.getElementById('bitrateBtn');
                    if (btn) btn.textContent = `Bitrate: ${currentBitrate}`;
                }

                if (data.wifi_ssid !== undefined && document.activeElement !== document.getElementById('ssidInput')) {
                    document.getElementById('ssidInput').value = data.wifi_ssid;
                }
                if (data.wifi_pass !== undefined && document.activeElement !== document.getElementById('passInput')) {
                    document.getElementById('passInput').value = data.wifi_pass;
                }
                if (data.use_static !== undefined) {
                    document.getElementById('staticCheck').checked = data.use_static;
                    document.getElementById('staticIpFields').style.display = data.use_static ? 'block' : 'none';
                }
                if (data.static_ip) document.getElementById('staticIpInput').value = data.static_ip;
                if (data.static_gw) document.getElementById('gatewayInput').value = data.static_gw;
                if (data.static_sn) document.getElementById('subnetInput').value = data.static_sn;
                if (data.static_dns) document.getElementById('dnsInput').value = data.static_dns;

                const globalMuteBtn = document.getElementById('globalMuteBtn');
                if (data.host_muted) {
                    globalMuteBtn.textContent = "Unmute Server";
                    globalMuteBtn.classList.add('muted');
                } else {
                    globalMuteBtn.textContent = "Mute Server";
                    globalMuteBtn.classList.remove('muted');
                }

                const streamBtn = document.getElementById('streamToggleBtn');
                streamBtn.textContent = "Streaming";
                streamBtn.style.backgroundColor = data.streaming_enabled ? "#166534" : "#991b1b";

                const audioInputSelect = document.getElementById('audioInputSelect');
                if (data.audio_input && document.activeElement !== audioInputSelect) {
                    audioInputSelect.value = data.audio_input;
                }

                if (data.audio_buffer !== undefined && !isSlidingBuffer) {
                    document.getElementById('bufferSlider').value = data.audio_buffer;
                    document.getElementById('bufferLabel').innerText = data.audio_buffer;
                }

                if (data.logs && Array.isArray(data.logs)) {
                    let logHtml = '';
                    data.logs.forEach(log => {
                        logHtml += `<div>${escapeHtml(log)}</div>`;
                    });
                    logWindow.innerHTML = logHtml;
                    logWindow.scrollTop = logWindow.scrollHeight;
                }

                renderGroupsAndNodes(currentNodesData);
            } catch (e) {
                console.error("Failed to fetch state:", e);
                document.getElementById('groupList').innerHTML = '<div style="color: var(--danger-color); font-size: 0.85rem;">Error loading node state from server.</div>';
            }
        }

        async function toggleStreaming() {
            await fetch('/api/control', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ action: 'toggle_streaming' })
            });
            fetchState();
        }

        async function toggleGlobalMute() {
            await fetch('/api/control', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ action: 'toggle_host_mute' })
            });
            fetchState();
        }

        async function changeAudioInput(mode) {
            document.getElementById('audioInputSelect').value = mode;
            await fetch('/api/control', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ action: 'set_audio_input', input: mode })
            });
            triggerNvmAutoSave();
            fetchState();
        }

        async function changeBuffer(val) {
            isSlidingBuffer = true;
            document.getElementById('bufferLabel').innerText = val;
            await fetch('/api/control', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ action: 'set_buffer', buffer: parseInt(val) })
            });
            isSlidingBuffer = false;
            triggerNvmAutoSave();
            fetchState();
        }

        function createNewGroup() {
            const groupName = prompt("Enter new group (room) name:");
            if (!groupName || !groupName.trim()) return;
            const cleanName = groupName.trim();
            
            fetch('/api/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ action: 'create_group', group: cleanName })
            }).then(() => fetchState());
        }

        function deleteNode(uuid) {
            if (!confirm("Are you sure you want to delete this node from configuration?")) return;
            fetch('/api/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ action: 'delete_node', node_uuid: uuid })
            }).then(() => fetchState());
        }

        function deleteGroup(groupName) {
            const groups = new Set();
            for (const info of Object.values(currentNodesData)) {
                groups.add(info.group || 'Main Room');
            }

            if (groups.size <= 1) {
                alert("Cannot delete the last remaining room.");
                return;
            }

            if (!confirm(`Delete room "${groupName}"? All nodes will move to "Main Room".`)) return;

            fetch('/api/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ action: 'delete_group', group: groupName })
            }).then(() => fetchState());
        }

        function openRenameModal(groupName) {
            currentEditingGroup = groupName;
            const input = document.getElementById('renameInput');
            input.value = groupName;
            document.getElementById('renameModal').classList.add('active');
            setTimeout(() => { input.focus(); input.select(); }, 50);
        }

        function closeRenameModal() {
            document.getElementById('renameModal').classList.remove('active');
            currentEditingGroup = null;
        }

        function submitRenameGroup() {
            const inputEl = document.getElementById('renameInput');
            const newName = inputEl ? inputEl.value.trim() : '';
            if (!newName || !currentEditingGroup || newName === currentEditingGroup) {
                closeRenameModal();
                return;
            }
            const oldName = currentEditingGroup;
            closeRenameModal();

            fetch('/api/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ action: 'rename_group', old_group: oldName, new_group: newName })
            }).then(() => fetchState());
        }

        document.getElementById('renameInput').addEventListener('keydown', (e) => {
            if (e.key === 'Enter') submitRenameGroup();
        });

        function renderGroupsAndNodes(nodes) {
            const groups = { 'Main Room': [] };
            for (const [uuid, info] of Object.entries(nodes)) {
                const g = info.group || 'Main Room';
                if (!groups[g]) groups[g] = [];
                groups[g].push({ uuid, ...info });
            }

            let html = '';
            for (const [groupName, nodeList] of Object.entries(groups)) {
                if (groupVolumes[groupName] === undefined) groupVolumes[groupName] = 100;
                
                const isMain = (groupName === 'Main Room');

                html += `
                    <div class="group-container" ondragover="allowDrop(event)" ondragleave="removeDropStyle(event)" ondrop="dropNode(event, '${escapeHtml(groupName)}')">
                        <div class="group-header">
                            <div class="group-title-wrapper">
                                <button class="group-title-clickable" onclick="openRenameModal('${escapeHtml(groupName)}')">${escapeHtml(groupName)}</button>
                            </div>
                            <div class="group-controls">
                                <span>Vol: <span id="volLabel_${escapeHtml(groupName)}">${groupVolumes[groupName]}</span>%</span>
                                <input type="range" min="0" max="100" value="${groupVolumes[groupName]}" oninput="activeSliderGroup='${escapeHtml(groupName)}'; document.getElementById('volLabel_${escapeHtml(groupName)}').innerText = this.value; updateGroupVolume('${escapeHtml(groupName)}', this.value);" onchange="activeSliderGroup=null;">
                `;

                if (!isMain) {
                    html += `<button class="action-btn danger" onclick="deleteGroup('${escapeHtml(groupName)}')">Delete Room</button>`;
                }

                html += `
                            </div>
                        </div>
                        <div class="node-list">
                `;

                nodeList.forEach(node => {
                    const isServerNode = (node.uuid === 'server_node' || node.is_server);
                    const statusClass = node.connected ? 'connected' : 'disconnected';
                    const statusText = node.connected ? 'Connected' : 'Disconnected';

                    html += `
                        <div class="node-item" draggable="true" ondragstart="dragNode(event, '${node.uuid}')" ondragend="endDragNode(event)">
                            <div class="node-info-inline">
                                <span class="node-name">${escapeHtml(node.name || 'Node')}</span>
                                <span class="status-badge ${statusClass}">${statusText}</span>
                                <div class="node-ip-container">
                    `;

                    if (isServerNode) {
                        html += `<span class="node-ip-text">${node.ip}</span>`;
                    } else {
                        if (node.connected) {
                            html += `<a class="node-ip-link" href="http://${node.ip}" target="_blank">${node.ip}</a>`;
                        } else {
                            html += `<span class="node-ip-text">${node.ip}</span>`;
                        }
                        html += `<button class="action-btn danger" onclick="deleteNode('${node.uuid}')">Remove</button>`;
                    }

                    html += `
                                </div>
                            </div>
                        </div>
                    `;
                });

                html += `
                        </div>
                    </div>
                `;
            }
            groupList.innerHTML = html;
        }

        function dragNode(event, uuid) {
            event.dataTransfer.setData('text/plain', uuid);
            event.currentTarget.classList.add('dragging');
        }

        function endDragNode(event) {
            event.currentTarget.classList.remove('dragging');
        }

        function allowDrop(event) {
            event.preventDefault();
            event.currentTarget.classList.add('drag-over');
        }

        function removeDropStyle(event) {
            event.currentTarget.classList.remove('drag-over');
        }

        async function dropNode(event, targetGroup) {
            event.preventDefault();
            event.currentTarget.classList.remove('drag-over');
            const nodeUuid = event.dataTransfer.getData('text/plain');
            if (!nodeUuid) return;

            await fetch('/api/control', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ action: 'move_node', node_uuid: nodeUuid, group: targetGroup })
            });
            fetchState();
        }

        async function updateGroupVolume(groupName, val) {
            groupVolumes[groupName] = parseInt(val);
            await fetch('/api/control', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ action: 'set_group_volume', group: groupName, volume: parseInt(val) / 100.0 })
            });
        }

        setInterval(fetchState, 10000);
        fetchState();
    </script>
</body>
</html>
)rawliteral";

    return html;
}

#endif