// app.js — wiring: camera -> vision -> attention -> board link, plus the UI.
import {
  MoodMachine, classifyFrame, computeBaseline, smooth, clamp, dayKey, eLine,
  DEFAULT_THRESHOLDS, DEFAULT_TIMINGS, MOOD_LABELS,
} from './attention.js';
import { SerialLink, BleLink } from './link.js';
import { Engine as EyeEngine, Mirror, ScreenPreview } from './eyes.js';
import { ColorPicker, DEFAULT_COLORS, hex6 } from './colors.js';

const $ = (id) => document.getElementById(id);
const el = {
  video: $('video'), overlay: $('overlay'), camMsg: $('camMsg'), chkOverlay: $('chkOverlay'),
  visionStatus: $('visionStatus'), cls: $('cls'), reasons: $('reasons'), mood: $('mood'),
  moodWord: $('moodWord'), caught: $('caught'), btnCal: $('btnCal'), btnPause: $('btnPause'),
  calMsg: $('calMsg'), demo: $('demo'), demoMsg: $('demoMsg'), pill: $('pill'),
  btnUsb: $('btnUsb'), btnUsbAny: $('btnUsbAny'), btnBle: $('btnBle'), toast: $('toast'),
  sigBody: $('sigBody'), baselineInfo: $('baselineInfo'), btnDefaults: $('btnDefaults'), tuning: $('tuning'),
};

// ------------------------------------------------------------------ settings (localStorage)
const LS = {
  settings: 'eyebuddy.settings', baseline: 'eyebuddy.baseline', caught: 'eyebuddy.caught',
  colors: 'eyebuddy.colors', savedColors: 'eyebuddy.savedColors',
};
const DEFAULT_LOOK = { gain: 1.0, tauMs: 150 };
const SEND_HZ = 20;
const CAL_SECONDS = 3;

function loadJSON(key, fallback) {
  try { const v = JSON.parse(localStorage.getItem(key)); return v ?? fallback; } catch { return fallback; }
}
function saveJSON(key, v) { try { localStorage.setItem(key, JSON.stringify(v)); } catch {} }

const stored = loadJSON(LS.settings, {});
const SETTINGS_VERSION = 2;
if ((stored.v || 1) < 2 && stored.timings) delete stored.timings.sideDelay;   // v2: side-eye is now immediate
const settings = {
  thresholds: { ...DEFAULT_THRESHOLDS, ...stored.thresholds },
  timings: { ...DEFAULT_TIMINGS, ...stored.timings },
  look: { ...DEFAULT_LOOK, ...stored.look },
  v: SETTINGS_VERSION,
};
const saveSettings = () => saveJSON(LS.settings, settings);
let baseline = loadJSON(LS.baseline, null);

// ------------------------------------------------------------------ state
const machine = new MoodMachine(settings.timings);
const state = {
  signals: null,
  cls: null,            // raw per-frame classification
  reasons: [],
  mood: 'neutral',
  lookX: 0, lookY: 0,   // smoothed, -100..100
  lastFrameT: null,
  paused: false,
  demo: false,
  manual: null,         // expression picked with the buttons (null = Auto: the camera decides)
  cal: null,            // {until, samples, auto}
  fps: 0,
  userBlink: false,     // your eyes are closed right now (camera)
};

// ------------------------------------------------------------------ caught-today counter
function caughtToday() {
  const c = loadJSON(LS.caught, null);
  return c && c.day === dayKey(new Date()) ? c.count : 0;
}
function bumpCaught() { saveJSON(LS.caught, { day: dayKey(new Date()), count: caughtToday() + 1 }); renderCaught(); }
function renderCaught() { el.caught.textContent = caughtToday(); }
setInterval(renderCaught, 30_000); // rolls over to 0 at local midnight

// ------------------------------------------------------------------ board link
const usb = new SerialLink();
const ble = new BleLink();
let link = null; // the active Link
let boardHello = '';

