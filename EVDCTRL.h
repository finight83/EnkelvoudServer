#pragma once

static const char EVDCTRL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Enkelvoud Dashboard</title>
  <style>
    :root {
      --text-main: #fff;
      --text-muted: #cbd5e1;
      --accent-primary: #00d2ff;
      --accent-secondary: #8e2de2;
      --glass-bg: linear-gradient(135deg, rgba(255,255,255,0.12) 0%, rgba(255,255,255,0.02) 100%);
      --glass-border-light: rgba(255,255,255,0.3);
      --glass-border-dim: rgba(255,255,255,0.05);
      --danger: #ef4444;
      --success: #10b981;
      --warn: #fbbf24;
    }
    * { margin: 0; padding: 0; box-sizing: border-box; font-family: 'Inter', sans-serif; }

    #top-loading-bar {
      position: fixed; top: 0; left: 0; right: 0; height: 3px;
      background: linear-gradient(90deg, #00d2ff, #8e2de2);
      box-shadow: 0 0 8px rgba(0,210,255,0.6);
      transform-origin: left center; transform: scaleX(0);
      opacity: 0; transition: transform .25s ease, opacity .3s ease;
      z-index: 9999; pointer-events: none;
    }
    #top-loading-bar.active { opacity: 1; transform: scaleX(0.3); transition: transform .1s ease, opacity .1s ease; }
    #top-loading-bar.done   { opacity: 0; transform: scaleX(1); transition: transform .2s ease, opacity .3s ease .2s; }

    body {
      background-color: #050510; color: var(--text-main);
      min-height: 100vh; display: flex; justify-content: center; align-items: flex-start;
      overflow-x: hidden; overflow-y: auto; -webkit-overflow-scrolling: touch;
      position: relative; padding: 20px;
    }

    .galaxy-bg { position: fixed; top: 0; left: 0; width: 100vw; height: 100vh; overflow: hidden; z-index: 1; pointer-events: none; }
    .orb { position: absolute; border-radius: 50%; filter: blur(70px); opacity: 0.75; will-change: transform; }
    .orb-1 { width: 400px; height: 400px; background: #4a00e0; top: -100px; left: -100px; animation: float-a 18s infinite ease-in-out alternate; }
    .orb-2 { width: 500px; height: 500px; background: #8e2de2; bottom: -150px; right: -100px; animation: float-b 22s infinite ease-in-out alternate; animation-delay: -6s; }
    .orb-3 { width: 300px; height: 300px; background: #00d2ff; top: 40%; left: 45%; animation: float-c 15s infinite ease-in-out alternate; animation-delay: -9s; }
    @keyframes float-a { 0%{transform:translate(0,0) scale(1);} 50%{transform:translate(120px,-90px) scale(1.15);} 100%{transform:translate(-40px,60px) scale(.95);} }
    @keyframes float-b { 0%{transform:translate(0,0) scale(1);} 50%{transform:translate(-140px,80px) scale(1.1);} 100%{transform:translate(60px,-40px) scale(.9);} }
    @keyframes float-c { 0%{transform:translate(-50%,-50%) scale(1);} 50%{transform:translate(calc(-50% + 100px),calc(-50% - 70px)) scale(1.2);} 100%{transform:translate(calc(-50% - 50px),calc(-50% + 50px)) scale(.85);} }

    .stars, .stars::before, .stars::after { position: absolute; inset: 0; background-repeat: repeat; animation: twinkle 3.6s infinite ease-in-out; }
    .stars { background-image: radial-gradient(1px 1px at 20px 30px,#fff,transparent), radial-gradient(1px 1px at 40px 70px,#fff,transparent), radial-gradient(1.5px 1.5px at 90px 40px,#fff,transparent); background-size: 200px 200px; opacity:.55; }
    .stars::before { content:''; background-image: radial-gradient(1.5px 1.5px at 50px 160px,#fff,transparent), radial-gradient(1px 1px at 130px 80px,#fff,transparent), radial-gradient(2px 2px at 160px 120px,#fff,transparent); background-size: 250px 250px; opacity:.35; animation-duration: 5.2s; animation-delay: -1.4s; }
    .stars::after { content:''; background-image: radial-gradient(1px 1px at 15px 90px,#fff,transparent), radial-gradient(1px 1px at 110px 20px,#fff,transparent), radial-gradient(1.5px 1.5px at 70px 200px,#fff,transparent); background-size: 180px 180px; opacity:.45; animation-duration: 6.5s; animation-delay: -3.1s; }
    @keyframes twinkle { 0%,100%{opacity:.2;} 50%{opacity:.85;} }

    .dashboard-container {
      position: relative; z-index: 10; width: 100%; max-width: 900px;
      background: var(--glass-bg);
      backdrop-filter: blur(20px) saturate(180%); -webkit-backdrop-filter: blur(20px) saturate(180%);
      border-radius: 24px;
      border-top: 1px solid var(--glass-border-light); border-left: 1px solid var(--glass-border-light);
      border-bottom: 1px solid var(--glass-border-dim); border-right: 1px solid var(--glass-border-dim);
      box-shadow: 0 25px 50px rgba(0,0,0,0.5), inset 0 0 15px rgba(255,255,255,0.05);
      overflow: clip; display: flex; flex-direction: column; margin: 20px 0;
    }

    .top-bar { display: flex; align-items: center; padding: 14px 30px 6px; gap: 14px; flex-wrap: wrap; }
    .brand { flex: 0 0 auto; min-width: 0; }
    .page-title { font-size: 1.6rem; font-weight: 700; letter-spacing: 0.5px; background: linear-gradient(to right,#00d2ff,#8e2de2); -webkit-background-clip: text; -webkit-text-fill-color: transparent; text-shadow: 0 2px 10px rgba(0,210,255,0.3); }
    .page-title { cursor: pointer; user-select: none; display: flex; align-items: center; gap: 8px; }
    .page-title svg { color: var(--text-muted); opacity: .5; transition: opacity .2s, color .2s; fill: currentColor; }
    .page-title:hover svg { opacity: 1 !important; color: #fff; }

    .menu-bar { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; flex: 1 1 auto; padding: 0; }
    .nav-btn { background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.1); color: var(--text-muted); padding: 10px 20px; border-radius: 30px; font-size: .9rem; font-weight: 500; cursor: pointer; transition: all .3s ease; display: flex; align-items: center; gap: 8px; backdrop-filter: blur(5px); }
    .nav-btn:hover { background: rgba(255,255,255,0.1); color: var(--text-main); }
    .nav-btn.active { background: linear-gradient(135deg, rgba(255,255,255,0.2), rgba(255,255,255,0.05)); color: #fff; border-top: 1px solid rgba(255,255,255,0.6); border-left: 1px solid rgba(255,255,255,0.6); box-shadow: 0 0 15px rgba(142,45,226,0.4); text-shadow: 0 1px 5px rgba(0,210,255,0.5); }
    .nav-btn-primary { background: linear-gradient(135deg, rgba(0,210,255,0.35), rgba(142,45,226,0.35)); color: #fff; border-color: rgba(255,255,255,0.28); box-shadow: 0 0 12px rgba(0,210,255,0.3); }
    .nav-btn-primary:hover { background: linear-gradient(135deg, rgba(0,210,255,0.55), rgba(142,45,226,0.55)); }

    .master-label { font-size: 0.85rem; color: var(--text-muted); text-transform: uppercase; letter-spacing: 1px; white-space: nowrap; }
    .master-label b { color: var(--text-main); margin-left: 6px; }

    .tab-content { display: none; padding: 24px 30px; animation: fadeIn 0.4s ease forwards; }
    .tab-content.active { display: block; }
    @keyframes fadeIn { from{opacity:0; transform:translateY(10px);} to{opacity:1; transform:translateY(0);} }

    .section-title { font-size: 1.2rem; margin-bottom: 20px; font-weight: 600; letter-spacing: .5px; background: linear-gradient(to right,#00d2ff,#8e2de2); -webkit-background-clip: text; -webkit-text-fill-color: transparent; text-shadow: 0 2px 10px rgba(0,210,255,0.2); }

    .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
    .control-card { background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.1); border-top: 1px solid rgba(255,255,255,0.2); border-radius: 16px; padding: 20px; box-shadow: inset 0 2px 10px rgba(255,255,255,0.02); }
    .data-label { font-size: 0.8rem; color: var(--text-muted); text-transform: uppercase; letter-spacing: 1px; margin-bottom: 8px; }
    .data-value { font-size: 1.4rem; font-weight: 600; text-shadow: 0 1px 5px rgba(0,0,0,0.5); }
    .muted { color: var(--text-muted); font-size: 0.85rem; }

    .checkbox { display: flex; gap: 10px; align-items: center; cursor: pointer; }
    .checkbox input[type=checkbox] { width: 18px; height: 18px; accent-color: var(--accent-primary); flex: 0 0 auto; margin: 0; }
    .check { display: flex; align-items: center; gap: 10px; min-height: 44px; cursor: pointer; }
    .check input[type=checkbox] { width: 20px; height: 20px; accent-color: var(--accent-primary); flex: 0 0 auto; }

    .btn { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.2); color: var(--text-main); padding: 10px 18px; border-radius: 8px; font: inherit; font-size: .92rem; font-weight: 600; cursor: pointer; min-height: 44px; transition: all .2s ease; text-decoration: none; display: inline-block; }
    .btn:hover { background: rgba(255,255,255,0.15); }
    .btn:active { transform: translateY(1px); }
    .btn:disabled { opacity: .5; cursor: default; }
    .btn-sm { padding: 8px 14px; font-size: .85rem; min-height: 38px; }

    .grid { display: grid; grid-template-columns: 1fr; gap: 20px; }
    @media (min-width: 720px) { .grid { grid-template-columns: 1fr 1fr; gap: 20px; } .grid.single { grid-template-columns: 1fr; } }

    .field { margin-top: 14px; }
    .field:first-child { margin-top: 0; }
    .field > label { display: block; color: var(--text-muted); font-size: .82rem; margin-bottom: 6px; text-transform: uppercase; letter-spacing: .5px; }
    input[type=text], input[type=password], input[type=number] {
      width: 100%; padding: 11px 12px; background: rgba(0,0,0,.42); border: 1px solid rgba(255,255,255,0.12);
      border-radius: 10px; color: var(--text-main); font: inherit; font-size: 16px; min-height: 44px; transition: all .2s ease;
    }
    input[type=text]:focus, input[type=password]:focus, input[type=number]:focus { outline: none; border-color: var(--accent-primary); box-shadow: 0 0 0 3px rgba(0,210,255,.18); }

    .btn-glass { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.2); color: var(--text-main); padding: 8px 16px; border-radius: 8px; cursor: pointer; text-decoration: none; display: inline-block; }
    .btn-glass:hover { background: rgba(255,255,255,0.15); }
    .btn-glass:disabled { opacity: .4; cursor: not-allowed; }
    .btn-mute-active { background: var(--danger) !important; border-color: var(--danger) !important; color: #fff !important; box-shadow: 0 0 10px var(--danger); }

    /* ---- Zones controls (mode/source/volume/mute, only visible on Zones) --- */
    .zones-controls { padding: 16px 30px 4px; display: flex; flex-direction: column; gap: 14px; }
    .zones-mode-row { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
    .zones-volume-row { display: flex; gap: 12px; align-items: center; }
    .zones-volume-row #volume {
      flex: 1 1 auto; -webkit-appearance: none;
      height: 6px; background: rgba(255,255,255,0.2);
      border-radius: 3px; outline: none;
      box-shadow: inset 0 1px 3px rgba(0,0,0,0.5);
    }
    .zones-volume-row #volume::-webkit-slider-thumb {
      -webkit-appearance: none; width: 18px; height: 18px; border-radius: 50%;
      background: #fff; cursor: pointer; border: 2px solid var(--accent-secondary);
      box-shadow: 0 0 15px rgba(0,210,255,0.8);
    }
    .zones-volume-row #volume::-moz-range-thumb {
      width: 18px; height: 18px; border-radius: 50%; background: #fff; cursor: pointer;
      border: 2px solid var(--accent-secondary); box-shadow: 0 0 15px rgba(0,210,255,0.8);
    }
    .zones-volume-row .icon-btn { flex: 0 0 auto; }

    .mode-select {
      appearance: none; -webkit-appearance: none;
      background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.14);
      color: var(--text-main); padding: 8px 30px 8px 14px; border-radius: 20px;
      font-size: .82rem; font-weight: 600; cursor: pointer; line-height: 1.2;
      background-image: url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='10' height='6' viewBox='0 0 10 6'><path d='M0 0l5 6 5-6z' fill='%23cbd5e1'/></svg>");
      background-repeat: no-repeat; background-position: right 11px center;
    }
    .mode-select:focus { outline: none; border-color: var(--accent-primary); }
    .mode-select option { background:#111116; color:#f6f6f8; }

    .zone-toolbar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }
    .zone-toolbar .section-title { margin: 0; }
    .zone-container { display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 20px; }
    .zone-box { background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.1); border-radius: 16px; padding: 18px; min-height: 200px; display: flex; flex-direction: column; transition: all 0.3s ease; }
    .zone-box.drag-over { border-color: var(--accent-primary); background: rgba(0,210,255,0.08); }
    .zone-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; border-bottom: 1px solid rgba(255,255,255,0.1); padding-bottom: 8px; gap: 6px; }
    .zone-header input { background: transparent; border: none; color: #fff; font-size: 1.1rem; font-weight: 600; width: 60%; }
    .zone-header input:focus { outline: 1px solid var(--accent-primary); border-radius: 4px; padding: 2px; }
    .zone-controls { margin-bottom: 12px; }
    .zone-row { display: flex; justify-content: space-between; font-size: 0.75rem; color: var(--text-muted); margin-bottom: 4px; align-items: center; }
    .zone-controls input[type=range] { width: 100%; -webkit-appearance: none; height: 5px; background: rgba(255,255,255,0.2); border-radius: 3px; outline: none; }
    .zone-controls input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 14px; height: 14px; border-radius: 50%; background: #fff; cursor: pointer; border: 2px solid var(--accent-secondary); }
    .zone-actions { display: flex; gap: 8px; margin-top: 8px; justify-content: space-between; }

    .node-chip { background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.15); border-radius: 12px; padding: 10px 12px; margin-bottom: 10px; cursor: grab; transition: transform 0.2s; display: flex; justify-content: space-between; align-items: center; gap: 10px; }
    .node-chip:active { cursor: grabbing; }
    .node-chip:hover { background: rgba(255,255,255,0.12); transform: translateY(-2px); }
    .node-meta { min-width: 0; }
    .node-meta strong { display: block; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .node-ip { font-size: 0.75rem; color: var(--text-muted); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .gear { flex: 0 0 auto; width: 34px; height: 34px; border-radius: 50%; display: flex; align-items: center; justify-content: center; background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.15); color: var(--accent-primary); font-size: 1.15rem; text-decoration: none; cursor: pointer; transition: all 0.2s; }
    .gear:hover { background: rgba(0,210,255,0.15); color: #fff; transform: scale(1.08); }
    .node-chip.offline { opacity: 0.5; filter: grayscale(0.75); }
    .node-chip.offline:hover { background: rgba(255,255,255,0.06); transform: none; }
    .node-chip.offline .node-meta strong { font-weight: 500; }
    .gear.disabled, .node-chip.offline .gear { pointer-events: none; cursor: not-allowed; opacity: 0.35; color: var(--text-muted); background: rgba(255,255,255,0.03); border-color: rgba(255,255,255,0.08); transform: none; }

    /* ---- Sub-tabs inside Advanced ------------------------------------- */
    .sub-tab-bar { display: flex; gap: 6px; margin-bottom: 20px; flex-wrap: wrap; padding: 4px; background: rgba(0,0,0,0.25); border-radius: 30px; border: 1px solid rgba(255,255,255,0.08); }
    .sub-tab-btn { background: transparent; border: 1px solid transparent; color: var(--text-muted); padding: 8px 18px; border-radius: 24px; font-size: .85rem; font-weight: 600; cursor: pointer; transition: all .2s ease; }
    .sub-tab-btn:hover { color: var(--text-main); }
    .sub-tab-btn.active { background: linear-gradient(135deg, rgba(0,210,255,0.28), rgba(142,45,226,0.28)); color: #fff; border-color: rgba(255,255,255,0.2); box-shadow: 0 0 10px rgba(0,210,255,0.2); }
    .sub-panel { display: none; }
    .sub-panel.active { display: block; animation: fadeIn 0.3s ease forwards; }

    /* ---- Compact telemetry pills (Advanced > Status) ------------------ */
    .telemetry-bar { display: flex; flex-wrap: wrap; gap: 6px; margin-bottom: 16px; }
    .tel-pill { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.1); border-radius: 10px; padding: 6px 12px; font-size: .78rem; color: var(--text-muted); font-variant-numeric: tabular-nums; }
    .tel-pill b { color: var(--text-main); font-weight: 700; margin-right: 4px; }
    .tel-pill.warn b { color: var(--warn); }
    .tel-pill.crit b { color: var(--danger); }

    /* ---- Stat pills row (CPU/RAM/PSRAM) under Advanced > Status ----- */
    .stat-row { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 16px; }
    .stat-pill { background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.12); color: var(--text-muted); padding: 4px 12px; border-radius: 12px; font-size: .72rem; font-weight: 600; font-variant-numeric: tabular-nums; letter-spacing: .5px; white-space: nowrap; }
    .stat-pill.warn { color: var(--warn); border-color: rgba(251,191,36,0.4); }
    .stat-pill.crit { color: var(--danger); border-color: rgba(239,68,68,0.5); }

    /* ---- Encoding section ---------------------------------------------- */
    .enc-card { background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.1); border-top: 1px solid rgba(255,255,255,0.2); border-radius: 16px; padding: 20px; margin-bottom: 20px; }
    .enc-card h3 { font-size: 1rem; margin-bottom: 12px; color: var(--text-main); font-weight: 600; }
    .enc-target-row { display: flex; align-items: center; gap: 14px; padding: 6px 0 12px; border-bottom: 1px solid rgba(255,255,255,0.08); margin-bottom: 12px; }
    .enc-target-row .enc-label { font-size: .82rem; color: var(--text-muted); text-transform: uppercase; letter-spacing: .5px; white-space: nowrap; }
    .enc-target-row input[type=range] { flex: 1 1 auto; -webkit-appearance: none; height: 6px; background: rgba(255,255,255,0.2); border-radius: 3px; outline: none; }
    .enc-target-row input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 18px; height: 18px; border-radius: 50%; background: #fff; cursor: pointer; border: 2px solid var(--accent-secondary); box-shadow: 0 0 15px rgba(0,210,255,0.8); }
    .enc-target-row .enc-target-val { font-size: 1rem; font-weight: 700; color: var(--accent-primary); font-variant-numeric: tabular-nums; min-width: 80px; text-align: right; }
    .enc-field { display: flex; align-items: center; gap: 10px; padding: 9px 0; border-bottom: 1px solid rgba(255,255,255,0.06); }
    .enc-field:last-child { border-bottom: 0; }
    .enc-field .enc-label { flex: 1 1 auto; color: var(--text-muted); font-size: .85rem; }
    .enc-field .enc-value { font-weight: 600; font-variant-numeric: tabular-nums; min-width: 70px; text-align: right; }
    .enc-field.auto .enc-value { color: var(--text-muted); font-style: italic; }
    .enc-field .enc-input { display: none; width: 100px; padding: 6px 10px; background: rgba(0,0,0,0.42); border: 1px solid rgba(255,255,255,0.2); border-radius: 6px; color: var(--text-main); font: inherit; font-size: .88rem; }
    .enc-field.manual .enc-value { display: none; }
    .enc-field.manual .enc-input { display: inline-block; }
    .enc-lock { background: transparent; border: 1px solid rgba(255,255,255,0.2); border-radius: 6px; width: 34px; height: 34px; cursor: pointer; color: var(--text-muted); font-size: 1rem; line-height: 1; display: flex; align-items: center; justify-content: center; transition: all .15s; flex: 0 0 auto; }
    .enc-lock:hover { background: rgba(255,255,255,0.1); }
    .enc-lock.locked { background: var(--accent-primary); color: #000; border-color: transparent; }
    .enc-field.readonly .enc-value { color: var(--text-muted); font-style: italic; }
    .enc-field.readonly { opacity: .7; }
    .enc-hint { font-size: .78rem; color: var(--text-muted); margin-top: 8px; line-height: 1.5; }

    /* ---- Analyze/Record modal ----------------------------------------- */
    .rec-overlay { position: fixed; inset: 0; z-index: 200; display: none; align-items: center; justify-content: center; background: rgba(0,0,0,0.75); padding: 20px; }
    .rec-overlay.open { display: flex; }
    .rec-panel { width: 100%; max-width: 460px; background: linear-gradient(135deg, rgba(30,25,55,0.97), rgba(15,10,30,0.97)); border: 1px solid rgba(255,255,255,0.15); border-radius: 20px; padding: 24px; box-shadow: 0 25px 60px rgba(0,0,0,0.6); }
    .rec-panel h3 { font-size: 1.1rem; margin-bottom: 14px; color: var(--text-main); }
    .rec-progress-bar { height: 8px; background: rgba(255,255,255,0.1); border-radius: 4px; overflow: hidden; margin: 16px 0; }
    .rec-progress-fill { height: 100%; width: 0%; background: linear-gradient(to right, #00d2ff, #8e2de2); transition: width .3s ease; }
    .rec-progress-text { font-size: .9rem; color: var(--text-muted); font-variant-numeric: tabular-nums; }
    .rec-result-panel { width: 100%; max-width: 720px; max-height: 90vh; display: flex; flex-direction: column; }
    .rec-result-textarea { flex: 1 1 auto; width: 100%; min-height: 300px; max-height: 60vh; padding: 12px; background: rgba(0,0,0,0.5); border: 1px solid rgba(255,255,255,0.15); border-radius: 10px; color: #cbd5e1; font-family: ui-monospace, 'SF Mono', Menlo, Consolas, monospace; font-size: .72rem; line-height: 1.4; resize: vertical; }
    .rec-result-actions { display: flex; gap: 10px; margin-top: 14px; justify-content: flex-end; }

    /* ---- OTA wizard modal (unchanged) --------------------------------- */
    .ota-modal { position: fixed; inset: 0; z-index: 1000; display: none; align-items: center; justify-content: center; padding: 20px; }
    .ota-modal.open { display: flex; }
    .ota-modal-backdrop { position: absolute; inset: 0; background: rgba(0,0,0,0.65); backdrop-filter: blur(6px); -webkit-backdrop-filter: blur(6px); }
    .ota-modal-panel { position: relative; width: 100%; max-width: 540px; background: linear-gradient(135deg, rgba(30,25,55,0.97), rgba(15,10,30,0.97)); border-top: 1px solid var(--glass-border-light); border-left: 1px solid var(--glass-border-light); border-bottom: 1px solid var(--glass-border-dim); border-right: 1px solid var(--glass-border-dim); border-radius: 20px; box-shadow: 0 25px 60px rgba(0,0,0,0.6), inset 0 0 20px rgba(255,255,255,0.03); overflow: hidden; display: flex; flex-direction: column; max-height: 90vh; }
    .ota-modal-header { display: flex; align-items: center; justify-content: space-between; padding: 16px 20px 4px; }
    .ota-close { background: transparent; border: none; color: var(--text-muted); font-size: 1.6rem; line-height: 1; cursor: pointer; padding: 0 4px; }
    .ota-close:hover { color: #fff; }
    .ota-steps { display: flex; gap: 8px; align-items: center; }
    .ota-step-dot { width: 8px; height: 8px; border-radius: 50%; background: rgba(255,255,255,0.15); transition: all .3s ease; }
    .ota-step-dot.active { width: 22px; border-radius: 4px; background: var(--accent-primary); box-shadow: 0 0 10px var(--accent-primary); }
    .ota-step-dot.done { background: var(--success); }
    .ota-modal-body { padding: 8px 24px 4px; overflow-y: auto; }
    .ota-step { display: none; }
    .ota-step.active { display: block; animation: fadeIn .3s ease forwards; }
    .ota-step h3 { font-size: 1.15rem; margin-bottom: 10px; font-weight: 700; background: linear-gradient(to right,#00d2ff,#8e2de2); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
    .ota-step p { font-size: .88rem; color: var(--text-muted); margin-bottom: 12px; line-height: 1.45; }
    .ota-step code { background: rgba(255,255,255,0.08); padding: 1px 6px; border-radius: 4px; font-size: .82rem; color: var(--text-main); }
    .ota-modal-footer { display: flex; justify-content: flex-end; gap: 10px; padding: 16px 24px 20px; }
    .btn-primary { background: linear-gradient(135deg,#00d2ff,#8e2de2); border: none; color: #fff; padding: 9px 24px; border-radius: 8px; cursor: pointer; font-weight: 600; font-size: .88rem; box-shadow: 0 0 18px rgba(0,210,255,0.35); }
    .btn-primary:disabled { opacity: 0.35; cursor: not-allowed; box-shadow: none; filter: grayscale(0.6); }
    .btn-primary:hover:not(:disabled) { filter: brightness(1.1); }
    .ota-progress-bar { height: 6px; background: rgba(255,255,255,0.1); border-radius: 3px; margin-top: 14px; overflow: hidden; }
    .ota-progress-fill { height: 100%; width: 0%; background: linear-gradient(to right,#00d2ff,#8e2de2); transition: width .4s ease; }
    .ota-current-wrap { margin-top: 18px; padding-top: 16px; border-top: 1px solid rgba(255,255,255,0.08); display: none; }
    .ota-current-wrap.visible { display: block; }
    .ota-current-wrap .checkbox span { font-size: .88rem; }
    .ota-current-wrap b { color: var(--accent-primary); margin-left: 4px; }
    .ota-target-row { background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.08); border-radius: 10px; padding: 12px 14px; margin-bottom: 14px; transition: opacity .2s, filter .2s; }
    .ota-target-row.disabled { opacity: .32; filter: grayscale(.6); }
    .ota-target-row .checkbox span { font-size: .88rem; }
    .ota-target-row .checkbox input[type=checkbox]:disabled { cursor: not-allowed; }
    .ota-server-meta { font-size: .76rem; color: var(--text-muted); margin-top: 6px; margin-left: 28px; font-family: 'SF Mono', ui-monospace, Menlo, Consolas, monospace; }
    .ota-targets-list { max-height: 260px; overflow-y: auto; transition: opacity .2s, filter .2s; }
    .ota-targets-list.disabled { opacity: .32; filter: grayscale(.6); pointer-events: none; }
    .ota-section-label { font-size: .72rem; text-transform: uppercase; letter-spacing: 1.2px; color: var(--text-muted); margin: 4px 0 8px; }
    .ota-node-header { display: flex; justify-content: space-between; align-items: center; gap: 12px; }
    .ota-select-all { margin: 0; gap: .4rem; text-transform: none; letter-spacing: 0; font-size: .75rem; font-weight: 600; color: var(--text-muted); cursor: pointer; user-select: none; }
    .ota-select-all input { width: 15px; height: 15px; margin: 0; accent-color: var(--accent-primary); cursor: pointer; }
    .ota-select-all input:disabled { cursor: not-allowed; opacity: .4; }
    .ota-select-all input:disabled + span { opacity: .4; }
    .ota-node-item { display: flex; align-items: center; gap: 12px; padding: 10px 14px; margin-bottom: 8px; background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.08); border-radius: 10px; cursor: pointer; transition: background .2s, border-color .2s; font-size: .88rem; }
    .ota-node-item:hover { background: rgba(255,255,255,0.06); border-color: rgba(255,255,255,0.15); }
    .ota-node-item input[type=checkbox] { width: 16px; height: 16px; accent-color: var(--accent-primary); flex: 0 0 auto; margin: 0; }
    .ota-node-item input[type=checkbox]:disabled { cursor: not-allowed; }
    .ota-node-body { flex: 1 1 auto; min-width: 0; }
    .ota-node-name { font-weight: 600; font-size: .9rem; color: var(--text-main); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .ota-node-ip { font-size: .76rem; color: var(--text-muted); font-family: 'SF Mono', ui-monospace, Menlo, Consolas, monospace; margin-top: 2px; }
    .ota-node-ver { flex: 0 0 auto; font-size: .78rem; color: var(--text-muted); text-align: right; font-family: 'SF Mono', ui-monospace, Menlo, Consolas, monospace; }
    .ota-node-ver .new { color: var(--accent-primary); font-weight: 600; }
    .ota-spinner { width: 18px; height: 18px; border: 2px solid rgba(255,255,255,0.2); border-top-color: var(--accent-primary); border-radius: 50%; animation: busySpin .8s linear infinite; display: inline-block; vertical-align: middle; }
    @keyframes busySpin { to { transform: rotate(360deg); } }
    .ok-tick { color: var(--success); font-size: 1.2rem; font-weight: bold; }
    .bad-x { color: var(--danger); font-size: 1.2rem; font-weight: bold; }

    /* ---- OTA wizard live log pane ------------------------------------- */
    .ota-log-wrap { margin-top: 12px; border-top: 1px solid rgba(255,255,255,0.08); padding-top: 10px; display: none; }
    .ota-log-wrap.visible { display: block; }
    .ota-log-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px; }
    .ota-log-header span { font-size: .74rem; text-transform: uppercase; letter-spacing: 1px; color: var(--text-muted); }
    .ota-log-toggle { background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.14); color: var(--text-muted); padding: 3px 10px; border-radius: 6px; font: inherit; font-size: .72rem; cursor: pointer; min-height: 26px; }
    .ota-log-toggle:hover { background: rgba(255,255,255,0.14); color: var(--text-main); }
    .ota-log-pre { margin: 0; padding: 10px 12px; max-height: 180px; overflow-y: auto; background: rgba(0,0,0,0.45); border: 1px solid rgba(255,255,255,0.08); border-radius: 8px; font-family: ui-monospace, 'SF Mono', Menlo, Consolas, monospace; font-size: .72rem; line-height: 1.45; white-space: pre-wrap; word-break: break-all; color: #cbd5e1; }
    .ota-log-pre .lg-e { color: #f87171; }
    .ota-log-pre .lg-w { color: #fbbf24; }
    .ota-log-pre .lg-i { color: #cbd5e1; }

    /* ---- Wi-Fi scan modal (node-parity overlay/modal) ---------------- */
    .wifi-modal { position: fixed; inset: 0; z-index: 60; display: none; align-items: flex-end; justify-content: center; background: rgba(0,0,0,.7); }
    .wifi-modal.open { display: flex; }
    .wifi-modal-panel { position: relative; width: 100%; max-width: 460px; background: linear-gradient(135deg, rgba(30,30,45,.95), rgba(15,15,25,.95)); border: 1px solid rgba(255,255,255,0.15); border-radius: 20px 20px 0 0; padding: 18px 18px calc(18px + env(safe-area-inset-bottom)); backdrop-filter: blur(20px); -webkit-backdrop-filter: blur(20px); overflow: hidden; display: flex; flex-direction: column; max-height: 80vh; }
    .wifi-modal-header { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
    .wifi-modal-body { max-height: 46vh; overflow-y: auto; margin: 12px 0 0; border: 1px solid rgba(255,255,255,0.1); border-radius: 10px; }
    .wifi-net { display: flex; justify-content: space-between; align-items: center; gap: 12px; width: 100%; text-align: left; padding: 14px 12px; background: none; border: 0; border-bottom: 1px solid rgba(255,255,255,0.08); color: var(--text-main); font: inherit; cursor: pointer; min-height: 48px; transition: background .15s ease; }
    .wifi-net:last-child { border-bottom: 0; }
    .wifi-net:hover { background: rgba(255,255,255,0.05); }
    .wifi-net .rssi { color: var(--text-muted); font-size: .82rem; flex: 0 0 auto; }

    /* ---- Sticky shell + icon buttons -------------------------------- */
    .sticky-shell { position: sticky; top: 0; z-index: 30; background: linear-gradient(180deg, rgba(10,10,22,0.96) 0%, rgba(10,10,22,0.88) 70%, rgba(10,10,22,0.78) 100%); backdrop-filter: blur(16px) saturate(180%); -webkit-backdrop-filter: blur(16px) saturate(180%); border-bottom: 1px solid rgba(255,255,255,0.08); }
    .top-actions { display: flex; align-items: center; gap: 8px; flex: 0 0 auto; flex-wrap: nowrap; margin-left: auto; }
    .icon-btn { width: 40px; height: 40px; padding: 0; border-radius: 50%; background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.14); color: var(--text-main); display: inline-flex; align-items: center; justify-content: center; cursor: pointer; text-decoration: none; font-size: 1.15rem; line-height: 1; transition: background .2s ease, border-color .2s ease; flex: 0 0 auto; }
    .icon-btn:hover { background: rgba(255,255,255,0.14); }
    .icon-btn.active { background: linear-gradient(135deg,#00d2ff,#8e2de2); border-color: transparent; color: #fff; box-shadow: 0 0 14px rgba(0,210,255,0.45); }
    .icon-btn.danger { color: var(--danger); border-color: rgba(239,68,68,0.5); }

    .volume-float { display: none; }
    .log-controls { display: flex; gap: 8px; align-items: center; margin-top: 16px; }
    .log-controls .muted { font-size: .8rem; }
    #src-log { font-family: ui-monospace, Consolas, monospace; }

    @media (max-width: 600px) {
      body { padding: 8px; padding-bottom: 140px; }
      .dashboard-container { margin: 10px 0; border-radius: 18px; }
      .top-bar { padding: 10px 16px 4px; gap: 8px; }
      .page-title { font-size: 1.25rem; }
      .menu-bar { padding: 0; gap: 6px; }
      .nav-btn { padding: 8px 14px; font-size: 0.82rem; gap: 5px; }
      .mode-select { font-size: 0.8rem; padding: 6px 30px 6px 12px; }
      .tab-content { padding: 16px 18px; }
      .grid-2 { grid-template-columns: 1fr; gap: 14px; }
      .zone-container { grid-template-columns: 1fr; gap: 14px; }
      .zones-controls { padding: 12px 16px 4px; }
      .stat-row { padding: 0 16px 8px; gap: 6px; }
      .stat-pill { padding: 3px 9px; font-size: 0.68rem; }
      .ota-modal { padding: 8px; }
      .ota-modal-panel { max-width: 100%; }
      .ota-modal-body { padding: 8px 18px 4px; }
      .ota-modal-footer { padding: 14px 18px 18px; }
      .rec-result-panel { max-width: 100%; }
      .rec-result-textarea { font-size: .65rem; }
      .control-row #volume, .control-row .master-label { display: none; }
    }

    @media (prefers-reduced-motion: reduce) {
      .nav-btn, .mode-btn, .btn-glass, .control-card, .zone-box, .node-chip, .gear, .tab-content, input, select, .checkbox, .ota-modal, .ota-modal-panel, .ota-step, .ota-progress-fill, .ota-step-dot, .ota-close, .btn-primary, .ota-node-item, .ota-target-row, .sub-tab-btn, .rec-progress-fill {
        transition: none !important; animation: none !important;
      }
    }

    /* ---- Toast Notifications ---- */
    #toast {
      position: fixed;
      bottom: 24px;
      left: 50%;
      transform: translateX(-50%) translateY(100px);
      background: linear-gradient(135deg, rgba(30,30,45,0.95), rgba(15,15,25,0.95));
      color: var(--text-main);
      padding: 12px 24px;
      border-radius: 12px;
      border: 1px solid rgba(255,255,255,0.15);
      font-size: 0.9rem;
      font-weight: 600;
      box-shadow: 0 10px 30px rgba(0,0,0,0.5);
      opacity: 0;
      transition: transform 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275), opacity 0.3s ease;
      pointer-events: none;
      z-index: 9999;
      white-space: nowrap;
    }
    #toast.show {
      transform: translateX(-50%) translateY(0);
      opacity: 1;
    }
    #toast.show.ok {
      border-color: var(--success);
      box-shadow: 0 0 15px rgba(16, 185, 129, 0.2);
    }
    #toast.show.err {
      border-color: var(--danger);
      box-shadow: 0 0 15px rgba(239, 68, 68, 0.2);
    }
  </style>
</head>
<body>
  <div id="top-loading-bar" aria-hidden="true"></div>
  <div class="galaxy-bg" aria-hidden="true">
    <div class="orb orb-1"></div><div class="orb orb-2"></div><div class="orb orb-3"></div>
    <div class="stars"></div>
  </div>

  <div class="dashboard-container">
    <div class="sticky-shell">
      <div class="top-bar">
        <div class="brand">
          <h1 class="page-title" id="server-title" onclick="openNameModal()" title="Click to edit server name">
            <span id="server-title-text">Enkelvoud</span>
            <svg viewBox="0 0 24 24" width="18" height="18">
              <path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04c.39-.39.39-1.02 0-1.41l-2.34-2.34c-.39-.39-1.02-.39-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/>
            </svg>
          </h1>
          <div class="node-sub" id="server-version" style="color:var(--text-muted);font-size:0.78rem;margin-top:2px;">firmware —</div>
        </div>

        <nav class="menu-bar">
          <button type="button" class="nav-btn active" onclick="openTab(event, 'tab-zones')">Zones</button>
          <button type="button" class="nav-btn"        onclick="openTab(event, 'tab-settings')">Network</button>
          <button type="button" class="nav-btn"        onclick="openTab(event, 'tab-advanced')">Advanced</button>
          <button type="button" class="nav-btn"        onclick="openUpdateWizard()">Update</button>
        </nav>

        <div class="top-actions">
          <button type="button" class="nav-btn" id="net-revert-btn"
                  style="display:none" title="Revert network changes">Revert</button>
          <button type="button" class="nav-btn nav-btn-primary" id="net-save-btn"
                  style="display:none" title="Save network settings">Save</button>
        </div>
      </div>
    </div>

    <div class="volume-float" id="volumeFloat">
      <span class="vf-value" id="volume-float-label">100</span>
      <input type="range" id="volume-mobile" class="vol-vertical"
             min="0" max="100" value="100" aria-label="Master volume">
    </div>

    <!-- ======================= TAB: ZONES ======================= -->
    <div id="tab-zones" class="tab-content active">
      <div class="zones-controls" style="padding-bottom: 0;">
        
        <!-- Moved + Add Room next to title -->
        <div class="zone-toolbar" style="flex-wrap:wrap;gap:10px; margin-bottom: 10px; display: flex; justify-content: flex-start; align-items: center; width: 100%;">
          <div style="display:flex;gap:10px;align-items:center;">
            <h2 class="section-title" style="margin:0;">Zone Architecture</h2>
            <button type="button" class="btn-glass" onclick="createNewZone()"
                    style="border-color: var(--accent-primary); color: var(--accent-primary); padding: 6px 12px; font-size: 0.85rem;">
              + Add Room
            </button>
          </div>
        </div>

        <!-- Volume / Source / Mute / Quiet row -->
        <div class="zones-volume-row" style="margin-bottom: 20px; display: flex; gap: 10px; align-items: center; width: 100%;">
          <!-- Source moved to the left -->
          <select id="source-select" class="mode-select" aria-label="Audio source" style="flex: 0 0 auto;">
            <option value="BT">BT</option>
            <option value="AUX">AUX</option>
            <option value="USB">USB</option>
          </select>
          
          <span class="master-label" style="flex: 0 0 auto; margin-left: 10px;">Master <b id="volume-label">100%</b></span>
          
          <input type="range" id="volume" min="0" max="100" value="100" aria-label="Master volume" style="flex: 1 1 auto;">
          
          <!-- Quiet Mode icon (Moon) -->
          <button type="button" class="icon-btn" id="btn-quiet" onclick="toggleQuietMode()" title="Quiet Mode (Frequency Limiting)" style="flex: 0 0 auto;">
            <span id="quiet-icon">🌙</span>
          </button>
          
          <!-- Mute button moved to the right -->
          <button type="button" class="icon-btn" id="btn-mute" onclick="muteAllZones()" title="Mute all rooms" style="flex: 0 0 auto;">
            <span id="mute-icon">&#128266;</span>
          </button>
        </div>
      </div>
      <div class="zone-container" id="zonesContainer">
        <div class="control-card"><p class="muted">Loading zone map...</p></div>
      </div>
    </div>

    <!-- ======================= TAB: NETWORK ===================== -->
    <div id="tab-settings" class="tab-content">
      <input type="hidden" id="net-server-name" name="server_name" value="">
      <h2 class="section-title">Wi-Fi</h2>
      <div class="control-card">
        <div class="field">
          <label for="net-ssid">Network name</label>
          <div style="display:flex;gap:8px">
            <input type="text" id="net-ssid" name="ssid" autocomplete="on" autocapitalize="off" spellcheck="false">
            <button type="button" class="btn btn-sm" id="net-scan-btn" style="flex:0 0 auto">Scan</button>
          </div>
        </div>
        <div class="field">
          <label for="net-pass">Password</label>
          <input type="password" id="net-pass" name="password" autocomplete="off">
        </div>
      </div>

      <div class="control-card">
        <label class="check">
          <input type="checkbox" id="net-use-static" name="use_static_ip">
          <span>Use a static IP</span>
        </label>
        <div id="net-static-fields" hidden>
          <div class="grid" style="margin-top:12px">
            <div class="field"><label for="net-ip">IP address</label>
              <input type="text" id="net-ip" name="ip" inputmode="decimal" autocomplete="on"></div>
            <div class="field"><label for="net-gw">Gateway</label>
              <input type="text" id="net-gw" name="gateway" inputmode="decimal" autocomplete="on"></div>
            <div class="field"><label for="net-sn">Subnet mask</label>
              <input type="text" id="net-sn" name="subnet" inputmode="decimal" autocomplete="on"></div>
            <div class="field"><label for="net-dns">DNS server</label>
              <input type="text" id="net-dns" name="dns" inputmode="decimal" autocomplete="on"></div>
          </div>
        </div>
      </div>

      <div class="control-card" id="net-test-card" style="display:none;">
        <div class="data-label">Connection Test</div>
        <div class="data-value" id="net-test-state" style="font-size:1rem;">—</div>
      </div>
    </div>

    <!-- ======================= TAB: ADVANCED ==================== -->
    <div id="tab-advanced" class="tab-content">
      <div class="sub-tab-bar">
        <button type="button" class="sub-tab-btn active" data-subtab="adv-status" onclick="openSubTab('adv-status')">Status</button>
        <button type="button" class="sub-tab-btn" data-subtab="adv-source" onclick="openSubTab('adv-source')">Source</button>
        <button type="button" class="sub-tab-btn" data-subtab="adv-encoding" onclick="openSubTab('adv-encoding')">Encoding</button>
      </div>

      <!-- ---- ADV > Status ---- -->
      <div id="adv-status" class="sub-panel active">
        <div class="stat-row" aria-label="System load">
          <span class="stat-pill" id="stat-core0" title="Core 0 CPU load">C0-%</span>
          <span class="stat-pill" id="stat-core1" title="Core 1 CPU load">C1-%</span>
          <span class="stat-pill" id="stat-ram" title="Internal heap used (max = largest contiguous free block)">RAM-%</span>
          <span class="stat-pill" id="stat-dma" title="DMA-capable free bytes" style="display:none">DMA-%</span>
          <span class="stat-pill" id="stat-psram" title="PSRAM used" style="display:none">PS-%</span>
        </div>

        <h2 class="section-title" style="font-size:1rem;margin-bottom:10px;">Stream Telemetry</h2>
        <div class="telemetry-bar">
          <span class="tel-pill"><b id="stat-mode">--</b>mode</span>
          <span class="tel-pill" title="Configured playback target sent to nodes"><b id="stat-buffer">--</b>target buffer</span>
          <span class="tel-pill" title="Legacy field; not applied to playback"><b id="stat-delay">--</b>legacy adjustment</span>
          <span class="tel-pill" title="Average of node-reported latency; not the configured target or measured playout timing"><b id="stat-latency">--</b>node latency</span>
          <span class="tel-pill"><b id="stat-clients">--</b>clients</span>
          <span class="tel-pill"><b id="stat-master">--</b>master</span>
        </div>

        <div class="log-controls" style="margin-top:6px;align-items:center;">
          <button type="button" class="btn-glass" id="status-advanced-toggle">Show advanced</button>
          <button type="button" class="btn-glass" id="analyzeBtn" title="Record up to 10 min — click Finish Now to end early">
            ⏺ Analyze / Record (10 min max)
          </button>
          <span class="muted" id="status-advanced-state">basic view</span>
        </div>

        <div id="status-advanced-panel" style="display:none;">
          <div class="grid-2" style="margin-top:16px;">
            <div class="control-card" style="grid-column: span 2;">
              <div class="data-label">Node Latencies</div>
              <p style="font-size:0.85rem;color:var(--text-muted);margin-top:5px;" id="stat-nodes">Fetching…</p>
            </div>
            <div class="control-card" style="grid-column: span 2;">
              <div class="data-label">Diagnostics</div>
              <p style="font-size:0.85rem;color:var(--text-muted);margin-top:5px;" id="stat-diag">Fetching…</p>
            </div>
            <div class="control-card" style="grid-column: span 2;">
              <div class="data-label">Audio Pipeline (last window)</div>
              <p style="font-size:0.85rem;color:var(--text-muted);margin-top:5px;" id="stat-pipeline">Fetching…</p>
            </div>
          </div>
          <div class="data-label" style="margin-top:18px;">Raw /api/stats JSON</div>
          <pre id="status-raw-json" style="font-size:0.78rem;max-height:40vh;overflow:auto;background:rgba(0,0,0,0.35);padding:10px;border-radius:10px;border:1px solid rgba(255,255,255,0.08);white-space:pre-wrap;margin-top:8px;">(waiting for poll…)</pre>
        </div>

        <div class="log-controls">
          <button type="button" class="btn-glass" id="status-log-pause">Pause</button>
          <button type="button" class="btn-glass" id="status-log-clear">Clear</button>
          <span class="muted" id="status-log-state">polling</span>
        </div>

        <div class="log-controls" style="margin-top:14px;align-items:center;">
          <button type="button" class="btn-glass" id="status-advanced-log">Enable advanced logging</button>
          <span class="muted" id="status-advanced-log-state">off (errors only)</span>
        </div>

        <div class="log-controls" style="margin-top:14px;align-items:center;">
          <button type="button" class="btn-glass" id="status-dlna-toggle" disabled>DLNA: …</button>
          <span class="muted" id="status-dlna-state">loading</span>
        </div>

        <div id="status-log-viewer" style="display:none;margin-top:14px;">
          <div class="data-label">Serial Log (live)</div>
          <pre id="status-log-pre" style="font-size:0.78rem;max-height:40vh;overflow:auto;background:rgba(0,0,0,0.35);padding:10px;border-radius:10px;border:1px solid rgba(255,255,255,0.08);white-space:pre-wrap;margin-top:8px;">(waiting for lines…)</pre>
          <div class="log-controls" style="margin-top:8px;">
            <button type="button" class="btn-glass" id="status-logview-clear">Clear</button>
            <span class="muted" id="status-logview-state">idle</span>
          </div>
        </div>
      </div>

      <!-- ---- ADV > Source ---- -->
      <div id="adv-source" class="sub-panel">
        <h2 class="section-title" style="font-size:1.1rem;">Source Board</h2>
        <div class="grid-2">
          <div class="control-card"><div class="data-label">Link</div><div class="data-value" id="src-link">--</div></div>
          <div class="control-card"><div class="data-label">BT State</div><div class="data-value" id="src-bt">--</div></div>
          <div class="control-card"><div class="data-label">Connected MAC</div><div class="data-value" id="src-mac" style="font-size:1rem;">--</div></div>
          <div class="control-card"><div class="data-label">Volume (source-side)</div><div class="data-value" id="src-vol">--</div></div>
          <div class="control-card" style="grid-column: span 2;">
            <div class="data-label">Device List</div>
            <p style="font-size:0.85rem;color:var(--text-muted);margin-top:5px;" id="src-devices">Fetching…</p>
          </div>
          <div class="control-card" style="grid-column: span 2;">
            <div class="data-label">Diagnostics</div>
            <p style="font-size:0.85rem;color:var(--text-muted);margin-top:5px;" id="src-diag">Fetching…</p>
          </div>
        </div>

        <div id="src-kick-wrap" style="display:none;margin-top:16px;">
          <button type="button" class="btn-glass" id="src-kick-btn"
                  style="border-color:var(--danger);color:var(--danger);width:100%;padding:10px;">
            Kick connected BT device (pair a new one)
          </button>
          <p class="muted" style="font-size:0.78rem;margin-top:8px;text-align:center;">
            Temporarily disconnects the current device without unpairing. You have
            ~2 minutes to pair a new device before the source attempts to reconnect.
          </p>
        </div>

        <div class="log-controls" style="margin-top:20px;">
          <button type="button" class="btn-glass" id="source-log-pause">Pause</button>
          <button type="button" class="btn-glass" id="source-log-clear">Clear</button>
          <span class="muted" id="source-log-state">polling</span>
        </div>
        <pre id="src-log" style="font-size:0.78rem;max-height:40vh;overflow:auto;background:rgba(0,0,0,0.35);padding:10px;border-radius:10px;border:1px solid rgba(255,255,255,0.08);white-space:pre-wrap;">Connecting to source board…</pre>
      </div>

      <!-- ---- ADV > Encoding ---- -->
      <div id="adv-encoding" class="sub-panel">
        <h2 class="section-title" style="font-size:1.1rem;">Opus Encoder</h2>

        <div class="enc-card">
          <h3>Playback Mode (Preset)</h3>
          <div class="enc-field" style="border-bottom: none; padding-bottom: 0;">
            <span class="enc-label">Mode</span>
            <select id="mode-select" class="mode-select" style="width: 140px;" aria-label="Playback mode">
              <option value="off">Off</option>
              <option value="music">Music</option>
              <option value="movie">Movie</option>
            </select>
          </div>
          <p class="enc-hint">
            Selecting a preset automatically adjusts the node's target latency, audio buffer, and encoder complexity. 
            (Use the 🌙 icon on the Zones tab for frequency limiting).
          </p>
        </div>

        <div class="enc-card">
          <h3>Target Quality</h3>
          <div class="enc-target-row">
            <span class="enc-label">Target Bitrate</span>
            <input type="range" id="enc-bitrate" min="32" max="192" step="8" value="96">
            <span class="enc-target-val" id="enc-bitrate-val">96 kbps</span>
          </div>
          <p class="enc-hint">
            Suggested fields below auto-calculate from the target bitrate. Click
            the <b>🔓 lock</b> button to override any value manually.
          </p>
        </div>

      <div class="enc-card">
        <h3>Target Buffer Latency</h3>
        <div class="enc-target-row">
          <span class="enc-label">Buffer (ms)</span>
          <!-- [buffer-latency-floor] min matches AUDIO_BUFFER_MIN_MS (EVDState.h).
              Values below 20 are rejected by /api/config -> 400. Regenerate
              EVDCTRL_GZ.h after editing this attribute. -->
          <input type="range" id="buffer-latency" min="20" max="300" step="5" value="150">
          <span class="enc-target-val" id="buffer-latency-val">150 ms</span>
        </div>
        <p class="enc-hint">
          This configured target (<code>bufferMs</code>) is sent to every player.
          Node-reported latency and actual playout timing are separate measurements.
          Higher values increase resistance to network jitter but increase latency.
        </p>
      </div>

        <div class="enc-card">
          <h3>Encoder Settings</h3>
          <div class="enc-field auto" data-field="complexity">
            <span class="enc-label">Complexity (0–10)</span>
            <span class="enc-value" id="enc-complexity-val">8</span>
            <input type="number" class="enc-input" id="enc-complexity-in" min="0" max="10" step="1" value="8">
            <button type="button" class="enc-lock" id="enc-complexity-lock" data-field="complexity" title="Unlock to override">🔓</button>
          </div>
          <div class="enc-field auto" data-field="vbr">
            <span class="enc-label">Variable Bitrate (VBR)</span>
            <span class="enc-value" id="enc-vbr-val">On</span>
            <select class="enc-input" id="enc-vbr-in">
              <option value="1">On</option>
              <option value="0">Off</option>
            </select>
            <button type="button" class="enc-lock" id="enc-vbr-lock" data-field="vbr" title="Unlock to override">🔓</button>
          </div>
          <div class="enc-field auto" data-field="vbrConstraint">
            <span class="enc-label">VBR Constraint (bound peak)</span>
            <span class="enc-value" id="enc-vbrConstraint-val">On</span>
            <select class="enc-input" id="enc-vbrConstraint-in">
              <option value="1">On</option>
              <option value="0">Off</option>
            </select>
            <button type="button" class="enc-lock" id="enc-vbrConstraint-lock" data-field="vbrConstraint" title="Unlock to override">🔓</button>
          </div>
          <div class="enc-field auto" data-field="signal">
            <span class="enc-label">Signal Type</span>
            <span class="enc-value" id="enc-signal-val">Music</span>
            <select class="enc-input" id="enc-signal-in">
              <option value="music">Music</option>
              <option value="voice">Voice</option>
              <option value="auto">Auto</option>
            </select>
            <button type="button" class="enc-lock" id="enc-signal-lock" data-field="signal" title="Unlock to override">🔓</button>
          </div>
          <div class="enc-field auto" data-field="application">
            <span class="enc-label">Application</span>
            <span class="enc-value" id="enc-application-val">Audio</span>
            <select class="enc-input" id="enc-application-in">
              <option value="audio">Audio</option>
              <option value="voip">VoIP</option>
              <option value="lowdelay">Low Delay (Not for Music)</option>
            </select>
            <button type="button" class="enc-lock" id="enc-application-lock" data-field="application" title="Unlock to override">🔓</button>
          </div>
        </div>

        <div class="enc-card">
          <h3>Opus Sample Rate</h3>
          <div class="enc-field" style="border-bottom: none; padding-bottom: 0;">
            <span class="enc-label">Encoder / Node Decoder Rate</span>
            <select id="enc-sampleRate-in" class="mode-select" style="width: auto; min-width: 220px;" aria-label="Opus sample rate">
              <option value="48000">48 kHz (default, full quality)</option>
              <option value="24000">24 kHz (lower CPU on nodes)</option>
            </select>
          </div>
          <p class="enc-hint">
            Changing the rate re-creates the server encoder and notifies every
            node to re-create its decoder at the matching rate. Audio pauses
            for ~40&nbsp;ms during the switch. Use 24&nbsp;kHz to save <b>server</b>
            encoding CPU and bandwidth; node decoders will consume slightly
            <i>more</i> CPU due to resampling, and audio content above 12&nbsp;kHz
            is permanently discarded.
          </p>
        </div>

<div class="enc-card">
  <h3>Transport / Buffer</h3>
  <div class="enc-field readonly"><span class="enc-label">Source Sample Rate</span><span class="enc-value" id="enc-source-rate">— Hz</span></div>
          <div class="enc-field readonly"><span class="enc-label">Opus Sample Rate</span><span class="enc-value" id="enc-opus-rate">48,000 Hz</span></div>
          <div class="enc-field readonly"><span class="enc-label">Channels</span><span class="enc-value">2 (Stereo)</span></div>
          <div class="enc-field readonly"><span class="enc-label">Frame Duration</span><span class="enc-value">20 ms (960 samples)</span></div>
<div class="enc-field readonly"><span class="enc-label">Max Packet</span><span class="enc-value">1024 bytes</span></div>
          <div class="enc-field readonly"><span class="enc-label">Application Mode</span><span class="enc-value">Runtime (see above)</span></div>
        </div>

        <div style="display:flex;gap:10px;">
          <button type="button" class="btn btn-primary" id="enc-save" style="flex:2;">Save</button>
          <button type="button" class="btn" id="enc-revert" style="flex:1;" title="Discard unsaved changes for this mode">Revert</button>
          <button type="button" class="btn" id="enc-defaults" style="flex:0 0 auto;" title="Load factory defaults for this mode">Load Defaults</button>
        </div>
        <p class="enc-hint" style="margin-top:14px;">
          <b>Note:</b> bitrate/VBR/signal changes require backend support on
          <code>/api/audio/encoding</code>. Complexity changes take effect on
          the next encoder tick. Application-mode changes need a firmware restart.
        </p>
      </div>
    </div>
  </div>

  <!-- ===================== OTA wizard modal (unchanged) ===================== -->
  <div id="otaModal" class="ota-modal" aria-hidden="true">
    <div class="ota-modal-backdrop" onclick="otaCancel()"></div>
    <div class="ota-modal-panel" role="dialog" aria-modal="true" aria-labelledby="otaModalTitle">
      <div class="ota-modal-header">
        <div class="ota-steps" aria-hidden="true">
          <span class="ota-step-dot" data-step="1"></span>
          <span class="ota-step-dot" data-step="2"></span>
          <span class="ota-step-dot" data-step="3"></span>
          <span class="ota-step-dot" data-step="4"></span>
        </div>
        <button type="button" class="ota-close" onclick="otaCancel()" aria-label="Close">&times;</button>
      </div>
      <div class="ota-modal-body">
        <section class="ota-step" data-step="1">
          <h3 id="otaModalTitle">Mute audio before updating</h3>
          <p>Audio will be muted on this server and on every selected player node while the update runs.</p>
          <label class="checkbox" style="margin-top:6px;">
            <input type="checkbox" id="otaRestoreMute" checked style="width:auto;">
            <span>Restore previous mute state when finished</span>
          </label>
        </section>
        <section class="ota-step" data-step="2">
          <h3>Select bin file</h3>
          <p>Choose <code>node_&lt;version&gt;.bin</code> for players, <code>server_&lt;version&gt;.bin</code> for this server, or <code>source_&lt;version&gt;.bin</code> for the source board.</p>
          
          <!-- ... Existing file picker code ... -->
          <input type="file" id="otaFiles" accept=".bin" style="display:none">
          <button type="button" class="btn-glass" id="otaPickFiles">Choose file…</button>
          
          <div id="otaUploadBar" class="ota-progress-bar" style="display:none;margin-top:16px;"><div class="ota-progress-fill" id="otaUploadBarFill"></div></div>
          <div id="otaUploadMsg" class="muted" style="margin-top:12px;">No file uploaded yet.</div>
          
          <!-- NEW: SPOOL STORAGE DIAGNOSTICS -->
          <div id="otaSpoolStats" style="margin-top: 14px; font-size: 0.8rem; color: var(--text-muted); font-family: ui-monospace, monospace;"></div>
          <div id="otaSpoolList" style="margin-top: 6px; max-height: 160px; overflow-y: auto; font-size: 0.75rem; font-family: ui-monospace, monospace; background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.1); border-radius: 8px; padding: 8px;"></div>
        </section>
        <section class="ota-step" data-step="3">
          <h3>Select targets</h3>
          <p id="otaTargetHint">--</p>
          <div id="otaServerRow" class="ota-target-row">
            <label class="checkbox"><input type="checkbox" id="otaDoServer" style="width:auto;"><span>Update <b>this server</b> (currently v<b id="otaServerVer">--</b>)</span></label>
            <div id="otaServerFile" class="ota-server-meta"></div>
          </div>
          <div id="otaSourceRow" class="ota-target-row">
            <label class="checkbox">
              <input type="checkbox" id="otaDoSource" style="width:auto;">
              <span>Update <b>source board</b> over UART</span>
            </label>
            <div id="otaSourceFile" class="ota-server-meta"></div>
          </div>
          <div class="ota-section-label ota-node-header">
            <span>Player nodes</span>
            <label class="checkbox ota-select-all"><input type="checkbox" id="otaSelectAll"><span>Select all</span></label>
          </div>
          <div id="otaNodeList" class="ota-targets-list"></div>
        </section>
        <section class="ota-step" data-step="4">
          <h3>Applying update</h3>
          <div id="otaNodeStatusList" class="ota-targets-list" style="max-height: 300px;"></div>
        </section>
        <div class="ota-log-wrap" id="otaLogWrap">
          <div class="ota-log-header">
            <span>Update log</span>
            <button type="button" class="ota-log-toggle" id="otaLogToggle">Hide</button>
          </div>
          <pre class="ota-log-pre" id="otaLogPre">(waiting for log lines…)</pre>
        </div>
      </div>
      <div class="ota-modal-footer">
        <button type="button" class="btn-glass" id="otaBackBtn" onclick="otaBack()">Back</button>
        <button type="button" class="btn-primary" id="otaNextBtn" onclick="otaNext()">OK</button>
      </div>
    </div>
  </div>

  <!-- ===================== Wi-Fi scan modal ===================== -->
  <div id="wifiScanModal" class="wifi-modal" aria-hidden="true">
    <div class="wifi-modal-panel" role="dialog" aria-modal="true" aria-label="Available networks">
      <div class="wifi-modal-header">
        <strong>Available networks</strong>
        <button type="button" class="btn btn-sm" onclick="closeWifiScan()">Close</button>
      </div>
      <div id="wifiScanList" class="wifi-modal-body">
        <div class="muted" style="padding:18px 20px;">Scanning…</div>
      </div>
    </div>
  </div>

  <!-- ===================== Server name modal ===================== -->
  <div id="nameModal" class="ota-modal" aria-hidden="true">
    <div class="ota-modal-backdrop" onclick="closeNameModal()"></div>
    <div class="ota-modal-panel" role="dialog" aria-modal="true" aria-label="Edit Server Name">
      <div class="ota-modal-header">
        <strong style="font-size:1rem;">Edit Server Name</strong>
        <button type="button" class="ota-close" onclick="closeNameModal()" aria-label="Close">&times;</button>
      </div>
      <div class="ota-modal-body">
        <p style="margin-bottom:12px;">Letters and numbers only. Becomes the <strong>.local</strong> address.</p>
        <div class="field">
          <label for="edit-name-input">Server name</label>
          <input type="text" id="edit-name-input" maxlength="32" pattern="[A-Za-z0-9]+"
                 autocomplete="off" autocapitalize="off" spellcheck="false"
                 onbeforeinput="if (event.data && /[^A-Za-z0-9]/.test(event.data)) event.preventDefault()">
        </div>
      </div>
      <div class="ota-modal-footer">
        <button type="button" class="btn-glass" onclick="closeNameModal()">Cancel</button>
        <button type="button" class="btn-primary" onclick="saveName()">Save Name</button>
      </div>
    </div>
  </div>

  <!-- ===================== Recording modals ===================== -->
  <div id="recordProgressOverlay" class="rec-overlay" aria-hidden="true">
    <div class="rec-panel">
      <h3>Recording Diagnostics</h3>
      <p class="muted" style="font-size:.85rem;line-height:1.5;">
        Capturing CPU/RAM, queue depths, drop counters, log lines every second.
        Auto-finishes after 10&nbsp;min &mdash; click <b>Finish Now</b> at any time to
        end the capture and generate the report immediately.
      </p>
      <div class="rec-progress-bar"><div class="rec-progress-fill" id="recordProgressFill"></div></div>
      <p class="rec-progress-text" id="recordProgressText">0.0s / 600s</p>
      <div style="display:flex;gap:10px;margin-top:16px;justify-content:flex-end;">
        <button type="button" class="btn-primary" id="recordFinishNowBtn">Finish Now</button>
      </div>
    </div>
  </div>

  <div id="recordResultOverlay" class="rec-overlay" aria-hidden="true">
    <div class="rec-panel rec-result-panel">
      <h3>Diagnostic Recording Complete</h3>
      <p class="muted" style="font-size:.82rem;margin-bottom:10px;">
        Copy or download. Paste the file for analysis.
      </p>
      <textarea class="rec-result-textarea" id="recordResultText" readonly></textarea>
      <div class="rec-result-actions">
        <button type="button" class="btn-glass" onclick="closeRecordResult()">Close</button>
        <button type="button" class="btn-glass" onclick="copyRecordResult()">Copy</button>
        <button type="button" class="btn-primary" onclick="downloadRecordResult()">Download .txt</button>
      </div>
    </div>
  </div>

  <div id="toast" role="status" aria-live="polite"></div>

  <script>
    (function(){
    "use strict";

    var $ = function(id){ return document.getElementById(id); };
    var sanitizeName = function(s){ return (s||'').replace(/[^A-Za-z0-9]/g,''); };
    var setText = function(id, text){ var el = $(id); if (el && el.textContent !== text) el.textContent = text; };
    var updateText = function(id, text){ var el = $(id); if (el && el.innerHTML !== text) el.innerHTML = text; };

    // ---- Toast ----------------------------------------------------------
    var toastTimer;
    function toast(msg, kind) {
      var t = $('toast');
      if (!t) return;
      t.textContent = msg;
      t.className = 'show ' + (kind || '');
      clearTimeout(toastTimer);
      toastTimer = setTimeout(function(){ t.className = ''; }, 2600);
    }

    // ---- Global loading bar ---------------------------------------------
    var __loadingCount = 0;
    function showLoading() { __loadingCount++; var b = $('top-loading-bar'); if (!b) return; b.classList.remove('done'); b.classList.add('active'); }
    function hideLoading() {
      __loadingCount = Math.max(0, __loadingCount - 1); if (__loadingCount > 0) return;
      var b = $('top-loading-bar'); if (!b) return;
      b.classList.remove('active'); b.classList.add('done');
      setTimeout(function(){ if (__loadingCount === 0) b.classList.remove('done'); }, 500);
    }

    async function api(path, method, data, opts) {
      method = method || 'GET'; opts = opts || {};
      var fetchOpts = { method: method, headers: {} };
      if (data) { fetchOpts.headers['Content-Type'] = 'application/json'; fetchOpts.body = JSON.stringify(data); }
      if (!opts.silent) showLoading();
      try {
        var res = await fetch(path, fetchOpts);
        return await res.json();
      } catch (e) { return { ok: false }; }
      finally { if (!opts.silent) hideLoading(); }
    }

    // ---- Global state ---------------------------------------------------
    var isMuted       = false;
    var isQuietMode   = false;
    var currentTabId  = 'tab-zones';
    var currentSubTab = 'adv-status';
    var playbackMode  = 'off';
    var currentSource = 'BT';
    var zonesData     = [];
    var zonesKey      = '';
    var activeDrags   = { volume: false, buffer: false };
    var pendingDefaultSourceSave = null;
    var pendingModeSave = null;
    var pendingModeSaveTimer = null;
    var setModeSeq = 0;

    // ====================================================================
    //  Tab navigation
    // ====================================================================
    window.openTab = function (evt, tabId) {
      currentTabId = tabId;
      document.querySelectorAll('.tab-content').forEach(function(el){ el.classList.remove('active'); });
      document.querySelectorAll('.menu-bar .nav-btn').forEach(function(el){ el.classList.remove('active'); });
      var panel = $(tabId);
      if (panel) panel.classList.add('active');
      if (evt && evt.currentTarget) evt.currentTarget.classList.add('active');

      if (tabId === 'tab-advanced') openSubTab(currentSubTab);
      if (tabId === 'tab-settings') loadNetworkState();
      if (tabId === 'tab-zones')    refreshStatus();

      var showNetActions = (tabId === 'tab-settings');
      var saveB = $('net-save-btn'), revB = $('net-revert-btn');
      if (saveB) saveB.style.display = showNetActions ? '' : 'none';
      if (revB)  revB.style.display  = showNetActions ? '' : 'none';

      // Hide phone floating volume when not on Zones
      var vf = $('volumeFloat');
      if (vf) vf.style.display = (tabId === 'tab-zones' && window.innerWidth <= 600) ? '' : 'none';
    };

    window.openSubTab = function (subtabId) {
      currentSubTab = subtabId;
      document.querySelectorAll('.sub-panel').forEach(function(el){ el.classList.remove('active'); });
      document.querySelectorAll('.sub-tab-btn').forEach(function(el){ el.classList.remove('active'); });
      var panel = $(subtabId);
      if (panel) panel.classList.add('active');
      var btn = document.querySelector('.sub-tab-btn[data-subtab="' + subtabId + '"]');
      if (btn) btn.classList.add('active');

      if (subtabId === 'adv-status')   fetchHeavyStats();
      if (subtabId === 'adv-source')   fetchSourceInfo();
      if (subtabId === 'adv-encoding') loadEncoderSettings();
    };

    // ====================================================================
    //  Mode / source / volume
    // ====================================================================
    async function setMode(m) {
      var seq = ++setModeSeq;

      // [mode-switch-flush] (unchanged — captures in-flight encoder edits
      // into the OLD mode's preset before we leave it)
      if (encSendTimer) {
        clearTimeout(encSendTimer);
        encSendTimer = null;
        try {
          await fetch('/api/audio/encoding', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(encBuildPayload())
          });
        } catch (e) { /* best-effort; mode switch takes priority */ }
      }
      if (seq !== setModeSeq) return;

      var sel = $('mode-select');
      if (sel && sel.value !== m) sel.value = m;
      playbackMode = m;

      // [pending-mode-save-fix]
      // Optimistically lock the dropdown across the round-trip. Without
      // this, the very next encApplyFromResponse() (called by the
      // loadEncoderSettings() below) reverts the select to whatever the
      // server still reports — which is exactly the "music → snaps back
      // to movie" bug. refreshStatus() already honours this flag; the
      // encoder-form path (Patch 3) is what was missing the guard.
      pendingModeSave = m;
      if (pendingModeSaveTimer) clearTimeout(pendingModeSaveTimer);
      pendingModeSaveTimer = setTimeout(function(){
        if (pendingModeSave === m && seq === setModeSeq) {
          // Server never confirmed within 10 s. Give up and re-sync
          // so the UI shows reality rather than a stuck optimistic state.
          pendingModeSave = null;
          pendingModeSaveTimer = null;
          refreshStatus();
        }
      }, 10000);

      var r = await api('/api/config', 'POST', { mode: playbackMode });
      if (seq !== setModeSeq) return;   // superseded by a newer click

      if (!r || !r.ok) {
        // Server refused the switch (validation, preset missing, etc.).
        // Release the lock immediately and revert.
        pendingModeSave = null;
        if (pendingModeSaveTimer) { clearTimeout(pendingModeSaveTimer); pendingModeSaveTimer = null; }
        toast('Mode change rejected by server', 'err');
        refreshStatus();
        return;
      }

      if (currentSubTab === 'adv-encoding') loadEncoderSettings();
    }

    async function toggleQuietMode() {
      isQuietMode = !isQuietMode;
      var btn = $('btn-quiet'), icon = $('quiet-icon');
      btn.classList.toggle('active', isQuietMode);
      btn.classList.toggle('danger', isQuietMode);
      icon.textContent = isQuietMode ? '🌕' : '🌙';
      await api('/api/config', 'POST', { quietMode: isQuietMode });
    }
    window.toggleQuietMode = toggleQuietMode;

    async function setSource(src) {
      var r = await api('/api/source', 'POST', { source: src });
      if (r && r.ok) {
        currentSource = src;
        if (pendingDefaultSourceSave) clearTimeout(pendingDefaultSourceSave);
        pendingDefaultSourceSave = setTimeout(async function(){
          pendingDefaultSourceSave = null;
          await api('/api/config', 'POST', { defaultSource: currentSource });
        }, 6000);
      } else {
        var el = $('source-select');
        if (el) el.value = currentSource;
        toast('Source change not accepted', 'err');
      }
    }

    function syncSourceFromState(s) {
      var src = (s && s.receiverAudio && s.receiverAudio.source) || '';
      if (src && src !== currentSource) {
        currentSource = src;
        var el = $('source-select');
        if (el && el.value !== src) el.value = src;
      }
      var m = s && s.mode;
      if (!pendingModeSave && m && m !== playbackMode) {
        playbackMode = m;
        var sel = $('mode-select');
        if (sel && sel.value !== m) sel.value = m;
      }
    }

    // Bulk mute for all rooms. Mirrors the old master-mute button's
    // position but changes semantics: instead of touching masterMuted (which
    // is reserved for the OTA wizard), it flips every zone's `muted` flag.
    // Clicking when all are muted unmutes all; clicking when any is unmuted
    // mutes all. Individual room buttons keep working independently, so the
    // operator can mute-all then selectively unmute specific rooms.
    async function muteAllZones() {
      if (isMuted) { toast('Master mute active (OTA)'); return; }
      if (!zonesData.length) return;
      var allMuted = zonesData.every(function(z){ return z.muted; });
      var wantMute = !allMuted;
      var targets = zonesData.filter(function(z){ return z.muted !== wantMute; });
      if (!targets.length) { await refreshStatus(); return; }
      var results = await Promise.all(targets.map(function(z){
        return api('/api/zones/config', 'POST', { zoneId: z.id, muted: wantMute }, { silent: true });
      }));
      if (results.some(function(r){ return !r || !r.ok; })) {
        toast('Some rooms could not be updated', 'err');
      }
      await refreshStatus();
    }
    window.muteAllZones = muteAllZones;

    // Paints the bulk-mute button. Reflects EITHER server masterMuted
    // (OTA-driven) OR all-zones-muted, so the icon is honest in both states.
    function paintMuteAllButton() {
      var btn = $('btn-mute'), icon = $('mute-icon');
      if (!btn || !icon) return;
      var allZonesMuted = zonesData.length > 0 && zonesData.every(function(z){ return z.muted; });
      var effectiveMuted = isMuted || allZonesMuted;
      btn.classList.toggle('active', effectiveMuted);
      btn.classList.toggle('danger', effectiveMuted);
      if (effectiveMuted) {
        btn.title = isMuted ? 'Muted (server/OTA)' : 'All rooms muted \u2014 click to unmute all';
        icon.textContent = '\u{1F507}';
      } else {
        btn.title = 'Mute all rooms';
        icon.textContent = '\u{1F50A}';
      }
    }

    // ====================================================================
    //  Name modal
    // ====================================================================
    window.openNameModal = function () {
      var modal = $('nameModal'), input = $('edit-name-input');
      if (!modal || !input) return;
      var current = ($('server-title-text') && $('server-title-text').textContent || 'Enkelvoud').trim();
      input.value = current;
      modal.classList.add('open');
      modal.setAttribute('aria-hidden', 'false');
      setTimeout(function(){ input.focus(); input.select(); }, 100);
    };
    window.closeNameModal = function () {
      var modal = $('nameModal');
      if (modal) { modal.classList.remove('open'); modal.setAttribute('aria-hidden', 'true'); }
    };
    window.saveName = async function () {
      var input = $('edit-name-input');
      if (!input) return;
      var newName = (input.value || '').replace(/[^A-Za-z0-9]/g, '');
      if (!newName) { alert('Server name is required.'); return; }
      if (newName.length > 32) { alert('Server name must be 32 characters or fewer.'); return; }
      var r = await api('/api/config', 'POST', { serverName: newName });
      if (!r || !r.ok) { alert('Could not save name'); return; }
      setText('server-title-text', newName);
      var hiddenNet = $('net-server-name');
      if (hiddenNet) hiddenNet.value = newName;
      closeNameModal();
      if (confirm('Name saved. Reboot now so the new .local address takes effect?')) {
        fetch('/api/restart', { method: 'POST' }).catch(function(){});
      }
    };
    (function(){
      var nameInput = $('edit-name-input');
      if (!nameInput) return;
      nameInput.addEventListener('keydown', function(e){
        if (e.key === 'Enter') { e.preventDefault(); saveName(); }
      });
    })();

    // ====================================================================
    //  Status refresh / heavy stats
    // ====================================================================
    async function fetchHeavyStats() {
      var s = await api('/api/stats', 'GET', undefined, { silent: true });
      if (!s || !s.ok) return;
      // [ota-heap-safety-client] Server returns {"ok":true,"ota":true}
      // during OTA. The field set is intentionally empty there, so
      // rendering would show undefined/garbage. Hold the panel on its
      // last good snapshot instead.
      if (s.ota) return;
      var msFmt = function(v){ return (v||0) + ' <span style="font-size:0.8rem;color:var(--text-muted);">ms</span>'; };
      updateText('stat-latency', (s.avgLatencyMs || 0).toFixed(1) + ' <span style="font-size:0.8rem;color:var(--text-muted);">ms</span>');
      updateText('stat-buffer',  msFmt(s.audioBufferMs));
      // [dynamic-source-rate] Populate the Transport section with the
      // measured I2S clock, not a hardcoded value. The field is on the
      // Advanced > Encoding panel, so it may not be in the DOM yet; the
      // guard covers that.
      (function(){
        var el = document.getElementById('enc-source-rate');
        if (el && s.sourceSampleRate) {
          el.textContent = s.sourceSampleRate.toLocaleString() + ' Hz';
        }
      })();
      updateText('stat-delay',   msFmt(s.latencyAdjustmentMs));
      updateText('stat-mode',    s.mode || 'off');
      updateText('stat-clients', s.wsClients);
      updateText('stat-master', (s.masterVolume || 0) + '%' + (s.masterMuted ? ' <span style="color:var(--danger);">(muted)</span>' : ''));

      var nodeHtml = '';
      (s.nodes || []).forEach(function(n){
        nodeHtml += '<div>' + n.ip + ' — reported latency ' + n.latencyMs + ' ms (queue ' + n.queueDepth + '/' + (n.target || '?') + ', rebuffers ' + n.rebuffers + ')</div>';
      });
      updateText('stat-nodes', nodeHtml || 'No nodes reporting yet');

      var dropped = (s.streaming && s.streaming.opusDroppedLast5s) || 0;
      var inHz = s.measuredInputHz || 0;
      var inQuality;
      if (inHz <= 0) inQuality = '—';
      else {
        // [incoming-rate-classification]
        // Classify the measured source rate against the nearest standard
        // audio rate, not against a hardcoded 48000. SOURCE_SAMPLE_RATE
        // in EVDConfig.h is the server pipeline reference, not a claim
        // about the source. A 44.1 kHz A2DP peer is nominal, not 8.8 %
        // off. Also matches the server-side drift math, which now
        // references the same standard table (see [drift-ref-is-actual-
        // source-nominal] in EVDAudio.h).
        var _stdRates = [8000, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000];
        var _refHz = 48000;
        for (var _i = 0; _i < _stdRates.length; _i++) {
          if (Math.abs(inHz - _stdRates[_i]) / _stdRates[_i] < 0.01) { _refHz = _stdRates[_i]; break; }
        }
        var devPct = ((inHz - _refHz) / _refHz) * 100;
        var devTag = Math.abs(devPct) < 0.05
          ? ' <span style="color:var(--success);">✓ nominal</span>'
          : ' <span style="color:var(--text-muted);">(' + (devPct >= 0 ? '+' : '') + devPct.toFixed(2) + ' %)</span>';
        inQuality = inHz.toFixed(0) + ' Hz' + devTag;
      }
      updateText('stat-diag',
        'I2S Read Errors: ' + s.i2sReadErrors + ' <br>' +
        'Dropped Opus (5s): ' + dropped + ' <br>' +
        'Avg Encode Time: ' + (s.avgEncodeUs || 0) + ' us <br>' +
        'Clock drift: ' + s.clockDriftPpm + ' ppm <br>' +
        'Incoming Quality: ' + inQuality + ' <br>' +
        'Input path: ' + (s.serverInputSource || 'I2S') +
        (s.serverInputSource === 'USB' ? ' (' + (((s.usbBytesLastWin||0)/1024).toFixed(1)) + ' KiB win)' : '') +
        '<br>RSSI: ' + s.wifiRssi + 'dBm');
      updateText('stat-pipeline',
        'rawQueue:   ' + s.rawQueueDepth + ' / ' + s.rawQueueLen + ' <br>' +
        'opusQueue:  ' + s.opusQueueDepth + ' / ' + s.opusQueueLen + ' <br>' +
        'Opus encoded (win): ' + s.opusEncodedLastWin + ' <br>' +
        'Opus dropped (win): ' + s.opusDroppedLastWin + ' <br>' +
        'WS sent (win): ' + s.wsSentLastWin + '  ' + (((s.wsBytesLastWin||0)/1024).toFixed(1)) + ' KiB');

      var rawEl = $('status-raw-json');
      if (rawEl) rawEl.textContent = JSON.stringify(s, null, 2);
    }

    async function refreshStatus() {
      if (document.hidden) return;
      var s = await api('/api/state', 'GET', undefined, { silent: true });
      if (!s || !s.ok) return;
      setText('server-title-text', s.serverName || 'Enkelvoud');

      (function paintLoadPills(){
        // [pills-selector-fix] Removed the '#' prefix. $ is document.getElementById.
        var c0 = $('stat-core0'), c1 = $('stat-core1'), rm = $('stat-ram'), dm = $('stat-dma'), ps = $('stat-psram');
        
        function setPill(el, text, pct, extraClass) {
          if (!el) return;
          el.textContent = text;
          el.classList.remove('warn', 'crit');
          if (extraClass) el.classList.add(extraClass);
          if (typeof pct === 'number' && pct >= 0) {
            if (pct >= 85) el.classList.add('crit');
            else if (pct >= 65) el.classList.add('warn');
          }
        }
        
        // [cpu-pill-honesty] Show n/a rather than "0 %" when the queue
        // has depth — a system that is clearly serving audio traffic
        // cannot honestly be 0 % busy. This matches the server-side
        // diagnostic in handleApiState(). A truly idle node still
        // reports 0 %.
        var queueBusy = ((s.rawQueueDepth | 0) > 0) || ((s.opusQueueDepth | 0) > 0);
        function cpuLabel(prefix, v) {
          if (typeof v !== 'number' || v < 0) return prefix + ' n/a';
          if (v < 2 && queueBusy) return prefix + ' n/a';
          return prefix + ' ' + v + '%';
        }
        setPill(c0, cpuLabel('C0', s.cpuCore0Pct), s.cpuCore0Pct);
        setPill(c1, cpuLabel('C1', s.cpuCore1Pct), s.cpuCore1Pct);

        // [ram-pill-fragmentation] Show the largest contiguous free block
        // inline. A "RAM 40 %" pill with a "max 3 KiB" suffix is the
        // exact signature of "looks fine, next malloc will fail". Warn-
        // color when the largest block cannot hold the OTA job task
        // stack (4 KiB) with headroom.
        if (typeof s.heapUsedPct === 'number') {
          var maxAlloc = (typeof s.heapMaxAlloc === 'number') ? s.heapMaxAlloc : -1;
          var peak = (typeof s.heapPeakUsedPct === 'number') ? s.heapPeakUsedPct : -1;
          var suffix = '';
          if (maxAlloc >= 0) {
            suffix = ' · max ' + (maxAlloc / 1024).toFixed(1) + ' KiB';
          }
          if (peak >= 0 && peak > s.heapUsedPct) {
            suffix += ' · pk ' + peak + '%';
          }
          var extraCls = (maxAlloc >= 0 && maxAlloc < 8192) ? 'warn' : null;
          setPill(rm, 'RAM ' + s.heapUsedPct + '%' + suffix, s.heapUsedPct, extraCls);
        }

        // [dma-pill] DMA-capable free bytes — the number that gates WiFi
        // and Opus reliability on this SoC. Hidden on firmware that does
        // not send it.
        if (dm) {
          if (typeof s.dmaFreeBytes === 'number') {
            dm.style.display = '';
            var warnDma = (s.dmaFreeBytes < 24576); // < 24 KiB is a red flag
            setPill(dm, 'DMA ' + ((s.dmaFreeBytes / 1024) | 0) + ' KiB' +
              (typeof s.dmaMaxAlloc === 'number' ? ' · max ' + (s.dmaMaxAlloc / 1024).toFixed(1) : ''),
              warnDma ? 80 : 20);
          } else {
            dm.style.display = 'none';
          }
        }

        if (ps) {
          if (s.psramTotalBytes > 0) {
            ps.style.display = '';
            var psTxt = 'PS ' + (s.psramUsedPct || 0) + '%';
            if (typeof s.psramPeakUsedPct === 'number' && s.psramPeakUsedPct > s.psramUsedPct) {
              psTxt += ' · pk ' + s.psramPeakUsedPct + '%';
            }
            setPill(ps, psTxt, s.psramUsedPct || 0);
          } else {
            ps.style.display = 'none';
          }
        }
      })();

      if (!activeDrags.volume) {
        var v = s.masterVolume;
        var volD = $('volume'), volM = $('volume-mobile');
        var lblD = $('volume-label'), lblM = $('volume-float-label');
        if (volD && document.activeElement !== volD && volD.value !== String(v)) volD.value = v;
        if (volM && document.activeElement !== volM && volM.value !== String(v)) volM.value = v;
        if (lblD) lblD.textContent = v + '%';
        if (lblM) lblM.textContent = v;
      }
      if (!activeDrags.buffer) {
        var b = s.audioBufferMs;
        var bEl = $('buffer-latency'), bVal = $('buffer-latency-val');
        if (bEl && document.activeElement !== bEl && bEl.value !== String(b)) {
          bEl.value = b;
        }
        if (bVal) bVal.textContent = b + ' ms';
        // [audio-buffer-per-mode-sync] ALWAYS sync encState from the
        // server value when not actively dragging. Previously this only
        // ran inside the `bEl.value !== b` branch, so if the DOM slider
        // already matched the server (e.g. old mode also had 150 ms),
        // encState stayed stale and the next Save wrote the wrong
        // preset's bufferMs.
        if (typeof b === 'number') encState.bufferMs = b;
      }   
      // [pending-mode-save-confirm]
      // The optimistic lock set by setMode() is released here as soon as
      // the poll observes the server has applied the request. Healthy
      // switches clear within ≤ 4 s; the 10 s timeout in setMode() is
      // only the failure fallback.
      var modeJustConfirmed = false;
      if (pendingModeSave && s.mode === pendingModeSave) {
        pendingModeSave = null;
        if (pendingModeSaveTimer) { clearTimeout(pendingModeSaveTimer); pendingModeSaveTimer = null; }
        modeJustConfirmed = true;
      }
      if (modeJustConfirmed && currentSubTab === 'adv-encoding') {
        // [mode-confirm-form-reload] Lock released; re-pull the encoder
        // form so stale field values (complexity/bitrate/VBR) written by
        // the mid-switch encApplyFromResponse() are replaced with the new
        // mode's preset. Guarded by modeJustConfirmed so it fires exactly
        // once per successful mode switch, not on every poll.
        loadEncoderSettings();
      }
      if (!pendingModeSave && s.mode && s.mode !== playbackMode) {
        playbackMode = s.mode;
        var sel = $('mode-select');
        if (sel && sel.value !== s.mode) sel.value = s.mode;
        // [mode-switch-form-reload] A mode change came from elsewhere
        // (another browser tab, an API call, or the 1 s debounce in
        // setMode() finally landing). Reload the encoder form so it does
        // not keep showing the previous mode's values.
        if (currentSubTab === 'adv-encoding') loadEncoderSettings();
      }
      if (s.masterMuted !== isMuted) {
        isMuted = s.masterMuted;
      }
      if (s.quietMode !== undefined && s.quietMode !== isQuietMode) {
        isQuietMode = s.quietMode;
        var btn = $('btn-quiet'), icon = $('quiet-icon');
        if (btn && icon) {
          btn.classList.toggle('active', isQuietMode);
          btn.classList.toggle('danger', isQuietMode);
          icon.textContent = isQuietMode ? '🌕' : '🌙';
        }
      }
      if (s.zones) {
        var k = JSON.stringify(s.zones);
        if (k !== zonesKey) { zonesKey = k; zonesData = s.zones; renderZones(); }
      }
      paintMuteAllButton();
      syncSourceFromState(s);
    }

    // ====================================================================
    //  Zones rendering
    // ====================================================================
    function renderZones() {
      var container = $('zonesContainer');
      if (!container) return;
      var html = '';
      zonesData.forEach(function(zone){
        var safeName = (zone.name || '').replace(/"/g, '&quot;');
        html +=
          '<div class="zone-box" ondragover="allowDrop(event)" ondragleave="removeDropStyle(event)" ondrop="dropNode(event, \'' + zone.id + '\')">' +
            '<div class="zone-header">' +
              '<input type="text" value="' + safeName + '" pattern="[A-Za-z0-9]+" maxlength="24" ' +
                     'autocomplete="off" autocapitalize="off" spellcheck="false" ' +
                     'onbeforeinput="if (event.data && /[^A-Za-z0-9]/.test(event.data)) event.preventDefault()" ' +
                     'onchange="updateZoneName(\'' + zone.id + '\', this.value)" ' +
                     (zone.id === 'main' ? 'title="Main room cannot be deleted"' : '') + '>' +
              (zone.id !== 'main'
                ? '<button type="button" class="btn-glass" style="padding:4px 8px;font-size:0.75rem;border-color:var(--danger);color:var(--danger);" onclick="deleteZone(\'' + zone.id + '\')">Del</button>'
                : '') +
            '</div>' +
            '<div class="zone-controls">' +
              '<div class="zone-row"><span>Room Volume</span><span id="zv-' + zone.id + '">' + zone.volume + '%</span></div>' +
              '<input type="range" min="0" max="100" value="' + zone.volume + '" ' +
                     'oninput="zoneVolLive(\'' + zone.id + '\', this.value)" ' +
                     'onchange="updateZoneVolume(\'' + zone.id + '\', this.value)">' +
              '<div class="zone-actions">' +
                '<button type="button" class="btn-glass ' + (zone.muted ? 'btn-mute-active' : '') + '" ' +
                        'style="padding:5px 12px;font-size:0.78rem;" ' +
                        'onclick="toggleZoneMute(\'' + zone.id + '\')">' +
                  (zone.muted ? 'Unmute' : 'Mute Room') +
                '</button>' +
              '</div>' +
            '</div>' +
            '<div style="flex-grow:1;min-height:50px;">';
    var nodes = (zone.nodes && zone.nodes.length) ? zone.nodes.slice() : [];
    // Sort: online (known===true) first, offline (known===false) last.
    // Array.prototype.sort is stable in every supported browser, so online
    // nodes keep their existing order and offline nodes stack at the bottom.
    nodes.sort(function(a, b){
      var aOff = (a.known === false) ? 1 : 0;
      var bOff = (b.known === false) ? 1 : 0;
      return aOff - bOff;
    });
    if (nodes.length > 0) {
      nodes.forEach(function(node){
        var safeNodeName = (node.name || 'Node').replace(/"/g, '&quot;');
        var verTag = node.version ? ' <span style="color:var(--text-muted);font-weight:400;font-size:0.75rem;">v' + String(node.version).replace(/[^0-9A-Za-z._-]/g, '') + '</span>' : '';
        var isOffline = (node.known === false);
        var chipClass = isOffline ? 'node-chip offline' : 'node-chip';
        var gearHtml = isOffline
          ? '<span class="gear disabled" title="Node offline" aria-disabled="true">&#9881;</span>'
          : '<a class="gear" href="http://' + node.ip + '/" title="Open node settings">&#9881;</a>';
        var delHtml = isOffline
          ? '<button type="button" draggable="false" class="btn-glass" style="flex:0 0 auto;padding:3px 7px;font-size:0.7rem;border-color:var(--danger);color:var(--danger);" title="Remove offline node" onclick="removeOfflineNode(event, \'' + zone.id + '\', \'' + node.ip + '\')">Del</button>'
          : '';
        var displayName = isOffline ? 'Offline' : safeNodeName;
        var actionsHtml =
          '<div style="display:flex;align-items:center;gap:6px;flex:0 0 auto;">' +
            delHtml + gearHtml +
          '</div>';
        html +=
          '<div class="' + chipClass + '" draggable="true" ondragstart="dragNode(event, \'' + node.ip + '\')">' +
            '<div class="node-meta"><strong>' + displayName + verTag + '</strong><span class="node-ip">' + node.ip + '</span></div>' +
            actionsHtml +
          '</div>';
      });
        } else {
          html += '<p style="font-size:0.8rem;color:var(--text-muted);text-align:center;margin-top:10px;">Drag nodes here</p>';
        }
        html += '</div></div>';
      });
      container.innerHTML = html;
    }

    window.zoneVolLive = function(id, v) { var el = $('zv-' + id); if (el) el.textContent = v + '%'; };
    window.dragNode = function(ev, ip) { ev.dataTransfer.setData('text/plain', ip); };
    window.allowDrop = function(ev) { ev.preventDefault(); ev.currentTarget.classList.add('drag-over'); };
    window.removeDropStyle = function(ev) { ev.currentTarget.classList.remove('drag-over'); };
    window.dropNode = async function(ev, zoneId) {
      ev.preventDefault(); ev.currentTarget.classList.remove('drag-over');
      var ip = ev.dataTransfer.getData('text/plain');
      await api('/api/zones/move', 'POST', { ip: ip, zoneId: zoneId });
      refreshStatus();
    };
    window.createNewZone = async function() {
      var raw = prompt('New room name (letters and numbers only):', 'Zone' + (zonesData.length + 1));
      if (raw === null) return;
      var name = sanitizeName(raw);
      if (name.length === 0) { alert('Use letters and numbers only.'); return; }
      var r = await api('/api/zones/create', 'POST', { name: name });
      if (r && r.ok) await refreshStatus();
    };
    window.deleteZone = async function(zoneId) {
      if (!confirm('Delete this room? Nodes move back to Main Room.')) return;
      await api('/api/zones/delete', 'POST', { zoneId: zoneId });
      refreshStatus();
    };
    window.updateZoneName = async function(zoneId, name) {
      var clean = sanitizeName(name);
      if (!clean) { refreshStatus(); return; }
      await api('/api/zones/config', 'POST', { zoneId: zoneId, name: clean });
    };
    window.updateZoneVolume = async function(zoneId, volume) {
      await api('/api/zones/config', 'POST', { zoneId: zoneId, volume: parseInt(volume, 10) });
    };
    window.toggleZoneMute = async function(zoneId) {
      var z = zonesData.find(function(x){ return x.id === zoneId; });
      if (!z) return;
      await api('/api/zones/config', 'POST', { zoneId: zoneId, muted: !z.muted });
      refreshStatus();
    };

    window.removeOfflineNode = async function(ev, zoneId, ip) {
      if (ev && ev.stopPropagation) ev.stopPropagation();
      if (!confirm('Remove offline node ' + ip + ' from this room?')) return;
      const r = await api('/api/zones/remove-node', 'POST', { zoneId: zoneId, ip: ip });
      if (!r || !r.ok) toast('Remove failed', 'err');
      refreshStatus();
    };

    // ====================================================================
    //  Network tab
    // ====================================================================
    var wifiScanPollTimer = null, wifiScanPollBusy = false;

    async function loadNetworkState() {
      var s = await api('/api/network/state');
      if (!s || !s.ok) return;
      setVal('net-server-name', s.serverName || '');
      setVal('net-ssid',        s.ssid || '');
      // [security-password-not-echoed] Server no longer sends the password.
      // Do NOT setVal('net-pass', '') here: loadNetworkState runs on every
      // Network-tab activation and would wipe any password the user typed
      // before switching tabs. Only set the placeholder; the field is
      // already blank on first render.
      (function(){
        var pEl = $('net-pass');
        if (!pEl) return;
        if (s.passwordSet) pEl.placeholder = '\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022 (unchanged if blank)';
        else               pEl.placeholder = 'Set a password';
      })();
      setVal('net-ip',          s.ip || '');
      setVal('net-gw',          s.gateway || '');
      setVal('net-sn',          s.subnet || '255.255.255.0');
      setVal('net-dns',         s.dns || '');
      var chk = $('net-use-static'), box = $('net-static-fields');
      if (chk) chk.checked = !!s.useStatic;
      if (box) box.hidden = !s.useStatic;
    }
    function setVal(id, v) { var el = $(id); if (el && document.activeElement !== el) el.value = v; }

    async function saveNetworkSettings() {
      var btn = $('net-save-btn'), nameEl = $('net-server-name'), ssidEl = $('net-ssid');
      var serverName = (nameEl && nameEl.value || '').trim();
      var ssid = (ssidEl && ssidEl.value || '').trim();
      if (!serverName) { alert('Server name is required.'); return; }
      if (!ssid) { alert('Wi-Fi SSID is required.'); return; }
      if (!/^[A-Za-z0-9]+$/.test(serverName)) { alert('Server name: letters and digits only.'); return; }

      var body = new URLSearchParams();
      body.append('server_name', serverName);
      body.append('ssid', ssid);
      body.append('password', $('net-pass') ? $('net-pass').value || '' : '');
      if ($('net-use-static') && $('net-use-static').checked) {
        body.append('use_static_ip', 'on');
        body.append('ip', $('net-ip') ? $('net-ip').value || '' : '');
        body.append('gateway', $('net-gw') ? $('net-gw').value || '' : '');
        body.append('subnet', $('net-sn') ? $('net-sn').value || '' : '');
        body.append('dns', $('net-dns') ? $('net-dns').value || '' : '');
      }
      var testCard = $('net-test-card'), testTxt = $('net-test-state');
      if (testCard) testCard.style.display = '';
      if (testTxt) { testTxt.textContent = 'Testing connection…'; testTxt.style.color = ''; }
      if (btn) { btn.disabled = true; btn.textContent = 'Testing…'; }

      try {
        var r = await fetch('/save', { method: 'POST', body: body });
        var d = await r.json();
        if (!r.ok || !d.ok) throw new Error(d.error || ('HTTP ' + r.status));
        var st;
        do { await new Promise(function(res){ setTimeout(res, 500); }); st = await (await fetch('/api/network-test')).json(); }
        while (st.state === 'running');
        if (st.state !== 'success') throw new Error(st.error || 'Unable to connect');
        if (testTxt) { testTxt.innerHTML = 'Connected as ' + st.ip + ' — rebooting…'; testTxt.style.color = 'var(--success)'; }
        try { await fetch('/api/restart', { method: 'POST' }); } catch(e){}
        setTimeout(function(){ window.location.href = 'http://' + serverName + '.local/'; }, 8000);
      } catch(e) {
        if (testTxt) { testTxt.textContent = 'Failed: ' + e.message; testTxt.style.color = 'var(--danger)'; }
        if (btn) { btn.disabled = false; btn.textContent = 'Save'; }
      }
    }

    window.closeWifiScan = function() {
      var m = $('wifiScanModal');
      if (m) { m.classList.remove('open'); m.setAttribute('aria-hidden', 'true'); }
      if (wifiScanPollTimer) { clearInterval(wifiScanPollTimer); wifiScanPollTimer = null; }
    };
    window.startWifiScan = async function() {
      var m = $('wifiScanModal'), list = $('wifiScanList');
      if (!m || !list) return;
      m.classList.add('open'); m.setAttribute('aria-hidden', 'false');
      list.innerHTML = '<div class="muted" style="padding:18px 20px;">Scanning…</div>';
      try { await fetch('/api/wifi/scan/start', { cache: 'no-store' }); }
      catch(e) { list.innerHTML = '<div class="muted" style="padding:18px 20px;color:var(--danger);">Could not start scan.</div>'; return; }

      if (wifiScanPollTimer) clearInterval(wifiScanPollTimer);
      wifiScanPollBusy = false;
      var attempts = 0;
      wifiScanPollTimer = setInterval(async function(){
        if (wifiScanPollBusy) return;
        wifiScanPollBusy = true;
        attempts++;
        if (attempts > 40) {
          clearInterval(wifiScanPollTimer); wifiScanPollTimer = null; wifiScanPollBusy = false;
          list.innerHTML = '<div class="muted" style="padding:18px 20px;">Scan timed out.</div>';
          return;
        }
        var s;
        try { s = await (await fetch('/api/wifi/scan/status', { cache: 'no-store' })).json(); }
        catch(e) { wifiScanPollBusy = false; return; }
        if (s.state === 'running') { wifiScanPollBusy = false; return; }
        if (s.state === 'failed') {
          if (Array.isArray(s.networks)) {
            clearInterval(wifiScanPollTimer); wifiScanPollTimer = null; wifiScanPollBusy = false;
            list.innerHTML = '<div class="muted" style="padding:18px 20px;">No networks found.</div>';
            return;
          }
          wifiScanPollBusy = false; return;
        }
        if (!s.networks) { wifiScanPollBusy = false; return; }
        clearInterval(wifiScanPollTimer); wifiScanPollTimer = null; wifiScanPollBusy = false;
        if (!s.networks.length) { list.innerHTML = '<div class="muted" style="padding:18px 20px;">No networks found.</div>'; return; }
        var nets = s.networks.slice().sort(function(a,b){ return b.rssi - a.rssi; });
        list.innerHTML = '';
        nets.forEach(function(n){
          var b = document.createElement('button');
          b.type = 'button'; b.className = 'wifi-net';
          var sEl = document.createElement('span'); sEl.textContent = n.ssid || '(hidden)';
          var rEl = document.createElement('span'); rEl.className = 'rssi'; rEl.textContent = (n.rssi|0) + ' dBm';
          b.appendChild(sEl); b.appendChild(rEl);
          b.addEventListener('click', function(){
            var ssidEl = $('net-ssid'); if (ssidEl) ssidEl.value = n.ssid || '';
            closeWifiScan();
            if ($('net-pass')) $('net-pass').focus();
          });
          list.appendChild(b);
        });
      }, 500);
    };

    // ====================================================================
    //  Source board
    // ====================================================================
    async function fetchSourceInfo() {
      try {
        var j = await api('/api/source/info', 'GET', undefined, { silent: true });
        if (!j || !j.ok) { setText('src-link', 'no data'); return; }
        setText('src-link', j.link ? 'UP' : 'DOWN');
        setText('src-bt',   j.btConnected ? 'connected' : 'idle');
        setText('src-mac',  j.connectedMac || '—');
        setText('src-vol',  (j.volume | 0) + ' / 127');

        var kickWrap = $('src-kick-wrap');
        if (kickWrap) kickWrap.style.display = ((j.activeSource === 'BT') && (j.btConnected === true)) ? '' : 'none';

        var dj = j.devices;
        var devArr = Array.isArray(dj) ? dj : (dj && Array.isArray(dj.devices)) ? dj.devices : [];
        var devs = devArr.map(function(d){ return d.mac + (d.conn ? ' *' : '') + ' (vol ' + d.vol + ')'; }).join('\n');
        setText('src-devices', devs || 'no devices');
        setText('src-diag',
          'BT connecting: '   + (j.a2dpConnecting    | 0) + '\n' +
          'BT connected: '    + (j.a2dpConnected     | 0) + '\n' +
          'BT disconnected: ' + (j.a2dpDisconnected  | 0) + '\n' +
          'AVRCP CT: '        + (j.avrcConnected ? 'yes' : 'no') + '\n' +
          'Vol events: '      + (j.volumeEvents      | 0) + '\n' +
          'UART lines: '      + (j.uartLines         | 0) + '\n' +
          'Source: '          + (j.activeSource      || '?') + '\n' +
          'Last heartbeat: '  + (j.heartbeatAgeMs === undefined ? '—' : ((j.heartbeatAgeMs / 1000).toFixed(1) + ' s ago')));
        setText('src-log', (j.log || []).join('\n') || '(no log entries)');
      } catch(e) { setText('src-link', 'unreachable'); }
    }

    // ====================================================================
    //  Log controllers
    // ====================================================================
    function makeLogController(opts) {
      var paused = false;
      var pauseBtn = $(opts.pauseBtnId), clearBtn = $(opts.clearBtnId), stateEl = $(opts.stateId);
      if (stateEl) stateEl.textContent = 'polling';

      async function applyLevel(level) {
        try { await api('/api/log/level', 'POST', { level: level }); } catch(e){}
      }
      if (pauseBtn) {
        pauseBtn.addEventListener('click', function(){
          paused = !paused;
          pauseBtn.textContent = paused ? 'Resume' : 'Pause';
          if (stateEl) stateEl.textContent = paused ? 'paused' : 'polling';
          // [per-tab-pause] Do NOT touch /api/log/level here.
          // gSerialLogLevel is a single server-side global shared by both
          // tabs, so pausing the Status log silenced the Source log too
          // (and vice versa). Pause is now client-side only: stop polling.
          // Server keeps emitting at whatever level the advanced-logging
          // button set. Cost: ring keeps filling while a tab is paused;
          // negligible on a 32-entry ring.
        });
      }
      if (clearBtn && opts.clearFn) clearBtn.addEventListener('click', opts.clearFn);
      setInterval(function(){
        if (paused || document.hidden) return;
        // Recording polls /api/stats and /api/source/info itself. Skip
        // while it's active so the recorder captures a clean heap
        // window without the page's own pollers adding String churn
        // on the same Core 0 task.
        if (recording) return;
        if (opts.tabId && currentTabId !== opts.tabId) return;
        if (opts.subtabId && currentSubTab !== opts.subtabId) return;
        opts.pollFn();
      }, 2000);
    }

    makeLogController({
      pauseBtnId: 'status-log-pause',
      clearBtnId: 'status-log-clear',
      stateId:    'status-log-state',
      pollFn:     function(){ fetchHeavyStats(); },
      tabId:      'tab-advanced',
      subtabId:   'adv-status',
      clearFn:    function(){
        ['stat-latency','stat-buffer','stat-delay','stat-mode','stat-clients','stat-master','stat-nodes','stat-diag','stat-pipeline'].forEach(function(id){ setText(id, '—'); });
      },
      silenceLevels: [0, 1]
    });

    makeLogController({
      pauseBtnId: 'source-log-pause',
      clearBtnId: 'source-log-clear',
      stateId:    'source-log-state',
      pollFn:     function(){ fetchSourceInfo(); },
      tabId:      'tab-advanced',
      subtabId:   'adv-source',
      clearFn:    function(){ setText('src-log', '(cleared)'); },
      silenceLevels: [0, 1]
    });

    // Kick BT device
    (function(){
      var btn = $('src-kick-btn');
      if (!btn) return;
      btn.addEventListener('click', async function(){
        if (!confirm('Kick the connected BT device? You will have ~2 minutes to pair a new one.')) return;
        var r = await api('/api/bt/kick', 'POST', {});
        if (r && r.ok) {
          var wrap = $('src-kick-wrap'); if (wrap) wrap.style.display = 'none';
          setTimeout(function(){ fetchSourceInfo(); }, 500);
        } else toast('Kick failed', 'err');
      });
    })();

    // Advanced logging + live viewer
    (function(){
      var btn = $('status-advanced-log'), state = $('status-advanced-log-state');
      var viewer = $('status-log-viewer'), pre = $('status-log-pre');
      var viewState = $('status-logview-state'), clearBtn = $('status-logview-clear');
      if (!btn || !state || !viewer || !pre) return;
      var advanced = false, pollTimer = null, lastSeq = 0, lineCount = 0;
      var MAX_LINES = 200;

      function tagFor(level) { return level === 'E' ? '[ERROR]' : level === 'W' ? '[WARN ]' : '[INFO ]'; }
      function appendLine(line) {
        pre.textContent += tagFor(line.level) + ' ' + String(line.ms).padStart(8,' ') + ' ms | ' + line.text + '\n';
        lineCount++;
        if (lineCount > MAX_LINES) {
          var lines = pre.textContent.split('\n');
          pre.textContent = lines.slice(-MAX_LINES - 1).join('\n');
          lineCount = MAX_LINES;
        }
        pre.scrollTop = pre.scrollHeight;
      }
      async function pollTail() {
        // The recorder polls /api/log/tail?since= itself during the
        // capture window. Skipping here avoids two in-flight tail
        // requests racing each other for the lastSeq cursor, which
        // would otherwise cause the recorder's nextSeq to leap forward
        // and drop log lines out of the report.
        if (recording) return;
        try {
          var r = await fetch('/api/log/tail?since=' + lastSeq, { cache: 'no-store' });
          var j = await r.json();
          if (!j || !j.ok) return;
          if (Array.isArray(j.lines)) {
            for (var i = 0; i < j.lines.length; i++) appendLine(j.lines[i]);
            if (j.lines.length) viewState.textContent = j.lines.length + ' new';
          }
          if (typeof j.nextSeq === 'number') lastSeq = j.nextSeq;
        } catch(e){}
      }
      if (clearBtn) clearBtn.addEventListener('click', function(){ pre.textContent = ''; lineCount = 0; viewState.textContent = 'idle'; });
      btn.addEventListener('click', async function(){
        advanced = !advanced;
        btn.textContent = advanced ? 'Disable advanced logging (may affect audio)' : 'Enable advanced logging';
        state.textContent = advanced ? 'verbose (level 3)' : 'off (errors only)';
        try { await api('/api/log/level', 'POST', { level: advanced ? 3 : 1 }); } catch(e){}
        if (advanced) {
          viewer.style.display = ''; pre.textContent = ''; lineCount = 0; lastSeq = 0; viewState.textContent = 'polling';
          await pollTail();
          if (pollTimer) clearInterval(pollTimer);
          pollTimer = setInterval(pollTail, 1000);
        } else {
          if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
          viewer.style.display = 'none';
        }
      });
    })();

    // DLNA on/off
    (function(){
      var btn = $('status-dlna-toggle'), state = $('status-dlna-state');
      if (!btn || !state) return;
      var enabled = true;
      function render() {
        btn.textContent = enabled ? 'Disable DLNA' : 'Enable DLNA';
        state.textContent = enabled ? 'on (visible to phones/TVs)' : 'off (not advertised)';
      }
      api('/api/dlna', 'GET', null, { silent: true }).then(function(j){
        if (j && j.ok) { enabled = !!j.enabled; render(); btn.disabled = false; }
        else state.textContent = 'unavailable';
      });
      btn.addEventListener('click', async function(){
        btn.disabled = true;
        var j = await api('/api/dlna', 'POST', { enabled: !enabled });
        if (j && j.ok) { enabled = !!j.enabled; render(); }
        else state.textContent = 'failed';
        btn.disabled = false;
      });
    })();

    // Basic / advanced toggle on Status
    (function(){
      var btn = $('status-advanced-toggle'), panel = $('status-advanced-panel'), state = $('status-advanced-state');
      if (!btn || !panel) return;
      var advanced = false;
      btn.addEventListener('click', function(){
        advanced = !advanced;
        panel.style.display = advanced ? '' : 'none';
        btn.textContent = advanced ? 'Hide advanced' : 'Show advanced';
        if (state) state.textContent = advanced ? 'full detail' : 'basic view';
        if (advanced) fetchHeavyStats();
      });
    })();

    // ====================================================================
    //  Encoder settings
    // ====================================================================
    var encState = {
      targetBitrate: 96,
      bufferMs:      150,      // [audio-buffer-per-mode] tracked here so Save can send it
      // [opus-sample-rate] Effective (pending or current) sample rate, in Hz.
      sampleRate:    48000,
      fields: {
        complexity:    { value: 8,       locked: false },
        vbr:           { value: true,    locked: false },
        vbrConstraint: { value: true,    locked: false },
        signal:        { value: 'music', locked: false },
        application:   { value: 'audio', locked: false }
      }
    };

    function computeSuggestions(kbps) {
      var complexity;
      if (kbps < 48)       complexity = 5;
      else if (kbps < 80)  complexity = 7;
      else if (kbps < 128) complexity = 8;
      else                 complexity = 10;
      return {
        complexity:    complexity,
        vbr:           true,
        vbrConstraint: true,
        signal:        'music',
        application:   'audio'
      };
    }

    function formatEncValue(field, val) {
      if (field === 'vbr' || field === 'vbrConstraint') return val ? 'On' : 'Off';
      if (field === 'signal') return val.charAt(0).toUpperCase() + val.slice(1);
      if (field === 'application') return val === 'lowdelay' ? 'Low Delay' : val.charAt(0).toUpperCase() + val.slice(1);
      return String(val);
    }

    function renderEncFields() {
      var bitrateEl = $('enc-bitrate');
      var bitrateVal = $('enc-bitrate-val');
      if (bitrateEl && bitrateEl.value !== String(encState.targetBitrate)) bitrateEl.value = encState.targetBitrate;
      if (bitrateVal) bitrateVal.textContent = encState.targetBitrate + ' kbps';

      // [audio-buffer-per-mode] Sync the Target Buffer Latency slider from
      // encState.bufferMs. Without this, loadEncoderSettings() populated
      // encState.bufferMs but the slider stayed on its previous value, so
      // the user could not see the buffer the Save button would persist.
      var bufEl  = $('buffer-latency');
      var bufVal = $('buffer-latency-val');
      if (bufEl  && document.activeElement !== bufEl) bufEl.value = encState.bufferMs;
      if (bufVal) bufVal.textContent = encState.bufferMs + ' ms';

      // [opus-sample-rate] Sync the select + the readonly Transport display.
      var srSel = $('enc-sampleRate-in');
      if (srSel && document.activeElement !== srSel) {
        var srStr = String(encState.sampleRate);
        if (srSel.value !== srStr) srSel.value = srStr;
      }
      var srReadonly = $('enc-opus-rate');
      if (srReadonly) {
        srReadonly.textContent = (encState.sampleRate).toLocaleString() + ' Hz';
      }

      for (var field in encState.fields) {
        if (!encState.fields.hasOwnProperty(field)) continue;
        var st = encState.fields[field];
        var valEl = $('enc-' + field + '-val');
        var inEl = $('enc-' + field + '-in');
        var lockEl = $('enc-' + field + '-lock');
        var fieldEl = document.querySelector('.enc-field[data-field="' + field + '"]');
        if (valEl) valEl.textContent = formatEncValue(field, st.value);
        if (inEl) {
          if (inEl.tagName === 'SELECT') inEl.value = (field === 'vbr' || field === 'vbrConstraint') ? (st.value ? '1' : '0') : String(st.value);
          else inEl.value = String(st.value);
        }
        if (lockEl) lockEl.classList.toggle('locked', st.locked);
        if (fieldEl) { fieldEl.classList.toggle('manual', st.locked); fieldEl.classList.toggle('auto', !st.locked); }
      }
    }

    function refreshEncSuggestions() {
      var sugg = computeSuggestions(encState.targetBitrate);
      for (var field in encState.fields) {
        if (!encState.fields.hasOwnProperty(field)) continue;
        if (!encState.fields[field].locked) encState.fields[field].value = sugg[field];
      }
      renderEncFields();
    }

    function toggleEncLock(field) {
      if (!encState.fields[field]) return;
      encState.fields[field].locked = !encState.fields[field].locked;
      if (!encState.fields[field].locked) {
        var sugg = computeSuggestions(encState.targetBitrate);
        encState.fields[field].value = sugg[field];
      }
      renderEncFields();
    }

    function loadEncoderSettings() {
      fetch('/api/audio/encoding', { cache: 'no-store' })
        .then(function(r){ return r.ok ? r.json() : null; })
        .then(function(j){ encApplyFromResponse(j); })
        .catch(function(){ renderEncFields(); });
    }

    // [per-mode-encoder-settings]
    // Every slider/select change posts to /api/audio/encoding WITHOUT
    // save:true, so the live encoder tracks the UI immediately but NVS is
    // not touched. Save is the only path that writes to NVS (under the
    // current playback mode). Revert reloads the current mode's preset
    // from NVS. Load Defaults overwrites the current mode's in-RAM preset
    // with factory values — persist it by clicking Save afterwards.
    var encSendTimer = null;
    function encBuildPayload() {
      return {
        bitrate:       encState.targetBitrate * 1000,
        bufferMs:      encState.bufferMs,
        complexity:    encState.fields.complexity.value,
        vbr:           encState.fields.vbr.value,
        vbrConstraint: encState.fields.vbrConstraint.value,
        signal:        encState.fields.signal.value,
        application:   encState.fields.application.value,
        sampleRate:    encState.sampleRate
      };
    }
    function encApplyFromResponse(j) {
      if (!j || !j.ok) return;
      // [pending-mode-save-guard]
      // If the user just switched modes and the server hasn't caught up
      // yet, j.mode is the OLD mode. Do NOT write it back — that's what
      // reverted the dropdown mid-switch. Clear the lock only when the
      // server actually echoes the mode we asked for.
      if (j.mode) {
        var mSel = $('mode-select');
        if (pendingModeSave) {
          if (j.mode === pendingModeSave) {
            pendingModeSave = null;
            if (pendingModeSaveTimer) { clearTimeout(pendingModeSaveTimer); pendingModeSaveTimer = null; }
            if (mSel && mSel.value !== j.mode) mSel.value = j.mode;
          }
          // else: server still stale — leave the user's selection alone.
        } else if (mSel && mSel.value !== j.mode) {
          mSel.value = j.mode;
        }
      }
      if (typeof j.bitrate === 'number') encState.targetBitrate = Math.round(j.bitrate / 1000);
      if (typeof j.bufferMs === 'number') encState.bufferMs = j.bufferMs;   // [audio-buffer-per-mode]
      if (typeof j.complexity === 'number') encState.fields.complexity.value = j.complexity;
      if (typeof j.vbr === 'boolean') encState.fields.vbr.value = j.vbr;
      if (typeof j.vbrConstraint === 'boolean') encState.fields.vbrConstraint.value = j.vbrConstraint;
      if (typeof j.signal === 'string') encState.fields.signal.value = j.signal;
      if (typeof j.application === 'string') encState.fields.application.value = j.application;
      // [opus-sample-rate] Prefer the server's value; fall back to 48 kHz
      // if missing (older server build).
      if (typeof j.sampleRate === 'number') encState.sampleRate = j.sampleRate;
      for (var f in encState.fields) if (encState.fields.hasOwnProperty(f)) encState.fields[f].locked = true;
      renderEncFields();
    }
    function encSendRealtime() {
      clearTimeout(encSendTimer);
      encSendTimer = setTimeout(async function(){
        try {
          var r = await fetch('/api/audio/encoding', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(encBuildPayload())
          });
          if (r.status === 404) { toast('Backend /api/audio/encoding not yet implemented', 'err'); return; }
          if (!r.ok) throw new Error('HTTP ' + r.status);
        } catch(e) { /* slider drag: don't spam toasts */ }
      }, 250);
    }
    async function encSave() {
      try {
        var r = await fetch('/api/audio/encoding', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(Object.assign({}, encBuildPayload(), { save: true }))
        });
        if (!r.ok) throw new Error('HTTP ' + r.status);
        var j = await r.json();
        if (j && j.ok) toast('Encoder saved for mode: ' + (j.mode || '?'), 'ok');
      } catch(e) { toast('Save failed: ' + e.message, 'err'); }
    }
    async function encRevert() {
      try {
        var r = await fetch('/api/audio/encoding/revert', { method: 'POST' });
        if (!r.ok) throw new Error('HTTP ' + r.status);
        await loadEncoderSettings();
        toast('Reverted to saved preset', 'ok');
      } catch(e) { toast('Revert failed: ' + e.message, 'err'); }
    }
    async function encDefaults() {
      try {
        var r = await fetch('/api/audio/encoding/defaults', { method: 'POST' });
        if (!r.ok) throw new Error('HTTP ' + r.status);
        await loadEncoderSettings();
        toast('Factory defaults loaded (Save to persist)', 'ok');
      } catch(e) { toast('Load Defaults failed: ' + e.message, 'err'); }
    }

    (function(){
      var bitrateEl = $('enc-bitrate');
      if (bitrateEl) bitrateEl.addEventListener('input', function(){
        encState.targetBitrate = parseInt(bitrateEl.value, 10) || 96;
        renderEncFields();
        encSendRealtime();
      });
      for (var field in encState.fields) {
        if (!encState.fields.hasOwnProperty(field)) continue;
        (function(f){
          var lockEl = $('enc-' + f + '-lock');
          if (lockEl) lockEl.addEventListener('click', function(){ toggleEncLock(f); encSendRealtime(); });
          var inEl = $('enc-' + f + '-in');
          if (inEl) inEl.addEventListener('change', function(){
            var v = inEl.value;
            if (f === 'vbr' || f === 'vbrConstraint') v = (v === '1');
            else if (f === 'complexity') v = parseInt(v, 10) || 8;
            encState.fields[f].value = v;
            encState.fields[f].locked = true;
            renderEncFields();
            encSendRealtime();
          });
        })(field);
      }
      // [opus-sample-rate] Bind the sample rate dropdown. Live preview:
      // encoder is re-created on the next audio frame; Save persists it
      // into the current mode's preset.
      var srSel = $('enc-sampleRate-in');
      if (srSel) srSel.addEventListener('change', function(){
        var v = parseInt(srSel.value, 10);
        if (v !== 48000 && v !== 24000) return;
        encState.sampleRate = v;
        renderEncFields();
        encSendRealtime();
      });
      var saveBtn = $('enc-save');     if (saveBtn) saveBtn.addEventListener('click', encSave);
      var revBtn  = $('enc-revert');   if (revBtn)  revBtn.addEventListener('click', encRevert);
      var defBtn  = $('enc-defaults'); if (defBtn)  defBtn.addEventListener('click', encDefaults);
      renderEncFields();
    })();

    // ====================================================================
    //  Diagnostics Recording (10 min, manual Finish Now supported)
    // ====================================================================
    var recording = false;

    function startRecording() {
      if (recording) return;
      recording = true;
      var overlay   = $('recordProgressOverlay');
      var fill      = $('recordProgressFill');
      var text      = $('recordProgressText');
      var analyze   = $('analyzeBtn');
      var finishBtn = $('recordFinishNowBtn');
      if (analyze)   analyze.disabled = true;
      if (overlay)   overlay.classList.add('open');
      if (fill)      fill.style.width = '0%';

      var REC_DURATION_MS = 600000; // 10 minutes
      var DURATION_MS     = REC_DURATION_MS;
      var DURATION_S      = DURATION_MS / 1000;
      var TICK_MS         = 1000;
      var FETCH_TIMEOUT_MS = 5000;
      if (text) text.textContent = '0.0s / ' + DURATION_S + 's';

      var startTime     = Date.now();
      var samples       = [];
      var allLogs       = [];
      var currentLogSeq = 0;
      var finished      = false;
      var inFlight      = false;   // one fetch set at a time — see tick()

      function fetchJson(url) {
        var ctrl = null;
        try { ctrl = new AbortController(); } catch (_) {}
        var to = ctrl ? setTimeout(function(){ try { ctrl.abort(); } catch(_){} }, FETCH_TIMEOUT_MS) : null;
        var opts = { cache: 'no-store' };
        if (ctrl) opts.signal = ctrl.signal;
        return fetch(url, opts)
          .then(function(r){ if (to) clearTimeout(to); return r.json(); })
          .catch(function(){ if (to) clearTimeout(to); return null; });
      }

      function finish() {
        if (finished) return;
        finished  = true;
        recording = false;
        if (finishBtn) finishBtn.disabled = true;
        if (fill)      fill.style.width = '100%';
        var elapsedS = (Date.now() - startTime) / 1000;
        if (text) text.textContent = elapsedS.toFixed(1) + 's — generating report…';
        setTimeout(function(){
          if (overlay) overlay.classList.remove('open');
          if (analyze) analyze.disabled = false;
          showRecordResult(samples, allLogs);
          refreshStatus();
          if (finishBtn && finishBtn.onclick === boundFinish) finishBtn.onclick = null;
        }, 400);
      }

      var boundFinish = function(){ finish(); };
      if (finishBtn) {
        finishBtn.disabled = false;
        finishBtn.onclick  = boundFinish;
      }

      // Seed log cursor past the ring so we only capture new lines.
      fetchJson('/api/log/tail?since=99999999')
        .then(function(j){ if (j && typeof j.nextSeq === 'number') currentLogSeq = j.nextSeq; });

      function tick() {
        if (finished) return;
        var elapsed = Date.now() - startTime;
        var t = elapsed / 1000;
        if (elapsed >= DURATION_MS) { finish(); return; }
        if (fill) fill.style.width = (elapsed / DURATION_MS * 100) + '%';
        if (text) text.textContent = t.toFixed(1) + 's / ' + DURATION_S + 's — ' + samples.length + ' samples';

        // UI stays alive at 1 Hz regardless of fetch completion.
        if (!finished) setTimeout(tick, TICK_MS);

        // [recording-seq] Bail out if a prior fetch set is still running.
        // Previously Promise.all() launched four concurrent requests per
        // tick; with ~3 KB String per response (×2 for beginResponse's
        // copy) that is ~24 KB peak churn against a heap that then cannot
        // allocate a 1500-byte pbuf for the audio UDP path. Result:
        // "[E][NetworkUdp.cpp:255] endPacket(): could not send data: 12"
        // (ENOMEM). Serial awaits cap the server-side peak to one response
        // String at a time; the inFlight guard prevents the next tick
        // from stacking on top if a fetch is slow.
        if (inFlight) return;
        inFlight = true;

        (async function () {
          var st      = await fetchJson('/api/state');
          var stats   = await fetchJson('/api/stats');
          var logData = await fetchJson('/api/log/tail?since=' + currentLogSeq);
          var srcData = await fetchJson('/api/source/info');
          if (finished) return;

          var wall = new Date().toISOString().substring(11, 23);
          var newLogs = [];
          if (logData && Array.isArray(logData.lines)) {
            for (var i = 0; i < logData.lines.length; i++) {
              newLogs.push(logData.lines[i]);
              allLogs.push({ t: t, wall: wall, line: logData.lines[i] });
            }
            if (typeof logData.nextSeq === 'number') currentLogSeq = logData.nextSeq;
          }
          samples.push({ t: t, wall: wall, state: st, stats: stats,
                         source: srcData, newLogs: newLogs });
        })().catch(function(){
          // Never let a fetch failure kill the recorder.
        }).finally(function(){
          inFlight = false;
        });
      }

      tick();
    }

    // [record-report-fmtNum] Right-aligns a value into a fixed column width.
    // Was called ~20x inside generateRecordReport() but never defined, which
    // threw ReferenceError on the first timeline row and prevented the
    // result overlay from ever opening (progress overlay had already been
    // closed by finish() -> user saw an empty page). Keep this above
    // generateRecordReport().
    function fmtNum(v, w) {
      if (v === undefined || v === null || v === '') return '-'.padStart(w || 1);
      return String(v).padStart(w || 1);
    }

    function generateRecordReport(samples, allLogs) {
      var lines = [];
      var first = samples[0] || {};
      var nodeName = ($('server-title-text') && $('server-title-text').textContent) || 'Enkelvoud';
      var fwVer = ($('server-version') && $('server-version').textContent) || '?';
      var lastState = samples.length ? samples[samples.length - 1].state : null;
      var lastStats = samples.length ? samples[samples.length - 1].stats : null;

      lines.push('=== Enkelvoud Diagnostic Recording ===');
      // Report actual measured duration and interval rather than the
      // nominal 60.0 s / 1.0 s. Each tick awaits Promise.all of four
      // fetches before scheduling the next setTimeout(1000); the fetch
      // latency adds up, so a 60 s wall-clock window catches ~57 samples
      // and the interval is ~1.05 s. Printing the nominal values made
      // the report internally inconsistent ("Duration 60.0s" next to
      // "Samples: 57"). Derive both from the recorded timestamps so the
      // header is honest regardless of how many ticks the network
      // managed to deliver.
      var recStart = samples.length ? samples[0].t : 0;
      var recEnd   = samples.length ? samples[samples.length - 1].t : 0;
      var recDur   = (recEnd - recStart);
      var recInterval = samples.length > 1
                        ? (recDur / (samples.length - 1))
                        : 0;
      var recStartWall = samples.length ? samples[0].wall : '';
      lines.push('Node:       ' + nodeName);
      lines.push('Version:    ' + fwVer);
      if (lastState && lastState.bootCount) lines.push('Boot count: ' + lastState.bootCount);
      if (lastState && lastState.bootReason) lines.push('Boot reason:' + lastState.bootReason);
      lines.push('Started:    ' + (recStartWall || new Date(Date.now() - 60000).toISOString()));
      // [report-nominal-duration-from-constant]
      // Hardcoding "nominal 60.0s" here means a 10-minute recording opens
      // with a Duration line that claims 60 s was expected. Read the
      // nominal from the same constant startRecording() uses so the two
      // cannot drift apart on the next duration change.
      var _nominalS = (typeof REC_DURATION_MS === 'number' ? REC_DURATION_MS : 600000) / 1000;
      lines.push('Duration:   ' + recDur.toFixed(1) + 's (nominal ' + _nominalS.toFixed(1) + 's)');
      lines.push('Interval:   ' + recInterval.toFixed(3) + 's (nominal 1.000s)');
      lines.push('Samples:    ' + samples.length);
      lines.push('');

      // ---- Source board (from the last /api/source/info poll) ----------
      var lastSrcInfo = samples.length ? samples[samples.length - 1].source : null;
      var lastSrcBoard = (lastStats && lastStats.sourceBoard) || {};
      lines.push('=== Source Board ===');
      if (lastSrcInfo && lastSrcInfo.ok) {
        lines.push('Firmware:    v' + (lastSrcBoard.fw || '?'));
        lines.push('Link:        ' + (lastSrcInfo.link ? 'UP' : 'DOWN') +
                   ' (last heartbeat ' + ((lastSrcInfo.heartbeatAgeMs / 1000).toFixed(1)) + ' s ago)');
        lines.push('BT state:    ' + (lastSrcInfo.btConnected ? 'connected' : 'idle') +
                   '  mac=' + (lastSrcInfo.connectedMac || '(none)') +
                   '  vol=' + (lastSrcInfo.volume | 0) + '/127');
        lines.push('AVRCP:       ' + (lastSrcInfo.avrcConnected ? 'connected' : 'idle'));
        lines.push('Active src:  ' + (lastSrcInfo.activeSource || '?'));
        lines.push('Loop ticks:  ' + (lastSrcBoard.loopTicks | 0) +
                   ' (delta ' + ((lastSrcBoard.loopTicks | 0) - ((samples[0] && samples[0].stats && samples[0].stats.sourceBoard && samples[0].stats.sourceBoard.loopTicks) | 0)) + ' over ' + samples.length + ' s)');
        lines.push('Free heap:   ' + (((lastSrcBoard.heapFree | 0) / 1024).toFixed(0)) + ' KiB');
        lines.push('Uptime:      ' + (lastSrcBoard.uptimeSec | 0) + ' s');
        lines.push('A2DP events: a2dpCg=' + (lastSrcBoard.a2dpConnecting | 0) +
                   '  a2dpC=' + (lastSrcBoard.a2dpConnected | 0) +
                   '  a2dpDg=' + (lastSrcBoard.a2dpDisconnecting | 0) +
                   '  a2dpD=' + (lastSrcBoard.a2dpDisconnected | 0));
        lines.push('Volume:      volEv=' + (lastSrcBoard.volumeEvents | 0) +
                   '  volAp=' + (lastSrcBoard.volumeApplied | 0));
        lines.push('UART:        bytes=' + (lastSrcBoard.uartBytes | 0) +
                   '  lines=' + (lastSrcBoard.uartLines | 0) +
                   '  overflow=' + (lastSrcBoard.uartOverflow | 0) +
                   '  txDropped=' + (lastSrcBoard.uartTxDropped | 0));
        lines.push('Source changes: ' + (lastSrcBoard.sourceChanges | 0));
      } else {
        lines.push('(no /api/source/info response — source board unreachable)');
      }
      lines.push('');

      lines.push('=== Column Legend ===');
      lines.push('t      = seconds from start');
      lines.push('c0/c1  = CPU core 0/1 %');
      lines.push('ram/ps = heap/PSRAM used %');
      lines.push('qd/qq  = rawQueue / opusQueue depth');
      lines.push('lat    = avg latency ms');
      lines.push('l5/20/100 = late frame counts');
      lines.push('disc   = frames discarded');
      lines.push('conc   = frames concealed (PLC)');
      lines.push('rebuf  = cumulative rebuffers');
      lines.push('corr   = correction samples');
      lines.push('plat   = playout delay current/target (ms)');
      lines.push('bVol   = source-board BT volume (0-127)');
      lines.push('bSt    = source BT state (C=connected / D=idle)');
      lines.push('src    = source active input (BT/AUX/USB)');
      lines.push('');

      lines.push('=== Timeline ===');
      lines.push('t     | wall         | c0  | c1  | ram | ps  | qd/qq  | lat   | l5  | l20 | l100 | disc | conc | rebuf | corr | plat  | bVol | bSt | src');
      lines.push('-'.repeat(150));

      var prevRebuf = -1;
      var rebufEvents = [];
      // [rebuffer-monotonic-max-by-ip]
      // `ss.rebuffers` is the SUM over currently-active nodePerf[] entries.
      // A node whose 20 s perf TTL expires drops out of the sum, so the
      // "cumulative" column decreases (293 -> 98 -> 102 in the 2026-11-13
      // recording). Track per-node maximum-ever instead: a node's own
      // rebuffer counter is monotonic, so max() across all samples is
      // genuinely cumulative, and a TTL expiry no longer erases history.
      var rebufMaxByIp = {};

      for (var i = 0; i < samples.length; i++) {
        var s = samples[i];
        var st = s.state || {};
        var ss = s.stats || {};
        var c0 = st.cpuCore0Pct, c1 = st.cpuCore1Pct;
        var ram = st.heapUsedPct, ps = st.psramUsedPct;
        var qd = ss.rawQueueDepth, qq = ss.opusQueueDepth;
        var lat = ss.avgLatencyMs;
        var l5 = ss.framesLate5to20;
        var l20 = ss.framesLate20to100;
        var l100 = ss.framesLate100to200;
        var disc = ss.framesDiscarded;
        var conc = ss.framesConcealed;
        // [rebuffer-monotonic-max-by-ip] See Anchor A.
        (ss.nodes || []).forEach(function(n){
          var prev = rebufMaxByIp[n.ip] || 0;
          if ((n.rebuffers || 0) > prev) rebufMaxByIp[n.ip] = n.rebuffers;
        });
        var rebuf = 0;
        for (var _ip in rebufMaxByIp) rebuf += rebufMaxByIp[_ip];
        var corr = ss.correctionSamples;
        var plat = (ss.playoutDelayMs !== undefined && ss.playoutDelayTargetMs !== undefined)
                   ? ss.playoutDelayMs + '/' + ss.playoutDelayTargetMs : '-';

        var srcBoard = (ss && ss.sourceBoard) || {};
        var srcBVol = (srcBoard.volume !== undefined) ? srcBoard.volume : '-';
        var srcBtSt = srcBoard.btConnected ? 'C' : 'D';
        var srcActive = srcBoard.activeSource || '-';

        lines.push(
          fmtNum(s.t.toFixed(1), 5) + ' | ' +
          (s.wall || '').padEnd(12) + ' | ' +
          fmtNum(c0, 3) + ' | ' +
          fmtNum(c1, 3) + ' | ' +
          fmtNum(ram, 3) + ' | ' +
          fmtNum(ps, 3) + ' | ' +
          fmtNum(qd, 2) + '/' + fmtNum(qq, 2) + ' | ' +
          String(lat !== undefined ? parseFloat(lat).toFixed(1) : '-').padStart(5) + ' | ' +
          fmtNum(l5, 3) + ' | ' +
          fmtNum(l20, 3) + ' | ' +
          fmtNum(l100, 4) + ' | ' +
          fmtNum(disc, 4) + ' | ' +
          fmtNum(conc, 4) + ' | ' +
          fmtNum(rebuf, 5) + ' | ' +
          fmtNum(corr, 4) + ' | ' +
          plat.padEnd(5) + ' | ' +
          fmtNum(srcBVol, 4) + ' | ' +
          srcBtSt.padEnd(3) + ' | ' +
          srcActive
        );

        if (prevRebuf >= 0 && rebuf > prevRebuf) {
          rebufEvents.push({ t: s.t, wall: s.wall, count: rebuf });
        }
        prevRebuf = rebuf;
      }

      lines.push('');

      if (rebufEvents.length) {
        lines.push('=== Rebuffer Events ===');
        for (var r = 0; r < rebufEvents.length; r++) {
          lines.push('[' + rebufEvents[r].t.toFixed(1) + 's ' + rebufEvents[r].wall + '] rebuffers = ' + rebufEvents[r].count);
        }
        lines.push('');
      }

      // ---- Source-board log ring ---------------------------------------
      lines.push('=== Source Log Ring (newest 50) ===');
      if (lastSrcInfo && lastSrcInfo.log && lastSrcInfo.log.length) {
        for (var sli = 0; sli < lastSrcInfo.log.length; sli++) {
          lines.push(lastSrcInfo.log[sli]);
        }
      } else {
        lines.push('(no source log entries — either fresh boot or UART link quiet)');
      }
      lines.push('');


      lines.push('=== Log Lines ===');
      if (!allLogs.length) {
        lines.push('(no log lines captured — enable Advanced Logging before recording)');
      } else {
        for (var lg = 0; lg < allLogs.length; lg++) {
          var ln = allLogs[lg].line;
          var lvl = ln.level === 'E' ? '[ERROR]' : ln.level === 'W' ? '[WARN ]' : '[INFO ]';
          lines.push('[' + allLogs[lg].t.toFixed(1) + 's ' + allLogs[lg].wall + '] ' + lvl + ' ' + ln.text);
        }
      }
      lines.push('');

      lines.push('=== Last /api/stats Snapshot ===');
      if (lastStats) lines.push(JSON.stringify(lastStats, null, 2));

      return lines.join('\n');
    }

    // [record-report-guard] Open the overlay BEFORE generating the report,
    // and catch any failure so a broken generator shows an error body
    // instead of silently closing the progress panel and leaving the user
    // with nothing (the previous "Finish Now closes the window" symptom).
    function showRecordResult(samples, allLogs) {
      var overlay = $('recordResultOverlay');
      var ta = $('recordResultText');
      var report;
      try {
        report = generateRecordReport(samples, allLogs);
      } catch (e) {
        report = 'Report generation failed: ' +
                 (e && e.message ? e.message : String(e)) +
                 '\n\n' + (e && e.stack ? e.stack : '') +
                 '\n\nRaw sample count: ' + (samples ? samples.length : 0);
        console.error('generateRecordReport failed', e);
      }
      if (ta) ta.value = report;
      if (overlay) overlay.classList.add('open');
    }

    window.closeRecordResult = function() {
      var o = $('recordResultOverlay');
      if (o) o.classList.remove('open');
    };
    window.copyRecordResult = function() {
      var ta = $('recordResultText');
      if (!ta) return;
      ta.select();
      try { document.execCommand('copy'); toast('Copied to clipboard', 'ok'); }
      catch(e) { toast('Copy failed — select and copy manually', 'err'); }
    };
    window.downloadRecordResult = function() {
      var ta = $('recordResultText');
      if (!ta) return;
      var blob = new Blob([ta.value], { type: 'text/plain' });
      var url = URL.createObjectURL(blob);
      var a = document.createElement('a');
      a.href = url;
      a.download = 'enkelvoud-diag-' + new Date().toISOString().replace(/[:.]/g, '-') + '.txt';
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      setTimeout(function(){ URL.revokeObjectURL(url); }, 1000);
    };

    // ====================================================================
    //  OTA wizard (unchanged)
    // ====================================================================
    (function () {
      var modal = $('otaModal');
      if (!modal) return;
      var steps = modal.querySelectorAll('.ota-step');
      var dots = modal.querySelectorAll('.ota-step-dot');
      var backBtn = $('otaBackBtn'), nextBtn = $('otaNextBtn');
      var filesInput = $('otaFiles'), msgEl = $('otaUploadMsg');
      var uploadBar = $('otaUploadBar'), uploadFill = $('otaUploadBarFill');
      var nodeListEl = $('otaNodeList');
      var serverRow = $('otaServerRow'), serverFileEl = $('otaServerFile');
      var selectAllCb = $('otaSelectAll');
      var TOTAL_STEPS = 4;
      var verifyStartTime = 0;
      window.__otaTargets = null;
      var currentStep = 1, plan = null, pollTimer = null;
      var prevMuted = false, muteApplied = false, restoreMute = true;
      var step4Done = false, selectedKind = null, selectedSpoolFile = null;
      // [ota-log-pane] Live log state. otaLogSeq is the /api/log/tail cursor;
      // otaLogTimer is the 1 s poll interval; otaLogLines caps the pane at
      // OTA_LOG_MAX_LINES to keep the pre from growing unbounded across a
      // long multi-node job. prevLogLevel/logLevelSaved allow us to bump the
      // serial log level to 3 for the wizard session and restore it exactly
      // on close, so the OTA pane gets INFO lines without leaving the whole
      // system verbose forever.
      var otaLogSeq = 0, otaLogTimer = null, otaLogLines = 0;
      var pollOtaLogBusy = false;
      var prevLogLevel = 1, logLevelSaved = false;
      var OTA_LOG_MAX_LINES = 200, OTA_LOG_TICK_MS = 1000;

      function setMsg(t) { if (msgEl) msgEl.textContent = t; }
      function applyMuteUI(m) { isMuted = m; paintMuteAllButton(); }
      async function restoreMuteIfNeeded() {
        if (!muteApplied || !restoreMute) return;
        await api('/api/config', 'POST', { masterMuted: prevMuted });
        applyMuteUI(prevMuted);
        muteApplied = false;
      }
      function pickFileByKind(kind) {
        if (!plan || !plan.files) return null;
        var best = null;
        for (var i = 0; i < plan.files.length; i++) {
          var f = plan.files[i];
          if (f.kind !== kind) continue;
          if (!best || f.version.localeCompare(best.version, undefined, {numeric:true}) > 0) best = f;
        }
        return best;
      }
      function updateStep2() {
        // [ota-spool-selection] Enable Next if a valid spool file is selected.
        var hasSelection = false;
        if (selectedSpoolFile && plan && plan.files) {
          hasSelection = plan.files.some(function(f){ return f.name === selectedSpoolFile; });
        }
        if (currentStep === 2) { 
          nextBtn.disabled = !hasSelection; 
        }
      }
      function setStep(n) {
        currentStep = n;
        steps.forEach(function(s){ s.classList.toggle('active', +s.dataset.step === n); });
        dots.forEach(function(d){ var ds = +d.dataset.step; d.classList.toggle('active', ds === n); d.classList.toggle('done', ds < n); });
        backBtn.style.display = (n > 1 && n < TOTAL_STEPS) ? '' : 'none';
        if (n < TOTAL_STEPS) {
          nextBtn.style.display = ''; nextBtn.disabled = false;
          nextBtn.textContent = n === 1 ? 'OK' : n === 2 ? 'Next' : 'Start update';
        } else {
          nextBtn.style.display = step4Done ? '' : 'none';
          nextBtn.textContent = 'Close';
          nextBtn.disabled = false;
        }
        // [ota-log-pane] Show the log from step 2 (file/upload) onward.
        // Step 1 is just a mute confirmation, so the pane would be noise.
        var logWrap = $('otaLogWrap');
        if (logWrap) logWrap.classList.toggle('visible', n >= 2);
        if (n >= 2 && !otaLogTimer) startOtaLog();
        if (n === 2) { refreshPlan(); updateStep2(); }
        if (n === 3) refreshPlan();
      }

      function otaLogAppend(line) {
        var pre = $('otaLogPre');
        if (!pre) return;
        var cls = line.level === 'E' ? 'lg-e' : line.level === 'W' ? 'lg-w' : 'lg-i';
        var tag = line.level === 'E' ? '[ERR]' : line.level === 'W' ? '[WRN]' : '[INF]';
        var span = document.createElement('span');
        span.className = cls;
        span.textContent = tag + ' ' + String(line.ms).padStart(8, ' ') + ' | ' +
                           String(line.text) + '\n';
        pre.appendChild(span);
        otaLogLines++;
        // Cap the pane so a multi-minute run does not grow the DOM unbounded.
        while (otaLogLines > OTA_LOG_MAX_LINES && pre.firstChild) {
          pre.removeChild(pre.firstChild);
          otaLogLines--;
        }
        pre.scrollTop = pre.scrollHeight;
      }
      function otaLogClear() {
        var pre = $('otaLogPre');
        if (pre) pre.textContent = '(waiting for log lines…)';
        otaLogLines = 0;
        otaLogSeq   = 0;
      }
      async function otaSetLogLevel(level) {
        try {
          await fetch('/api/log/level', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ level: level })
          });
        } catch (e) { /* best-effort; retries not needed */ }
      }
      async function pollOtaLog() {
        if (!otaLogTimer) return;
        if (recording) return;          // do not race the diagnostic recorder's tail poll
        if (pollOtaLogBusy) return;
        pollOtaLogBusy = true;
        try {
          var r = await fetch('/api/log/tail?since=' + otaLogSeq, { cache: 'no-store' });
          var j = await r.json();
          if (j && j.ok && Array.isArray(j.lines)) {
            for (var i = 0; i < j.lines.length; i++) otaLogAppend(j.lines[i]);
            if (typeof j.nextSeq === 'number') otaLogSeq = j.nextSeq;
          }
        } catch (e) { /* 503 / network: retry next tick */ }
        finally { pollOtaLogBusy = false; }
      }
      async function startOtaLog() {
        if (otaLogTimer) return;
        if (!logLevelSaved) {
          try {
            var lr = await fetch('/api/log/level');
            var lj = await lr.json();
            prevLogLevel = (lj && typeof lj.level === 'number') ? lj.level : 1;
          } catch (e) { prevLogLevel = 1; }
          logLevelSaved = true;
        }
        await otaSetLogLevel(3);
        otaLogTimer = setInterval(pollOtaLog, OTA_LOG_TICK_MS);
        await pollOtaLog();            // drain the ring immediately
      }
      async function stopOtaLog() {
        if (otaLogTimer) { clearInterval(otaLogTimer); otaLogTimer = null; }
        if (logLevelSaved) {
          await otaSetLogLevel(prevLogLevel);
          logLevelSaved = false;
        }
      }
      (function bindOtaLogToggle() {
        var t = $('otaLogToggle');
        if (!t) return;
        t.addEventListener('click', function () {
          var pre = $('otaLogPre');
          if (!pre) return;
          var hidden = (pre.style.display === 'none');
          pre.style.display = hidden ? '' : 'none';
          t.textContent = hidden ? 'Hide' : 'Show';
        });
      })();

      window.openUpdateWizard = async function () {
        modal.classList.add('open'); modal.setAttribute('aria-hidden', 'false');
        step4Done = false; muteApplied = false; plan = null;
        selectedKind = null; selectedSpoolFile = null;
        // ...
        otaLogClear();
        if (uploadBar) { uploadBar.style.display = 'none'; uploadFill.style.width = '0%'; }
        window.__otaTargets = null; verifyStartTime = 0; setMsg('No file uploaded yet.');
        try {
          var st = await (await fetch('/api/ota/status')).json();
          if (st && st.inProgress) {
            window.__otaTargets = { server: false, nodes: [], newVersion: '', kind: 'node', resumed: true };
            document.getElementById('otaNodeStatusList').innerHTML = '<div class="ota-node-item"><div class="ota-node-body"><div class="ota-node-name">Update in progress…</div></div></div>';
            setStep(4);
            if (!pollTimer) pollTimer = setInterval(pollStatus, 1000);
            return;
          }
        } catch(e){}
        try { var s = await api('/api/state'); prevMuted = !!(s && s.masterMuted); } catch(e){ prevMuted = isMuted; }
        var cb = $('otaRestoreMute'); if (cb) cb.checked = !prevMuted;
        setStep(1);
      };
      window.otaCancel = async function () {
        modal.classList.remove('open'); modal.setAttribute('aria-hidden', 'true');
        if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
        await stopOtaLog();
        await restoreMuteIfNeeded(); refreshStatus();
      };
      window.otaBack = function () { if (currentStep > 1 && currentStep < TOTAL_STEPS) setStep(currentStep - 1); };
      window.otaNext = async function () {
        if (currentStep === 1) {
          restoreMute = $('otaRestoreMute').checked;
          try { await api('/api/config', 'POST', { masterMuted: true }); applyMuteUI(true); muteApplied = true; } catch(e){}
          setStep(2);
        } else if (currentStep === 2) {
          if (!selectedSpoolFile) return;
          var f = (plan && plan.files) ? plan.files.find(function(x){ return x.name === selectedSpoolFile; }) : null;
          if (!f) return;
          selectedKind = f.kind;
          setStep(3);
        } else if (currentStep === 3) {
          var nodes = [];
          modal.querySelectorAll('.otaNodeChk:checked').forEach(function(c){ nodes.push(c.value); });
          var doServer = $('otaDoServer').checked;
          var doSource = $('otaDoSource').checked;                           // NEW
          if (!doServer && !doSource && !nodes.length) {                     // CHANGED
            alert('Nothing selected.'); return;
          }

          var pickedNodeFile   = (selectedKind === 'node')   ? selectedSpoolFile : '';
          var pickedServerFile = (selectedKind === 'server') ? selectedSpoolFile : '';
          var pickedSourceFile = (selectedKind === 'source') ? selectedSpoolFile : '';   // NEW

          function verOf(fname, kind) {
            if (!fname || !plan || !plan.files) return '';
            var f = plan.files.find(function(x){ return x.kind === kind && x.name === fname; });
            return f ? f.version : '';
          }
          var newVer = selectedKind === 'node'   ? verOf(pickedNodeFile,   'node')
                     : selectedKind === 'server' ? verOf(pickedServerFile, 'server')
                     : selectedKind === 'source' ? verOf(pickedSourceFile, 'source')  // NEW
                     : '';
          window.__otaTargets = {                                              // CHANGED
            nodes: nodes, server: doServer, source: doSource,
            newVersion: newVer, kind: selectedKind
          };
          verifyStartTime = 0;
          renderStep4List();
          step4Done = false; setStep(4);
          try {
            var ac = new AbortController();
            var to = setTimeout(function(){ ac.abort(); }, 30000);
            var r = await fetch('/api/ota/apply', {
              method: 'POST',
              headers: {'Content-Type':'application/json'},
              body: JSON.stringify({
                server:     doServer,
                source:     doSource,                                          // NEW
                nodes:      nodes,
                nodeFile:   pickedNodeFile,
                serverFile: pickedServerFile,
                sourceFile: pickedSourceFile                                   // NEW
              }),
              signal: ac.signal
            });
            clearTimeout(to);
            if (!r.ok) { alert('Apply failed: HTTP ' + r.status); step4Done = true; nextBtn.style.display = ''; return; }
          } catch(e) {
            alert('Apply failed: ' + (e && e.message || 'unknown'));
            step4Done = true; nextBtn.style.display = '';
            return;
          }
          // [ota-apply-first-poll] Poll once immediately after apply returns
          // so the first target's spinner appears the moment the POST 202
          // lands, instead of waiting a full 1 s interval — which was the
          // visible "1.5 s dead air" between clicking Start update and the
          // first spinner appearing. Falls back to the interval afterwards.
          await pollStatus();
          if (!pollTimer) pollTimer = setInterval(pollStatus, 1000);
        } else if (currentStep === 4) {
          await otaCancel();
        }
      };
      async function uploadFile(file) {
        var m = (file.name || '').match(/^(server|node|source)_[^/]+\.bin$/i);
        if (!m) { setMsg('Filename must be server_<ver>.bin, node_<ver>.bin or source_<ver>.bin.'); return; }
        var kind = m[1].toLowerCase();
        var pickBtn = $('otaPickFiles'); pickBtn.disabled = true;
        uploadBar.style.display = ''; uploadFill.style.width = '0%'; uploadFill.style.background = '';
        setMsg('Uploading ' + file.name + ' …');
        try {
          await new Promise(function(resolve, reject){
            var xhr = new XMLHttpRequest();
            xhr.open('POST', '/api/ota/upload');
            xhr.upload.onprogress = function(e){ if (e.lengthComputable) uploadFill.style.width = ((e.loaded / e.total) * 100).toFixed(0) + '%'; };
            xhr.onload = function(){ (xhr.status >= 200 && xhr.status < 300) ? resolve() : reject(new Error('HTTP ' + xhr.status)); };
            xhr.onerror = function(){ reject(new Error('network error')); };
            var fd = new FormData(); fd.append('fw', file, file.name); xhr.send(fd);
          });
          uploadFill.style.width = '100%';
          setMsg('Uploaded ' + file.name);
          selectedSpoolFile = file.name;
          await pollOtaLog();               // capture the final "stored …" line
          await refreshPlan();
        } catch(e) {
          uploadFill.style.background = 'var(--danger)';
          setMsg('Upload failed: ' + e.message);
          await pollOtaLog();               // capture the last LOGW/LOGE before the error toast
        } finally { pickBtn.disabled = false; updateStep2(); }
      }
      if (filesInput) $('otaPickFiles').onclick = function(){ filesInput.click(); };
      if (filesInput) filesInput.onchange = function(){ var f = filesInput.files && filesInput.files[0]; if (f) uploadFile(f); filesInput.value = ''; };

      function syncSelectAllState() {
        if (!selectAllCb || !nodeListEl) return;
        var enabled = nodeListEl.querySelectorAll('.otaNodeChk:not(:disabled)');
        if (enabled.length === 0) { selectAllCb.checked = false; selectAllCb.indeterminate = false; selectAllCb.disabled = true; return; }
        selectAllCb.disabled = false;
        var nChecked = 0;
        enabled.forEach(function(cb){ if (cb.checked) nChecked++; });
        selectAllCb.checked = (nChecked === enabled.length);
        selectAllCb.indeterminate = (nChecked > 0 && nChecked < enabled.length);
      }
      if (selectAllCb) selectAllCb.addEventListener('change', function(){
        var want = selectAllCb.checked;
        nodeListEl.querySelectorAll('.otaNodeChk:not(:disabled)').forEach(function(cb){ cb.checked = want; });
        syncSelectAllState();
      });
      if (nodeListEl) nodeListEl.addEventListener('change', function(ev){
        if (!ev.target || !ev.target.classList || !ev.target.classList.contains('otaNodeChk')) return;
        syncSelectAllState();
      });

      function render() {
        if (!plan) return;
        var sf = pickFileByKind('server'), nf = pickFileByKind('node');
        var sfSource = pickFileByKind('source');                 // NEW
        var verEl = $('otaServerVer'); if (verEl) verEl.textContent = plan.serverVersion || '--';

        // [ota-selected-spool-file]
        // If the user explicitly picked a file in Step 2, use that one
        // instead of the newest. This matches the radio selection.
        if (selectedSpoolFile && plan.files) {
          var chosen = plan.files.find(function(f){ return f.name === selectedSpoolFile; });
          if (chosen) {
            if (chosen.kind === 'server') sf = chosen;
            else if (chosen.kind === 'node') nf = chosen;
            else if (chosen.kind === 'source') sfSource = chosen;   // NEW
          }
        }

        var serverAllowed = selectedKind === 'server' && !!sf;
        var nodesAllowed = selectedKind === 'node' && !!nf;
        if (serverAllowed) {
          serverRow.classList.remove('disabled');
          $('otaDoServer').disabled = false;
          serverFileEl.textContent = 'Will flash: ' + sf.name + '  (' + (sf.size / 1024).toFixed(0) + ' KiB)';
        } else {
          serverRow.classList.add('disabled');
          var cb = $('otaDoServer'); cb.disabled = true; cb.checked = false;
          serverFileEl.textContent = selectedKind !== 'server' ? 'Server bin not selected' : 'No server bin in spool';
        }

        // NEW: source row (same allow/deny shape as server)
        var sourceRowEl    = $('otaSourceRow');
        var sourceFileEl   = $('otaSourceFile');
        var sourceCb       = $('otaDoSource');
        var sourceAllowed  = selectedKind === 'source' && !!sfSource;
        if (sourceRowEl) {
          if (sourceAllowed) {
            sourceRowEl.style.display = '';
            sourceRowEl.classList.remove('disabled');
            if (sourceCb) { sourceCb.disabled = false; }
            if (sourceFileEl) {
              sourceFileEl.textContent = 'Will flash: ' + sfSource.name +
                                         '  (' + (sfSource.size / 1024).toFixed(0) + ' KiB)';
            }
          } else {
            sourceRowEl.style.display = '';
            sourceRowEl.classList.add('disabled');
            if (sourceCb) { sourceCb.disabled = true; sourceCb.checked = false; }
            if (sourceFileEl) {
              sourceFileEl.textContent = selectedKind !== 'source'
                ? 'Source bin not selected' : 'No source bin in spool';
            }
          }
        }

        var hint = $('otaTargetHint');
        if (selectedKind === 'node') hint.textContent = 'Player node bin selected — only nodes can be flashed.';
        else if (selectedKind === 'server') hint.textContent = 'Server bin selected — only this server can be flashed.';
        else if (selectedKind === 'source') hint.textContent = 'Source bin selected — only the source board can be flashed.';   // NEW
        else hint.textContent = 'Pick a bin first.';
        if (nodesAllowed) nodeListEl.classList.remove('disabled'); else nodeListEl.classList.add('disabled');
        var html = '';
        if (!plan.nodes || !plan.nodes.length) html = '<p class="muted" style="font-size:0.82rem;">No player nodes registered.</p>';
        else {
          plan.nodes.forEach(function(n){
            var canCheck = nodesAllowed && n.reachable;
            var showNewer = nf && nf.version !== 'unknown' && nf.version !== n.version;
            html += '<label class="ota-node-item">' +
              '<input type="checkbox" class="otaNodeChk" value="' + n.ip + '" ' + (canCheck ? '' : 'disabled') + '>' +
              '<div class="ota-node-body"><div class="ota-node-name">' + (n.name || n.ip) + '</div><div class="ota-node-ip">' + n.ip + '</div></div>' +
              '<div class="ota-node-ver">v' + String(n.version).replace(/[^0-9A-Za-z._-]/g, '') + (showNewer ? ' <span class="new">→ v' + String(nf.version).replace(/[^0-9A-Za-z._-]/g, '') + '</span>' : '') + (n.reachable ? '' : '<br><span style="color:var(--danger);">offline</span>') + '</div>' +
              '</label>';
          });
        }
        nodeListEl.innerHTML = html;
        syncSelectAllState();
      }
      async function refreshPlan() {
        var verEl = $('otaServerVer');
        try {
          var ctrl = new AbortController();
          var to = setTimeout(function(){ ctrl.abort(); }, 8000);
          var r = await fetch('/api/ota/plan?_=' + Date.now(), { cache: 'no-store', signal: ctrl.signal });
          clearTimeout(to);
          if (!r.ok) { if (verEl && (verEl.textContent === '--' || verEl.textContent === 'Offline')) verEl.textContent = 'Server ' + r.status; return; }
          plan = await r.json();
          
          // NEW: Render spool diagnostics
          var statsEl = $('otaSpoolStats');
          var listEl = $('otaSpoolList');
          if (plan.totalBytes !== undefined && statsEl) {
            var freeB = plan.totalBytes - plan.usedBytes;
            statsEl.innerHTML = `Spool: <b>${(plan.usedBytes/1024/1024).toFixed(2)} MB</b> used / ${(plan.totalBytes/1024/1024).toFixed(2)} MB (${(freeB/1024/1024).toFixed(2)} MB free)`;
          }
          if (plan.allSpoolFiles && listEl) {
            if (plan.allSpoolFiles.length === 0) {
              listEl.innerHTML = '<span style="color:var(--warn);">Directory is empty.</span>';
            } else {
              // [ota-spool-select-ui] Render a radio button for valid bins so
              // the operator can explicitly select which file to flash.
              function escHtml(s) {
                return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;')
                                .replace(/>/g,'&gt;').replace(/"/g,'&quot;');
              }
              listEl.innerHTML = plan.allSpoolFiles.map(function(f) {
                var sizeKb = (f.size / 1024).toFixed(0);
                var arg = String(f.name).replace(/\\/g,'\\\\').replace(/'/g,"\\'");
                var shown = escHtml(f.name);
                
                var validFile = (plan.files || []).find(function(vf){ return vf.name === f.name; });
                var radioHtml = '';
                if (validFile) {
                  var checked = (selectedSpoolFile === f.name) ? 'checked' : '';
                  radioHtml = '<input type="radio" name="otaSelectedFile" value="' + arg + '" ' + checked + ' onchange="otaSelectSpoolFile(\'' + arg + '\')" style="margin-right:10px; cursor:pointer; accent-color:var(--accent-primary);">';
                } else {
                  radioHtml = '<span style="display:inline-block; width:22px;"></span>';
                }

                return '<div style="display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid rgba(255,255,255,0.05); padding: 4px 0;">' +
                  '<div style="display:flex; align-items:center; flex:1 1 auto; overflow:hidden;">' +
                    radioHtml +
                    '<span style="overflow:hidden; text-overflow:ellipsis; white-space:nowrap;">' + shown + '</span>' +
                  '</div>' +
                  '<span style="color:var(--accent-primary); margin-left:8px; flex:0 0 auto;">' + sizeKb + ' KiB</span>' +
                  '<button type="button" class="btn-glass" onclick="otaDeleteSpoolFile(\'' + arg + '\')" ' +
                    'style="margin-left:8px; padding:2px 8px; font-size:0.7rem; border-color:var(--danger); color:var(--danger); flex:0 0 auto;" ' +
                    'title="Delete this file from the spool">Del</button>' +
                '</div>';
              }).join('');
            }
          }

          if (currentStep === 2) updateStep2();
          if (currentStep === 3) render();
        } catch(e) {
          if (verEl && (verEl.textContent === '--' || verEl.textContent === 'Offline')) verEl.textContent = (e && e.name === 'AbortError') ? 'Timeout' : 'Offline';
        }
      }

      // [ota-select-spool-file] Called by the radio button in the spool list.
      window.otaSelectSpoolFile = function(name) {
        selectedSpoolFile = name;
        updateStep2();
      };

      // [ota-delete-spool-fn] Global because it is called from an inline
      // onclick in the file list HTML built by refreshPlan(). Refreshes the
      // plan after success so the row disappears without a page reload.
      window.otaDeleteSpoolFile = async function(filename) {
        if (!confirm('Delete ' + filename + ' from the spool?\n\nThis cannot be undone.')) return;
        try {
          var r = await fetch('/api/ota/delete', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name: filename })
          });
          var j = await r.json();
          if (!r.ok || !j || !j.ok) {
            throw new Error((j && j.error) || ('HTTP ' + r.status));
          }
          toast('Deleted ' + filename, 'ok');
          await refreshPlan();
        } catch (e) {
          toast('Delete failed: ' + (e && e.message ? e.message : e), 'err');
        }
      };


      function renderStep4List() {
        var list = $('otaNodeStatusList'), targets = window.__otaTargets;
        if (!targets) return;
        var html = '';
        if (targets.server) html += '<div class="ota-node-item" id="ota-status-server"><div class="ota-node-body"><div class="ota-node-name">This Server</div></div><div class="ota-node-ver" id="ota-status-icon-server">Waiting…</div></div>';
        if (targets.source) html += '<div class="ota-node-item" id="ota-status-source"><div class="ota-node-body"><div class="ota-node-name">Source board</div><div class="ota-node-ip">UART link</div></div><div class="ota-node-ver" id="ota-status-icon-source">Waiting…</div></div>';   // NEW
        targets.nodes.forEach(function(ip){
          var name = ip;
          if (plan && plan.nodes) { var m = plan.nodes.find(function(n){ return n.ip === ip; }); if (m) name = m.name || ip; }
          var safeIp = ip.replace(/\./g, '-');
          html += '<div class="ota-node-item" id="ota-status-' + safeIp + '"><div class="ota-node-body"><div class="ota-node-name">' + name + '</div><div class="ota-node-ip">' + ip + '</div></div><div class="ota-node-ver" id="ota-status-icon-' + safeIp + '">Waiting…</div></div>';
        });
        list.innerHTML = html;
      }
      async function verifyUpdates() {
        var targets = window.__otaTargets;
        if (!targets) return;
        if (Date.now() - verifyStartTime > 40000) { finishOta(true); return; }
        var state;
        try { state = await (await fetch('/api/state', { cache: 'no-store' })).json(); } catch(e){ return; }
        var allResolved = true;
        if (targets.server) {
          var el = $('ota-status-icon-server');
          if (el && el.innerHTML.indexOf('✔') === -1 && el.innerHTML.indexOf('✖') === -1) {
            if (Date.now() - verifyStartTime > 15000) {
              // [server-verify-by-version] Only mark ✔ when /api/fw actually
              // reports the new version. A failed Update.end() leaves the
              // old firmware running; a plain 15 s timer would falsely
              // report success.
              var fwOk = false;
              try {
                var fw = await (await fetch('/api/fw?_=' + Date.now(),
                                            { cache: 'no-store' })).json();
                if (fw && fw.version && targets.newVersion &&
                    fw.version === targets.newVersion) fwOk = true;
              } catch(_) { /* server mid-reboot */ }
              if (fwOk) {
                el.innerHTML = '<span class="ok-tick">✔</span>';
              } else if (Date.now() - verifyStartTime > 35000) {
                el.innerHTML = '<span class="bad-x">✖</span>';
              } else {
                allResolved = false;
              }
            } else {
              allResolved = false;
            }
          }
        }
        targets.nodes.forEach(function(ip){
          var safeIp = ip.replace(/\./g, '-');
          var el = $('ota-status-icon-' + safeIp);
          if (!el) return;
          if (el.innerHTML.indexOf('✔') !== -1 || el.innerHTML.indexOf('✖') !== -1) return;
          var player = state.players ? state.players.find(function(p){ return p.ip === ip; }) : null;
          if (player && player.version === targets.newVersion) el.innerHTML = '<span class="ok-tick">✔</span>';
          else allResolved = false;
        });
        // NEW: source board timeout net (normally resolved by the
        // completed/failed arrays painted in pollStatus()).
        if (targets.source) {
          var srcEl = $('ota-status-icon-source');
          if (srcEl &&
              srcEl.innerHTML.indexOf('✔') === -1 &&
              srcEl.innerHTML.indexOf('✖') === -1) {
            if (Date.now() - verifyStartTime > 35000) {
              srcEl.innerHTML = '<span class="bad-x">✖</span>';
            } else {
              allResolved = false;
            }
          }
        }

        if (allResolved) finishOta(false);
      }
      async function finishOta(timedOut) {
        if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
        step4Done = true; nextBtn.style.display = ''; nextBtn.textContent = 'Close';
        var targets = window.__otaTargets;
        var okCount = 0, failCount = 0;
        if (targets && targets.server) {
          var el = $('ota-status-icon-server');
          if (el) { if (el.innerHTML.indexOf('✔') !== -1) okCount++; else if (timedOut) { failCount++; el.innerHTML = '<span class="bad-x">✖</span>'; } else { okCount++; el.innerHTML = '<span class="ok-tick">✔</span>'; } }
        }
        // NEW: source board tally
        if (targets && targets.source) {
          var srcEl = $('ota-status-icon-source');
          if (srcEl) {
            if (srcEl.innerHTML.indexOf('✔') !== -1) okCount++;
            else if (srcEl.innerHTML.indexOf('✖') !== -1) failCount++;
            else if (timedOut) { srcEl.innerHTML = '<span class="bad-x">✖</span>'; failCount++; }
            else              { srcEl.innerHTML = '<span class="ok-tick">✔</span>'; okCount++; }
          }
        }

        if (targets) targets.nodes.forEach(function(ip){
          var safeIp = ip.replace(/\./g, '-');
          var el = $('ota-status-icon-' + safeIp); if (!el) return;
          if (el.innerHTML.indexOf('✔') !== -1) { okCount++; return; }
          if (el.innerHTML.indexOf('✖') !== -1) { failCount++; return; }
          if (timedOut) { el.innerHTML = '<span class="bad-x">✖</span>'; failCount++; }
          else { el.innerHTML = '<span class="ok-tick">✔</span>'; okCount++; }
        });
        await restoreMuteIfNeeded(); refreshStatus();
        setTimeout(function(){
          var msg = 'Update complete.\n';
          if (okCount > 0) msg += '✔ ' + okCount + ' updated\n';
          if (failCount > 0) msg += '✖ ' + failCount + ' failed\n';
          alert(msg);
          otaCancel();
          window.openTab(null, 'tab-zones');
        }, 1000);
      }
      async function pollStatus() {
        var s;
        try {
          var ctrl = new AbortController();
          var to = setTimeout(function(){ ctrl.abort(); }, 4000);
          s = await (await fetch('/api/ota/status', { cache: 'no-store', signal: ctrl.signal })).json();
          clearTimeout(to);
        } catch(e){ return; }
        if (s.inProgress) {
          // [ota-stall-banner] If the server reports the job has made no
          // progress for 120 s, show a banner instead of spinning forever.
          // The wizard keeps polling (a slow but healthy node might recover),
          // but the operator now knows to check the log / reboot if the
          // banner persists.
          var logWrap = $('otaLogWrap');
          var pre     = $('otaLogPre');
          if (s.stalled && pre) {
            // Append once per stall transition (dedup by simple flag).
            if (!pre.dataset.stallBannerShown) {
              pre.dataset.stallBannerShown = '1';
              otaLogAppend({ level: 'E', ms: s.stalledAgeMs,
                             text: 'OTA stalled for ' + Math.round(s.stalledAgeMs/1000) +
                                   's on ' + (s.target || 'unknown') +
                                   '. Audio resumed. REBOOT the server to recover.' });
            }
          } else if (pre) {
            delete pre.dataset.stallBannerShown;
          }
          // [ota-per-node-progress] Paint completions FIRST so a just-finished
          // node shows ✔ the instant the job advances to the next node.
          var completed = Array.isArray(s.completed) ? s.completed : [];
          var failed    = Array.isArray(s.failed)    ? s.failed    : [];
          completed.forEach(function(ip){
            var el = $('ota-status-icon-' + ip.replace(/\./g, '-'));
            if (el && el.innerHTML.indexOf('✔') === -1) {
              el.innerHTML = '<span class="ok-tick">✔</span>';
            }
          });
          failed.forEach(function(ip){
            var el = $('ota-status-icon-' + ip.replace(/\./g, '-'));
            if (el && el.innerHTML.indexOf('✖') === -1) {
              el.innerHTML = '<span class="bad-x">✖</span>';
            }
          });

          var tgt = s.target || '';
          var targetId = '';
          if (tgt === 'server')        targetId = 'ota-status-icon-server';
          else if (tgt === 'source')   targetId = 'ota-status-icon-source';   // NEW
          else if (tgt.indexOf('node ') === 0) {
            var ip = tgt.replace('node ', '');
            targetId = 'ota-status-icon-' + ip.replace(/\./g, '-');
          }

          // Clear any stale spinner that is no longer the current target.
          // A spinner can only legitimately persist on the current target
          // between two polls; anything else is a leftover from the previous
          // node that has already been marked ✔ / ✖ above.
          document.querySelectorAll('.ota-node-ver').forEach(function(el){
            if (el.id === targetId) return;
            if (el.innerHTML.indexOf('spinner') !== -1) el.innerHTML = 'Waiting…';
          });

          // Paint the current target: spinner + live upload percentage.
          // progressPct has always been available in the status payload;
          // it was simply never rendered before.
          if (targetId) {
            var el = $(targetId);
            if (el) {
              var pct = (typeof s.progressPct === 'number' && s.progressPct >= 0)
                        ? s.progressPct : 0;
              el.innerHTML = '<div class="ota-spinner"></div> ' + pct + '%';
            }
          } else if (completed.length === 0 && failed.length === 0) {
            // No target, no completions — this can only happen in the first
            // ~200 ms after apply, before the task has published its target.
            // Paint a "Starting…" indicator on the first node so the list
            // does not sit dead silent at "Waiting…" during that window.
            var list = $('otaNodeStatusList');
            if (list) {
              var first = list.querySelector('.ota-node-ver');
              if (first && first.textContent === 'Waiting…') {
                first.innerHTML = '<div class="ota-spinner"></div> Starting…';
              }
            }
          }
          return;
        }
        if (!verifyStartTime) {
          verifyStartTime = Date.now();
          if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
          pollTimer = setInterval(verifyUpdates, 2000);
        }
      }
    })();

    // ====================================================================
    //  Target Buffer Latency bindings
    // ====================================================================
    (function(){
      var el = $('buffer-latency');
      var valEl = $('buffer-latency-val');
      if (!el || !valEl) return;
      // [pointer-capture] Without capture, releasing the mouse outside the
      // slider fires pointerup on a sibling element and activeDrags.buffer
      // stays true, silently freezing the slider's server-side refresh
      // until the operator clicks it again. setPointerCapture keeps the
      // up event targeted at the slider on both mouse and touch.
      el.addEventListener('pointerdown', function(e){
        activeDrags.buffer = true;
        try { el.setPointerCapture(e.pointerId); } catch (_) {}
      });
      el.addEventListener('pointerup',     function(){ activeDrags.buffer = false; });
      el.addEventListener('pointercancel', function(){ activeDrags.buffer = false; });
      el.addEventListener('input', function(){
        valEl.textContent = el.value + ' ms';
        // [audio-buffer-per-mode] Keep the form state in sync so the Save
        // button sends the buffer the user is looking at, not the value
        // that was loaded when the tab was first opened.
        encState.bufferMs = parseInt(el.value, 10) || 150;
      });
      el.addEventListener('change', async function(){
        activeDrags.buffer = false;
        // Live preview: apply to the running pipeline immediately so nodes
        // receive the new buffer hint before the user clicks Save.
        // Persistence happens on Save (POST /api/audio/encoding with
        // bufferMs + save:true), which writes enc_<mode>_buf to NVS.
        await api('/api/config', 'POST', { audioBufferMs: parseInt(el.value, 10) });
        refreshStatus();
      });
    })();

    // ====================================================================
    //  Volume slider bindings
    // ====================================================================
    (function(){
      var volD = $('volume'), volM = $('volume-mobile');
      var lblD = $('volume-label'), lblM = $('volume-float-label');
      var volSaveTimer = null;
      function paintVolume(v) {
        if (volD && volD.value !== String(v)) volD.value = v;
        if (volM && volM.value !== String(v)) volM.value = v;
        if (lblD) lblD.textContent = v + '%';
        if (lblM) lblM.textContent = v;
      }
      function onVolumeInput(v) {
        activeDrags.volume = true;
        paintVolume(v);
        if (isMuted) { isMuted = false; paintMuteAllButton(); }
        if (volSaveTimer) clearTimeout(volSaveTimer);
        volSaveTimer = setTimeout(async function(){
          activeDrags.volume = false;
          await api('/api/config', 'POST', { masterVolume: parseInt(v, 10) });
        }, 180);
      }
      if (volD) volD.addEventListener('input', function(){ onVolumeInput(volD.value); });
      if (volM) volM.addEventListener('input', function(){ onVolumeInput(volM.value); });
      paintVolume(100);
    })();

    // ====================================================================
    //  Mode + source select bindings
    // ====================================================================
    (function(){
      var modeSel = $('mode-select');
      if (modeSel) modeSel.addEventListener('change', function(){ setMode(modeSel.value); });
      var srcSel = $('source-select');
      if (srcSel) srcSel.addEventListener('change', function(){ setSource(srcSel.value); });
    })();

    // ====================================================================
    //  Network tab IIFE
    // ====================================================================
    (function(){
      var staticChk = $('net-use-static'), staticBox = $('net-static-fields');
      if (staticChk && staticBox) {
        staticChk.addEventListener('change', function(){ staticBox.hidden = !staticChk.checked; });
      }
      var own = { 'net-gw': true, 'net-sn': true, 'net-dns': true };
      ['net-gw','net-sn','net-dns'].forEach(function(id){
        var el = $(id);
        if (el) el.addEventListener('input', function(){ own[id] = (this.value.trim() === ''); });
      });
      var ipEl = $('net-ip');
      if (ipEl) ipEl.addEventListener('input', function(){
        var p = this.value.trim().split('.');
        if (p.length !== 4) return;
        for (var i = 0; i < 4; i++) if (!/^\d{1,3}$/.test(p[i]) || +p[i] > 255) return;
        var base = p[0] + '.' + p[1] + '.' + p[2] + '.1';
        var gw = $('net-gw'), dns = $('net-dns'), sn = $('net-sn');
        if (own['net-gw'] && gw) gw.value = base;
        if (own['net-dns'] && dns) dns.value = base;
        if (own['net-sn'] && sn) sn.value = '255.255.255.0';
      });
      var wifiModal = $('wifiScanModal');
      if (wifiModal) wifiModal.addEventListener('click', function(e){ if (e.target === wifiModal) closeWifiScan(); });
      var revertBtn = $('net-revert-btn'); if (revertBtn) revertBtn.addEventListener('click', loadNetworkState);
      var scanBtn = $('net-scan-btn'); if (scanBtn) scanBtn.addEventListener('click', startWifiScan);
      var saveBtn = $('net-save-btn'); if (saveBtn) saveBtn.addEventListener('click', saveNetworkSettings);
    })();

    // ====================================================================
    //  Analyze button
    // ====================================================================
    (function(){
      var btn = $('analyzeBtn');
      if (btn) btn.addEventListener('click', startRecording);
    })();

    // ====================================================================
    //  Firmware fetch + polling
    // ====================================================================
    fetch('/api/fw').then(function(r){ return r.json(); }).then(function(d){
      var el = $('server-version');
      if (el && d && d.version) el.textContent = 'firmware v' + d.version;
    }).catch(function(){});

    async function pollLoop() {
      var otaModal = $('otaModal');
      var isOtaOpen = otaModal && otaModal.classList.contains('open');
      // Skip polling during recording OR when the OTA wizard is open.
      // This frees up critical heap for the LittleFS flash writes.
      if (!recording && !isOtaOpen) await refreshStatus();
      setTimeout(pollLoop, 4000);
    }
    setTimeout(pollLoop, 150);

    // ====================================================================
    //  Touch swipe navigation
    // ====================================================================
    (function swipeNav() {
      var TAB_ORDER = ['tab-zones', 'tab-settings', 'tab-advanced'];
      var SWIPE_MIN_X = 60, SWIPE_MAX_Y = 100, SWIPE_RATIO = 1.5, SWIPE_MAX_MS = 700, EDGE_GUARD = 24;
      var startX = 0, startY = 0, startT = 0, tracking = false;

      function isInteractive(el) {
        while (el && el !== document.body) {
          var tag = (el.tagName || '').toLowerCase();
          if (tag === 'input' || tag === 'textarea' || tag === 'select' || tag === 'button' || tag === 'a' || tag === 'label') return true;
          if (el.classList) {
            if (el.classList.contains('slider') || el.classList.contains('node-chip') || el.classList.contains('nav-btn') ||
                el.classList.contains('menu-bar') || el.classList.contains('sub-tab-bar') || el.classList.contains('overlay') ||
                el.classList.contains('modal') || el.classList.contains('ota-modal') || el.classList.contains('ota-modal-panel') ||
                el.classList.contains('rec-overlay')) return true;
          }
          if (el.getAttribute && el.getAttribute('draggable') === 'true') return true;
          el = el.parentElement;
        }
        return false;
      }
      function navigateToTab(tabId) {
        var btns = document.querySelectorAll('.menu-bar .nav-btn');
        for (var i = 0; i < btns.length; i++) {
          var oc = btns[i].getAttribute('onclick') || '';
          if (oc.indexOf("'" + tabId + "'") !== -1 || oc.indexOf('"' + tabId + '"') !== -1) { btns[i].click(); return; }
        }
        if (typeof window.openTab === 'function') window.openTab(null, tabId);
      }
      document.addEventListener('touchstart', function(e){
        tracking = false;
        if (!e.touches || e.touches.length !== 1) return;
        var t = e.touches[0];
        if (t.clientX < EDGE_GUARD || t.clientX > window.innerWidth - EDGE_GUARD) return;
        if (isInteractive(e.target)) return;
        startX = t.clientX; startY = t.clientY; startT = Date.now(); tracking = true;
      }, { passive: true });
      document.addEventListener('touchend', function(e){
        if (!tracking) return;
        tracking = false;
        if (!e.changedTouches || e.changedTouches.length === 0) return;
        var t = e.changedTouches[0];
        var dx = t.clientX - startX, dy = t.clientY - startY, dt = Date.now() - startT;
        if (dt > SWIPE_MAX_MS) return;
        if (Math.abs(dx) < SWIPE_MIN_X) return;
        if (Math.abs(dy) > SWIPE_MAX_Y) return;
        if (Math.abs(dx) < Math.abs(dy) * SWIPE_RATIO) return;
        var idx = TAB_ORDER.indexOf(currentTabId); if (idx < 0) return;
        var next = dx < 0 ? idx + 1 : idx - 1;
        if (next < 0 || next >= TAB_ORDER.length) return;
        navigateToTab(TAB_ORDER[next]);
      }, { passive: true });
    })();

    })();
  </script>
</body>
</html>
)HTML";