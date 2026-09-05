#ifndef EVDPLR_H
#define EVDPLR_H

#include <Arduino.h>

const char PLAYER_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>Enkelvoud Audio Receiver</title>
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
            --bg-color: #f1f5f9;
            --card-bg: #ffffff;
            --text-main: #0f172a;
            --text-sub: #64748b;
            --accent: #0284c7;
            --accent-hover: #0369a1;
            --danger: #dc2626;
            --border: #cbd5e1;
            --metric-bg: #f8fafc;
            --title-color: #0284c7;
        }

        [data-theme="blue"] {
            --bg-color: #091e3a;
            --card-bg: #132f4c;
            --text-main: #e2e8f0;
            --text-sub: #90cdf4;
            --accent: #3182ce;
            --accent-hover: #2b6cb0;
            --danger: #e53e3e;
            --border: #2c5282;
            --metric-bg: #091e3a;
            --title-color: #63b3ed;
        }

        * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
        body { background-color: var(--bg-color); color: var(--text-main); display: flex; flex-direction: column; justify-content: center; align-items: center; min-height: 100vh; padding: 20px; transition: background 0.3s ease, color 0.3s ease; }
        .card { background: var(--card-bg); padding: 24px; border-radius: 16px; box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.3); width: 100%; max-width: 420px; text-align: center; transition: background 0.3s ease; }
        .back-link { display: block; text-align: left; color: var(--text-sub); text-decoration: none; font-size: 0.85rem; margin-bottom: 15px; transition: color 0.2s; }
        .back-link:hover { color: var(--text-main); }
        h1 { font-size: 1.5rem; margin-bottom: 2px; color: var(--title-color); }
        p.subtitle { font-size: 0.875rem; color: var(--text-sub); margin-bottom: 20px; }
        .input-group { margin-bottom: 12px; text-align: left; }
        label { display: block; font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.05em; color: var(--text-sub); margin-bottom: 4px; }
        input, select { width: 100%; padding: 10px; border-radius: 8px; border: 1px solid var(--border); background: var(--metric-bg); color: var(--text-main); font-size: 0.95rem; outline: none; transition: background 0.3s ease, color 0.3s ease, border-color 0.3s ease; }
        input:focus, select:focus { border-color: var(--title-color); }
        .btn { width: 100%; padding: 14px; border-radius: 8px; border: none; background: var(--accent); color: white; font-size: 1rem; font-weight: 600; cursor: pointer; transition: background 0.2s ease; margin-top: 8px; }
        .btn:active { background: var(--accent-hover); }
        .btn.connected { background: var(--danger); }
        .status-badge { display: inline-block; margin-top: 16px; padding: 6px 12px; border-radius: 20px; font-size: 0.85rem; background: var(--border); color: var(--text-sub); }
        .status-badge.active { background: #166534; color: #4ade80; }
        .status-badge.error { background: #991b1b; color: #fca5a5; }
        .metrics { margin-top: 16px; display: grid; grid-template-columns: 1fr 1fr; gap: 8px; font-size: 0.75rem; color: var(--text-sub); }
        .metric-box { background: var(--metric-bg); padding: 8px; border-radius: 6px; transition: background 0.3s ease; }
        
        .log-container {
            margin-top: 16px;
            text-align: left;
            background: #000;
            color: #00ff66;
            font-family: monospace;
            font-size: 0.7rem;
            padding: 10px;
            border-radius: 8px;
            height: 120px;
            overflow-y: scroll;
            border: 1px solid var(--border);
        }
    </style>
</head>
<body>

<div class="card">
    <a href="/control" class="back-link">&larr; Back to Control</a>
    <h1>Enkelvoud Receiver</h1>
    <p class="subtitle" id="deviceDisplayTitle">Multi-room Audio Node</p>

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
        <label for="serverIp">Server IP / Hostname</label>
        <input type="text" id="serverIp" placeholder="e.g. 192.168.1.50" autocomplete="off" spellcheck="false">
    </div>

    <div class="input-group">
        <label for="serverPort">WebSocket Port</label>
        <input type="number" id="serverPort" value="8081" autocomplete="off">
    </div>

    <button id="toggleBtn" class="btn">Start Audio Receiver</button>

    <div id="status" class="status-badge">Disconnected</div>

    <div class="metrics">
        <div class="metric-box">
            <div>Clock Offset</div>
            <strong id="offsetVal" style="color: var(--text-main);">0 ms</strong>
        </div>
        <div class="metric-box">
            <div>Packets Received</div>
            <strong id="packetVal" style="color: var(--text-main);">0</strong>
        </div>
    </div>

    <div class="log-container" id="logBox"><div>System ready. Click Start Audio Receiver.</div></div>
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
    let packetCount = 0;
    let hostVolume = 1.0;
    let hostMuted = false;
    
    let storedVol = parseFloat(localStorage.getItem('enkelvoud_node_volume'));
    let nodeVolume = (!isNaN(storedVol) && storedVol !== null) ? storedVol : 1.0;
    let nodeMuted = localStorage.getItem('enkelvoud_node_muted') === 'true';

    let nextPlayTime = 0;

    let nodeUuid = localStorage.getItem('enkelvoud_node_uuid');
    if (!nodeUuid) {
        nodeUuid = 'node_' + Math.random().toString(36).substring(2, 15) + Math.random().toString(36).substring(2, 15);
        localStorage.setItem('enkelvoud_node_uuid', nodeUuid);
    }

    let currentAssignedName = localStorage.getItem('enkelvoud_friendly_name') || 'EnkelvoudNode';

    const themeSelect = document.getElementById('themeSelect');
    const savedTheme = localStorage.getItem('enkelvoud_theme') || 'black';
    document.documentElement.setAttribute('data-theme', savedTheme);
    themeSelect.value = savedTheme;

    themeSelect.addEventListener('change', () => {
        const selectedTheme = themeSelect.value;
        document.documentElement.setAttribute('data-theme', selectedTheme);
        localStorage.setItem('enkelvoud_theme', selectedTheme);
    });

    const toggleBtn = document.getElementById('toggleBtn');
    const statusBadge = document.getElementById('status');
    const serverIpInput = document.getElementById('serverIp');
    const serverPortInput = document.getElementById('serverPort');
    const friendlyNameInput = document.getElementById('friendlyName');
    const deviceDisplayTitle = document.getElementById('deviceDisplayTitle');
    const offsetVal = document.getElementById('offsetVal');
    const packetVal = document.getElementById('packetVal');

    serverIpInput.value = window.location.hostname || 'localhost';
    friendlyNameInput.value = currentAssignedName;
    deviceDisplayTitle.textContent = `Node: ${currentAssignedName}`;

    friendlyNameInput.addEventListener('change', () => {
        const val = friendlyNameInput.value.trim();
        if (val) {
            currentAssignedName = val;
            localStorage.setItem('enkelvoud_friendly_name', val);
            deviceDisplayTitle.textContent = `Node: ${val}`;
            logMessage(`Friendly name updated: ${val}`);
        }
    });

    toggleBtn.addEventListener('click', async () => {
        if (!isRunning) {
            await startReceiver();
        } else {
            stopReceiver();
        }
    });

    const originalFetch = window.fetch;
    window.fetch = async function(resource, init) {
        const urlStr = typeof resource === 'string' ? resource : resource.url;
        if (urlStr && urlStr.includes('/api/volume')) {
            if (init && init.method === 'POST') {
                try {
                    const body = JSON.parse(init.body);
                    if (body.volume !== undefined) {
                        nodeVolume = parseFloat(body.volume);
                        if (isNaN(nodeVolume)) nodeVolume = 1.0;
                        localStorage.setItem('enkelvoud_node_volume', nodeVolume);
                    }
                    if (body.muted !== undefined) {
                        nodeMuted = Boolean(body.muted);
                        localStorage.setItem('enkelvoud_node_muted', nodeMuted);
                    }
                } catch (e) {}
                return new Response(JSON.stringify({ status: 'ok', volume: nodeVolume, muted: nodeMuted }), {
                    status: 200, headers: { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' }
                });
            } else {
                return new Response(JSON.stringify({ volume: nodeVolume, muted: nodeMuted }), {
                    status: 200, headers: { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' }
                });
            }
        }
        return originalFetch.apply(this, arguments);
    };

    async function startReceiver() {
        const ip = serverIpInput.value.trim();
        const port = serverPortInput.value.trim();
        if (!ip) { alert("Please enter server IP."); return; }

        logMessage(`Starting receiver targeting ws://${ip}:${port}...`);

        try {
            const AudioContextClass = window.AudioContext || window.webkitAudioContext;
            audioCtx = new AudioContextClass();
            logMessage(`AudioContext state: ${audioCtx.state}`);

            if (audioCtx.state === 'suspended') {
                await audioCtx.resume();
                logMessage(`Resumed AudioContext, new state: ${audioCtx.state}`);
            }

            const silentBuffer = audioCtx.createBuffer(1, 1, 22050);
            const silentSource = audioCtx.createBufferSource();
            silentSource.buffer = silentBuffer;
            silentSource.connect(audioCtx.destination);
            silentSource.start(0);

            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            wsOrSocket = new WebSocket(`${protocol}//${ip}:${port}`);
            wsOrSocket.binaryType = 'arraybuffer';

            wsOrSocket.onopen = () => {
                isRunning = true;
                nextPlayTime = 0;
                toggleBtn.textContent = "Stop Receiver";
                toggleBtn.classList.add('connected');
                statusBadge.textContent = "Streaming Audio";
                statusBadge.className = 'status-badge active';
                logMessage("WebSocket connection opened successfully.");

                registerNode();
            };

            wsOrSocket.onmessage = async (event) => {
                try {
                    if (typeof event.data === 'string') {
                        const msg = JSON.parse(event.data);
                        if (msg.type === 'host_audio_sync') {
                            if (msg.volume !== undefined && !isNaN(parseFloat(msg.volume))) hostVolume = parseFloat(msg.volume);
                            if (msg.muted !== undefined) hostMuted = Boolean(msg.muted);
                            logMessage(`Host audio sync -> Volume: ${hostVolume}, Muted: ${hostMuted}`);
                        }
                    } else if (event.data instanceof ArrayBuffer) {
                        packetCount++;
                        packetVal.textContent = packetCount;
                        await handleAudioPacket(event.data);
                    }
                } catch (err) {
                    logMessage(`Error handling message: ${err.message}`);
                }
            };

            wsOrSocket.onerror = (e) => { 
                statusBadge.textContent = "Error"; 
                statusBadge.className = 'status-badge error'; 
                logMessage("WebSocket error encountered.");
            };
            
            wsOrSocket.onclose = () => { 
                logMessage("WebSocket connection closed.");
                stopReceiver(); 
            };

        } catch (err) {
            logMessage(`Startup failed: ${err.message}`);
            stopReceiver();
        }
    }

    function registerNode() {
        if (wsOrSocket && wsOrSocket.readyState === WebSocket.OPEN) {
            wsOrSocket.send(JSON.stringify({
                type: 'register_node',
                node_uuid: nodeUuid,
                node_id: currentAssignedName
            }));
            logMessage(`Registered node as: ${currentAssignedName}`);
        }
    }

    async function handleAudioPacket(buffer) {
        if (!audioCtx || !isRunning) return;

        if (audioCtx.state === 'suspended') {
            await audioCtx.resume();
        }
        
        if (hostMuted || nodeMuted) {
            return; 
        }

        try {
            const int16View = new Int16Array(buffer);
            if (int16View.length === 0) return;

            const numSamples = int16View.length / 2;
            const sampleRate = 48000;
            const audioBuffer = audioCtx.createBuffer(2, numSamples, sampleRate);
            
            const leftChannel = audioBuffer.getChannelData(0);
            const rightChannel = audioBuffer.getChannelData(1);

            for (let i = 0, j = 0; i < numSamples; i++, j += 2) {
                leftChannel[i] = int16View[j] / 32768.0;
                rightChannel[i] = int16View[j + 1] / 32768.0;
            }

            const source = audioCtx.createBufferSource();
            source.buffer = audioBuffer;

            const gainNode = audioCtx.createGain();
            let computedGain = (isFinite(hostVolume) ? hostVolume : 1.0) * (isFinite(nodeVolume) ? nodeVolume : 1.0);
            gainNode.gain.value = isFinite(computedGain) ? computedGain : 1.0;

            source.connect(gainNode);
            gainNode.connect(audioCtx.destination);

            const currentTime = audioCtx.currentTime;
            if (nextPlayTime < currentTime) {
                nextPlayTime = currentTime + 0.08;
            }

            source.start(nextPlayTime);
            nextPlayTime += audioBuffer.duration;

        } catch (err) {
            logMessage(`Audio processing error: ${err.message}`);
        }
    }

    function stopReceiver() {
        isRunning = false;
        packetCount = 0;
        nextPlayTime = 0;
        if (wsOrSocket) wsOrSocket.close();
        if (audioCtx) audioCtx.close();

        toggleBtn.textContent = "Start Audio Receiver";
        toggleBtn.classList.remove('connected');
        statusBadge.textContent = "Disconnected";
        statusBadge.className = 'status-badge';
        logMessage("Receiver stopped.");
    }
</script>
</body>
</html>
)rawliteral";

#endif