for (const l of [usb, ble]) {
  l.onStatus((s) => {
    if (s.state === 'connected') link = l;
    if (link === l || !link) renderPill(s);
    if (s.state !== 'connected' && link === l) { boardHello = ''; if (s.state === 'disconnected') link = null; }
    renderConnButtons();
    if (s.state === 'connected') {   // hello blink: the board blinks on its first E line, the preview right now
      sendColors();                  // the website is in charge of the colors
      sendNow();
      eyeEngine.blinkNow(Math.floor(performance.now()), false);
    }
  });
  l.onLine(handleBoardLine);
}

function renderPill(s) {
  el.pill.dataset.state = s.state;
  const kind = s.kind === 'usb' ? 'USB' : 'Bluetooth';
  const txt = {
    connected: `${kind} connected${boardHello ? ' · ' + boardHello : ''}`,
    connecting: `${kind}: connecting…`,
    reconnecting: s.detail || `${kind}: reconnecting…`,
    error: s.detail || `${kind} error`,
    disconnected: s.detail ? `not connected (${s.detail})` : 'not connected',
  }[s.state];
  el.pill.textContent = txt;
  el.pill.title = s.detail || '';
}

function renderConnButtons() {
  el.btnUsb.textContent = usb.connected ? 'Disconnect USB' : 'Connect USB';
  el.btnBle.textContent = ble.connected || ble.state === 'reconnecting' ? 'Disconnect Bluetooth' : 'Connect Bluetooth';
  el.btnUsb.disabled = !SerialLink.supported();
  el.btnUsbAny.disabled = !SerialLink.supported() || usb.connected;
  el.btnBle.disabled = !BleLink.supported();
}

async function connectWith(l, opts) {
  const other = l === usb ? ble : usb;
  try {
    if (other.connected || other.state === 'reconnecting') await other.disconnect();
    await l.connect(opts);
  } catch (e) {
    if (e?.name === 'NotFoundError') {
      // user cancelled the chooser or nothing matched
      renderPill({ state: 'disconnected', kind: l.kind, detail: l === usb ? 'no EyeBuddy picked — try "Other port…"' : 'no EyeBuddy picked' });
    } else {
      renderPill({ state: 'error', kind: l.kind, detail: e?.message || String(e) });
    }
  }
  renderConnButtons();
}

el.btnUsb.onclick = () => (usb.connected ? usb.disconnect() : connectWith(usb));
el.btnUsbAny.onclick = () => connectWith(usb, { anyPort: true });
el.btnBle.onclick = () => (ble.connected || ble.state === 'reconnecting' ? ble.disconnect() : connectWith(ble));

const TOUCH_TOASTS = {
  touch: 'EyeBuddy is happy to see you',
  release: 'EyeBuddy liked that',
};

function handleBoardLine(line) {
  const parts = line.trim().split(/\s+/);
  if (parts[0] === 'HELLO') {
    boardHello = `${parts[1] || 'board'} v${parts[2] || '?'}`; // "HELLO EyeBuddy 1" -> "EyeBuddy v1"
    if (link) renderPill({ state: link.state, kind: link.kind });
  } else if (parts[0] === 'T' && parts[1]) {
    mirror.touch(parts[1], performance.now());   // board was touched: preview reacts the same way
    toast(TOUCH_TOASTS[parts[1]] || `touch: ${parts[1]}`);
  } else if (parts[0] === 'P') {
    // ping reply; nothing to show
  } else {
    console.debug('[board]', line);
  }
}

let toastTimer = 0;
function toast(msg) {
  el.toast.textContent = msg;
  el.toast.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.toast.hidden = true; }, 2500);
}

// What the board should show right now.
function currentFrame() {
  const tracking = !state.demo && !state.paused;
  const lookX = tracking ? state.lookX : 0, lookY = tracking ? state.lookY : 0;
  if (state.manual) return [state.manual, lookX, lookY];   // held by a button; eyes still follow you
  if (!tracking) return ['neutral', 0, 0];
  return [state.mood, lookX, lookY];
}
// The preview engine is the clock: its small glances ride along in the E line, and its blinks are
// sent as B / B2, so the board's eyes do exactly what the preview does.
function sendNow() {
  if (!link?.connected) return;
  const [mood, x, y] = currentFrame();
  link.send(eLine(mood, x + eyeEngine.jitterX * 100, y + eyeEngine.jitterY * 100));
}

