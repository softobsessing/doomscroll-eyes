// eyes.js — line-for-line port of the board's EyeBuddy/eyes.cpp (+ the touch part of brain.cpp),
// so the on-screen preview shows exactly what the round AMOLED shows. If you change the shapes or
// timings in eyes.cpp, change them here too (test/eyes_port.test.mjs compares the two).

export const MOODS = ['neutral', 'happy', 'side', 'sad'];
const NEUTRAL = 0, HAPPY = 1, SIDE = 2, SAD = 3;
export const SCREEN = 466;

// spacing, gazeX, gazeY, halfW, halfH  (same table as eyes.cpp)
const STYLES = [
  [100, 70, 55, 36, 36],
  [105, 30, 20, 40, 27],
  [98, 45, 25, 74, 37],
  [100, 30, 15, 70, 48],
];
const DOT_R = 36;
const HAPPY_HALF_W = 34, HAPPY_HALF_H = 21, HAPPY_STROKE = 6.0;
const SIDE_R = 74;
const SAD_RX = 70, SAD_RY = 66, SAD_H = 76;
const SAD_TILT = 7.0 * 3.14159265 / 180.0;
const COS_T = Math.cos(SAD_TILT), SIN_T = Math.sin(SAD_TILT);
const CREASE_DROP = 17, CREASE_R = 130, CREASE_HALF = 2.2, CREASE_HALF_LEN = 50;
const MIN_CLOSED_H = 4;
const CX = SCREEN / 2, CY = SCREEN / 2;
const BLINK_CLOSE = 70, BLINK_SHUT = 100, BLINK_END = 230, DOUBLE_GAP = 80;

const clamp01 = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);
const len = (x, y) => Math.sqrt(x * x + y * y);

function sdEllipse(x, y, rx, ry) {
  const k0 = len(x / rx, y / ry);
  const k1 = len(x / (rx * rx), y / (ry * ry));
  return k1 > 1e-6 ? k0 * (k0 - 1) / k1 : -Math.min(rx, ry);
}
function sdSegment(px, py, ax, ay, bx, by) {
  const pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
  const d = bax * bax + bay * bay;
  const h = d > 1e-6 ? clamp01((pax * bax + pay * bay) / d) : 0;
  return len(pax - bax * h, pay - bay * h);
}
function sdBezier(px, py, ax, ay, bx, by, cx, cy) {
  const a_x = bx - ax, a_y = by - ay;
  const b_x = ax - 2 * bx + cx, b_y = ay - 2 * by + cy;
  const bb = b_x * b_x + b_y * b_y;
  if (bb < 1e-3) return sdSegment(px, py, ax, ay, cx, cy);
  const c_x = 2 * a_x, c_y = 2 * a_y, d_x = ax - px, d_y = ay - py;
  const kk = 1 / bb;
  const kx = kk * (a_x * b_x + a_y * b_y);
  const ky = kk * (2 * (a_x * a_x + a_y * a_y) + (d_x * b_x + d_y * b_y)) / 3;
  const kz = kk * (d_x * a_x + d_y * a_y);
  const p = ky - kx * kx, p3 = p * p * p;
  const q = kx * (2 * kx * kx - 3 * ky) + kz;
  let h = q * q + 4 * p3;
  const dist2 = (t) => {
    const x = d_x + (c_x + b_x * t) * t, y = d_y + (c_y + b_y * t) * t;
    return x * x + y * y;
  };
  if (h >= 0) {
    h = Math.sqrt(h);
    const t = Math.cbrt((h - q) / 2) + Math.cbrt((-h - q) / 2) - kx;
    return Math.sqrt(dist2(clamp01(t)));
  }
  const z = Math.sqrt(-p);
  const v = Math.acos(q / (p * z * 2)) / 3;
  const m = Math.cos(v), n = Math.sin(v) * 1.732050808;
  const t0 = clamp01((m + m) * z - kx), t1 = clamp01((-n - m) * z - kx);
  return Math.sqrt(Math.min(dist2(t0), dist2(t1)));
}

// returns [distance, creaseCoverage]
function shapeDistance(shape, x, y, s, side) {
  switch (shape) {
    case NEUTRAL: return [sdEllipse(x, y, DOT_R, Math.max(DOT_R * s, MIN_CLOSED_H / 2)), 0];
    case HAPPY: {
      const h = HAPPY_HALF_H * s;
      return [sdBezier(x, y, -HAPPY_HALF_W, h, 0, -3 * h, HAPPY_HALF_W, h) - HAPPY_STROKE, 0];
    }
    case SIDE: {
      const H = Math.max(SIDE_R * s, MIN_CLOSED_H);
      const ry = SIDE_R * (H / SIDE_R), ecy = H / 2 - ry;
      return [Math.max(sdEllipse(x, y - ecy, SIDE_R, ry), -H / 2 - y), 0];
    }
    case SAD: {
      const H = Math.max(SAD_H * s, MIN_CLOSED_H);
      const k = H / SAD_H;
      const ry = SAD_RY * k, ecy = H / 2 - ry, L = -H / 2;
      const ct = COS_T, st = side * SIN_T;
      const qx = x * ct + (y - L) * st;
      const qy = -x * st + (y - L) * ct;
      const d = Math.max(sdEllipse(x, y - ecy, SAD_RX, ry), -qy);
      const dc = Math.abs(len(qx, qy - (CREASE_DROP * k - CREASE_R)) - CREASE_R) - CREASE_HALF;
      const ends = clamp01((CREASE_HALF_LEN - Math.abs(qx)) / 8);
      return [d, clamp01(0.5 - dc) * ends * clamp01((s - 0.5) / 0.3)];
    }
  }
  return [1e9, 0];
}

