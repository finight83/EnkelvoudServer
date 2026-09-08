#ifndef EVDPLR_H
#define EVDPLR_H

#include <Arduino.h>

const char PLAYER_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>Enkelvoud Audio Player</title>
    <style>
        :root {
            --bg-color: #0f172a;
            --card-bg: #1e293b;
            --text-main: #f8fafc;
            --text-sub: #94a3b8;
            --accent: #0284c7;
            --accent-hover: #0369a1;
            --danger: #ef4444;
            --border: #334155;
            --metric-bg: #0f172a;
            --title-color: #38bdf8;
        }
        [data-theme="light"] {
            --bg-color: #f1f5f9; --card-bg: #ffffff; --text-main: #0f172a;
            --text-sub: #64748b; --accent: #0284c7; --danger: #dc2626;
            --border: #cbd5e1; --metric-bg: #f8fafc; --title-color: #0284c7;
        }
        [data-theme="blue"] {
            --bg-color: #091e3a; --card-bg: #132f4c; --text-main: #e2e8f0;
            --text-sub: #90cdf4; --accent: #3182ce; --danger: #e53e3e;
            --border: #2c5282; --metric-bg: #091e3a; --title-color: #63b3ed;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
        body { background-color: var(--bg-color); color: var(--text-main); display: flex; flex-direction: column; justify-content: center; align-items: center; min-height: 100vh; padding: 20px; }
        .card { background: var(--card-bg); padding: 24px; border-radius: 16px; width: 100%; max-width: 420px; text-align: center; }
        .back-link { display: block; text-align: left; color: var(--text-sub); text-decoration: none; font-size: 0.85rem; margin-bottom: 15px; }
        h1 { font-size: 1.5rem; margin-bottom: 2px; color: var(--title-color); }
        p.subtitle { font-size: 0.875rem; color: var(--text-sub); margin-bottom: 20px; }
        .input-group { margin-bottom: 12px; text-align: left; }
        label { display: block; font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.05em; color: var(--text-sub); margin-bottom: 4px; }
        input, select { width: 100%; padding: 10px; border-radius: 8px; border: 1px solid var(--border); background: var(--metric-bg); color: var(--text-main); font-size: 0.95rem; }
        .btn { width: 100%; padding: 14px; border-radius: 8px; border: none; background: var(--accent); color: white; font-size: 1rem; font-weight: 600; cursor: pointer; margin-top: 8px; }
        .btn.connected { background: var(--danger); }
        .status-badge { display: inline-block; margin-top: 16px; padding: 6px 12px; border-radius: 20px; font-size: 0.85rem; background: var(--border); color: var(--text-sub); }
        .status-badge.active { background: #166534; color: #4ade80; }
        .status-badge.error { background: #991b1b; color: #fca5a5; }
        .metrics { margin-top: 16px; display: grid; grid-template-columns: 1fr 1fr; gap: 8px; font-size: 0.75rem; color: var(--text-sub); }
        .metric-box { background: var(--metric-bg); padding: 8px; border-radius: 6px; }
        .log-container { margin-top: 16px; text-align: left; background: #000; color: #00ff66; font-family: monospace; font-size: 0.7rem; padding: 10px; border-radius: 8px; height: 120px; overflow-y: scroll; }
    </style>
</head>
<body>
<div class="card">
    <a href="/" class="back-link">&larr; Back to Control</a>
    <h1>Enkelvoud Player</h1>
    <p class="subtitle" id="deviceDisplayTitle">Phone audio node over HTTP</p>

    <div class="input-group">
        <label>Theme</label>
        <select id="themeSelect">
            <option value="black">Black (Default)</option>
            <option value="light">Light</option>
            <option value="blue">Blue</option>
        </select>
    </div>
    <div class="input-group">
        <label>Assigned Node Name</label>
        <input type="text" id="friendlyName" autocomplete="off">
    </div>
    <div class="input-group">
        <label>Server</label>
        <input type="text" id="serverHost" autocomplete="off" spellcheck="false">
    </div>

    <button id="toggleBtn" class="btn">Start Audio Player</button>
    <div id="status" class="status-badge">Disconnected</div>
    <div class="metrics">
        <div class="metric-box"><div>Clock Offset</div><strong id="offsetVal" style="color: var(--text-main);">0 ms</strong></div>
        <div class="metric-box"><div>Packets Received</div><strong id="packetVal" style="color: var(--text-main);">0</strong></div>
    </div>
    <div class="log-container" id="logBox"><div>Open this page at http://server-ip/player or http://server-name.local/player</div></div>
</div>
<script>
    const logBox = document.getElementById('logBox');
    function logMessage(text) {
        const timestamp = new Date().toISOString().split('T')[1].slice(0, 8);
        logBox.innerHTML += `<div>[${timestamp}] ${text}</div>`;
        logBox.scrollTop = logBox.scrollHeight;
    }

    let isRunning = false;
    let audioCtx = null;
    let wsOrSocket = null;
    let heartbeatTimer = null;
    let packetCount = 0;
    let nextPlayTime = 0;
    let nodeVolume = parseFloat(localStorage.getItem('enkelvoud_node_volume'));
    if (isNaN(nodeVolume)) nodeVolume = 1.0;
    let nodeMuted = localStorage.getItem('enkelvoud_node_muted') === 'true';

    let nodeUuid = localStorage.getItem('enkelvoud_node_uuid');
    if (!nodeUuid) {
        nodeUuid = 'phone_' + Math.random().toString(36).substring(2, 15);
        localStorage.setItem('enkelvoud_node_uuid', nodeUuid);
    }
    let currentAssignedName = localStorage.getItem('enkelvoud_friendly_name') || 'Phone Player';

    const themeSelect = document.getElementById('themeSelect');
    const savedTheme = localStorage.getItem('enkelvoud_theme') || 'black';
    document.documentElement.setAttribute('data-theme', savedTheme);
    themeSelect.value = savedTheme;
    themeSelect.addEventListener('change', () => {
        document.documentElement.setAttribute('data-theme', themeSelect.value);
        localStorage.setItem('enkelvoud_theme', themeSelect.value);
    });

    const toggleBtn = document.getElementById('toggleBtn');
    const statusBadge = document.getElementById('status');
    const serverHostInput = document.getElementById('serverHost');
    const friendlyNameInput = document.getElementById('friendlyName');
    const deviceDisplayTitle = document.getElementById('deviceDisplayTitle');
    const offsetVal = document.getElementById('offsetVal');
    const packetVal = document.getElementById('packetVal');

    serverHostInput.value = window.location.host || window.location.hostname;
    friendlyNameInput.value = currentAssignedName;
    deviceDisplayTitle.textContent = `Node: ${currentAssignedName}`;
    friendlyNameInput.addEventListener('change', () => {
        const val = friendlyNameInput.value.trim();
        if (!val) return;
        currentAssignedName = val;
        localStorage.setItem('enkelvoud_friendly_name', val);
        deviceDisplayTitle.textContent = `Node: ${val}`;
        registerNode();
    });

    toggleBtn.addEventListener('click', async () => {
        if (!isRunning) await startReceiver();
        else stopReceiver();
    });

    async function registerNode() {
        try {
            await fetch('/api/nodes', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    node_uuid: nodeUuid,
                    name: currentAssignedName,
                    ip: 'phone'
                })
            });
        } catch (e) {
            logMessage('Node register failed: ' + e.message);
        }
    }

    async function startReceiver() {
        const host = serverHostInput.value.trim() || window.location.host;
        logMessage('Connecting to ws://' + host + '/pcm');
        try {
            const AudioContextClass = window.AudioContext || window.webkitAudioContext;
            audioCtx = new AudioContextClass({ sampleRate: 48000 });
            if (audioCtx.state === 'suspended') await audioCtx.resume();

            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            wsOrSocket = new WebSocket(`${protocol}//${host}/pcm`);
            wsOrSocket.binaryType = 'arraybuffer';

            wsOrSocket.onopen = () => {
                isRunning = true;
                nextPlayTime = 0;
                toggleBtn.textContent = 'Stop Player';
                toggleBtn.classList.add('connected');
                statusBadge.textContent = 'Streaming Audio';
                statusBadge.className = 'status-badge active';
                logMessage('HTTP player connected. Enable Stream on the control panel.');
                registerNode();
                heartbeatTimer = setInterval(registerNode, 10000);
            };

            wsOrSocket.onmessage = async (event) => {
                if (typeof event.data === 'string') return;
                packetCount++;
                packetVal.textContent = packetCount;
                await handlePcmPacket(event.data);
            };
            wsOrSocket.onerror = () => {
                statusBadge.textContent = 'Error';
                statusBadge.className = 'status-badge error';
                logMessage('WebSocket error');
            };
            wsOrSocket.onclose = () => {
                logMessage('WebSocket closed');
                stopReceiver();
            };
        } catch (err) {
            logMessage('Startup failed: ' + err.message);
            stopReceiver();
        }
    }

    async function handlePcmPacket(buffer) {
        if (!audioCtx || !isRunning || nodeMuted) return;
        if (audioCtx.state === 'suspended') await audioCtx.resume();
        const int16View = new Int16Array(buffer);
        if (int16View.length < 2) return;
        const numSamples = int16View.length / 2;
        const audioBuffer = audioCtx.createBuffer(2, numSamples, 48000);
        const leftChannel = audioBuffer.getChannelData(0);
        const rightChannel = audioBuffer.getChannelData(1);
        for (let i = 0, j = 0; i < numSamples; i++, j += 2) {
            leftChannel[i] = int16View[j] / 32768.0;
            rightChannel[i] = int16View[j + 1] / 32768.0;
        }
        const source = audioCtx.createBufferSource();
        source.buffer = audioBuffer;
        const gainNode = audioCtx.createGain();
        gainNode.gain.value = isFinite(nodeVolume) ? nodeVolume : 1.0;
        source.connect(gainNode);
        gainNode.connect(audioCtx.destination);
        const currentTime = audioCtx.currentTime;
        if (nextPlayTime < currentTime) nextPlayTime = currentTime + 0.08;
        source.start(nextPlayTime);
        nextPlayTime += audioBuffer.duration;
        offsetVal.textContent = Math.round((nextPlayTime - currentTime) * 1000) + ' ms';
    }

    function stopReceiver() {
        isRunning = false;
        packetCount = 0;
        nextPlayTime = 0;
        if (heartbeatTimer) clearInterval(heartbeatTimer);
        heartbeatTimer = null;
        if (wsOrSocket) {
            wsOrSocket.onclose = null;
            wsOrSocket.close();
            wsOrSocket = null;
        }
        if (audioCtx) {
            audioCtx.close();
            audioCtx = null;
        }
        toggleBtn.textContent = 'Start Audio Player';
        toggleBtn.classList.remove('connected');
        statusBadge.textContent = 'Disconnected';
        statusBadge.className = 'status-badge';
    }
</script>
</body>
</html>
)rawliteral";

#endif