// ------------------------------------------------------------------ live preview of the board's screen
// Same eye code as the firmware (eyes.js is a port of eyes.cpp), fed the same mood/look we send.
const eyeEngine = new EyeEngine((Math.random() * 2 ** 32) >>> 0);
const mirror = new Mirror(eyeEngine);
const screenPreview = new ScreenPreview($('screen'));

// ------------------------------------------------------------------ colors (screen + eyes)
let colors = loadJSON(LS.colors, DEFAULT_COLORS);
eyeEngine.setColors(colors.face, colors.eye);
function sendColors() {
  if (link?.connected) link.send(`K ${hex6(colors.face)} ${hex6(colors.eye)}`);
}
new ColorPicker({
  colors,
  saved: loadJSON(LS.savedColors, []),
  onChange: (c) => {   // live while dragging: preview now, board right away
    colors = c;
    eyeEngine.setColors(c.face, c.eye);
    sendColors();
    saveJSON(LS.colors, c);
  },
  onSavedChange: (saved) => saveJSON(LS.savedColors, saved),
});
function stepEyes() {
  const now = Math.floor(performance.now());
  const [mood, x, y] = currentFrame();
  mirror.update(now, true, mood, Math.max(-100, Math.min(100, Math.round(x))), Math.max(-100, Math.min(100, Math.round(y))));   // ints, like the E line
  const jx = eyeEngine.jitterX, jy = eyeEngine.jitterY;
  eyeEngine.update(now);
  const blink = eyeEngine.takeBlinkEvent();
  if (blink && link?.connected) link.send(blink.twice ? 'B2' : 'B');   // board blinks right now, with the preview
  if (jx !== eyeEngine.jitterX || jy !== eyeEngine.jitterY) sendNow();  // new glance: tell the board straight away
}
function blinkWithUser() {
  stepEyes();
  if (eyeEngine.blinkNow(Math.floor(performance.now()), false) && link?.connected) link.send('B');
}
// Mood changed: step the preview first so it starts its switch-blink at the same instant the board
// gets the E line (otherwise the board would lead by up to one screen refresh).
function syncNow() { stepEyes(); sendNow(); }
function previewLoop() {
  stepEyes();
  screenPreview.draw(eyeEngine);
  requestAnimationFrame(previewLoop);
}
requestAnimationFrame(previewLoop);

// 20 Hz sender driven by a Worker timer: window timers get throttled to 1 Hz in background
// tabs, which would let the board fall into idle mode (it idles after 3 s without `E`).
const ticker = new Worker(URL.createObjectURL(new Blob(
  [`setInterval(() => postMessage(0), ${Math.round(1000 / SEND_HZ)});`], { type: 'text/javascript' })));
ticker.onmessage = () => { stepEyes(); sendNow(); };   // keeps the blink clock running in background tabs too

// ------------------------------------------------------------------ vision
let vision = null;       // Vision instance once the model is loaded
let frameReader = null;  // MediaStreamTrackProcessor reader (keeps running in background tabs)

let modelReady = null;   // Promise from vision.init()

async function startTracking() {
  el.camMsg.textContent = 'starting camera…';
  let stage = 'model';
  try {
    if (!vision) {
      const { Vision } = await import('./vision.js');
      vision = new Vision();
      // Model load runs in parallel with the camera permission prompt.
      modelReady = vision.init().then((d) => {
        el.visionStatus.textContent = `face model ready (${d})`;
        document.body.dataset.vision = `ready-${d}`;
      });
      modelReady.catch(() => {}); // reported below
    }
    stage = 'camera';
    if (!navigator.mediaDevices?.getUserMedia) {
      throw Object.assign(new Error('camera API unavailable'), { name: 'NoCameraApi' });
    }
    await vision.startCamera(el.video);
    stage = 'model';
    await modelReady;
  } catch (e) {
    return handleStartError(e, stage);
  }
  el.camMsg.textContent = '';
  setDemo(false);
  runFrameLoop();
}