const reached = (now, when) => ((now - when) | 0) >= 0;

export class Engine {
  constructor(seed = 1) {
    this.rng = (seed >>> 0) || 1;
    this.started = false; this.lastMs = 0;
    this.mood = NEUTRAL; this.target = NEUTRAL;
    this.blinkPhase = 0; this.blinkStart = 0; this.nextBlink = 0;
    this.autoBlink = true; this.blinkEvent = null;   // blinkEvent: {twice} once per self-started blink
    this.doubleBlink = false; this.swapped = false;
    this.lookTX = 0; this.lookTY = 0; this.lookX = 0; this.lookY = 0;
    this.jitX = 0; this.jitY = 0; this.nextJitter = 0;
    this.wander = false; this.wanderX = 0; this.wanderY = 0; this.nextWander = 0;
    this.eyes = [{}, {}];
  }
  rand32() {
    let r = this.rng;
    r ^= r << 13; r >>>= 0;
    r ^= r >>> 17;
    r ^= r << 5; r >>>= 0;
    return (this.rng = r);
  }
  randf(lo, hi) { return lo + (hi - lo) * (this.rand32() & 0xFFFFFF) / 0x1000000; }
  setMood(m) { const i = typeof m === 'string' ? MOODS.indexOf(m) : m; if (i >= 0) this.target = i; }
  setLook(x, y) { this.lookTX = Math.max(-1, Math.min(1, x)); this.lookTY = Math.max(-1, Math.min(1, y)); }
  setWander(on) { this.wander = on; }
  setAutoBlink(on) { this.autoBlink = on; }
  blinkNow(now, twice) {
    if (this.blinkPhase || this.target !== this.mood) return false;
    this.startBlink(now >>> 0); this.doubleBlink = !!twice; return true;
  }
  takeBlinkEvent() { const e = this.blinkEvent; this.blinkEvent = null; return e; }
  get jitterX() { return this.jitX; }
  get jitterY() { return this.jitY; }
  get moodName() { return MOODS[this.mood]; }
  get blinking() { return this.blinkPhase !== 0; }

  startBlink(now) { this.blinkPhase = 1; this.blinkStart = now; this.swapped = false; this.doubleBlink = false; }
  blinkOpenness(now) {
    let t = (now - this.blinkStart) >>> 0;
    if (!this.swapped && t >= BLINK_CLOSE) { this.mood = this.target; this.swapped = true; }   // see eyes.cpp
    const total = this.doubleBlink ? 2 * BLINK_END + DOUBLE_GAP : BLINK_END;
    if (t >= total) { this.blinkPhase = 0; return 1; }
    if (t >= BLINK_END) {
      if (t < BLINK_END + DOUBLE_GAP) return 1;
      t -= BLINK_END + DOUBLE_GAP;
    }
    if (t < BLINK_CLOSE) { const u = t / BLINK_CLOSE; return 1 - u * u; }
    if (t < BLINK_SHUT) return 0;
    const u = (t - BLINK_SHUT) / (BLINK_END - BLINK_SHUT);
    return 1 - (1 - u) * (1 - u);
  }

  update(now) {
    now >>>= 0;
    if (!this.started) {
      this.started = true; this.lastMs = now;
      this.nextBlink = (now + Math.trunc(this.randf(1500, 4000))) >>> 0;
      this.nextJitter = this.nextWander = now;
    }
    let dt = ((now - this.lastMs) | 0) / 1000;
    if (dt < 0) dt = 0;
    if (dt > 0.1) dt = 0.1;
    this.lastMs = now;

    if (!this.blinkPhase && (this.target !== this.mood || (this.autoBlink && reached(now, this.nextBlink)))) {
      const natural = this.target === this.mood;
      this.startBlink(now);
      this.doubleBlink = natural && this.randf(0, 1) < 0.15;
      if (natural) this.blinkEvent = { twice: this.doubleBlink };
    }
    let open = 1;
    if (this.blinkPhase) {
      open = this.blinkOpenness(now);
      if (!this.blinkPhase) this.nextBlink = (now + Math.trunc(this.randf(2000, 6000))) >>> 0;
    }

    if (this.wander && reached(now, this.nextWander)) {
      if (this.randf(0, 1) < 0.3) this.wanderX = this.wanderY = 0;
      else { this.wanderX = this.randf(-0.7, 0.7); this.wanderY = this.randf(-0.45, 0.45); }
      this.nextWander = (now + Math.trunc(this.randf(900, 3200))) >>> 0;
    }
    if (!this.autoBlink) {
      this.jitX = this.jitY = 0;
    } else if (reached(now, this.nextJitter)) {
      this.jitX = this.randf(-0.05, 0.05);
      this.jitY = this.randf(-0.04, 0.04);
      this.nextJitter = (now + Math.trunc(this.randf(400, 1600))) >>> 0;
    }
    const tx = (this.wander ? this.wanderX : this.lookTX) + this.jitX;
    const ty = (this.wander ? this.wanderY : this.lookTY) + this.jitY;
    const a = 1 - Math.exp(-dt / 0.055);
    this.lookX += (tx - this.lookX) * a;
    this.lookY += (ty - this.lookY) * a;

    const breathe = 1 + 0.018 * Math.sin(now * (6.2831853 / 4200));
    const [spacing, gazeX, gazeY, halfW, halfH] = STYLES[this.mood];
    for (let i = 0; i < 2; i++) {
      const e = this.eyes[i];
      e.shape = this.mood;
      e.side = i === 0 ? -1 : 1;
      e.cx = CX + e.side * spacing + this.lookX * gazeX;
      e.cy = CY + this.lookY * gazeY;
      e.scale = breathe;
      e.squash = open;
      const hw = halfW * breathe + 3, hh = halfH * breathe + 3;
      e.x0 = Math.floor(e.cx - hw); e.x1 = Math.ceil(e.cx + hw);
      e.y0 = Math.floor(e.cy - hh); e.y1 = Math.ceil(e.cy + hh);
    }
  }

