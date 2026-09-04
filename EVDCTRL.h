#ifndef EVDCTRL_H
#define EVDCTRL_H

#include <Arduino.h>

const char CONTROL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Enkelvoud Control Panel</title>
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

        h1 {
            font-size: 1.5rem;
            margin: 0;
            color: var(--text-color);
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
    </style>
</head>
<body>
    <div class="container">
        <div class="card" style="display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 15px; padding: 15px 20px;">
            <div style="display: flex; align-items: center; gap: 15px; flex-wrap: wrap;">
                <button class="btn-toggle" id="streamToggleBtn" onclick="toggleStreaming()">Stream: OFF</button>
                <div style="display: flex; align-items: center; gap: 8px;">
                    <label style="font-size: 0.85rem; font-weight: 600;">Audio In:</label>
                    <select class="input-select" id="audioInputSelect" onchange="changeAudioInput(this.value)">
                        <option value="USB">USB</option>
                        <option value="Bluetooth">Bluetooth</option>
                        <option value="AUX in">AUX in</option>
                        <option value="Stream Radio Test">Stream Radio Test</option>
                    </select>
                </div>
            </div>
            <div style="display: flex; align-items: center; gap: 8px;">
                <a href="/player" class="action-btn" title="Audio Player" style="text-decoration: none;">🔊 Player</a>
                <a href="/settings" class="action-btn" title="Settings" style="text-decoration: none;">⚙️ Settings</a>
            </div>
        </div>

        <div class="header-actions">
            <div style="display: flex; align-items: center; gap: 15px;">
                <h1>Enkelvoud Control Panel</h1>
                <button class="action-btn" id="globalMuteBtn" onclick="toggleGlobalMute()">Mute Server</button>
            </div>
            <div class="header-controls">
                <select class="theme-select" id="themeSelector" onchange="changeTheme(this.value)">
                    <option value="black">Black (Default)</option>
                    <option value="light">Light</option>
                    <option value="blue">Blue</option>
                </select>
                <button class="btn-primary" onclick="createNewGroup()">+ New Group</button>
            </div>
        </div>
        
        <div class="card">
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

    <script>
        const groupList = document.getElementById('groupList');
        const logWindow = document.getElementById('logWindow');
        let currentNodesData = {};
        const groupVolumes = {};
        let activeSliderGroup = null;
        let currentEditingGroup = null;

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

        async function fetchState() {
            if (document.getElementById('renameModal').classList.contains('active')) return;
            if (activeSliderGroup !== null) return;

            try {
                const res = await fetch('/api/state?' + new Date().getTime(), { cache: 'no-store' });
                if (!res.ok) throw new Error('Network response was not ok');
                const data = await res.json();
                currentNodesData = data.nodes || {};
                
                const globalMuteBtn = document.getElementById('globalMuteBtn');
                if (data.host_muted) {
                    globalMuteBtn.textContent = "Unmute Server";
                    globalMuteBtn.classList.add('muted');
                } else {
                    globalMuteBtn.textContent = "Mute Server";
                    globalMuteBtn.classList.remove('muted');
                }

                const streamBtn = document.getElementById('streamToggleBtn');
                if (data.streaming_enabled) {
                    streamBtn.textContent = "Stream: ON";
                    streamBtn.classList.add('active');
                } else {
                    streamBtn.textContent = "Stream: OFF";
                    streamBtn.classList.remove('active');
                }

                const audioInputSelect = document.getElementById('audioInputSelect');
                if (data.audio_input && document.activeElement !== audioInputSelect) {
                    audioInputSelect.value = data.audio_input;
                }

                if (data.logs && Array.isArray(data.logs)) {
                    logWindow.innerHTML = data.logs.map(log => `<div>${escapeHtml(log)}</div>`).join('');
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
                groups.add(info.group || 'Default Room');
            }

            if (groups.size <= 1) {
                alert("Cannot delete the last remaining room.");
                return;
            }

            if (!confirm(`Delete room "${groupName}"? All nodes will move to "Default Room".`)) return;

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
            const groups = {};
            for (const [uuid, info] of Object.entries(nodes)) {
                const g = info.group || 'Default Room';
                if (!groups[g]) groups[g] = [];
                groups[g].push({ uuid, ...info });
            }

            let html = '';
            for (const [groupName, nodeList] of Object.entries(groups)) {
                if (groupVolumes[groupName] === undefined) groupVolumes[groupName] = 100;
                
                html += `
                    <div class="group-container">
                        <div class="group-header">
                            <div class="group-title-wrapper">
                                <button class="group-title-clickable" onclick="openRenameModal('${escapeHtml(groupName)}')">${escapeHtml(groupName)}</button>
                            </div>
                            <div class="group-controls">
                                <span>Vol: <span id="volLabel_${escapeHtml(groupName)}">${groupVolumes[groupName]}</span>%</span>
                                <input type="range" min="0" max="100" value="${groupVolumes[groupName]}" oninput="activeSliderGroup='${escapeHtml(groupName)}'; document.getElementById('volLabel_${escapeHtml(groupName)}').innerText = this.value; updateGroupVolume('${escapeHtml(groupName)}', this.value);" onchange="activeSliderGroup=null;">
                                <button class="action-btn danger" onclick="deleteGroup('${escapeHtml(groupName)}')">Delete Room</button>
                            </div>
                        </div>
                        <div class="node-list">
                `;

                nodeList.forEach(node => {
                    html += `
                        <div class="node-item">
                            <div class="node-info-inline">
                                <span class="node-name">${escapeHtml(node.name || 'Node')}</span>
                                <div class="node-ip-container">
                                    <a class="node-ip-link" href="http://${node.ip}" target="_blank">${node.ip}</a>
                                    <button class="action-btn danger" onclick="deleteNode('${node.uuid}')">Remove</button>
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

        async function updateGroupVolume(groupName, val) {
            groupVolumes[groupName] = parseInt(val);
            await fetch('/api/control', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ action: 'set_group_volume', group: groupName, volume: parseInt(val) / 100.0 })
            });
        }

        setInterval(fetchState, 3000);
        fetchState();
    </script>
</body>
</html>
)rawliteral";

#endif