function handleStartError(e, stage) {
  console.warn(`[app] tracking unavailable (${stage}):`, e?.name, e?.message);
  if (stage === 'camera') {
    const msg = {
      NotAllowedError: 'Camera permission denied.',
      NotFoundError: 'No camera found.',
      NotReadableError: 'Camera is busy in another app.',
      OverconstrainedError: 'Camera does not support the requested mode.',
      NoCameraApi: 'No camera access here (open http://localhost:8000 in Chrome).',
    }[e?.name] || `Camera unavailable: ${e?.message || e}`;
    el.camMsg.textContent = msg;
    el.demoMsg.textContent = `${msg} Pick an expression below. To use the camera, allow it via the icon in the address bar and reload.`;
  } else {
    el.camMsg.textContent = 'Face model failed to load.';
    el.visionStatus.textContent = `face model error: ${e?.message || e}`;
    el.demoMsg.textContent = 'Face tracking unavailable. Pick an expression below.';
    document.body.dataset.vision = 'error';
  }
  vision?.stopCamera(el.video);
  setDemo(true);
}

function runFrameLoop() {
  const track = vision.stream?.getVideoTracks()[0];
  if (!track) return;
  if ('MediaStreamTrackProcessor' in window) {
    const reader = new MediaStreamTrackProcessor({ track }).readable.getReader();
    frameReader = reader;
    (async () => {
      for (;;) {
        const { value: frame, done } = await reader.read().catch(() => ({ done: true }));
        if (done) break;
        try { onFrame(frame); } finally { frame.close(); }
      }
    })();
  } else {
    // Fallback: video frame callbacks (pause when the tab is hidden).
    const step = () => { if (!vision.stream) return; onFrame(el.video); el.video.requestVideoFrameCallback(step); };
    el.video.requestVideoFrameCallback(step);
  }
}

function onFrame(source) {
  if (state.paused || !vision?.ready) return;
  const nowMs = performance.now();
  let out;
  try { out = vision.detect(source, nowMs); } catch (e) { console.warn('[app] detect failed:', e?.message || e); return; }
  const t = nowMs / 1000;
  const dt = state.lastFrameT === null ? 0 : t - state.lastFrameT;
  if (dt > 0) state.fps = state.fps ? state.fps * 0.9 + (1 / dt) * 0.1 : 1 / dt;
  state.lastFrameT = t;

  const s = out.signals;
  state.signals = s;

  // Calibration (manual, or automatic the first time a face shows up).
  if (s.face && !baseline && !state.cal) startCalibration(true);
  if (state.cal) stepCalibration(s, t);

  const { cls, reasons } = classifyFrame(s, baseline, settings.thresholds);
  state.cls = cls; state.reasons = reasons;
  const r = machine.update(cls, t);
  state.effective = r.effective;
  if (r.caught) bumpCaught();
  if (r.changed) { state.mood = r.mood; syncNow(); }

  // You blink -> EyeBuddy blinks (preview now, board via B). Fires once as your eyes close; re-arms
  // once they're clearly open again. Its own random blinks only fill in when you don't blink for a while.
  const bth = settings.thresholds.blink;
  if (s.face && !state.userBlink && s.blink > bth) { state.userBlink = true; blinkWithUser(); }
  else if (!s.face || s.blink < bth - 0.15) state.userBlink = false;

  // Eye-follow target. The board faces the user like a mirror. In the RAW camera image the
  // user moving to THEIR left moves toward image +x (the camera sees them flipped). For the
  // board's pupils to point AT the user, the pupils must move toward the user's left as the
  // user looks at the board, i.e. toward the board screen's left = negative lookX. That is
  // exactly the mirrored-preview x: lookX = (0.5 - rawX). Positive lookX = pupils toward the
  // right edge of the board's screen as the user sees it; positive lookY = pupils toward the
  // bottom of the screen (user lower in the frame). Gain 1: face 30% of the frame off-centre = 100.
  const g = settings.look.gain * (100 / 0.3);
  const tx = s.face ? clamp((0.5 - s.cx) * g, -100, 100) : 0;
  const ty = s.face ? clamp((s.cy - 0.5) * g, -100, 100) : 0;
  const tau = settings.look.tauMs / 1000;
  state.lookX = dt > 0 ? smooth(state.lookX, tx, dt, tau) : tx;
  state.lookY = dt > 0 ? smooth(state.lookY, ty, dt, tau) : ty;

  drawOverlay(out.landmarks);
  scheduleRender();
}