  // RGB565 colour of logical pixel (px, py), exactly like Engine::shade on the board
  shade(px, py) {
    let alpha = 0, crease = 0;
    for (const e of this.eyes) {
      if (px < e.x0 || px > e.x1 || py < e.y0 || py > e.y1) continue;
      const x = (px + 0.5 - e.cx) / e.scale, y = (py + 0.5 - e.cy) / e.scale;
      const [dl, c] = shapeDistance(e.shape, x, y, e.squash, e.side);
      const al = clamp01(0.5 - dl * e.scale);
      if (al > alpha) { alpha = al; crease = c; }
    }
    if (alpha <= 0) return 0;
    const r = 29 + (20 - 29) * crease, g = 49 + (30 - 49) * crease, b = 8 + (6 - 8) * crease;
    return (((r * alpha + 0.5) | 0) << 11) | (((g * alpha + 0.5) | 0) << 5) | ((b * alpha + 0.5) | 0);
  }
}

// Mirrors brain.cpp: touch beats laptop. Touch arrives as the board's "T ..." lines.
export class Mirror {
  constructor(engine) { this.e = engine; this.override = null; this.held = false; }
  // any touch: the welcome-back smile while the finger is down, plus 2.5 s after it lifts
  touch(kind, now) {
    if (kind === 'touch') { this.held = true; this.override = { mood: HAPPY, until: now }; }
    else if (kind === 'release') { this.held = false; if (this.override) this.override.until = now + 2500; }
  }
  // linked: is the laptop driving it? (if not, the board just wanders)
  update(now, linked, mood, lookX, lookY) {
    if (this.override && !this.held && now >= this.override.until) this.override = null;
    if (this.override) { this.e.setWander(false); this.e.setMood(this.override.mood); this.e.setLook(linked ? lookX / 100 : 0, linked ? lookY / 100 : 0); }
    else if (linked) { this.e.setWander(false); this.e.setMood(mood); this.e.setLook(lookX / 100, lookY / 100); }
    else { this.e.setWander(true); this.e.setMood(NEUTRAL); }
  }
}

// Draws the engine into a 466x466 canvas, only touching the boxes around the eyes.
export class ScreenPreview {
  constructor(canvas) {
    canvas.width = canvas.height = SCREEN;
    this.ctx = canvas.getContext('2d');
    this.img = this.ctx.createImageData(SCREEN, SCREEN);
    this.px = new Uint32Array(this.img.data.buffer);
    this.px.fill(0xFF000000);
    this.prev = [];
    // RGB565 -> RGBA (little-endian ABGR) lookup, same expansion as the Mac preview tool
    this.lut = new Uint32Array(65536);
    for (let c = 0; c < 65536; c++) {
      const r = (((c >> 11) & 31) * 255 / 31) | 0, g = (((c >> 5) & 63) * 255 / 63) | 0, b = ((c & 31) * 255 / 31) | 0;
      this.lut[c] = 0xFF000000 | (b << 16) | (g << 8) | r;
    }
  }
  draw(engine) {
    const boxes = engine.eyes.map((e) => [e.x0, e.y0, e.x1, e.y1]);
    for (const [x0, y0, x1, y1] of this.prev.concat(boxes)) {
      const ax = Math.max(0, x0), ay = Math.max(0, y0), bx = Math.min(SCREEN - 1, x1), by = Math.min(SCREEN - 1, y1);
      for (let y = ay; y <= by; y++) {
        let i = y * SCREEN + ax;
        for (let x = ax; x <= bx; x++) this.px[i++] = this.lut[engine.shade(x, y)];
      }
    }
    this.prev = boxes;
    this.ctx.putImageData(this.img, 0, 0);
  }
}
