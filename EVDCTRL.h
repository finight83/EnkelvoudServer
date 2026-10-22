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
    }

    * { margin: 0; padding: 0; box-sizing: border-box; font-family: 'Inter', sans-serif; }

    /* Top loading bar. Uses transform:scaleX instead of width so it is
       GPU-composited and never triggers layout while an HTTP request is
       in flight. Three classes: idle (invisible), .active (pulse while
       work is outstanding), .done (fade the tail out). */
    #top-loading-bar {
      position: fixed; top: 0; left: 0; right: 0;
      height: 3px;
      background: linear-gradient(90deg, #00d2ff, #8e2de2);
      box-shadow: 0 0 8px rgba(0,210,255,0.6);
      transform-origin: left center;
      transform: scaleX(0);
      opacity: 0;
      transition: transform .25s ease, opacity .3s ease;
      z-index: 9999;
      pointer-events: none;
    }
    #top-loading-bar.active {
      opacity: 1;
      transform: scaleX(0.3);
      transition: transform .1s ease, opacity .1s ease;
    }
    #top-loading-bar.done {
      opacity: 0;
      transform: scaleX(1);
      transition: transform .2s ease, opacity .3s ease .2s;
    }

    body {
      background-color: #050510;
      color: var(--text-main);
      min-height: 100vh;
      display: flex; justify-content: center; align-items: flex-start;
      overflow-x: hidden; overflow-y: auto;
      -webkit-overflow-scrolling: touch;
      position: relative; padding: 20px;
    }

    .galaxy-bg {
      position: fixed; top: 0; left: 0;
      width: 100vw; height: 100vh;
      overflow: hidden; z-index: 1; pointer-events: none;
    }
    .orb {
      position: absolute; border-radius: 50%;
      filter: blur(70px); opacity: 0.75;
      will-change: transform;
    }
    .orb-1 { width: 400px; height: 400px; background: #4a00e0;
             top: -100px; left: -100px;
             animation: float-a 18s infinite ease-in-out alternate; }
    .orb-2 { width: 500px; height: 500px; background: #8e2de2;
             bottom: -150px; right: -100px;
             animation: float-b 22s infinite ease-in-out alternate;
             animation-delay: -6s; }
    .orb-3 { width: 300px; height: 300px; background: #00d2ff;
             top: 40%; left: 45%;
             animation: float-c 15s infinite ease-in-out alternate;
             animation-delay: -9s; }
    @keyframes float-a {
      0%   { transform: translate(0, 0) scale(1); }
      50%  { transform: translate(120px, -90px) scale(1.15); }
      100% { transform: translate(-40px, 60px) scale(0.95); }
    }
    @keyframes float-b {
      0%   { transform: translate(0, 0) scale(1); }
      50%  { transform: translate(-140px, 80px) scale(1.1); }
      100% { transform: translate(60px, -40px) scale(0.9); }
    }
    @keyframes float-c {
      0%   { transform: translate(-50%, -50%) scale(1); }
      50%  { transform: translate(calc(-50% + 100px), calc(-50% - 70px)) scale(1.2); }
      100% { transform: translate(calc(-50% - 50px), calc(-50% + 50px)) scale(0.85); }
    }

    .stars, .stars::before, .stars::after {
      position: absolute; inset: 0;
      background-repeat: repeat;
      animation: twinkle 3.6s infinite ease-in-out;
    }
    .stars {
      background-image:
        radial-gradient(1px 1px at 20px 30px, #fff, transparent),
        radial-gradient(1px 1px at 40px 70px, #fff, transparent),
        radial-gradient(1.5px 1.5px at 90px 40px, #fff, transparent);
      background-size: 200px 200px;
      opacity: 0.55;
    }
    .stars::before {
      content: '';
      background-image:
        radial-gradient(1.5px 1.5px at 50px 160px, #fff, transparent),
        radial-gradient(1px 1px at 130px 80px, #fff, transparent),
        radial-gradient(2px 2px at 160px 120px, #fff, transparent);
      background-size: 250px 250px;
      opacity: 0.35;
      animation-duration: 5.2s; animation-delay: -1.4s;
    }
    .stars::after {
      content: '';
      background-image:
        radial-gradient(1px 1px at 15px 90px, #fff, transparent),
        radial-gradient(1px 1px at 110px 20px, #fff, transparent),
        radial-gradient(1.5px 1.5px at 70px 200px, #fff, transparent);
      background-size: 180px 180px;
      opacity: 0.45;
      animation-duration: 6.5s; animation-delay: -3.1s;
    }
    @keyframes twinkle {
      0%, 100% { opacity: 0.2; }
      50%      { opacity: 0.85; }
    }

    .dashboard-container {
      position: relative; z-index: 10;
      width: 100%; max-width: 900px;
      background: var(--glass-bg);
      backdrop-filter: blur(20px) saturate(180%);
      -webkit-backdrop-filter: blur(20px) saturate(180%);
      border-radius: 24px;
      border-top: 1px solid var(--glass-border-light);
      border-left: 1px solid var(--glass-border-light);
      border-bottom: 1px solid var(--glass-border-dim);
      border-right: 1px solid var(--glass-border-dim);
      box-shadow: 0 25px 50px rgba(0,0,0,0.5), inset 0 0 15px rgba(255,255,255,0.05);
      /* overflow: hidden makes this element the scroll container for any
         position:sticky descendant, which kills the sticky header. overflow:
         clip clips the rounded corners the same way but does NOT create a
         scroll container, so the sticky shell sticks to the viewport. */
      overflow: clip;
      display: flex; flex-direction: column;
      margin: 20px 0;
    }

    .top-bar {
      display: flex; align-items: center;
      padding: 14px 30px 6px; gap: 14px; flex-wrap: wrap;
    }
    .brand { flex: 0 0 auto; min-width: 0; }
    .page-title {
      font-size: 1.6rem; font-weight: 700; letter-spacing: 0.5px;
      background: linear-gradient(to right, #00d2ff, #8e2de2);
      -webkit-background-clip: text; -webkit-text-fill-color: transparent;
      text-shadow: 0 2px 10px rgba(0,210,255,0.3);
    }
    .page-title {
      cursor: pointer;
      user-select: none;
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .page-title svg {
      color: var(--text-muted);
      opacity: 0.5;
      transition: opacity 0.2s, color 0.2s;
      fill: currentColor;
    }
    .page-title:hover svg {
      opacity: 1 !important;
      color: #fff;
    }
    .menu-bar {
      display: flex; gap: 10px; align-items: center; flex-wrap: wrap;
      flex: 1 1 auto; padding: 0;
    }
    .nav-btn {
      background: rgba(255,255,255,0.03);
      border: 1px solid rgba(255,255,255,0.1);
      color: var(--text-muted);
      padding: 10px 20px; border-radius: 30px;
      font-size: 0.9rem; font-weight: 500;
      cursor: pointer; transition: all 0.3s ease;
      display: flex; align-items: center; gap: 8px;
      backdrop-filter: blur(5px);
    }
    .nav-btn:hover { background: rgba(255,255,255,0.1); color: var(--text-main); }
    .nav-btn.active {
      background: linear-gradient(135deg, rgba(255,255,255,0.2), rgba(255,255,255,0.05));
      color: #fff;
      border-top: 1px solid rgba(255,255,255,0.6);
      border-left: 1px solid rgba(255,255,255,0.6);
      box-shadow: 0 0 15px rgba(142,45,226,0.4);
      text-shadow: 0 1px 5px rgba(0,210,255,0.5);
    }
    .master-label {
      font-size: 0.85rem; color: var(--text-muted);
      text-transform: uppercase; letter-spacing: 1px; white-space: nowrap;
    }
    .master-label b { color: var(--text-main); margin-left: 6px; }

    /* Row 2 of the sticky shell: mode, source, master slider, mute.
       Everything here sits below the title row and stays pinned with it. */
    .control-row {
      display: flex; align-items: center; gap: 12px;
      padding: 2px 30px 12px; flex-wrap: wrap;
    }
    .control-row #volume {
      flex: 1 1 180px; min-width: 120px;
      -webkit-appearance: none;
      height: 6px; background: rgba(255,255,255,0.2);
      border-radius: 3px; outline: none;
      box-shadow: inset 0 1px 3px rgba(0,0,0,0.5);
    }
    .control-row #volume::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 18px; height: 18px; border-radius: 50%;
      background: #fff; cursor: pointer;
      border: 2px solid var(--accent-secondary);
      box-shadow: 0 0 15px rgba(0,210,255,0.8);
    }
    /* Mute is pushed to the far right of the control row. */
    .control-row #btn-mute { margin-left: auto; }
    .mode-btn {
      background: rgba(255,255,255,0.03);
      border: 1px solid rgba(255,255,255,0.12);
      color: var(--text-muted);
      padding: 7px 16px; border-radius: 20px;
      font-size: 0.82rem; font-weight: 600;
      cursor: pointer; transition: all 0.25s ease;
    }
    .mode-btn:hover { background: rgba(255,255,255,0.1); color: var(--text-main); }
    .mode-btn.mode-active {
      background: linear-gradient(135deg, #00d2ff, #8e2de2);
      border-color: transparent; color: #fff;
      box-shadow: 0 0 18px rgba(0,210,255,0.5);
    }
    .source-select {
      background-color: rgba(255,255,255,0.03);
      border: 1px solid rgba(255,255,255,0.12);
      color: var(--text-main);
      padding: 8px 34px 8px 16px; border-radius: 20px;
      font-size: 0.85rem; font-weight: 600;
      cursor: pointer; transition: all 0.25s ease;
      -webkit-appearance: none; appearance: none;
      background-image: url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='10' height='6' viewBox='0 0 10 6'><path d='M0 0l5 6 5-6z' fill='%23cbd5e1'/></svg>");
      background-repeat: no-repeat; background-position: right 14px center;
    }
    .source-select:hover { background-color: rgba(255,255,255,0.10); border-color: var(--accent-primary); }
    .source-select:focus { outline: none; border-color: var(--accent-primary); box-shadow: 0 0 12px rgba(0,210,255,0.4); }
    .source-select option { background: #111116; color: #f6f6f8; }

    .tab-content { display: none; padding: 24px 30px; animation: fadeIn 0.4s ease forwards; }
    .tab-content.active { display: block; }
    @keyframes fadeIn {
      from { opacity: 0; transform: translateY(10px); }
      to   { opacity: 1; transform: translateY(0); }
    }
    .section-title {
      font-size: 1.2rem; margin-bottom: 20px; font-weight: 600;
      letter-spacing: 0.5px;
      background: linear-gradient(to right, #00d2ff, #8e2de2);
      -webkit-background-clip: text; -webkit-text-fill-color: transparent;
      text-shadow: 0 2px 10px rgba(0,210,255,0.2);
    }

    .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
    .control-card {
      background: rgba(255,255,255,0.03);
      border: 1px solid rgba(255,255,255,0.1);
      border-top: 1px solid rgba(255,255,255,0.2);
      border-radius: 16px; padding: 20px;
      box-shadow: inset 0 2px 10px rgba(255,255,255,0.02);
    }
    .data-label {
      font-size: 0.8rem; color: var(--text-muted);
      text-transform: uppercase; letter-spacing: 1px; margin-bottom: 8px;
    }
    .data-value { font-size: 1.4rem; font-weight: 600; text-shadow: 0 1px 5px rgba(0,0,0,0.5); }
    .muted { color: var(--text-muted); font-size: 0.85rem; }

    .checkbox { display: flex; gap: 10px; align-items: center; cursor: pointer; }
    .checkbox input[type=checkbox] {
      width: 18px; height: 18px;
      accent-color: var(--accent-primary);
      flex: 0 0 auto; margin: 0;
    }

    /* ---- Form fields (Network tab) ------------------------------------ */
    /* Same look as the node dashboard's EVDSET.h: dark filled inputs,
       uppercase muted labels, single accent focus ring. Without these
       rules the browser falls back to its default light-on-white text
       input which clashes with the dark glass theme. */
    .field { margin-top: 14px; }
    .field:first-child { margin-top: 0; }
    .field > label {
      display: block;
      color: var(--text-muted);
      font-size: .82rem;
      margin-bottom: 6px;
      text-transform: uppercase;
      letter-spacing: .5px;
    }
    input[type=text],
    input[type=password],
    input[type=number] {
      width: 100%;
      padding: 11px 12px;
      background: rgba(0,0,0,.42);
      border: 1px solid rgba(255,255,255,0.12);
      border-radius: 10px;
      color: var(--text-main);
      font: inherit;
      font-size: 16px;   /* >=16px prevents iOS auto-zoom on focus */
      min-height: 44px;
      transition: all .2s ease;
    }
    input[type=text]:focus,
    input[type=password]:focus,
    input[type=number]:focus {
      outline: none;
      border-color: var(--accent-primary);
      box-shadow: 0 0 0 3px rgba(0,210,255,.18);
    }

    .btn-glass {
      background: rgba(255,255,255,0.05);
      border: 1px solid rgba(255,255,255,0.2);
      color: var(--text-main);
      padding: 8px 16px; border-radius: 8px;
      cursor: pointer; text-decoration: none; display: inline-block;
    }
    .btn-glass:hover { background: rgba(255,255,255,0.15); }
    .btn-glass:disabled { opacity: 0.4; cursor: not-allowed; }
    .btn-mute-active {
      background: var(--danger) !important;
      border-color: var(--danger) !important;
      color: #fff !important; box-shadow: 0 0 10px var(--danger);
    }

    .zone-toolbar {
      display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px;
    }
    .zone-toolbar .section-title { margin: 0; }
    .zone-container {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
      gap: 20px;
    }
    .zone-box {
      background: rgba(255,255,255,0.04);
      border: 1px solid rgba(255,255,255,0.1);
      border-radius: 16px; padding: 18px; min-height: 200px;
      display: flex; flex-direction: column; transition: all 0.3s ease;
    }
    .zone-box.drag-over {
      border-color: var(--accent-primary);
      background: rgba(0,210,255,0.08);
    }
    .zone-header {
      display: flex; justify-content: space-between; align-items: center;
      margin-bottom: 12px;
      border-bottom: 1px solid rgba(255,255,255,0.1);
      padding-bottom: 8px; gap: 6px;
    }
    .zone-header input {
      background: transparent; border: none;
      color: #fff; font-size: 1.1rem; font-weight: 600; width: 60%;
    }
    .zone-header input:focus { outline: 1px solid var(--accent-primary); border-radius: 4px; padding: 2px; }
    .zone-controls { margin-bottom: 12px; }
    .zone-row {
      display: flex; justify-content: space-between;
      font-size: 0.75rem; color: var(--text-muted);
      margin-bottom: 4px; align-items: center;
    }
    .zone-controls input[type=range] {
      width: 100%; -webkit-appearance: none;
      height: 5px; background: rgba(255,255,255,0.2);
      border-radius: 3px; outline: none;
    }
    .zone-controls input[type=range]::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 14px; height: 14px; border-radius: 50%;
      background: #fff; cursor: pointer;
      border: 2px solid var(--accent-secondary);
    }
    .zone-actions { display: flex; gap: 8px; margin-top: 8px; justify-content: space-between; }

    .node-chip {
      background: rgba(255,255,255,0.08);
      border: 1px solid rgba(255,255,255,0.15);
      border-radius: 12px; padding: 10px 12px; margin-bottom: 10px;
      cursor: grab; transition: transform 0.2s;
      display: flex; justify-content: space-between; align-items: center; gap: 10px;
    }
    .node-chip:active { cursor: grabbing; }
    .node-chip:hover { background: rgba(255,255,255,0.12); transform: translateY(-2px); }
    .node-meta { min-width: 0; }
    .node-meta strong {
      display: block; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
    }
    .node-ip {
      font-size: 0.75rem; color: var(--text-muted);
      overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
    }
    .gear {
      flex: 0 0 auto; width: 34px; height: 34px; border-radius: 50%;
      display: flex; align-items: center; justify-content: center;
      background: rgba(255,255,255,0.06);
      border: 1px solid rgba(255,255,255,0.15);
      color: var(--accent-primary);
      font-size: 1.15rem; text-decoration: none;
      cursor: pointer; transition: all 0.2s;
    }
    .gear:hover { background: rgba(0,210,255,0.15); color: #fff; transform: scale(1.08); }

    /* ---- Offline node styling ----------------------------------------
       A node that is in a zone's nodeIps list but hasn't heart-beaten
       within PLAYER_STALE_TIMEOUT_MS is "known: false" in /api/state.
       Grey it out and disable the gear, but keep it draggable so the
       operator can still move it to a different zone (the drop handler
       is wired to ondragstart, which is independent of the gear). */
    .node-chip.offline {
      opacity: 0.5;
      filter: grayscale(0.75);
    }
    /* Keep the drag hint visible even at 50 % opacity: the chip is still
       a valid drag source, and a dead hover would look broken. */
    .node-chip.offline:hover {
      background: rgba(255,255,255,0.06);
      transform: none;
    }
    .node-chip.offline .node-meta strong {
      font-weight: 500;
    }
    /* The gear is rendered as a <span> when offline, but defensive CSS
       in case a future change reverts it to <a>. */
    .gear.disabled,
    .node-chip.offline .gear {
      pointer-events: none;
      cursor: not-allowed;
      opacity: 0.35;
      color: var(--text-muted);
      background: rgba(255,255,255,0.03);
      border-color: rgba(255,255,255,0.08);
      transform: none;
    }

    #netIframe {
      width: 100%; height: 80vh; min-height: 520px;
      border: 1px solid rgba(255,255,255,0.12);
      border-radius: 14px; background: #050507;
    }

    /* -------- OTA wizard modal -------------------------------------------- */
    .ota-modal {
      position: fixed; inset: 0; z-index: 1000;
      display: none; align-items: center; justify-content: center;
      padding: 20px;
    }
    .ota-modal.open { display: flex; }
    .ota-modal-backdrop {
      position: absolute; inset: 0;
      background: rgba(0,0,0,0.65);
      backdrop-filter: blur(6px);
      -webkit-backdrop-filter: blur(6px);
    }
    .ota-modal-panel {
      position: relative; width: 100%; max-width: 540px;
      background: linear-gradient(135deg, rgba(30,25,55,0.97), rgba(15,10,30,0.97));
      border-top: 1px solid var(--glass-border-light);
      border-left: 1px solid var(--glass-border-light);
      border-bottom: 1px solid var(--glass-border-dim);
      border-right: 1px solid var(--glass-border-dim);
      border-radius: 20px;
      box-shadow: 0 25px 60px rgba(0,0,0,0.6), inset 0 0 20px rgba(255,255,255,0.03);
      overflow: hidden;
      display: flex; flex-direction: column;
      max-height: 90vh;
    }
    .ota-modal-header {
      display: flex; align-items: center; justify-content: space-between;
      padding: 16px 20px 4px;
    }
    .ota-close {
      background: transparent; border: none; color: var(--text-muted);
      font-size: 1.6rem; line-height: 1; cursor: pointer; padding: 0 4px;
    }
    .ota-close:hover { color: #fff; }
    .ota-steps { display: flex; gap: 8px; align-items: center; }
    .ota-step-dot {
      width: 8px; height: 8px; border-radius: 50%;
      background: rgba(255,255,255,0.15);
      transition: all 0.3s ease;
    }
    .ota-step-dot.active {
      width: 22px; border-radius: 4px;
      background: var(--accent-primary);
      box-shadow: 0 0 10px var(--accent-primary);
    }
    .ota-step-dot.done { background: var(--success); }
    .ota-modal-body { padding: 8px 24px 4px; overflow-y: auto; }
    .ota-step { display: none; }
    .ota-step.active { display: block; animation: fadeIn 0.3s ease forwards; }
    .ota-step h3 {
      font-size: 1.15rem; margin-bottom: 10px; font-weight: 700;
      background: linear-gradient(to right, #00d2ff, #8e2de2);
      -webkit-background-clip: text; -webkit-text-fill-color: transparent;
    }
    .ota-step p {
      font-size: 0.88rem; color: var(--text-muted);
      margin-bottom: 12px; line-height: 1.45;
    }
    .ota-step code {
      background: rgba(255,255,255,0.08);
      padding: 1px 6px; border-radius: 4px;
      font-size: 0.82rem; color: var(--text-main);
    }
    .ota-modal-footer {
      display: flex; justify-content: flex-end; gap: 10px;
      padding: 16px 24px 20px;
    }
    .btn-primary {
      background: linear-gradient(135deg, #00d2ff, #8e2de2);
      border: none; color: #fff;
      padding: 9px 24px; border-radius: 8px;
      cursor: pointer; font-weight: 600; font-size: 0.88rem;
      box-shadow: 0 0 18px rgba(0,210,255,0.35);
    }
    .btn-primary:disabled {
      opacity: 0.35; cursor: not-allowed; box-shadow: none;
      filter: grayscale(0.6);
    }
    .btn-primary:hover:not(:disabled) { filter: brightness(1.1); }

    .ota-progress-bar {
      height: 6px; background: rgba(255,255,255,0.1);
      border-radius: 3px; margin-top: 14px; overflow: hidden;
    }
    .ota-progress-fill {
      height: 100%; width: 0%;
      background: linear-gradient(to right, #00d2ff, #8e2de2);
      transition: width 0.4s ease;
    }

    /* Step 2: current node version reuse option */
    .ota-current-wrap {
      margin-top: 18px;
      padding-top: 16px;
      border-top: 1px solid rgba(255,255,255,0.08);
      display: none;
    }
    .ota-current-wrap.visible { display: block; }
    .ota-current-wrap .checkbox span { font-size: 0.88rem; }
    .ota-current-wrap b { color: var(--accent-primary); margin-left: 4px; }

    /* Step 3: targets list */
    .ota-target-row {
      background: rgba(255,255,255,0.03);
      border: 1px solid rgba(255,255,255,0.08);
      border-radius: 10px;
      padding: 12px 14px;
      margin-bottom: 14px;
      transition: opacity 0.2s, filter 0.2s;
    }
    .ota-target-row.disabled { opacity: 0.32; filter: grayscale(0.6); }
    .ota-target-row .checkbox span { font-size: 0.88rem; }
    .ota-target-row .checkbox input[type=checkbox]:disabled { cursor: not-allowed; }
    .ota-server-meta {
      font-size: 0.76rem; color: var(--text-muted);
      margin-top: 6px; margin-left: 28px;
      font-family: 'SF Mono', ui-monospace, Menlo, Consolas, monospace;
    }

    .ota-targets-list {
      max-height: 260px; overflow-y: auto;
      transition: opacity 0.2s, filter 0.2s;
    }
    .ota-targets-list.disabled { opacity: 0.32; filter: grayscale(0.6); pointer-events: none; }

    .ota-section-label {
      font-size: 0.72rem; text-transform: uppercase;
      letter-spacing: 1.2px; color: var(--text-muted);
      margin: 4px 0 8px;
    }

    /* [ota-select-all] Row layout for the "Player nodes" label with the
       Select-all checkbox pinned to the right. Reuses .checkbox spacing
       so the tick lines up with the checkbox controls below, but strips
       the label typography so it does not clash with the section title. */
    .ota-node-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      gap: 12px;
    }
    .ota-select-all {
      margin: 0;
      gap: 0.4rem;
      text-transform: none;
      letter-spacing: 0;
      font-size: 0.75rem;
      font-weight: 600;
      color: var(--text-muted);
      cursor: pointer;
      user-select: none;
    }
    .ota-select-all input {
      width: 15px; height: 15px;
      margin: 0;
      accent-color: var(--accent-primary);
      cursor: pointer;
    }
    .ota-select-all input:disabled { cursor: not-allowed; opacity: 0.4; }
    .ota-select-all input:disabled + span { opacity: 0.4; }

    .ota-node-item {
      display: flex; align-items: center; gap: 12px;
      padding: 10px 14px; margin-bottom: 8px;
      background: rgba(255,255,255,0.03);
      border: 1px solid rgba(255,255,255,0.08);
      border-radius: 10px;
      cursor: pointer;
      transition: background 0.2s, border-color 0.2s;
      font-size: 0.88rem;
    }
    .ota-node-item:hover {
      background: rgba(255,255,255,0.06);
      border-color: rgba(255,255,255,0.15);
    }
    .ota-node-item input[type=checkbox] {
      width: 16px; height: 16px;
      accent-color: var(--accent-primary);
      flex: 0 0 auto; margin: 0;
    }
    .ota-node-item input[type=checkbox]:disabled { cursor: not-allowed; }
    .ota-node-body { flex: 1 1 auto; min-width: 0; }
    .ota-node-name {
      font-weight: 600; font-size: 0.9rem;
      color: var(--text-main);
      overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
    }
    .ota-node-ip {
      font-size: 0.76rem; color: var(--text-muted);
      font-family: 'SF Mono', ui-monospace, Menlo, Consolas, monospace;
      margin-top: 2px;
    }
    .ota-node-ver {
      flex: 0 0 auto;
      font-size: 0.78rem; color: var(--text-muted);
      text-align: right;
      font-family: 'SF Mono', ui-monospace, Menlo, Consolas, monospace;
    }
    .ota-node-ver .new { color: var(--accent-primary); font-weight: 600; }

    .ota-spinner {
      width: 18px;
      height: 18px;
      border: 2px solid rgba(255,255,255,0.2);
      border-top-color: var(--accent-primary);
      border-radius: 50%;
      animation: busySpin 0.8s linear infinite;
      display: inline-block;
      vertical-align: middle;
    }
    .ok-tick { color: var(--success); font-size: 1.2rem; font-weight: bold; }
    .bad-x { color: var(--danger); font-size: 1.2rem; font-weight: bold; }

        /* -------- Wi-Fi scan modal ------------------------------------------- */
    .wifi-modal {
      position: fixed; inset: 0; z-index: 1000;
      display: none; align-items: center; justify-content: center;
      padding: 20px;
    }
    .wifi-modal.open { display: flex; }
    .wifi-modal-backdrop {
      position: absolute; inset: 0;
      background: rgba(0,0,0,0.65);
      backdrop-filter: blur(6px);
      -webkit-backdrop-filter: blur(6px);
    }
    .wifi-modal-panel {
      position: relative; width: 100%; max-width: 440px;
      background: linear-gradient(135deg, rgba(30,25,55,0.97), rgba(15,10,30,0.97));
      border-top: 1px solid var(--glass-border-light);
      border-left: 1px solid var(--glass-border-light);
      border-bottom: 1px solid var(--glass-border-dim);
      border-right: 1px solid var(--glass-border-dim);
      border-radius: 20px;
      box-shadow: 0 25px 60px rgba(0,0,0,0.6), inset 0 0 20px rgba(255,255,255,0.03);
      overflow: hidden; display: flex; flex-direction: column;
      max-height: 80vh;
    }
    .wifi-modal-header {
      display: flex; align-items: center; justify-content: space-between;
      padding: 16px 20px; border-bottom: 1px solid rgba(255,255,255,0.08);
    }
    .wifi-modal-body { padding: 4px 0; overflow-y: auto; }
    .wifi-net {
      display: flex; justify-content: space-between; align-items: center;
      gap: 12px; width: 100%; text-align: left;
      padding: 12px 20px; margin: 0; background: none; border: 0;
      border-bottom: 1px solid rgba(255,255,255,0.06);
      color: var(--text-main); font: inherit; cursor: pointer;
      transition: background .15s ease;
    }
    .wifi-net:last-child { border-bottom: 0; }
    .wifi-net:hover { background: rgba(255,255,255,0.06); }
    .wifi-net .rssi { color: var(--text-muted); font-size: .82rem; flex: 0 0 auto; }

    /* ====================================================
       Sticky shell + icon buttons + mode/source selects
       ==================================================== */
    .sticky-shell {
      position: sticky; top: 0; z-index: 30;
      background: linear-gradient(180deg,
                  rgba(10,10,22,0.96) 0%,
                  rgba(10,10,22,0.88) 70%,
                  rgba(10,10,22,0.78) 100%);
      backdrop-filter: blur(16px) saturate(180%);
      -webkit-backdrop-filter: blur(16px) saturate(180%);
      border-bottom: 1px solid rgba(255,255,255,0.08);
    }
    .top-actions { display: flex; align-items: center; gap: 8px;
                   flex: 0 0 auto; flex-wrap: nowrap; margin-left: auto; }
    .icon-btn {
      width: 40px; height: 40px; padding: 0; border-radius: 50%;
      background: rgba(255,255,255,0.06);
      border: 1px solid rgba(255,255,255,0.14);
      color: var(--text-main);
      display: inline-flex; align-items: center; justify-content: center;
      cursor: pointer; text-decoration: none; font-size: 1.15rem; line-height: 1;
      transition: background .2s ease, border-color .2s ease;
      flex: 0 0 auto;
    }
    .icon-btn:hover { background: rgba(255,255,255,0.14); }
    .icon-btn.active {
      background: linear-gradient(135deg, #00d2ff, #8e2de2);
      border-color: transparent; color: #fff;
      box-shadow: 0 0 14px rgba(0,210,255,0.45);
    }
    .icon-btn.danger { color: var(--danger); border-color: rgba(239,68,68,0.5); }
    .mode-select {
      appearance: none; -webkit-appearance: none;
      background: rgba(255,255,255,0.06);
      border: 1px solid rgba(255,255,255,0.14);
      color: var(--text-main);
      padding: 8px 30px 8px 14px; border-radius: 20px;
      font-size: .82rem; font-weight: 600; cursor: pointer; line-height: 1.2;
      background-image: url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='10' height='6' viewBox='0 0 10 6'><path d='M0 0l5 6 5-6z' fill='%23cbd5e1'/></svg>");
      background-repeat: no-repeat; background-position: right 11px center;
    }
    .mode-select:focus { outline: none; border-color: var(--accent-primary); }
    .mode-select option { background:#111116; color:#f6f6f8; }
        /* Always-visible CPU / RAM / PSRAM stat pills (sticky shell row 3) */
    .stat-row {
      display: flex; gap: 8px;
      padding: 0 30px 10px; flex-wrap: wrap;
    }
    .stat-pill {
      background: rgba(255,255,255,0.06);
      border: 1px solid rgba(255,255,255,0.12);
      color: var(--text-muted);
      padding: 4px 12px;
      border-radius: 12px;
      font-size: 0.72rem;
      font-weight: 600;
      font-variant-numeric: tabular-nums;
      letter-spacing: 0.5px;
      white-space: nowrap;
    }
    .stat-pill.warn  { color: #fbbf24; border-color: rgba(251,191,36,0.4); }
    .stat-pill.crit  { color: var(--danger); border-color: rgba(239,68,68,0.5); }
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
      .control-row { padding: 2px 16px 10px; gap: 8px; }
      .stat-row { padding: 0 16px 8px; gap: 6px; }
      .stat-pill { padding: 3px 9px; font-size: 0.68rem; }
      .nav-btn { padding: 8px 14px; font-size: 0.82rem; gap: 5px; }
      .mode-btn { padding: 6px 12px; font-size: 0.78rem; }
      .source-select { font-size: 0.8rem; padding: 6px 30px 6px 12px; }
      .tab-content { padding: 16px 18px; }
      .grid-2 { grid-template-columns: 1fr; gap: 14px; }
      .zone-container { grid-template-columns: 1fr; gap: 14px; }
      #netIframe { height: 85vh; min-height: 560px; }
      .ota-modal { padding: 8px; }
      .ota-modal-panel { max-width: 100%; }
      .ota-modal-body { padding: 8px 18px 4px; }
      .ota-modal-footer { padding: 14px 18px 18px; }

      /* Phone-only vertical master volume, floating on the right edge.
         The horizontal slider + label in the control row are redundant on
         a phone, so they're hidden here and the .volume-float takes over. */
      .control-row #volume { display: none; }
      .control-row .master-label { display: none; }
      .volume-float {
        display: flex; flex-direction: column; align-items: center; gap: 10px;
        position: fixed; right: 8px; top: 50%; transform: translateY(-50%);
        z-index: 40; padding: 12px 8px;
        background: rgba(10,10,22,0.9);
        backdrop-filter: blur(16px) saturate(180%);
        -webkit-backdrop-filter: blur(16px) saturate(180%);
        border: 1px solid rgba(255,255,255,0.1);
        border-radius: 28px;
        box-shadow: 0 10px 30px rgba(0,0,0,0.5);
      }
      .volume-float .vf-value {
        font-size: .78rem; font-weight: 700;
        color: var(--accent-primary);
        font-variant-numeric: tabular-nums;
      }
      .volume-float .vol-vertical {
        writing-mode: vertical-lr; direction: rtl;
        -webkit-appearance: slider-vertical;
        width: 28px; height: 180px; margin: 0; padding: 0;
        background: transparent;
        accent-color: var(--accent-primary);
      }
    }

    @media (prefers-reduced-motion: reduce) {
      .nav-btn, .mode-btn, .btn-glass, .control-card, .zone-box, .node-chip,
      .gear, .tab-content, input, select, .checkbox,
      .ota-modal, .ota-modal-panel, .ota-step, .ota-progress-fill, .ota-step-dot,
      .ota-close, .btn-primary, .ota-node-item, .ota-target-row {
        transition: none !important;
        animation: none !important;
      }
    }
  </style>
</head>
<body>
  <div id="top-loading-bar" aria-hidden="true"></div>
  <div class="galaxy-bg" aria-hidden="true">
    <div class="orb orb-1"></div>
    <div class="orb orb-2"></div>
    <div class="orb orb-3"></div>
    <div class="stars"></div>
  </div>

  <div class="dashboard-container">
    <div class="sticky-shell">
      <!-- Row 1: title, nav tabs, player icon all on one line. -->
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
          <button type="button" class="nav-btn"        onclick="openTab(event, 'tab-status')">Status</button>
          <button type="button" class="nav-btn"        onclick="openTab(event, 'tab-source')">Source</button>
          <button type="button" class="nav-btn"        onclick="openUpdateWizard()">Update</button>
        </nav>

        <div class="top-actions">
          <a class="icon-btn" id="open-player-btn" href="/live"
             target="_blank" rel="noopener"
             title="Open Local Player" aria-label="Open Local Player">&#9654;</a>
        </div>
      </div>

      <!-- Row 2: mode + source, then the master slider, then mute on the right.
           Everything stays pinned with the shell as the page scrolls. -->
      <div class="control-row">
        <select id="mode-select" class="mode-select" aria-label="Playback mode">
          <option value="off">Off</option>
          <option value="music">Music</option>
          <option value="movie">Movie</option>
          <option value="quiet">Quiet</option>
        </select>
        <select id="source-select" class="mode-select" aria-label="Audio source">
          <option value="BT">BT</option>
          <option value="AUX">AUX</option>
          <option value="USB">USB</option>
        </select>
        <span class="master-label">Master <b id="volume-label">100%</b></span>
        <input type="range" id="volume" min="0" max="100" value="100">
        <button type="button" class="icon-btn" id="btn-mute"
                onclick="toggleMute()"
                title="Mute" aria-label="Toggle mute">
          <span id="mute-icon">&#128266;</span>
        </button>
      </div>

      <!-- Row 3: CPU / RAM / PSRAM stats, always visible in the sticky shell -->
      <div class="stat-row" aria-label="System load">
        <span class="stat-pill" id="stat-core0" title="Core 0 CPU load">C0 --%</span>
        <span class="stat-pill" id="stat-core1" title="Core 1 CPU load">C1 --%</span>
        <span class="stat-pill" id="stat-ram"   title="Internal heap used">RAM --%</span>
        <span class="stat-pill" id="stat-psram" title="PSRAM used" style="display:none">PS --%</span>
      </div>
    </div>

    <!-- Phone-only vertical master volume (fixed to right edge) -->
    <div class="volume-float" id="volumeFloat">
      <span class="vf-value" id="volume-float-label">100</span>
      <input type="range" id="volume-mobile" class="vol-vertical"
             min="0" max="100" value="100" aria-label="Master volume">
    </div>

    <div id="tab-zones" class="tab-content active">
      <div class="zone-toolbar">
        <h2 class="section-title">Zone Architecture</h2>
        <button type="button" class="btn-glass"
                onclick="createNewZone()"
                style="border-color: var(--accent-primary); color: var(--accent-primary);">
          + Add Room
        </button>
      </div>
      <div class="zone-container" id="zonesContainer">
        <div class="control-card">
          <p class="muted">Loading zone map...</p>
        </div>
      </div>
    </div>

    <div id="tab-settings" class="tab-content">
      <input type="hidden" id="net-server-name" name="server_name" value="">
      <h2 class="section-title">Wi-Fi</h2>
      <div class="control-card">
        <div class="field">
          <label for="net-ssid">Network name</label>
          <div style="display:flex;gap:8px">
            <input type="text" id="net-ssid" name="ssid"
                   autocomplete="on" autocapitalize="off" spellcheck="false">
            <button type="button" class="btn-glass" id="net-scan-btn"
                    style="flex:0 0 auto;white-space:nowrap;">Scan</button>
          </div>
        </div>
        <div class="field">
          <label for="net-pass">Password</label>
          <input type="password" id="net-pass" name="password" autocomplete="off">
        </div>
      </div>

      <div class="control-card">
        <label class="checkbox">
          <input type="checkbox" id="net-use-static" name="use_static_ip">
          <span>Use a static IP</span>
        </label>
        <div id="net-static-fields" hidden style="margin-top:12px">
          <div class="field">
            <label for="net-ip">IP address</label>
            <input type="text" id="net-ip" name="ip" inputmode="decimal" autocomplete="on">
          </div>
          <div class="field">
            <label for="net-gw">Gateway</label>
            <input type="text" id="net-gw" name="gateway" inputmode="decimal" autocomplete="on">
          </div>
          <div class="field">
            <label for="net-sn">Subnet mask</label>
            <input type="text" id="net-sn" name="subnet" inputmode="decimal" autocomplete="on">
          </div>
          <div class="field">
            <label for="net-dns">DNS server</label>
            <input type="text" id="net-dns" name="dns" inputmode="decimal" autocomplete="on">
          </div>
        </div>
      </div>

      <div class="control-card" id="net-test-card" style="display:none;">
        <div class="data-label">Connection Test</div>
        <div class="data-value" id="net-test-state" style="font-size:1rem;">—</div>
      </div>

      <div style="display:flex;gap:10px;margin-top:20px;">
        <button type="button" class="btn-glass" id="net-revert-btn" style="flex:1;">Revert</button>
        <button type="button" class="btn-primary" id="net-save-btn" style="flex:2;">Save &amp; Reconnect</button>
      </div>
    </div>

     <div id="tab-status" class="tab-content">
      <h2 class="section-title">Stream Telemetry</h2>

      <!-- Basic view: audio + network + processing essentials only. These
           are the six numbers you check first when something sounds wrong.
           Everything else lives behind the advanced toggle below. -->
      <div class="grid-2">
        <div class="control-card">
          <div class="data-label">Playback Mode</div>
          <div class="data-value" id="stat-mode">--</div>
        </div>
        <div class="control-card">
          <div class="data-label">Effective Buffer</div>
          <div class="data-value" id="stat-buffer">-- <span style="font-size:0.9rem;color:var(--text-muted);">ms</span></div>
        </div>
        <div class="control-card">
          <div class="data-label">Latency Adjustment</div>
          <div class="data-value" id="stat-delay">-- <span style="font-size:0.9rem;color:var(--text-muted);">ms</span></div>
        </div>
        <div class="control-card">
          <div class="data-label">Avg Latency (capture→send)</div>
          <div class="data-value" id="stat-latency">-- <span style="font-size:0.9rem;color:var(--text-muted);">ms</span></div>
        </div>
        <div class="control-card">
          <div class="data-label">Clients</div>
          <div class="data-value" id="stat-clients">--</div>
        </div>
        <div class="control-card">
          <div class="data-label">Master</div>
          <div class="data-value" id="stat-master" style="font-size:1.1rem;">--</div>
        </div>
      </div>

      <div class="log-controls" style="margin-top:16px;align-items:center;">
        <button type="button" class="btn-glass" id="status-advanced-toggle">
          Show advanced
        </button>
        <span class="muted" id="status-advanced-state">basic view</span>
      </div>

      <div id="status-advanced-panel" style="display:none;">
        <div class="grid-2" style="margin-top:16px;">
          <div class="control-card" style="grid-column: span 2;">
            <div class="data-label">Node Latencies</div>
            <p style="font-size:0.85rem;color:var(--text-muted);margin-top:5px;" id="stat-nodes">Fetching block...</p>
          </div>
          <div class="control-card" style="grid-column: span 2;">
            <div class="data-label">Diagnostics</div>
            <p style="font-size:0.85rem;color:var(--text-muted);margin-top:5px;" id="stat-diag">Fetching block...</p>
          </div>
          <div class="control-card" style="grid-column: span 2;">
            <div class="data-label">Audio Pipeline (last window)</div>
            <p style="font-size:0.85rem;color:var(--text-muted);margin-top:5px;" id="stat-pipeline">Fetching block...</p>
          </div>
        </div>

        <div class="data-label" style="margin-top:18px;">Raw /api/stats JSON</div>
        <pre id="status-raw-json"
             style="font-size:0.78rem;max-height:40vh;overflow:auto;
                    background:rgba(0,0,0,0.35);padding:10px;
                    border-radius:10px;border:1px solid rgba(255,255,255,0.08);
                    white-space:pre-wrap;margin-top:8px;"
        >(waiting for poll…)</pre>
      </div>

       <div class="log-controls">
        <button type="button" class="btn-glass" id="status-log-pause">Pause</button>
        <button type="button" class="btn-glass" id="status-log-clear">Clear</button>
        <span class="muted" id="status-log-state">polling</span>
      </div>

      <!-- Advanced logging. The device runs at error-only (level 1) by
           default, so the Arduino serial monitor shows only the boot
           trace and any real errors. This button switches to verbose
           (level 3), which turns on the periodic per-subsystem dumps
           in loop() and the UART RX line echo. Use it while diagnosing,
           then switch back. -->
      <div class="log-controls" style="margin-top:14px;align-items:center;">
        <button type="button" class="btn-glass" id="status-advanced-log">
          Enable advanced logging
        </button>
        <span class="muted" id="status-advanced-log-state">off (errors only)</span>
      </div>

      <!-- Live serial log. Hidden until advanced logging is enabled. The
           ring buffer on the device only captures lines emitted while the
           level gate lets them through, so with advanced logging off this
           viewer would stay empty even if it were visible. -->
      <div id="status-log-viewer"
           style="display:none;margin-top:14px;">
        <div class="data-label">Serial Log (live)</div>
        <pre id="status-log-pre"
             style="font-size:0.78rem;max-height:40vh;overflow:auto;
                    background:rgba(0,0,0,0.35);padding:10px;
                    border-radius:10px;border:1px solid rgba(255,255,255,0.08);
                    white-space:pre-wrap;margin-top:8px;"
        >(waiting for lines…)</pre>
        <div class="log-controls" style="margin-top:8px;">
          <button type="button" class="btn-glass"
                  id="status-logview-clear">Clear</button>
          <span class="muted" id="status-logview-state">idle</span>
        </div>
      </div>
    </div>

    <div id="tab-source" class="tab-content">
      <h2 class="section-title">Source Board</h2>
      <div class="grid-2">
        <div class="control-card">
          <div class="data-label">Link</div>
          <div class="data-value" id="src-link">--</div>
        </div>
        <div class="control-card">
          <div class="data-label">BT State</div>
          <div class="data-value" id="src-bt">--</div>
        </div>
        <div class="control-card">
          <div class="data-label">Connected MAC</div>
          <div class="data-value" id="src-mac" style="font-size:1rem;">--</div>
        </div>
        <div class="control-card">
          <div class="data-label">Volume (source-side)</div>
          <div class="data-value" id="src-vol">--</div>
        </div>
        <div class="control-card" style="grid-column: span 2;">
          <div class="data-label">Device List</div>
          <p style="font-size:0.85rem;color:var(--text-muted);margin-top:5px;" id="src-devices">Fetching…</p>
        </div>
        <div class="control-card" style="grid-column: span 2;">
          <div class="data-label">Diagnostics</div>
          <p style="font-size:0.85rem;color:var(--text-muted);margin-top:5px;" id="src-diag">Fetching…</p>
        </div>
      </div>

      <!-- Kick current BT device: only visible when source=BT AND a device
           is connected. Toggled by fetchSourceInfo() below. -->
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
  </div>

  <!-- ===================== OTA wizard modal ===================== -->
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

        <!-- Step 1 -->
        <section class="ota-step" data-step="1">
          <h3 id="otaModalTitle">Mute audio before updating</h3>
          <p>Audio will be muted on this server and on every selected player node
            while the update runs. The devices reboot when flashing finishes.</p>
          <label class="checkbox" style="margin-top:6px;">
            <input type="checkbox" id="otaRestoreMute" checked style="width:auto;">
            <span>Restore previous mute state when finished</span>
          </label>
        </section>

        <!-- Step 2 -->
        <section class="ota-step" data-step="2">
          <h3>Select bin file</h3>
          <p>Choose a single <code>node_&lt;version&gt;.bin</code> to flash player
             nodes, or a <code>server_&lt;version&gt;.bin</code> to flash this server.
             The version is read from the filename.</p>

          <input type="file" id="otaFiles" accept=".bin" style="display:none">
          <button type="button" class="btn-glass" id="otaPickFiles">Choose file…</button>

          <div id="otaUploadBar" class="ota-progress-bar" style="display:none;margin-top:16px;">
            <div class="ota-progress-fill" id="otaUploadBarFill"></div>
          </div>
          <div id="otaUploadMsg" class="muted" style="margin-top:12px;">No file uploaded yet.</div>

          <div id="otaCurrentWrap" class="ota-current-wrap">
            <label class="checkbox">
              <input type="checkbox" id="otaUseCurrent" style="width:auto;">
              <span>Use currently saved node firmware<b id="otaCurrentVer">v–</b></span>
            </label>
          </div>
        </section>

        <!-- Step 3 -->
        <section class="ota-step" data-step="3">
          <h3>Select targets</h3>
          <p id="otaTargetHint">--</p>

          <div id="otaServerRow" class="ota-target-row">
            <label class="checkbox">
              <input type="checkbox" id="otaDoServer" style="width:auto;">
              <span>Update <b>this server</b> (currently v<b id="otaServerVer">--</b>)</span>
            </label>
            <div id="otaServerFile" class="ota-server-meta"></div>
          </div>

          <div class="ota-section-label ota-node-header">
            <span>Player nodes</span>
            <label class="checkbox ota-select-all">
              <input type="checkbox" id="otaSelectAll">
              <span>Select all</span>
            </label>
          </div>
          <div id="otaNodeList" class="ota-targets-list"></div>
        </section>

        <!-- Step 4 -->
        <section class="ota-step" data-step="4">
          <h3>Applying update</h3>
          <div id="otaNodeStatusList" class="ota-targets-list" style="max-height: 300px;">
            <!-- Dynamically populated -->
          </div>
        </section>

      </div>

      <div class="ota-modal-footer">
        <button type="button" class="btn-glass"   id="otaBackBtn" onclick="otaBack()">Back</button>
        <button type="button" class="btn-primary" id="otaNextBtn" onclick="otaNext()">OK</button>
      </div>

    </div>
  </div>

  <!-- ===================== Wi-Fi scan modal ===================== -->
  <div id="wifiScanModal" class="wifi-modal" aria-hidden="true">
    <div class="wifi-modal-backdrop" onclick="closeWifiScan()"></div>
    <div class="wifi-modal-panel" role="dialog" aria-modal="true" aria-label="Available networks">
      <div class="wifi-modal-header">
        <strong>Available networks</strong>
        <button type="button" class="ota-close" onclick="closeWifiScan()" aria-label="Close">&times;</button>
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
  <script>

    /* ====================================================================
     * Dashboard behaviour
     * ==================================================================*/
    // ---- Global loading bar -----------------------------------------
    // Every api() call increments on entry and decrements on exit. Bar
    // is visible while the counter is > 0, so back-to-back requests do
    // not flicker it on and off.
    let __loadingCount = 0;
    function showLoading() {
      __loadingCount++;
      const bar = document.getElementById('top-loading-bar');
      if (!bar) return;
      bar.classList.remove('done');
      bar.classList.add('active');
    }
    function hideLoading() {
      __loadingCount = Math.max(0, __loadingCount - 1);
      if (__loadingCount > 0) return;
      const bar = document.getElementById('top-loading-bar');
      if (!bar) return;
      bar.classList.remove('active');
      bar.classList.add('done');
      setTimeout(() => {
        if (__loadingCount === 0) bar.classList.remove('done');
      }, 500);
    }

    // Background pollers (refreshStatus, fetchHeavyStats, fetchSourceInfo)
    // run on a timer and are not user-initiated. Passing {silent:true}
    // suppresses the top loading bar for those, so it only flashes when the
    // operator actually did something. Without this the bar pulsed every
    // 2 s while a status tab was open, which read as "the dashboard keeps
    // reloading itself".
    async function api(path, method = 'GET', data, opts = {}) {
      const fetchOpts = { method, headers: {} };
      if (data) {
        fetchOpts.headers['Content-Type'] = 'application/json';
        fetchOpts.body = JSON.stringify(data);
      }
      if (!opts.silent) showLoading();
      try {
        const res = await fetch(path, fetchOpts);
        return await res.json();
      } catch (e) {
        return { ok: false };
      } finally {
        if (!opts.silent) hideLoading();
      }
    }

    // Strip anything that isn't a plain ASCII letter or digit. Mirrors
    // sanitizeServerName() / sanitizeNodeNameStrict() on the C++ side:
    // no spaces, no punctuation, no unicode. Applied on every keystroke
    // so an invalid character can never appear in the field, and again
    // before POSTing so a race or a programmatic setter can't slip one
    // through.
    function sanitizeName(s) {
      return (s || '').replace(/[^A-Za-z0-9]/g, '');
    }

    // Plain-text setter. Use this instead of updateText() for anything
    // sourced from a user-configurable string. updateText() uses
    // innerHTML because some callers pass markup, which is fine for
    // internally-built strings but is an XSS footgun if a user value
    // ever reaches it unsanitised.
    function setText(id, text) {
      const el = document.getElementById(id);
      if (el && el.textContent !== text) el.textContent = text;
    }

    let isMuted       = false;
    let currentTabId  = 'tab-zones';
    let playbackMode  = 'off';
    let currentSource = 'BT';
    let zonesData     = [];
    let zonesKey      = '';
    let activeDrags   = { volume: false };
    let pendingDefaultSourceSave = null;
    let pendingModeSave = null;

    async function setSource(src) {
      const r = await api('/api/source', 'POST', { source: src });
      if (r && r.ok) {
        currentSource = src;
        // Persist as the boot default after a short idle, NOT on every
        // tap. Prevents an NVM write per selection when the user is
        // cycling through options.
        if (pendingDefaultSourceSave) clearTimeout(pendingDefaultSourceSave);
        pendingDefaultSourceSave = setTimeout(async () => {
          pendingDefaultSourceSave = null;
          await api('/api/config', 'POST', { defaultSource: currentSource });
        }, 6000);
      } else {
        const el = document.getElementById('source-select');
        if (el) el.value = currentSource;
      }
    }

    function syncSourceFromState(s) {
      const src = (s && s.receiverAudio && s.receiverAudio.source) || '';
      if (src && src !== currentSource) {
        currentSource = src;
        const el = document.getElementById('source-select');
        if (el && el.value !== src) el.value = src;
      }
      const m = s && s.mode;
      if (!pendingModeSave && m && m !== playbackMode) {
        playbackMode = m;
        const sel = document.getElementById('mode-select');
        if (sel && sel.value !== m) sel.value = m;
      }
    }

    function updateText(id, text) {
      const el = document.getElementById(id);
      if (el && el.innerHTML !== text) el.innerHTML = text;
    }

    async function fetchHeavyStats() {
      const s = await api('/api/stats', 'GET', undefined, { silent: true });
      if (!s || !s.ok) return;
      const ms = (v) => (v || 0) + ' <span style="font-size:0.9rem;color:var(--text-muted);">ms</span>';
      updateText('stat-latency', (s.avgLatencyMs || 0).toFixed(1) + ' <span style="font-size:0.9rem;color:var(--text-muted);">ms</span>');
      updateText('stat-buffer',  ms(s.audioBufferMs));
      updateText('stat-delay',   ms(s.latencyAdjustmentMs));
      updateText('stat-mode',    s.mode || 'off');
      updateText('stat-clients', s.wsClients);
      updateText('stat-master', (s.masterVolume || 0) + '%' +
        (s.masterMuted ? ' <span style="color:var(--danger);">(muted)</span>' : ''));

      let nodeHtml = '';
      (s.nodes || []).forEach(n => {
        nodeHtml += '<div>' + n.ip + ' — ' + n.latencyMs +
                    ' ms (queue ' + n.queueDepth + '/' + (n.target || '?') +
                    ', rebuffers ' + n.rebuffers + ')</div>';
      });
      updateText('stat-nodes', nodeHtml || 'No nodes reporting yet');
      const dropped = (s.streaming && s.streaming.opusDroppedLast5s) || 0;
      // Incoming Quality reads the live measured input rate out of the
      // resampler ratio. "44100 Hz ✓" means the source board is clocking at
      // nominal. Anything off by more than ~0.1 % is annotated with the
      // deviation so a slow drift is visible without needing to read the
      // raw ppm number in the "Clock drift" line above it.
      const inHz  = s.measuredInputHz || 0;
      const nominalHz = 44100;
      let inQuality;
      if (inHz <= 0) {
        inQuality = '—';
      } else {
        const devPct = ((inHz - nominalHz) / nominalHz) * 100;
        const devTag = Math.abs(devPct) < 0.05
          ? ' <span style="color:var(--success);">✓ nominal</span>'
          : ` <span style="color:var(--text-muted);">(${devPct >= 0 ? '+' : ''}${devPct.toFixed(2)} %)</span>`;
        inQuality = inHz.toFixed(0) + ' Hz' + devTag;
      }
      updateText('stat-diag',
    `I2S Read Errors: ${s.i2sReadErrors} <br>` +
    `Dropped Opus (5s): ${dropped} <br>` +
    `Avg Encode Time: ${s.avgEncodeUs || 0} us <br>` +
    `Clock drift: ${s.clockDriftPpm} ppm <br>` +
    `Incoming Quality: ${inQuality} <br>` +
    `Input path: ${s.serverInputSource || 'I2S'}` +
    (s.serverInputSource === 'USB'
      ? ` (${((s.usbBytesLastWin||0)/1024).toFixed(1)} KiB win)`
      : '') +
    `<br>RSSI: ${s.wifiRssi}dBm`);
         updateText('stat-pipeline',
        `rawQueue:   ${s.rawQueueDepth} / ${s.rawQueueLen} <br>` +
        `opusQueue:  ${s.opusQueueDepth} / ${s.opusQueueLen} <br>` +
        `Opus encoded (win): ${s.opusEncodedLastWin} <br>` +
        `Opus dropped (win): ${s.opusDroppedLastWin} <br>` +
        `WS sent (win): ${s.wsSentLastWin}  ${((s.wsBytesLastWin||0)/1024).toFixed(1)} KiB`);

      // Raw dump for the advanced panel. Cheap (a stringify of a few KB
      // object) and only written when the element exists.
      const rawEl = document.getElementById('status-raw-json');
      if (rawEl) rawEl.textContent = JSON.stringify(s, null, 2);
    }

    window.openTab = function (evt, tabId) {
      currentTabId = tabId;
      document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
      document.querySelectorAll('.nav-btn').forEach(el => el.classList.remove('active'));
      const panel = document.getElementById(tabId);
      if (panel) panel.classList.add('active');
      if (evt && evt.currentTarget) evt.currentTarget.classList.add('active');
      if (tabId === 'tab-status')  fetchHeavyStats();
      if (tabId === 'tab-source')  fetchSourceInfo();
      if (tabId === 'tab-settings') loadNetworkState();
    };

    async function setMode(m) {
      const sel = document.getElementById('mode-select');
      if (sel && sel.value !== m) sel.value = m;
      playbackMode = m;

      if (pendingModeSave) clearTimeout(pendingModeSave);
      pendingModeSave = setTimeout(async () => {
        pendingModeSave = null;
        await api('/api/config', 'POST', { mode: playbackMode });
      }, 1000);
    }

    async function toggleMute() {
      isMuted = !isMuted;
      setMuteButtonUI(isMuted);
      await api('/api/config', 'POST', { masterMuted: isMuted });
      refreshStatus();
    }

    function setMuteButtonUI(muted) {
      const btn  = document.getElementById('btn-mute');
      const icon = document.getElementById('mute-icon');
      if (!btn || !icon) return;
      btn.classList.toggle('active', muted);
      btn.classList.toggle('danger', muted);
      btn.title = muted ? 'Unmute' : 'Mute';
      // &#128266; is the speaker emoji, &#128263; is the muted-speaker emoji.
      icon.textContent = muted ? '\u{1F507}' : '\u{1F50A}';
    }

    // ---- Server name modal ------------------------------------------
    window.openNameModal = function () {
      const modal = document.getElementById('nameModal');
      const input = document.getElementById('edit-name-input');
      if (!modal || !input) return;
      const current = (document.getElementById('server-title-text')?.textContent || 'Enkelvoud').trim();
      input.value = current;
      modal.classList.add('open');
      modal.setAttribute('aria-hidden', 'false');
      setTimeout(() => { input.focus(); input.select(); }, 100);
    };

    window.closeNameModal = function () {
      const modal = document.getElementById('nameModal');
      if (modal) {
        modal.classList.remove('open');
        modal.setAttribute('aria-hidden', 'true');
      }
    };

    window.saveName = async function () {
      const input = document.getElementById('edit-name-input');
      if (!input) return;
      const newName = (input.value || '').replace(/[^A-Za-z0-9]/g, '');
      if (!newName) { alert('Server name is required.'); return; }
      if (newName.length > 32) { alert('Server name must be 32 characters or fewer.'); return; }
      const r = await api('/api/config', 'POST', { serverName: newName });
      if (!r || !r.ok) {
        alert('Could not save name: ' + ((r && r.error) || 'unknown error'));
        return;
      }
      const titleEl = document.getElementById('server-title-text');
      if (titleEl) titleEl.textContent = newName;
      const hiddenNet = document.getElementById('net-server-name');
      if (hiddenNet) hiddenNet.value = newName;
      closeNameModal();
      if (confirm('Name saved. Reboot now so the new .local address takes effect?')) {
        fetch('/api/restart', { method: 'POST' }).catch(() => {});
      }
    };

    // Enter key inside the name input triggers save.
    (function () {
      const nameInput = document.getElementById('edit-name-input');
      if (!nameInput) return;
      nameInput.addEventListener('keydown', function (e) {
        if (e.key === 'Enter') { e.preventDefault(); saveName(); }
      });
    })();

    async function refreshStatus() {
      if (document.hidden) return;
      const s = await api('/api/state', 'GET', undefined, { silent: true });
      if (!s || !s.ok) return;
      setText('server-title-text', s.serverName || 'Enkelvoud');
      
      // CPU / RAM / PSRAM stat pills
      (function paintLoadPills() {
        const c0 = document.getElementById('stat-core0');
        const c1 = document.getElementById('stat-core1');
        const rm = document.getElementById('stat-ram');
        const ps = document.getElementById('stat-psram');

        function setPill(el, text, pct) {
          if (!el) return;
          el.textContent = text;
          el.classList.remove('warn', 'crit');
          if (typeof pct === 'number' && pct >= 0) {
            if (pct >= 85)      el.classList.add('crit');
            else if (pct >= 65) el.classList.add('warn');
          }
        }

        if (s.cpuCore0Pct !== undefined && s.cpuCore0Pct >= 0)
          setPill(c0, 'C0 ' + s.cpuCore0Pct + '%', s.cpuCore0Pct);
        else
          setPill(c0, 'C0 n/a', -1);

        if (s.cpuCore1Pct !== undefined && s.cpuCore1Pct >= 0)
          setPill(c1, 'C1 ' + s.cpuCore1Pct + '%', s.cpuCore1Pct);
        else
          setPill(c1, 'C1 n/a', -1);

        if (s.heapUsedPct !== undefined)
          setPill(rm, 'RAM ' + s.heapUsedPct + '%', s.heapUsedPct);

        if (ps) {
          if (s.psramTotalBytes > 0) {
            ps.style.display = '';
            setPill(ps, 'PS ' + (s.psramUsedPct || 0) + '%', s.psramUsedPct || 0);
          } else {
            ps.style.display = 'none';
          }
        }
      })();
      if (!activeDrags.volume) {
        const v = s.masterVolume;
        const volD = document.getElementById('volume');
        const volM = document.getElementById('volume-mobile');
        const lblD = document.getElementById('volume-label');
        const lblM = document.getElementById('volume-float-label');
        if (volD && document.activeElement !== volD && volD.value !== String(v)) volD.value = v;
        if (volM && document.activeElement !== volM && volM.value !== String(v)) volM.value = v;
        if (lblD) lblD.textContent = v + '%';
        if (lblM) lblM.textContent = v;
      }
        if (!pendingModeSave && s.mode && s.mode !== playbackMode) {
        playbackMode = s.mode;
        const sel = document.getElementById('mode-select');
        if (sel && sel.value !== s.mode) sel.value = s.mode;
      }
      if (s.masterMuted !== isMuted) {
        isMuted = s.masterMuted;
        setMuteButtonUI(isMuted);
      }
      if (s.zones) {
        const k = JSON.stringify(s.zones);
        if (k !== zonesKey) { zonesKey = k; zonesData = s.zones; renderZones(); }
      }
      syncSourceFromState(s);
    }

    function renderZones() {
      const container = document.getElementById('zonesContainer');
      if (!container) return;
      let html = '';
      zonesData.forEach((zone) => {
        const safeName = (zone.name || '').replace(/"/g, '&quot;');
        html += `
          <div class="zone-box"
               ondragover="allowDrop(event)"
               ondragleave="removeDropStyle(event)"
               ondrop="dropNode(event, '${zone.id}')">
            <div class="zone-header">
              <input type="text"
                     value="${safeName}"
                     pattern="[A-Za-z0-9]+"
                     maxlength="24"
                     autocomplete="off" autocapitalize="off" spellcheck="false"
                     onbeforeinput="if (event.data && /[^A-Za-z0-9]/.test(event.data)) event.preventDefault()"
                     onchange="updateZoneName('${zone.id}', this.value)"
                     ${zone.id === 'main' ? 'title="Main room cannot be deleted"' : ''}>
              ${zone.id !== 'main'
                ? `<button type="button" class="btn-glass"
                           style="padding:4px 8px;font-size:0.75rem;border-color:var(--danger);color:var(--danger);"
                           onclick="deleteZone('${zone.id}')">Del</button>`
                : ''}
            </div>
            <div class="zone-controls">
              <div class="zone-row">
                <span>Room Volume</span>
                <span id="zv-${zone.id}">${zone.volume}%</span>
              </div>
              <input type="range" min="0" max="100" value="${zone.volume}"
                     oninput="zoneVolLive('${zone.id}', this.value)"
                     onchange="updateZoneVolume('${zone.id}', this.value)">
              <div class="zone-actions">
                <button type="button"
                        class="btn-glass ${zone.muted ? 'btn-mute-active' : ''}"
                        style="padding:5px 12px;font-size:0.78rem;"
                        onclick="toggleZoneMute('${zone.id}')">
                  ${zone.muted ? 'Unmute' : 'Mute Room'}
                </button>
              </div>
            </div>
            <div style="flex-grow:1;min-height:50px;">
        `;
        const nodes = (zone.nodes && zone.nodes.length) ? zone.nodes : [];
        if (nodes.length > 0) {
            nodes.forEach(node => {
              const safeNodeName = (node.name || 'Node').replace(/"/g, '&quot;');
              const verTag = node.version
                ? ' <span style="color:var(--text-muted);font-weight:400;font-size:0.75rem;">v' +
                  String(node.version).replace(/"/g, '') + '</span>'
                : '';

              // node.known is emitted by appendZoneJson() in EVDSTS.h.
              // false => the node is in this zone's nodeIps list but has
              // been pruned from the players snapshot (no heartbeat for
              // >PLAYER_STALE_TIMEOUT_MS). Default to "online" if the
              // field is missing so an older server never greys everything.
              const isOffline = (node.known === false);
              const chipClass = isOffline ? 'node-chip offline' : 'node-chip';

              // Render the gear as a <span> when offline so there is no
              // href to click at all; CSS also blocks pointer events as
              // belt-and-braces. Drag/drop on the chip is unaffected
              // because it is wired via ondragstart on the chip element,
              // not on the gear.
              const gearHtml = isOffline
                ? '<span class="gear disabled" title="Node offline — settings unavailable" aria-disabled="true">&#9881;</span>'
                : '<a class="gear" href="http://' + node.ip + '/" title="Open this node\'s settings">&#9881;</a>';

              // When offline, node.name is empty (the node has been pruned
              // from the players snapshot, so EVDSTS.h's appendZoneJson()
              // falls back to the IP). Show "Offline" as the primary label
              // so the chip reads clearly as a dead endpoint rather than
              // duplicating the IP in both lines.
              const displayName = isOffline ? 'Offline' : safeNodeName;

              html += `
                <div class="${chipClass}"
                    draggable="true"
                    ondragstart="dragNode(event, '${node.ip}')">
                  <div class="node-meta">
                    <strong>${displayName}${verTag}</strong>
                    <span class="node-ip">${node.ip}</span>
                  </div>
                  ${gearHtml}
                </div>
              `;
            });
        } else {
          html += `<p style="font-size:0.8rem;color:var(--text-muted);text-align:center;margin-top:10px;">Drag nodes here</p>`;
        }
        html += `</div></div>`;
      });
      container.innerHTML = html;
    }

    function zoneVolLive(zoneId, value) {
      const el = document.getElementById('zv-' + zoneId);
      if (el) el.textContent = value + '%';
    }
    function dragNode(ev, ip) { ev.dataTransfer.setData('text/plain', ip); }
    function allowDrop(ev) { ev.preventDefault(); ev.currentTarget.classList.add('drag-over'); }
    function removeDropStyle(ev) { ev.currentTarget.classList.remove('drag-over'); }
    async function dropNode(ev, zoneId) {
      ev.preventDefault();
      ev.currentTarget.classList.remove('drag-over');
      const ip = ev.dataTransfer.getData('text/plain');
      await api('/api/zones/move', 'POST', { ip: ip, zoneId: zoneId });
      refreshStatus();
    }
    async function createNewZone() {
      const raw = prompt('Enter new room name (letters and numbers only):',
                         'Zone' + (zonesData.length + 1));
      if (raw === null) return;
      const name = sanitizeName(raw);
      if (name.length === 0) { alert('Please use letters and numbers only.'); return; }
      const r = await api('/api/zones/create', 'POST', { name: name });
      if (r && r.ok) await refreshStatus();
    }
    async function deleteZone(zoneId) {
      if (!confirm('Delete this room? Unassigned nodes will move back to Main Room.')) return;
      await api('/api/zones/delete', 'POST', { zoneId: zoneId });
      refreshStatus();
    }
    async function updateZoneName(zoneId, name) {
      const clean = sanitizeName(name);
      if (clean.length === 0) {
        // Everything was stripped. Re-render from server state so the
        // field doesn't sit on a blank value the user can't undo.
        refreshStatus();
        return;
      }
      await api('/api/zones/config', 'POST', { zoneId: zoneId, name: clean });
    }
    async function updateZoneVolume(zoneId, volume) {
      await api('/api/zones/config', 'POST', { zoneId: zoneId, volume: parseInt(volume, 10) });
    }
    async function toggleZoneMute(zoneId) {
      const z = zonesData.find(z => z.id === zoneId);
      if (!z) return;
      await api('/api/zones/config', 'POST', { zoneId: zoneId, muted: !z.muted });
      refreshStatus();
    }

    /* ====================================================================
     * Firmware update wizard
     * ==================================================================*/
    (function () {
      const modal        = document.getElementById('otaModal');
      const steps        = modal.querySelectorAll('.ota-step');
      const dots         = modal.querySelectorAll('.ota-step-dot');
      const backBtn      = document.getElementById('otaBackBtn');
      const nextBtn      = document.getElementById('otaNextBtn');
      const filesInput   = document.getElementById('otaFiles');
      const msgEl        = document.getElementById('otaUploadMsg');
      const uploadBar    = document.getElementById('otaUploadBar');
      const uploadFill   = document.getElementById('otaUploadBarFill');
      const currentWrap  = document.getElementById('otaCurrentWrap');
      const useCurrentCb = document.getElementById('otaUseCurrent');
      const currentVer   = document.getElementById('otaCurrentVer');
      const nodeListEl   = document.getElementById('otaNodeList');
      const serverRow    = document.getElementById('otaServerRow');
      const serverFileEl = document.getElementById('otaServerFile');
      let verifyStartTime = 0;
      window.__otaTargets = null;
      const selectAllCb  = document.getElementById('otaSelectAll'); // [ota-select-all]

      const TOTAL_STEPS = 4;

      let currentStep      = 1;
      let plan             = null;
      let pollTimer        = null;
      let prevMuted        = false;
      let muteApplied      = false;
      let restoreMute      = true;
      let step4Done        = false;
      let uploadedOK       = false;
      let uploadedKind     = null;
      let useCurrent       = false;
      let selectedKind     = null;

      function setMsg(t) { if (msgEl) msgEl.textContent = t; }

      function applyMuteUI(muted) {
        isMuted = muted;
        setMuteButtonUI(muted);
      }
      async function restoreMuteIfNeeded() {
        if (!muteApplied || !restoreMute) return;
        await api('/api/config', 'POST', { masterMuted: prevMuted });
        applyMuteUI(prevMuted);
        muteApplied = false;
      }

      function pickFileByKind(kind) {
        if (!plan || !plan.files) return null;
        let best = null;
        for (const f of plan.files) {
          if (f.kind !== kind) continue;
          if (!best ||
              f.version.localeCompare(best.version, undefined, {numeric:true}) > 0)
            best = f;
        }
        return best;
      }

      /* ---- step 2: Next enable state + "use current" visibility ---- */
      function updateStep2() {
        const nf = pickFileByKind('node');
        const showCurrent = !!nf && !(uploadedOK && uploadedKind === 'node');
        if (showCurrent) {
          currentWrap.classList.add('visible');
          currentVer.textContent = 'v' + (nf.version || '?');
        } else {
          currentWrap.classList.remove('visible');
          if (useCurrentCb) useCurrentCb.checked = false;
          useCurrent = false;
        }
        if (currentStep === 2) {
          const canProceed = uploadedOK || (useCurrent && !!nf);
          nextBtn.disabled = !canProceed;
        }
      }

      /* ---- step navigation ---- */
      function setStep(n) {
        currentStep = n;
        steps.forEach(s => s.classList.toggle('active', +s.dataset.step === n));
        dots.forEach(d => {
          const ds = +d.dataset.step;
          d.classList.toggle('active', ds === n);
          d.classList.toggle('done',   ds <  n);
        });

        backBtn.style.display = (n > 1 && n < TOTAL_STEPS) ? '' : 'none';

        if (n < TOTAL_STEPS) {
          nextBtn.style.display = '';
          nextBtn.disabled = false;
          nextBtn.textContent =
            n === 1 ? 'OK' :
            n === 2 ? 'Next' :
                      'Start update';
        } else {
          nextBtn.style.display = step4Done ? '' : 'none';
          nextBtn.textContent = 'Close';
          nextBtn.disabled = false;
        }

        if (n === 2) { refreshPlan(); updateStep2(); }
        if (n === 3) { refreshPlan(); }   // one-shot, no background poll
      }

      /* ---- open / close ---- */
      window.openUpdateWizard = async function () {
        modal.classList.add('open');
        modal.setAttribute('aria-hidden', 'false');

        step4Done    = false;
        muteApplied  = false;
        plan         = null;
        uploadedOK   = false;
        uploadedKind = null;
        useCurrent   = false;
        selectedKind = null;
        if (useCurrentCb) useCurrentCb.checked = false;
        if (uploadBar) { uploadBar.style.display = 'none'; uploadFill.style.width = '0%'; }
        window.__otaTargets = null;
        verifyStartTime = 0;
        setMsg('No file uploaded yet.');
        
        // If a job is already running, jump straight to step 4.
        try {
          const st = await (await fetch('/api/ota/status')).json();
          if (st && st.inProgress) {
            window.__otaTargets = { server: false, nodes: [], newVersion: '', kind: 'node', resumed: true };
            verifyStartTime = 0;
            document.getElementById('otaNodeStatusList').innerHTML = `
              <div class="ota-node-item">
                <div class="ota-node-body"><div class="ota-node-name">Update in progress…</div></div>
              </div>`;
            setStep(4);
            if (!pollTimer) pollTimer = setInterval(pollStatus, 1000);
            return;
          }
        } catch (e) { /* fall through */ }

        try {
          const s = await api('/api/state');
          prevMuted = !!(s && s.masterMuted);
        } catch (e) {
          prevMuted = isMuted;
        }
        const cb = document.getElementById('otaRestoreMute');
        if (cb) cb.checked = !prevMuted;

        setStep(1);
      };

      window.otaCancel = async function () {
        modal.classList.remove('open');
        modal.setAttribute('aria-hidden', 'true');
        if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
        await restoreMuteIfNeeded();
        refreshStatus();
      };

      window.otaBack = function () {
        if (currentStep > 1 && currentStep < TOTAL_STEPS) setStep(currentStep - 1);
      };

      window.otaNext = async function () {
        if (currentStep === 1) {
          restoreMute = document.getElementById('otaRestoreMute').checked;
          try {
            await api('/api/config', 'POST', { masterMuted: true });
            applyMuteUI(true);
            muteApplied = true;
          } catch (e) {}
          setStep(2);

        } else if (currentStep === 2) {
          if (useCurrent) selectedKind = 'node';
          else if (uploadedOK) selectedKind = uploadedKind;
          else return;
          setStep(3);

        } else if (currentStep === 3) {
          const nodes = Array.from(modal.querySelectorAll('.otaNodeChk:checked'))
                            .map(c => c.value);
          const doServer = document.getElementById('otaDoServer').checked;
          if (!doServer && !nodes.length) { alert('Nothing selected.'); return; }

          const nf = pickFileByKind('node');
          const sf = pickFileByKind('server');
          let newVer = '';
          if (selectedKind === 'node') newVer = nf ? nf.version : '';
          else if (selectedKind === 'server') newVer = sf ? sf.version : '';

          window.__otaTargets = { nodes, server: doServer, newVersion: newVer, kind: selectedKind };
          verifyStartTime = 0;
          renderStep4List();

          step4Done = false;
          setStep(4);
          
          const ac = new AbortController();
          const to = setTimeout(() => ac.abort(), 30000);

          try {
            const r = await fetch('/api/ota/apply', {
              method: 'POST',
              headers: {'Content-Type':'application/json'},
              body: JSON.stringify({ server: doServer, nodes }),
              signal: ac.signal
            });
            clearTimeout(to);
            if (!r.ok) {
              alert('Apply failed: HTTP ' + r.status);
              step4Done = true;
              nextBtn.style.display = '';
              return;
            }
          } catch (e) {
            clearTimeout(to);
            const msg = (e && e.name === 'AbortError')
              ? 'server did not answer /api/ota/apply within 30 s'
              : (e && e.message) || 'unknown network error';
            alert('Apply failed: ' + msg);
            step4Done = true;
            nextBtn.style.display = '';
            return;
          }
          if (!pollTimer) pollTimer = setInterval(pollStatus, 1000);
        } else if (currentStep === 4) {
          await otaCancel();
        }
      };

      /* ---- step 2: upload via XHR so we can show a real progress bar ---- */
      async function uploadFile(file) {
        const m = (file.name || '').match(/^(server|node)_[^/]+\.bin$/i);
        if (!m) {
          setMsg('Filename must be server_<version>.bin or node_<version>.bin.');
          return;
        }
        const kind = m[1].toLowerCase();

        uploadedOK   = false;
        uploadedKind = null;

        const pickBtn = document.getElementById('otaPickFiles');
        pickBtn.disabled = true;

        uploadBar.style.display = '';
        uploadFill.style.width = '0%';
        uploadFill.style.background = '';
        setMsg('Uploading ' + file.name + ' …');

        try {
          await new Promise((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/api/ota/upload');
            xhr.upload.onprogress = (e) => {
              if (e.lengthComputable) {
                uploadFill.style.width = ((e.loaded / e.total) * 100).toFixed(0) + '%';
              }
            };
            xhr.onload = () => {
              if (xhr.status >= 200 && xhr.status < 300) resolve();
              else reject(new Error('HTTP ' + xhr.status));
            };
            xhr.onerror = () => reject(new Error('network error'));
            const fd = new FormData();
            fd.append('fw', file, file.name);
            xhr.send(fd);
          });

          uploadFill.style.width = '100%';
          setMsg('Uploaded ' + file.name);
          uploadedOK   = true;
          uploadedKind = kind;

          useCurrent = false;
          if (useCurrentCb) useCurrentCb.checked = false;

          await refreshPlan();
        } catch (e) {
          uploadFill.style.background = 'var(--danger)';
          setMsg('Upload failed: ' + e.message);
        } finally {
          pickBtn.disabled = false;
          updateStep2();
        }
      }

      if (filesInput) document.getElementById('otaPickFiles').onclick = () => filesInput.click();
      if (filesInput) filesInput.onchange = () => {
        const f = filesInput.files && filesInput.files[0];
        if (f) uploadFile(f);
        filesInput.value = '';
      };
      if (useCurrentCb) useCurrentCb.addEventListener('change', () => {
        useCurrent = useCurrentCb.checked;
        updateStep2();
      });

      /* ---- step 3: select-all ------------------------------------------- */
      // [ota-select-all]
      // Recomputes the tri-state of the Select-all checkbox from the
      // individual nodes currently in the list. Called after every list
      // rebuild and after any individual checkbox change. Excludes
      // disabled nodes from both the total and the checked count, so an
      // unreachable node cannot hold the aggregate in the indeterminate
      // state forever and cannot be silently toggled by Select all.
      function syncSelectAllState() {
        if (!selectAllCb) return;
        const enabled = nodeListEl.querySelectorAll(
            '.otaNodeChk:not(:disabled)');
        if (enabled.length === 0) {
          selectAllCb.checked       = false;
          selectAllCb.indeterminate = false;
          selectAllCb.disabled      = true;
          return;
        }
        selectAllCb.disabled = false;
        let nChecked = 0;
        enabled.forEach(cb => { if (cb.checked) nChecked++; });
        selectAllCb.checked       = (nChecked === enabled.length);
        selectAllCb.indeterminate = (nChecked > 0 && nChecked < enabled.length);
      }

      // Clicking Select all applies its new checked state to every
      // enabled node. The event fires after the checkbox's own state has
      // flipped, so `selectAllCb.checked` is already the target state.
      if (selectAllCb) {
        selectAllCb.addEventListener('change', () => {
          const want = selectAllCb.checked;
          nodeListEl.querySelectorAll('.otaNodeChk:not(:disabled)')
                    .forEach(cb => { cb.checked = want; });
          // force a re-sync so indeterminate is cleared/set correctly
          syncSelectAllState();
        });
      }

      // Individual checkbox changes bubble up to the container, so a
      // single delegated listener catches all of them without needing to
      // re-bind after every render().
      if (nodeListEl) {
        nodeListEl.addEventListener('change', (ev) => {
          if (!ev.target || !ev.target.classList) return;
          if (!ev.target.classList.contains('otaNodeChk')) return;
          syncSelectAllState();
        });
      }

      /* ---- step 3: render targets ---- */
      function render() {
        if (!plan) return;

        const sf = pickFileByKind('server');
        const nf = pickFileByKind('node');

        const verEl = document.getElementById('otaServerVer');
        if (verEl) verEl.textContent = plan.serverVersion || '--';

        const serverAllowed = selectedKind === 'server' && !!sf;
        const nodesAllowed  = selectedKind === 'node'   && !!nf;

        if (serverAllowed) {
          serverRow.classList.remove('disabled');
          document.getElementById('otaDoServer').disabled = false;
          serverFileEl.textContent = 'Will flash: ' + sf.name +
                                     '  (' + (sf.size / 1024).toFixed(0) + ' KiB)';
        } else {
          serverRow.classList.add('disabled');
          const cb = document.getElementById('otaDoServer');
          cb.disabled = true;
          cb.checked = false;
          if (selectedKind !== 'server') {
            serverFileEl.textContent = 'Server bin not selected';
          } else {
            serverFileEl.textContent = 'No server bin in spool';
          }
        }

        const hint = document.getElementById('otaTargetHint');
        if (selectedKind === 'node') {
          hint.textContent = 'Player node bin selected — only nodes can be flashed.';
        } else if (selectedKind === 'server') {
          hint.textContent = 'Server bin selected — only this server can be flashed.';
        } else {
          hint.textContent = 'Pick a bin first.';
        }

        if (nodesAllowed) nodeListEl.classList.remove('disabled');
        else               nodeListEl.classList.add('disabled');

        let html = '';
        if (!plan.nodes || !plan.nodes.length) {
          html = '<p class="muted" style="font-size:0.82rem;">No player nodes registered.</p>';
        } else {
          plan.nodes.forEach(n => {
            const canCheck = nodesAllowed && n.reachable;
            const showNewer = nf && nf.version !== 'unknown' && nf.version !== n.version;
            html += `
              <label class="ota-node-item">
                <input type="checkbox" class="otaNodeChk" value="${n.ip}"
                       ${canCheck ? '' : 'disabled'}>
                <div class="ota-node-body">
                  <div class="ota-node-name">${n.name || n.ip}</div>
                  <div class="ota-node-ip">${n.ip}</div>
                </div>
                <div class="ota-node-ver">
                  v${n.version}${showNewer ? ' <span class="new">→ v' + nf.version + '</span>' : ''}
                  ${n.reachable ? '' : '<br><span style="color:var(--danger);">offline</span>'}
                </div>
              </label>`;
          });
        }
        nodeListEl.innerHTML = html;
        syncSelectAllState();   // [ota-select-all]
      }

      async function refreshPlan() {
        const verEl = document.getElementById('otaServerVer');
        try {
          const ctrl = new AbortController();
          const to   = setTimeout(() => ctrl.abort(), 8000);
          const r    = await fetch('/api/ota/plan?_=' + Date.now(),
                                  { cache: 'no-store', signal: ctrl.signal });
          clearTimeout(to);
          if (!r.ok) {
            if (verEl && (verEl.textContent === '--' || verEl.textContent === 'Offline'))
              verEl.textContent = 'Server ' + r.status;
            return;
          }
          plan = await r.json();
          if (currentStep === 2) updateStep2();
          if (currentStep === 3) render();
        } catch (e) {
          const isAbort = (e && e.name === 'AbortError');
          if (verEl && (verEl.textContent === '--' || verEl.textContent === 'Offline'))
            verEl.textContent = isAbort ? 'Timeout' : 'Offline';
        }
      }

      function renderStep4List() {
        const list = document.getElementById('otaNodeStatusList');
        const targets = window.__otaTargets;
        if (!targets) return;
        
        let html = '';
        if (targets.server) {
          html += `
            <div class="ota-node-item" id="ota-status-server">
              <div class="ota-node-body">
                <div class="ota-node-name">This Server</div>
              </div>
              <div class="ota-node-ver" id="ota-status-icon-server">Waiting…</div>
            </div>`;
        }
        
        targets.nodes.forEach(ip => {
          let name = ip;
          if (plan && plan.nodes) {
            const match = plan.nodes.find(n => n.ip === ip);
            if (match) name = match.name || ip;
          }
          const safeIp = ip.replace(/\./g, '-');
          html += `
            <div class="ota-node-item" id="ota-status-${safeIp}">
              <div class="ota-node-body">
                <div class="ota-node-name">${name}</div>
                <div class="ota-node-ip">${ip}</div>
              </div>
              <div class="ota-node-ver" id="ota-status-icon-${safeIp}">Waiting…</div>
            </div>`;
        });
        
        list.innerHTML = html;
      }

      async function verifyUpdates() {
        const targets = window.__otaTargets;
        if (!targets) return;

        if (Date.now() - verifyStartTime > 40000) {
          finishOta(true);
          return;
        }

        let state;
        try {
          const r = await fetch('/api/state', { cache: 'no-store' });
          state = await r.json();
        } catch (e) {
          return; 
        }

        let allResolved = true;

        if (targets.server) {
          const el = document.getElementById('ota-status-icon-server');
          if (el && !el.innerHTML.includes('✔') && !el.innerHTML.includes('✖')) {
            if (Date.now() - verifyStartTime > 15000) {
              el.innerHTML = '<span class="ok-tick">✔</span>';
            } else {
              allResolved = false;
            }
          }
        }

        for (const ip of targets.nodes) {
          const safeIp = ip.replace(/\./g, '-');
          const el = document.getElementById(`ota-status-icon-${safeIp}`);
          if (!el) continue;
          if (el.innerHTML.includes('✔') || el.innerHTML.includes('✖')) continue;

          let player = state.players ? state.players.find(p => p.ip === ip) : null;
          if (player && player.version === targets.newVersion) {
            el.innerHTML = '<span class="ok-tick">✔</span>';
          } else {
            allResolved = false;
          }
        }

        if (allResolved) {
          finishOta(false);
        }
      }

      async function finishOta(timedOut) {
        if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
        step4Done = true;
        nextBtn.style.display = '';
        nextBtn.textContent = 'Close';
        
        const targets = window.__otaTargets;
        let okCount = 0;
        let failCount = 0;
        
        if (targets.server) {
          const el = document.getElementById('ota-status-icon-server');
          if (el) {
            if (el.innerHTML.includes('✔')) { okCount++; }
            else if (timedOut) { failCount++; el.innerHTML = '<span class="bad-x">✖</span>'; }
            else { okCount++; el.innerHTML = '<span class="ok-tick">✔</span>'; }
          }
        }
        
        for (const ip of targets.nodes) {
          const safeIp = ip.replace(/\./g, '-');
          const el = document.getElementById(`ota-status-icon-${safeIp}`);
          if (!el) continue;
          if (el.innerHTML.includes('✔')) { okCount++; continue; }
          if (el.innerHTML.includes('✖')) { failCount++; continue; }
          
          if (timedOut) {
            el.innerHTML = '<span class="bad-x">✖</span>';
            failCount++;
          } else {
            el.innerHTML = '<span class="ok-tick">✔</span>';
            okCount++;
          }
        }

        await restoreMuteIfNeeded();
        refreshStatus();

        setTimeout(() => {
          let msg = 'Update complete.\n';
          if (okCount > 0) msg += '✔ ' + okCount + ' updated successfully.\n';
          if (failCount > 0) msg += '✖ ' + failCount + ' failed to update.\n';
          alert(msg);
          otaCancel();
          window.openTab(null, 'tab-zones');
        }, 1000);
      }

      async function pollStatus() {
        let s;
        try {
          const ctrl = new AbortController();
          const to   = setTimeout(() => ctrl.abort(), 4000);
          const r    = await fetch('/api/ota/status',
                                   { cache: 'no-store', signal: ctrl.signal });
          clearTimeout(to);
          s = await r.json();
        } catch (e) {
          return;
        }

        if (s.inProgress) {
          const tgt = s.target || '';
          document.querySelectorAll('.ota-node-ver').forEach(el => {
            if (el.innerHTML.includes('spinner')) el.innerHTML = 'Waiting…';
          });
          
          let targetId = '';
          if (tgt === 'server') targetId = 'ota-status-icon-server';
          else if (tgt.startsWith('node ')) {
            const ip = tgt.replace('node ', '');
            targetId = `ota-status-icon-${ip.replace(/\./g, '-')}`;
          }
          
          if (targetId) {
            const el = document.getElementById(targetId);
            if (el) el.innerHTML = '<div class="ota-spinner"></div>';
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

    /* ====================================================================
     * Log controls — Status + Source tabs.
     *
     * Pause stops the client-side poll loop AND mirrors the level to the
     * server via /api/log/level, so the Serial.printf output is silenced
     * too (0 on pause, 1 on resume). The server forwards the level to the
     * source over UART (LOGLV:n), which does the same for its own serial
     * output.
     * ================================================================*/
    function makeLogController({ pauseBtnId, clearBtnId, stateId, pollFn,
                                 clearFn, silenceLevels, tabId }) {
      let paused = false;
      const pauseBtn = document.getElementById(pauseBtnId);
      const clearBtn = document.getElementById(clearBtnId);
      const stateEl  = document.getElementById(stateId);
      if (stateEl) stateEl.textContent = 'polling';

      async function applyLevel(level) {
        try { await api('/api/log/level', 'POST', { level }); } catch (e) {}
      }
      if (pauseBtn) {
        pauseBtn.addEventListener('click', async () => {
          paused = !paused;
          pauseBtn.textContent = paused ? 'Resume' : 'Pause';
          if (stateEl) stateEl.textContent = paused ? 'paused' : 'polling';
          // When paused we silence the serial monitor entirely (0);
          // when resumed we restore errors-only (1). Verbose stays manual.
          await applyLevel(paused ? silenceLevels[0] : silenceLevels[1]);
        });
      }
      if (clearBtn && clearFn) clearBtn.addEventListener('click', clearFn);

      // Poll loop — one controller per tab; both timers coexist but a
      // paused tab's pollFn is skipped so it makes no HTTP requests.
      // Only poll while the tab that owns this controller is actually
      // visible. Previously both the Status and Source timers ran
      // unconditionally, so /api/stats — a big JSON body with per-client
      // delivery counters — was fetched every 2 s even when the user was
      // sitting on Zones. That constant HTTP traffic was the single
      // largest source of background work on the device.
      setInterval(() => {
        if (paused || document.hidden) return;
        if (tabId && currentTabId !== tabId) return;
        pollFn();
      }, 2000);

      return { isPaused: () => paused };
    }

    async function fetchSourceInfo() {
      try {
        const j = await api('/api/source/info', 'GET', undefined, { silent: true });        if (!j || !j.ok) {
          setText('src-link', 'no data');
          return;
        }
        setText('src-link', j.link ? 'UP' : 'DOWN');
        setText('src-bt',   j.btConnected ? 'connected' : 'idle');
        setText('src-mac',  j.connectedMac || '—');
        setText('src-vol',  (j.volume | 0) + ' / 127');

        // Kick button visibility: only when the source is in BT mode and a
        // device is currently connected. j.activeSource comes from
        // /api/source/info (mirrors receiverSource); j.btConnected is derived
        // from the source's SRCSTAT heartbeat.
        const kickWrap = document.getElementById('src-kick-wrap');
        if (kickWrap) {
          const showKick = (j.activeSource === 'BT') && (j.btConnected === true);
          kickWrap.style.display = showKick ? '' : 'none';
        }
        // The server nests the source board's BT_JSON body under the
        // top-level "devices" key, so j.devices is actually
        //   { activeMac: "...", devices: [ ... ] }
        // not a bare array. Accept both shapes so a future server-side
        // cleanup that flattens it does not break this tab again.
        const dj = j.devices;
        const devArr = Array.isArray(dj) ? dj
                     : (dj && Array.isArray(dj.devices)) ? dj.devices
                     : [];
        const devs = devArr.map(d =>
          d.mac + (d.conn ? ' *' : '') + ' (vol ' + d.vol + ')').join('\n');
        setText('src-devices', devs || 'no devices');
        setText('src-diag',
          'BT connecting: '   + (j.a2dpConnecting    | 0) + '\n' +
          'BT connected: '    + (j.a2dpConnected     | 0) + '\n' +
          'BT disconnected: ' + (j.a2dpDisconnected  | 0) + '\n' +
          'AVRCP CT: '        + (j.avrcConnected ? 'yes' : 'no') + '\n' +
          'Vol events: '      + (j.volumeEvents      | 0) + '\n' +
          'UART lines: '      + (j.uartLines         | 0) + '\n' +
          'Source: '          + (j.activeSource      || '?') + '\n' +
          'Last heartbeat: '  + (j.heartbeatAgeMs === undefined ? '—' :
                                   ((j.heartbeatAgeMs / 1000).toFixed(1) + ' s ago')));
        setText('src-log', (j.log || []).join('\n') || '(no log entries)');
      } catch (e) {
        setText('src-link', 'unreachable');
      }
    }

    makeLogController({
      pauseBtnId: 'status-log-pause',
      clearBtnId: 'status-log-clear',
      stateId:    'status-log-state',
      pollFn:     () => fetchHeavyStats(),
      tabId:      'tab-status',
      clearFn:    () => {
        ['stat-latency','stat-buffer','stat-delay','stat-mode','stat-clients',
         'stat-master','stat-nodes','stat-diag','stat-pipeline']
          .forEach(id => setText(id, '—'));
      },
      silenceLevels: [0, 1],
    });

    document.getElementById('src-kick-btn')?.addEventListener('click', async () => {
      if (!confirm('Kick the connected BT device?\n\nThe current device will be disconnected without being unpaired. You will have ~2 minutes to pair a new device before the source attempts to reconnect.')) {
        return;
      }
      const r = await api('/api/bt/kick', 'POST', {});
      if (r && r.ok) {
        // Optimistic UI: hide until the next poll confirms the state.
        const wrap = document.getElementById('src-kick-wrap');
        if (wrap) wrap.style.display = 'none';
        // Fetch fresh state shortly after the kick lands on the source
        // so the button reappears immediately if the device re-pairs.
        setTimeout(() => fetchSourceInfo(), 500);
      } else {
        alert('Kick failed: source did not acknowledge');
      }
    });

    makeLogController({
      pauseBtnId: 'source-log-pause',
      clearBtnId: 'source-log-clear',
      stateId:    'source-log-state',
      pollFn:     () => fetchSourceInfo(),
      tabId:      'tab-source',
      clearFn:    () => setText('src-log', '(cleared)'),
      silenceLevels: [0, 1],
    });


     // ---- Advanced logging toggle + live viewer -----------------------
    // The device sits at level 1 (errors only) most of the time, which is
    // what keeps the Arduino serial monitor quiet after boot and keeps
    // Core 0 out of the UART. Clicking this button:
    //   1. Raises the device log level to 3 (verbose), which also starts
    //      filling the in-RAM log ring on the device.
    //   2. Reveals the live viewer below the button and starts polling
    //      /api/log/tail once a second for new lines.
    // Clicking it again hides the viewer, stops the poll, and drops the
    // device back to level 1 (errors only).
    (function () {
      const btn       = document.getElementById('status-advanced-log');
      const state     = document.getElementById('status-advanced-log-state');
      const viewer    = document.getElementById('status-log-viewer');
      const pre       = document.getElementById('status-log-pre');
      const viewState = document.getElementById('status-logview-state');
      const clearBtn  = document.getElementById('status-logview-clear');
      if (!btn || !state || !viewer || !pre) return;

      let advanced  = false;
      let pollTimer = null;
      let lastSeq   = 0;
      let lineCount = 0;
      const MAX_LINES = 200;   // trim the DOM so a long session stays cheap

      function tagFor(level) {
        return level === 'E' ? '[ERROR]'
             : level === 'W' ? '[WARN ]'
                             : '[INFO ]';
      }

      function appendLine(line) {
        pre.textContent +=
          tagFor(line.level) + ' ' +
          String(line.ms).padStart(8, ' ') + ' ms | ' +
          line.text + '\n';
        lineCount++;
        // Trim oldest lines once we exceed the cap. Splitting on every
        // append would be wasteful, so only do it once we're over budget.
        if (lineCount > MAX_LINES) {
          const lines = pre.textContent.split('\n');
          pre.textContent = lines.slice(-MAX_LINES - 1).join('\n');
          lineCount = MAX_LINES;
        }
        pre.scrollTop = pre.scrollHeight;
      }

      async function pollTail() {
        try {
          const r = await fetch('/api/log/tail?since=' + lastSeq,
                                { cache: 'no-store' });
          const j = await r.json();
          if (!j || !j.ok) return;
          if (Array.isArray(j.lines)) {
            for (const line of j.lines) appendLine(line);
            if (j.lines.length) {
              viewState.textContent = j.lines.length + ' new';
            }
          }
          if (typeof j.nextSeq === 'number') lastSeq = j.nextSeq;
        } catch (e) { /* transient; next tick retries */ }
      }

      if (clearBtn) {
        clearBtn.addEventListener('click', () => {
          pre.textContent = '';
          lineCount = 0;
          viewState.textContent = 'idle';
        });
      }

      btn.addEventListener('click', async () => {
        advanced = !advanced;
        btn.textContent = advanced
          ? 'Disable advanced logging (may affect audio)'
          : 'Enable advanced logging';
        state.textContent = advanced
          ? 'verbose (level 3)'
          : 'off (errors only)';

        try {
          await api('/api/log/level', 'POST', { level: advanced ? 3 : 1 });
        } catch (e) { /* server may be busy; ignore */ }

        if (advanced) {
          viewer.style.display = '';
          pre.textContent = '';
          lineCount = 0;
          // since=0 pulls the entire ring contents on the first call, so
          // any lines emitted between enabling and the first poll land
          // immediately instead of being skipped.
          lastSeq = 0;
          viewState.textContent = 'polling';
          await pollTail();
          if (pollTimer) clearInterval(pollTimer);
          pollTimer = setInterval(pollTail, 1000);
        } else {
          if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
          viewer.style.display = 'none';
        }
      });
    })();    

    /* ====================================================================
     * Network tab handlers
     * ==================================================================*/
    let wifiScanPollTimer = null;
    let wifiScanPollBusy  = false;

    async function loadNetworkState() {
      const s = await api('/api/network/state');
      if (!s || !s.ok) return;
      setVal('net-server-name', s.serverName || '');
      setVal('net-ssid',        s.ssid || '');
      setVal('net-pass',        s.password || '');
      setVal('net-ip',          s.ip || '');
      setVal('net-gw',          s.gateway || '');
      setVal('net-sn',          s.subnet || '255.255.255.0');
      setVal('net-dns',         s.dns || '');

      const chk = document.getElementById('net-use-static');
      const box = document.getElementById('net-static-fields');
      if (chk) chk.checked = !!s.useStatic;
      if (box) box.hidden  = !s.useStatic;
    }

    function setVal(id, v) {
      const el = document.getElementById(id);
      if (el && document.activeElement !== el) el.value = v;
    }

    async function saveNetworkSettings() {
      const btn = document.getElementById('net-save-btn');
      const nameEl = document.getElementById('net-server-name');
      const ssidEl = document.getElementById('net-ssid');

      const serverName = (nameEl?.value || '').trim();
      const ssid       = (ssidEl?.value || '').trim();
      if (!serverName) { alert('Server name is required.'); return; }
      if (!ssid)       { alert('Wi-Fi SSID is required.'); return; }
      if (!/^[A-Za-z0-9]+$/.test(serverName)) {
        alert('Server name must contain only letters and digits.');
        return;
      }

      const body = new URLSearchParams();
      body.append('server_name', serverName);
      body.append('ssid', ssid);
      body.append('password', document.getElementById('net-pass')?.value || '');

      if (document.getElementById('net-use-static')?.checked) {
        body.append('use_static_ip', 'on');
        body.append('ip',      document.getElementById('net-ip')?.value  || '');
        body.append('gateway', document.getElementById('net-gw')?.value  || '');
        body.append('subnet',  document.getElementById('net-sn')?.value  || '');
        body.append('dns',     document.getElementById('net-dns')?.value || '');
      }

      const testCard = document.getElementById('net-test-card');
      const testTxt  = document.getElementById('net-test-state');
      if (testCard) testCard.style.display = '';
      if (testTxt)  testTxt.textContent = 'Testing connection…';
      if (btn) { btn.disabled = true; btn.textContent = 'Testing…'; }

      try {
        const r = await fetch('/save', { method: 'POST', body });
        const d = await r.json();
        if (!r.ok || !d.ok) throw new Error(d.error || ('HTTP ' + r.status));

        // Poll the network-test task until it finishes. Success => the
        // server will reboot into STA; the browser loses the connection
        // shortly after, so we only get one shot at this.
        let s;
        do {
          await new Promise(res => setTimeout(res, 500));
          s = await (await fetch('/api/network-test')).json();
        } while (s.state === 'running');

        if (s.state !== 'success') {
          throw new Error(s.error || 'Unable to connect with those settings');
        }

        if (testTxt) testTxt.innerHTML =
          '<span style="color:var(--success);">Connected as ' + s.ip + '</span>';
        if (testTxt) testTxt.textContent = 'Connected as ' + s.ip + ' — rebooting…';

        // Server will reboot into STA. Trigger it explicitly, then wait.
        try { await fetch('/api/restart', { method: 'POST' }); } catch (e) {}

        setTimeout(() => {
          window.location.href = 'http://' + serverName + '.local/';
        }, 8000);
      } catch (e) {
        if (testTxt) {
          testTxt.textContent = 'Failed: ' + e.message;
          testTxt.style.color = 'var(--danger)';
        }
        if (btn) { btn.disabled = false; btn.textContent = 'Save & Reconnect'; }
      }
    }

    function closeWifiScan() {
      const m = document.getElementById('wifiScanModal');
      if (m) { m.classList.remove('open'); m.setAttribute('aria-hidden', 'true'); }
      if (wifiScanPollTimer) { clearInterval(wifiScanPollTimer); wifiScanPollTimer = null; }
    }

    async function startWifiScan() {
      const m = document.getElementById('wifiScanModal');
      const list = document.getElementById('wifiScanList');
      if (!m || !list) return;
      m.classList.add('open');
      m.setAttribute('aria-hidden', 'false');
      list.innerHTML = '<div class="muted" style="padding:18px 20px;">Scanning…</div>';

      try {
        await fetch('/api/wifi/scan/start', { cache: 'no-store' });
      } catch (e) {
        list.innerHTML =
          '<div class="muted" style="padding:18px 20px;color:var(--danger);">' +
          'Could not start scan.</div>';
        return;
      }

      if (wifiScanPollTimer) clearInterval(wifiScanPollTimer);
      wifiScanPollBusy = false;
      let attempts = 0;
      wifiScanPollTimer = setInterval(async () => {
        if (wifiScanPollBusy) return;        // drop overlapping ticks
        wifiScanPollBusy = true;
        attempts++;
        if (attempts > 40) {
          clearInterval(wifiScanPollTimer);
          wifiScanPollTimer = null;
          wifiScanPollBusy = false;
          list.innerHTML =
            '<div class="muted" style="padding:18px 20px;">Scan timed out.</div>';
          return;
        }
        let s;
        try {
          s = await (await fetch('/api/wifi/scan/status',
                                 { cache: 'no-store' })).json();
        } catch (e) {
          wifiScanPollBusy = false;
          return;                             // keep polling, do not wipe
        }
        if (s.state === 'running') {
          wifiScanPollBusy = false;
          return;
        }

        // A truncated body drops the socket and lands in the catch above.
        // A well-formed failed body always has "networks":[] — treat
        // exactly that as terminal, and any other shape as retryable.
        if (s.state === 'failed') {
          if (Array.isArray(s.networks)) {
            clearInterval(wifiScanPollTimer);
            wifiScanPollTimer = null;
            wifiScanPollBusy = false;
            list.innerHTML =
              '<div class="muted" style="padding:18px 20px;">No networks found.</div>';
            return;
          }
          wifiScanPollBusy = false;
          return;
        }
        if (!s.networks) {
          wifiScanPollBusy = false;
          return;
        }

        clearInterval(wifiScanPollTimer);
        wifiScanPollTimer = null;
        wifiScanPollBusy = false;

        if (!s.networks.length) {
          list.innerHTML =
            '<div class="muted" style="padding:18px 20px;">No networks found.</div>';
          return;
        }

        const nets = s.networks.slice().sort((a, b) => b.rssi - a.rssi);
        list.innerHTML = '';
        nets.forEach(n => {
          const b = document.createElement('button');
          b.type = 'button'; b.className = 'wifi-net';
          const sEl = document.createElement('span');
          sEl.textContent = n.ssid || '(hidden)';
          const rEl = document.createElement('span');
          rEl.className = 'rssi';
          rEl.textContent = (n.rssi|0) + ' dBm';
          b.appendChild(sEl); b.appendChild(rEl);
          b.addEventListener('click', () => {
            const ssidEl = document.getElementById('net-ssid');
            if (ssidEl) ssidEl.value = n.ssid || '';
            closeWifiScan();
            document.getElementById('net-pass')?.focus();
          });
          list.appendChild(b);
        });
      }, 500);
    }

    /* ====================================================================
     * Boot
     * ==================================================================*/

     
    document.addEventListener('DOMContentLoaded', () => {
      // ---- Dual master-volume bindings (desktop + phone) -----------
      const volD = document.getElementById('volume');
      const volM = document.getElementById('volume-mobile');
      const lblD = document.getElementById('volume-label');
      const lblM = document.getElementById('volume-float-label');
      let volSaveTimer = null;

      function paintVolume(v) {
        if (volD && volD.value !== String(v)) volD.value = v;
        if (volM && volM.value !== String(v)) volM.value = v;
        if (lblD) lblD.textContent = v + '%';
        if (lblM) lblM.textContent = v;
      }
      function onVolumeInput(v) {
        activeDrags.volume = true;
        paintVolume(v);
        if (isMuted) { isMuted = false; setMuteButtonUI(false); }
        if (volSaveTimer) clearTimeout(volSaveTimer);
        volSaveTimer = setTimeout(async () => {
          activeDrags.volume = false;
          await api('/api/config', 'POST', { masterVolume: parseInt(v, 10) });
        }, 180);
      }
      if (volD) volD.addEventListener('input', () => onVolumeInput(volD.value));
      if (volM) volM.addEventListener('input', () => onVolumeInput(volM.value));

      // ---- Mode + source selects ----------------------------------
      const modeSel = document.getElementById('mode-select');
      if (modeSel) modeSel.addEventListener('change', () => setMode(modeSel.value));
      const srcSel = document.getElementById('source-select');
      if (srcSel) srcSel.addEventListener('change', () => setSource(srcSel.value));

      // Sync from server state on first poll only (refreshStatus keeps it
      // up-to-date from then on).
      // Sync from server state on first poll only (refreshStatus keeps it
      // up-to-date from then on).
      paintVolume(100);

      // ---- Status tab: advanced toggle ---------------------------------
      // Splits the Status tab into a small always-visible "what is the
      // audio doing right now" set of cards and everything else (per-node
      // detail, error counters, raw JSON). Advanced is off by default so a
      // first glance is never muddied by diagnostic output.
      (function () {
        const btn   = document.getElementById('status-advanced-toggle');
        const panel = document.getElementById('status-advanced-panel');
        const state = document.getElementById('status-advanced-state');
        if (!btn || !panel) return;
        let advanced = false;
        btn.addEventListener('click', function () {
          advanced = !advanced;
          panel.style.display = advanced ? '' : 'none';
          btn.textContent     = advanced ? 'Hide advanced' : 'Show advanced';
          if (state) state.textContent = advanced ? 'full detail' : 'basic view';
          if (advanced) fetchHeavyStats();   // fresh values on open
        });
      })();
      /* ============================================================
       * Network tab — inline form (replaces the old /settings iframe)
       * ==========================================================*/
      (function () {
        const staticChk = document.getElementById('net-use-static');
        const staticBox = document.getElementById('net-static-fields');
        if (staticChk && staticBox) {
          staticChk.addEventListener('change',
            () => { staticBox.hidden = !staticChk.checked; });
        }

        // Auto-fill gateway/DNS/subnet from the IP's /24 the same way the
        // node dashboard does. Only fills fields that are still blank, so a
        // user-set value is never clobbered.
        const ipEl = document.getElementById('net-ip');
        if (ipEl) {
          ipEl.addEventListener('input', function () {
            const p = this.value.trim().split('.');
            if (p.length !== 4) return;
            for (let i = 0; i < 4; i++) {
              if (!/^\d{1,3}$/.test(p[i]) || +p[i] > 255) return;
            }
            const base = p[0] + '.' + p[1] + '.' + p[2] + '.1';
            const gw  = document.getElementById('net-gw');
            const dns = document.getElementById('net-dns');
            const sn  = document.getElementById('net-sn');
            if (gw  && !gw.value)  gw.value  = base;
            if (dns && !dns.value) dns.value = base;
            if (sn  && !sn.value)  sn.value  = '255.255.255.0';
          });
        }

        document.getElementById('net-revert-btn')
          ?.addEventListener('click', () => loadNetworkState());

        document.getElementById('net-scan-btn')
          ?.addEventListener('click', () => startWifiScan());

        document.getElementById('net-save-btn')
          ?.addEventListener('click', () => saveNetworkSettings());
      })();

      fetch('/api/fw')
        .then(r => r.json())
        .then(d => {
          const el = document.getElementById('server-version');
          if (el && d && d.version) el.textContent = 'firmware v' + d.version;
        })
        .catch(() => {});

      async function pollLoop() {
        await refreshStatus();
        setTimeout(pollLoop, 4000);
      }
      setTimeout(pollLoop, 150);
    });

    /* ====================================================================
     * Touch swipe navigation between tabs (mobile)
     * ==================================================================*/
    (function swipeNav() {
      const TAB_ORDER = ['tab-zones', 'tab-settings', 'tab-status'];
      const SWIPE_MIN_X  = 60;
      const SWIPE_MAX_Y  = 100;
      const SWIPE_RATIO  = 1.5;
      const SWIPE_MAX_MS = 700;
      const EDGE_GUARD   = 24;

      let startX = 0, startY = 0, startT = 0, tracking = false;

      function isInteractive(el) {
        while (el && el !== document.body) {
          const tag = (el.tagName || '').toLowerCase();
          if (tag === 'input' || tag === 'textarea' || tag === 'select' ||
              tag === 'button' || tag === 'a' || tag === 'label') return true;
          if (el.classList) {
            if (el.classList.contains('slider')         ||
                el.classList.contains('node-chip')      ||
                el.classList.contains('nav-btn')        ||
                el.classList.contains('menu-bar')       ||
                el.classList.contains('actionbar')      ||
                el.classList.contains('overlay')        ||
                el.classList.contains('modal')          ||
                el.classList.contains('ota-modal')      ||
                el.classList.contains('ota-modal-panel')) return true;
          }
          if (el.getAttribute && el.getAttribute('draggable') === 'true') return true;
          el = el.parentElement;
        }
        return false;
      }

      function navigateToTab(tabId) {
        const btns = document.querySelectorAll('.nav-btn');
        for (let i = 0; i < btns.length; i++) {
          const oc = btns[i].getAttribute('onclick') || '';
          if (oc.indexOf("'" + tabId + "'") !== -1 || oc.indexOf('"' + tabId + '"') !== -1) {
            btns[i].click();
            return;
          }
        }
        if (typeof window.openTab === 'function') window.openTab(null, tabId);
      }

      document.addEventListener('touchstart', function (e) {
        tracking = false;
        if (!e.touches || e.touches.length !== 1) return;
        const t = e.touches[0];
        if (t.clientX < EDGE_GUARD || t.clientX > window.innerWidth - EDGE_GUARD) return;
        if (isInteractive(e.target)) return;
        startX = t.clientX; startY = t.clientY;
        startT = Date.now(); tracking = true;
      }, { passive: true });

      document.addEventListener('touchend', function (e) {
        if (!tracking) return;
        tracking = false;
        if (!e.changedTouches || e.changedTouches.length === 0) return;
        const t  = e.changedTouches[0];
        const dx = t.clientX - startX;
        const dy = t.clientY - startY;
        const dt = Date.now() - startT;
        if (dt > SWIPE_MAX_MS) return;
        if (Math.abs(dx) < SWIPE_MIN_X) return;
        if (Math.abs(dy) > SWIPE_MAX_Y) return;
        if (Math.abs(dx) < Math.abs(dy) * SWIPE_RATIO) return;
        const idx = TAB_ORDER.indexOf(currentTabId);
        if (idx < 0) return;
        let next;
        if (dx < 0) next = idx + 1;
        else        next = idx - 1;
        if (next < 0 || next >= TAB_ORDER.length) return;
        navigateToTab(TAB_ORDER[next]);
      }, { passive: true });
    })();
  </script>
</body>
</html>
)HTML";