// ------------------------------------------------------------------ calibration
function startCalibration(auto = false) {
  if (state.paused || state.demo || !vision?.stream) return;
  state.cal = { until: performance.now() / 1000 + CAL_SECONDS, samples: [], auto };
  el.btnCal.disabled = true;
}

function stepCalibration(s, t) {
  const c = state.cal;
  if (s.face && s.blink <= settings.thresholds.blink) c.samples.push(s);
  const left = Math.max(0, c.until - t);
  el.calMsg.textContent = `${c.auto ? 'First run: ' : ''}look at your screen… ${left.toFixed(1)} s`;
  if (left > 0) return;
  state.cal = null;
  el.btnCal.disabled = false;
  if (c.samples.length < 10) {
    el.calMsg.textContent = 'Calibration failed: no face seen. Sit in view and try again.';
    return;
  }
  baseline = computeBaseline(c.samples);
  saveJSON(LS.baseline, baseline);
  machine.reset(); state.mood = 'neutral'; syncNow();
  el.calMsg.textContent = `Calibrated (${c.samples.length} frames).`;
  setTimeout(() => { if (!state.cal) el.calMsg.textContent = ''; }, 4000);
}

el.btnCal.onclick = () => startCalibration(false);

// ------------------------------------------------------------------ pause / demo
el.btnPause.onclick = async () => {
  state.paused = !state.paused;
  el.btnPause.setAttribute('aria-pressed', String(state.paused));
  el.btnPause.textContent = state.paused ? 'Resume tracking' : 'Pause tracking';
  if (state.paused) {
    state.cal = null; el.btnCal.disabled = true; el.calMsg.textContent = '';
    frameReader?.cancel().catch(() => {}); frameReader = null;
    vision?.stopCamera(el.video);
    machine.reset(); state.mood = 'neutral'; state.lookX = state.lookY = 0; state.cls = null; state.lastFrameT = null;
    drawOverlay(null);
    el.camMsg.textContent = 'tracking paused — camera off';
    syncNow(); // E neutral 0 0
    scheduleRender();
  } else {
    el.btnCal.disabled = false;
    await startTracking();
  }
};

function setDemo(on) {
  state.demo = on;
  el.demo.hidden = !on;
  el.btnCal.disabled = on;
  el.btnPause.disabled = on;
  scheduleRender();
}
// Expression buttons: Auto (camera decides) or hold one expression on the board.
const exprButtons = [...document.querySelectorAll('#exprBtns button')];
function setExpression(expr) {
  state.manual = expr === 'auto' ? null : expr;
  for (const btn of exprButtons) btn.setAttribute('aria-pressed', String(btn.dataset.expr === expr));
  syncNow();   // preview and board start the switch-blink at the same instant
  scheduleRender();
}
for (const btn of exprButtons) btn.onclick = () => setExpression(btn.dataset.expr);

// ------------------------------------------------------------------ overlay
function drawOverlay(landmarks) {
  const c = el.overlay;
  const ctx = c.getContext('2d');
  if (!el.chkOverlay.checked || !landmarks) { ctx.clearRect(0, 0, c.width, c.height); return; }
  const w = el.video.videoWidth || 640, h = el.video.videoHeight || 480;
  if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = 'rgba(232,180,0,.85)';
  for (const p of landmarks) ctx.fillRect(p.x * w - 1, p.y * h - 1, 2, 2);
  const s = state.signals;
  if (s?.face) { // face centre used for eye-follow
    ctx.strokeStyle = '#fff'; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(s.cx * w, s.cy * h, 8, 0, Math.PI * 2); ctx.stroke();
  }
}
el.chkOverlay.onchange = () => drawOverlay(null);

// ------------------------------------------------------------------ tuning UI
const SLIDERS = {
  slidersThresh: [
    ['thresholds', 'yawDeg', 'head turn (°)', 5, 60, 1],
    ['thresholds', 'pitchDeg', 'head down (°)', 5, 45, 1],
    ['thresholds', 'gazeDown', 'eyes down', 0.05, 1, 0.01],
    ['thresholds', 'gazeSide', 'eyes sideways', 0.05, 1, 0.01],
    ['thresholds', 'blink', 'blink (eyes closed)', 0.2, 0.9, 0.01],
  ],
  slidersTiming: [
    ['timings', 'sideDelay', 'away → side-eye', 0, 3, 0.05],
    ['timings', 'sadDelay', 'side-eye → disappointed', 0.5, 10, 0.1],
    ['timings', 'returnDelay', 'back → welcome', 0.05, 2, 0.05],
    ['timings', 'happyDuration', 'welcome back lasts', 0.3, 5, 0.1],
  ],
  slidersLook: [
    ['look', 'gain', 'follow gain', 0.5, 3, 0.1],
    ['look', 'tauMs', 'smoothing (ms)', 30, 600, 10],
  ],
};
const sliderInputs = [];

function buildSliders() {
  for (const [containerId, defs] of Object.entries(SLIDERS)) {
    const box = $(containerId);
    box.textContent = '';
    for (const [group, key, label, min, max, step] of defs) {
      const row = document.createElement('label');
      row.className = 'slider';
      row.innerHTML = `<span>${label}</span><input type="range" min="${min}" max="${max}" step="${step}"><output></output>`;
      const input = row.querySelector('input'), out = row.querySelector('output');
      const decimals = step < 0.1 ? 2 : step < 1 ? 1 : 0;
      const sync = () => { input.value = settings[group][key]; out.textContent = (+settings[group][key]).toFixed(decimals); };
      input.oninput = () => {
        settings[group][key] = +input.value;
        out.textContent = (+input.value).toFixed(decimals);
        if (group === 'timings') machine.setTimings(settings.timings);
        saveSettings();
      };
      sync();
      sliderInputs.push(sync);
      box.appendChild(row);
    }
  }
}

el.btnDefaults.onclick = () => {
  Object.assign(settings.thresholds, DEFAULT_THRESHOLDS);
  Object.assign(settings.timings, DEFAULT_TIMINGS);
  Object.assign(settings.look, DEFAULT_LOOK);
  machine.setTimings(settings.timings);
  saveSettings();
  sliderInputs.forEach((f) => f());
};

const SIG_ROWS = [
  // [label, signal key, baseline key, threshold key, trip(delta, th)]
  ['face', 'face'],
  ['yaw (°)', 'yaw', 'yaw', 'yawDeg', (d, th) => Math.abs(d) > th],
  ['pitch (°)', 'pitch', 'pitch', 'pitchDeg', (d, th) => d < -th],
  ['gaze down', 'gazeDown', 'gazeDown', 'gazeDown', (d, th) => d > th],
  ['gaze side', 'gazeSide', 'gazeSide', 'gazeSide', (d, th) => Math.abs(d) > th],
  ['gaze up', 'gazeUp'],
  ['blink', 'blink', null, 'blink', (v, th) => v > th],
  ['roll (°)', 'roll'],
  ['face x, y', 'cx'],
  ['look X, Y', 'look'],
  ['frame class', 'cls'],
  ['fps', 'fps'],
];
const sigCells = SIG_ROWS.map(([label]) => {
  const tr = document.createElement('tr');
  tr.innerHTML = `<td>${label}</td><td></td><td></td><td></td><td></td>`;
  el.sigBody.appendChild(tr);
  return tr;
});

const fmt = (v, d = 2) => (Number.isFinite(v) ? v.toFixed(d) : '—');

function renderSignals() {
  const s = state.signals, b = baseline, th = settings.thresholds;
  SIG_ROWS.forEach(([, key, bkey, tkey, trip], i) => {
    const tds = sigCells[i].children;
    let now = '—', base = '', delta = '', lim = '', tripped = false;
    if (key === 'face') now = s ? (s.face ? 'yes' : 'no') : '—';
    else if (key === 'cx') now = s?.face ? `${fmt(s.cx)}, ${fmt(s.cy)}` : '—';
    else if (key === 'look') now = `${Math.round(state.lookX)}, ${Math.round(state.lookY)}`;
    else if (key === 'cls') now = state.cls ?? '—';
    else if (key === 'fps') now = fmt(state.fps, 0);
    else if (s?.face) {
      const deg = key === 'yaw' || key === 'pitch' || key === 'roll';
      const v = s[key];
      now = fmt(v, deg ? 1 : 2);
      if (bkey) {
        const bv = b ? b[bkey] : 0;
        const d = v - bv;
        base = fmt(bv, deg ? 1 : 2);
        delta = (d >= 0 ? '+' : '') + fmt(d, deg ? 1 : 2);
        tripped = trip(d, th[tkey]);
      } else if (trip) {
        tripped = trip(v, th[tkey]);
      }
      if (tkey) lim = key === 'pitch' ? `< −${th[tkey]}` : key === 'blink' ? `> ${th[tkey]}` : key === 'gazeDown' ? `> ${th[tkey]}` : `± ${th[tkey]}`;
    }
    tds[1].textContent = now; tds[2].textContent = base; tds[3].textContent = delta; tds[4].textContent = lim;
    sigCells[i].classList.toggle('trip', tripped);
  });
  el.baselineInfo.textContent = baseline
    ? `Baseline: yaw ${fmt(baseline.yaw, 1)}°, pitch ${fmt(baseline.pitch, 1)}°, gaze down ${fmt(baseline.gazeDown)}, gaze side ${fmt(baseline.gazeSide)}`
    : 'Not calibrated yet (calibration runs automatically when your face is first seen).';
}

// ------------------------------------------------------------------ status render (once per animation frame)
let renderQueued = false;
function scheduleRender() {
  if (renderQueued) return;
  renderQueued = true;
  requestAnimationFrame(() => { renderQueued = false; render(); });
}

function render() {
  const [mood] = currentFrame();
  if (state.demo) {
    el.cls.textContent = 'demo'; el.cls.dataset.cls = '';
    el.reasons.innerHTML = '&nbsp;';
  } else if (state.paused) {
    el.cls.textContent = 'paused'; el.cls.dataset.cls = '';
    el.reasons.innerHTML = '&nbsp;';
  } else {
    const shown = state.cls === 'HOLD' ? state.effective : state.cls; // HOLD keeps the previous call
    el.cls.textContent = shown || '—';
    el.cls.dataset.cls = shown || '';
    el.reasons.textContent = state.cls === 'HOLD' ? 'blink (holding)' : state.reasons.join(', ') || ' ';
  }
  el.mood.textContent = MOOD_LABELS[mood];
  el.mood.dataset.mood = mood;
  el.moodWord.textContent = state.manual ? `${mood} · held (press Auto to let the camera decide)` : mood;
  document.body.dataset.cls = state.cls || '';
  if (vision?.ready && !state.demo && !state.paused) {
    el.visionStatus.textContent = `face model ready (${vision.delegate}) · ${state.signals?.face ? 'face found' : 'no face'} · ${fmt(state.fps, 0)} fps`;
  }
  if (el.tuning.open) renderSignals();
}

// ------------------------------------------------------------------ boot
buildSliders();
renderCaught();
renderConnButtons();
scheduleRender();
if (!window.isSecureContext) {
  el.camMsg.textContent = 'Open via http://localhost:8000 (camera needs a secure context).';
}
usb.autoReconnect().catch(() => {});
startTracking();
