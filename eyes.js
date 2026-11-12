// eyes.js — line-for-line port of the board's EyeBuddy/eyes.cpp (+ the touch part of brain.cpp),
// so the on-screen preview shows exactly what the round AMOLED shows. If you change the shapes or
// timings in eyes.cpp, change them here too (test/eyes_port.test.mjs compares the two).

// neutral = focused (tall ovals, the default), wow = surprised, chill = chilling (round dots),
// angry / dizzy evolve out of the focused ovals (dizzy ends in swirls, then melts back into focused)
export const MOODS = ['neutral', 'happy', 'side', 'sad', 'wow', 'chill', 'angry', 'dizzy'];
const NEUTRAL = 0, HAPPY = 1, SIDE = 2, SAD = 3, WOW = 4, CHILL = 5, ANGRY = 6, DIZZY = 7;
export const DEFAULT_EYE_COLOR = 0xEFC742, DEFAULT_FACE_COLOR = 0x000000;
const CREASE_DARKEN = 0.66;
export const SCREEN = 466;

// spacing, gazeX, gazeY, halfW, halfH  (same table as eyes.cpp)
const STYLES = [
  [103, 50, 40, 55, 74],   // focused: tall ovals
  [105, 30, 20, 40, 27],
  [98, 45, 25, 74, 37],
  [100, 30, 15, 70, 48],
  [105, 25, 15, 101, 140],   // WOW: spacing/size animate, see WOW_KEYS
  [100, 70, 55, 36, 36],     // CHILL: the original round dots
  [98, 40, 30, 72, 80],      // ANGRY: shape animates, see ANGRY_KEYS
  [103, 15, 10, 80, 80],     // DIZZY: shape animates, see DIZZY_KEYS
];

// ---- smooth keyframe curves for angry + dizzy (same as eyes.cpp) ----
function smoothKeys(keys, field, t) {
  const n = keys.length;
  if (t <= keys[0][0]) return keys[0][field];
  if (t >= keys[n - 1][0]) return keys[n - 1][field];
  let i = 0;
  while (t >= keys[i + 1][0]) i++;
  const tangent = (k) => {
    if (k <= 0 || k >= n - 1) return 0;
    const a = keys[k][field] - keys[k - 1][field], b = keys[k + 1][field] - keys[k][field];
    if (a * b <= 0) return 0;
    return (keys[k + 1][field] - keys[k - 1][field]) / (keys[k + 1][0] - keys[k - 1][0]);
  };
  const h = keys[i + 1][0] - keys[i][0], u = (t - keys[i][0]) / h;
  const u2 = u * u, u3 = u2 * u;
  return (2 * u3 - 3 * u2 + 1) * keys[i][field] + (u3 - 2 * u2 + u) * h * tangent(i) +
    (-2 * u3 + 3 * u2) * keys[i + 1][field] + (u3 - u2) * h * tangent(i + 1);
}
// t (s), spacing, centre y, rx, ry, lid y at the eye centre, lid slope toward the nose, corner rounding
const ANGRY_KEYS = [
  [0.00, 103, 0, 55, 74, -115.0, 0.55, 0],
  [0.40, 98, -11, 72, 69, -28.0, 0.55, 8],
  [0.85, 98, -12, 56, 54, -13.5, 0.60, 8],
];
const ANGRY_RELEASE_MS = 700;   // angry -> focused: smoothstep back to the focused ovals
// t (s), tilt (deg), spacing, centre x, centre y, rx, ry, swirl 0..1
const DIZZY_KEYS = [
  [0.00, 0, 103, 0, 0, 55, 74, 0],
  [0.35, 36, 100, -3, -9, 58, 78, 0],
  [0.75, -41, 104, 32, 27, 58, 78, 0],
  [1.15, 47, 107, -1, -1, 64, 70, 0],
  [1.55, -9, 100, 0, 0, 74, 41, 0],
  [1.90, 0, 104, 0, -2, 60, 60, 1],
  [2.90, 0, 104, 0, -2, 60, 60, 1],
  [3.30, 0, 103, 0, 0, 55, 74, 0],
];
export const DIZZY_TOTAL_MS = 3300;
const SWIRL_R = 64, SWIRL_TURNS = 2.75, SWIRL_HALF = 5.5, SWIRL_MIN = 0.6, SWIRL_SPIN = 12.566;
const TWO_PI = 6.2831853;
function sdSwirl(x, y, spin) {
  const thetaMax = SWIRL_TURNS * TWO_PI, b = SWIRL_R / thetaMax;
  const r = len(x, y);
  let phi = (Math.atan2(y, x) - spin) % TWO_PI;
  if (phi < 0) phi += TWO_PI;
  const n = Math.floor((r / b - phi) / TWO_PI + 0.5);
  let best = 1e9;
  for (let k = -1; k <= 1; k++) {
    const th = phi + TWO_PI * (n + k);
    const tc = th < SWIRL_MIN ? SWIRL_MIN : th > thetaMax ? thetaMax : th;
    const dr = r - b * tc, da = (th - tc) * b * tc;
    best = Math.min(best, Math.sqrt(dr * dr + da * da));
  }
  return best - SWIRL_HALF;
}

// surprised: the eyes grow through the four reference frames, smallest to biggest (same table as eyes.cpp)
// t (s), spacing, rx, ry, pupil r, pupil toward nose
const WOW_KEYS = [
  [0.00, 89, 53, 35, 3.0, 0],
  [0.22, 112, 95, 69, 5.0, 58],
  [0.50, 98, 83, 110, 3.5, 0],
  [0.85, 105, 101, 140, 3.0, 0],
];
const catmull = (p0, p1, p2, p3, u) =>
  0.5 * (2 * p1 + (p2 - p0) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u + (3 * p1 - p0 - 3 * p2 + p3) * u * u * u);
function wowAt(t) {
  const n = WOW_KEYS.length;
  const pack = (k) => ({ spacing: k[1], rx: k[2], ry: k[3], pupilR: k[4], pupilIn: k[5] });
  if (t <= WOW_KEYS[0][0]) return pack(WOW_KEYS[0]);
  if (t >= WOW_KEYS[n - 1][0]) return pack(WOW_KEYS[n - 1]);
  let i = 0;
  while (t >= WOW_KEYS[i + 1][0]) i++;
  const k0 = WOW_KEYS[i > 0 ? i - 1 : 0], k1 = WOW_KEYS[i], k2 = WOW_KEYS[i + 1], k3 = WOW_KEYS[i + 2 < n ? i + 2 : n - 1];
  const u = (t - k1[0]) / (k2[0] - k1[0]);
  const c = (j) => catmull(k0[j], k1[j], k2[j], k3[j], u);
  return { spacing: c(1), rx: c(2), ry: c(3), pupilR: Math.max(1.5, c(4)), pupilIn: Math.max(0, c(5)) };
}
const FOCUS_RX = 55, FOCUS_RY = 74;   // focused: tall ovals
const DOT_R = 36;                     // chilling: round dots
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
function shapeDistance(e, x, y) {
  const s = e.squash, side = e.side;
  switch (e.shape) {
    case NEUTRAL: return [sdEllipse(x, y, FOCUS_RX, Math.max(FOCUS_RY * s, MIN_CLOSED_H / 2)), 0];
    case CHILL: return [sdEllipse(x, y, DOT_R, Math.max(DOT_R * s, MIN_CLOSED_H / 2)), 0];
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
    case WOW: return [sdEllipse(x, y, e.rx, Math.max(e.ry * s, MIN_CLOSED_H / 2)), 0];
    case ANGRY: {
      const sq = Math.max(s, MIN_CLOSED_H / (2 * e.ry));
      const yy = y / sq, xi = x * -side;
      const R = e.round, norm = Math.sqrt(1 + e.lidK * e.lidK);
      const body = sdEllipse(x, yy, e.rx - R, e.ry - R);
      const lid = (e.lidC + R * norm + e.lidK * xi - yy) / norm;
      return [(Math.max(body, lid) - R) * sq, 0];
    }
    case DIZZY: {
      const rx = x * e.cosr + y * e.sinr, ry = -x * e.sinr + y * e.cosr;
      let d = e.swirl < 1 ? sdEllipse(rx, ry, e.rx, e.ry) : 0;
      if (e.swirl > 0) d = d * (1 - e.swirl) + sdSwirl(x, y, e.spin) * e.swirl;
      return [d, 0];
    }
  }
  return [1e9, 0];
}

const reached = (now, when) => ((now - when) | 0) >= 0;

// ---- timer screen: MM:SS in seven-segment digits + a ring that empties (same as eyes.cpp) ----
const DIGIT_W = 64, DIGIT_H = 120, DIGIT_T = 14, DIGIT_GAP = 22, COLON_W = 34, SEG_INSET = 11;
const RING_R = 212, RING_HALF = 4, RING_DIM = 0.22;
const SEGMENTS = [0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F];
function digitCoverage(digit, x, y, cx) {
  const xl = cx - DIGIT_W / 2 + DIGIT_T / 2, xr = cx + DIGIT_W / 2 - DIGIT_T / 2;
  const yt = -DIGIT_H / 2 + DIGIT_T / 2, yb = DIGIT_H / 2 - DIGIT_T / 2, s = SEG_INSET;
  const m = SEGMENTS[digit];
  let d = 1e9;
  if (m & 0x01) d = Math.min(d, sdSegment(x, y, xl + s, yt, xr - s, yt));
  if (m & 0x02) d = Math.min(d, sdSegment(x, y, xr, yt + s, xr, -s));
  if (m & 0x04) d = Math.min(d, sdSegment(x, y, xr, s, xr, yb - s));
  if (m & 0x08) d = Math.min(d, sdSegment(x, y, xl + s, yb, xr - s, yb));
  if (m & 0x10) d = Math.min(d, sdSegment(x, y, xl, s, xl, yb - s));
  if (m & 0x20) d = Math.min(d, sdSegment(x, y, xl, yt + s, xl, -s));
  if (m & 0x40) d = Math.min(d, sdSegment(x, y, xl + s, 0, xr - s, 0));
  return clamp01(0.5 - (d - DIGIT_T / 2));
}

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
    this.moodSince = 0;
    this.releasing = false; this.releaseStart = 0; this.releaseFromT = 0;   // angry -> focused settle
    this.fullVersion = 1;   // bumps whenever the whole screen must be redrawn
    this.pomoActive = false; this.timerShown = false;
    this.pomoEnd = 0; this.pomoTotalMs = 0; this.timerUntil = 0; this.timerSecs = 0; this.timerFrac = 0;
    this.setColors(DEFAULT_FACE_COLOR, DEFAULT_EYE_COLOR);
  }
  // ---- pomodoro (same as eyes.cpp): the countdown runs in the engine ----
  setPomodoro(now, secondsLeft, totalSeconds) {
    now >>>= 0;
    if (!secondsLeft) { this.pomoActive = false; this.timerUntil = 0; return; }
    this.pomoActive = true;
    this.pomoEnd = (now + secondsLeft * 1000) >>> 0;
    this.pomoTotalMs = (totalSeconds || secondsLeft) * 1000;
  }
  flashTimer(now, ms = 2000) {
    if (!this.pomoActive) return;
    this.timerUntil = ((now >>> 0) + ms) | 1;
  }
  pomodoroSecondsLeft(now) {
    if (!this.pomoActive || reached(now >>> 0, this.pomoEnd)) return 0;
    return Math.trunc((((this.pomoEnd - (now >>> 0)) >>> 0) + 999) / 1000);
  }
  get pomodoroActive() { return this.pomoActive; }
  // colors as 0xRRGGBB: face = the screen (and the pupils), eye = the eyes. Same math as eyes.cpp.
  setColors(face, eye) {
    face &= 0xFFFFFF; eye &= 0xFFFFFF;
    if (face !== this.face || (this.timerShown && eye !== this.eye)) this.fullVersion++;
    this.face = face; this.eye = eye;
    this.faceR = ((face >> 16) & 0xFF) * 31 / 255; this.faceG = ((face >> 8) & 0xFF) * 63 / 255; this.faceB = (face & 0xFF) * 31 / 255;
    this.eyeR = ((eye >> 16) & 0xFF) * 31 / 255; this.eyeG = ((eye >> 8) & 0xFF) * 63 / 255; this.eyeB = (eye & 0xFF) * 31 / 255;
    this.face565 = (Math.trunc(this.faceR + 0.5) << 11) | (Math.trunc(this.faceG + 0.5) << 5) | Math.trunc(this.faceB + 0.5);
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
  dizzyRunning(now) { return this.mood === DIZZY && (((now >>> 0) - this.moodSince) >>> 0) < DIZZY_TOTAL_MS; }
  blinkNow(now, twice) {
    if (this.blinkPhase || this.target !== this.mood || this.dizzyRunning(now) || this.releasing) return false;
    this.startBlink(now >>> 0); this.doubleBlink = !!twice; return true;
  }
  angryParam(field, animT, now) {
    if (!this.releasing) return smoothKeys(ANGRY_KEYS, field, animT);
    const a = smoothKeys(ANGRY_KEYS, field, this.releaseFromT);
    const u = clamp01(((now - this.releaseStart) >>> 0) / ANGRY_RELEASE_MS), k = u * u * (3 - 2 * u);
    return a + (ANGRY_KEYS[0][field] - a) * k;
  }
  takeBlinkEvent() { const e = this.blinkEvent; this.blinkEvent = null; return e; }
  get jitterX() { return this.jitX; }
  get jitterY() { return this.jitY; }
  get moodName() { return MOODS[this.mood]; }
  get blinking() { return this.blinkPhase !== 0; }

  startBlink(now) { this.blinkPhase = 1; this.blinkStart = now; this.swapped = false; this.doubleBlink = false; }
  blinkOpenness(now) {
    let t = (now - this.blinkStart) >>> 0;
    if (!this.swapped && t >= BLINK_CLOSE) { this.mood = this.target; this.swapped = true; this.moodSince = now; }   // see eyes.cpp
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
      this.moodSince = now;
    }
    let dt = ((now - this.lastMs) | 0) / 1000;
    if (dt < 0) dt = 0;
    if (dt > 0.1) dt = 0.1;
    this.lastMs = now;

    if (!this.blinkPhase && !this.releasing && this.mood === ANGRY && this.target === NEUTRAL) {   // see eyes.cpp
      this.releasing = true; this.releaseStart = now;
      this.releaseFromT = ((now - this.moodSince) >>> 0) / 1000;
    }
    if (this.releasing && ((now - this.releaseStart) >>> 0) >= ANGRY_RELEASE_MS) {
      this.releasing = false; this.mood = NEUTRAL; this.moodSince = now;
    }
    if (!this.blinkPhase && this.target !== this.mood) {
      const fromFocus = this.mood === NEUTRAL && (this.target === ANGRY || this.target === DIZZY);
      const dizzyDone = this.mood === DIZZY && !this.dizzyRunning(now) && this.target === NEUTRAL;
      if (fromFocus || dizzyDone) { this.mood = this.target; this.moodSince = now; }
    }
    const quiet = this.dizzyRunning(now) || this.releasing;
    if (!this.blinkPhase && !this.releasing && (this.target !== this.mood || (this.autoBlink && !quiet && reached(now, this.nextBlink)))) {
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
    let shape = this.mood;
    if (this.mood === DIZZY && !this.dizzyRunning(now)) shape = NEUTRAL;
    let [spacing, gazeX, gazeY, halfW, halfH] = STYLES[shape];
    const animT = ((now - this.moodSince) >>> 0) / 1000;
    let pairRot = 0, offX = 0, offY = 0, eyeCy = 0, rx = 0, ry = 0, lidC = 0, lidK = 0, rnd = 0, swirl = 0;
    if (shape === ANGRY) {
      spacing = this.angryParam(1, animT, now); eyeCy = this.angryParam(2, animT, now);
      rx = this.angryParam(3, animT, now); ry = this.angryParam(4, animT, now);
      lidC = this.angryParam(5, animT, now); lidK = this.angryParam(6, animT, now); rnd = this.angryParam(7, animT, now);
      halfW = rx; halfH = ry + Math.abs(eyeCy);
    } else if (shape === DIZZY) {
      const K = DIZZY_KEYS;
      pairRot = smoothKeys(K, 1, animT) * (3.14159265 / 180);
      spacing = smoothKeys(K, 2, animT); offX = smoothKeys(K, 3, animT); offY = smoothKeys(K, 4, animT);
      rx = smoothKeys(K, 5, animT); ry = smoothKeys(K, 6, animT);
      swirl = clamp01(smoothKeys(K, 7, animT));
      const c = Math.cos(pairRot), sn = Math.sin(pairRot);
      const hx = Math.sqrt(rx * c * rx * c + ry * sn * ry * sn), hy = Math.sqrt(rx * sn * rx * sn + ry * c * ry * c);
      const sw = swirl > 0 ? SWIRL_R + SWIRL_HALF : 0;
      halfW = Math.max(hx, sw); halfH = Math.max(hy, sw);
    }
    let wk = { spacing: 0, rx: 0, ry: 0, pupilR: 0, pupilIn: 0 };
    if (this.mood === WOW) {
      wk = wowAt(((now - this.moodSince) >>> 0) / 1000);
      spacing = wk.spacing; halfW = wk.rx; halfH = wk.ry;
    }
    for (let i = 0; i < 2; i++) {
      const e = this.eyes[i];
      e.shape = shape;
      e.side = i === 0 ? -1 : 1;
      e.cx = CX + e.side * spacing + this.lookX * gazeX;
      e.cy = CY + this.lookY * gazeY;
      e.rot = pairRot; e.cosr = Math.cos(pairRot); e.sinr = Math.sin(pairRot);
      e.lidC = lidC; e.lidK = lidK; e.round = rnd; e.swirl = swirl;
      e.spin = animT * SWIRL_SPIN;
      if (shape === DIZZY) {
        e.cx = CX + offX + e.side * spacing * e.cosr + this.lookX * gazeX;
        e.cy = CY + offY + e.side * spacing * e.sinr + this.lookY * gazeY;
      }
      if (shape === ANGRY) e.cy += eyeCy;
      e.scale = breathe;
      e.squash = open;
      e.rx = shape === WOW ? wk.rx : rx; e.ry = shape === WOW ? wk.ry : ry;
      e.pupilR = wk.pupilR; e.pupilIn = wk.pupilIn;
      e.pupilDX = this.lookX * wk.rx * 0.25;
      e.pupilDY = this.lookY * wk.ry * 0.2;
      const hw = halfW * breathe + 3, hh = halfH * breathe + 3;
      e.x0 = Math.floor(e.cx - hw); e.x1 = Math.ceil(e.cx + hw);
      e.y0 = Math.floor(e.cy - hh); e.y1 = Math.ceil(e.cy + hh);
    }

    // pomodoro: when time's up, show 00:00 for 3 s
    if (this.pomoActive && reached(now, this.pomoEnd)) { this.pomoActive = false; this.timerUntil = (now + 3000) | 1; }
    let shown = this.timerUntil !== 0 && !reached(now, this.timerUntil);
    if (!shown) this.timerUntil = 0;
    const secs = this.pomodoroSecondsLeft(now);
    if (shown !== this.timerShown || (shown && secs !== this.timerSecs)) this.fullVersion++;
    this.timerShown = shown;
    this.timerSecs = secs;
    this.timerFrac = this.pomoTotalMs ? Math.min(1, Math.fround(secs * 1000 / this.pomoTotalMs)) : 0;
  }

  shadeTimer(px, py) {
    const x = px + 0.5 - CX, y = py + 0.5 - CY;
    let a = 0;
    const r = len(x, y);
    const ring = clamp01(0.5 - (Math.abs(r - RING_R) - RING_HALF));
    if (ring > 0) {
      let ang = Math.atan2(x, -y);
      if (ang < 0) ang += 6.2831853;
      a = ring * (ang <= this.timerFrac * 6.2831853 ? 1 : RING_DIM);
    }
    const inner = COLON_W / 2 + DIGIT_W / 2, outer = COLON_W / 2 + DIGIT_W + DIGIT_GAP + DIGIT_W / 2;
    if (Math.abs(y) <= DIGIT_H / 2 + 2 && Math.abs(x) <= outer + DIGIT_W / 2 + 2) {
      const mm = Math.min(99, Math.trunc(this.timerSecs / 60)), ss = this.timerSecs % 60;
      const digits = [Math.trunc(mm / 10), mm % 10, Math.trunc(ss / 10), ss % 10];
      const centers = [-outer, -inner, inner, outer];
      const i = x < -(inner + outer) / 2 ? 0 : x < 0 ? 1 : x < (inner + outer) / 2 ? 2 : 3;
      a = Math.max(a, digitCoverage(digits[i], x, y, centers[i]));
      if (Math.abs(x) < COLON_W / 2) {
        const dc = Math.min(len(x, y + DIGIT_H * 0.2), len(x, y - DIGIT_H * 0.2)) - DIGIT_T * 0.6;
        a = Math.max(a, clamp01(0.5 - dc));
      }
    }
    return this.blend(a, 0);
  }

  blend(alpha, crease) {
    if (alpha <= 0) return this.face565;
    const er = this.eyeR + (this.eyeR * CREASE_DARKEN - this.eyeR) * crease;
    const eg = this.eyeG + (this.eyeG * CREASE_DARKEN - this.eyeG) * crease;
    const eb = this.eyeB + (this.eyeB * CREASE_DARKEN - this.eyeB) * crease;
    const r = this.faceR + (er - this.faceR) * alpha, g = this.faceG + (eg - this.faceG) * alpha, b = this.faceB + (eb - this.faceB) * alpha;
    return (Math.trunc(r + 0.5) << 11) | (Math.trunc(g + 0.5) << 5) | Math.trunc(b + 0.5);
  }

  // RGB565 colour of logical pixel (px, py), exactly like Engine::shade on the board
  shade(px, py) {
    if (this.timerShown) return this.shadeTimer(px, py);
    let alpha = 0, crease = 0;
    for (const e of this.eyes) {
      if (px < e.x0 || px > e.x1 || py < e.y0 || py > e.y1) continue;
      const x = (px + 0.5 - e.cx) / e.scale, y = (py + 0.5 - e.cy) / e.scale;
      const [dl, c] = shapeDistance(e, x, y);
      let al = clamp01(0.5 - dl * e.scale);
      if (e.shape === WOW && al > 0) {   // tiny pupil: a hole showing the screen color
        const pr = e.pupilR * clamp01((e.squash - 0.35) / 0.35);
        if (pr > 0) {
          const dp = (len(x - (-e.side * e.pupilIn + e.pupilDX), y - e.pupilDY * e.squash) - pr) * e.scale;
          al *= 1 - clamp01(0.5 - dp);
        }
      }
      if (al > alpha) { alpha = al; crease = c; }
    }
    return this.blend(alpha, crease);
  }
}

// Mirrors brain.cpp: touch beats laptop. Touch arrives as the board's "T ..." lines.
export class Mirror {
  constructor(engine) { this.e = engine; this.override = null; this.held = false; }
  // any touch: the welcome-back smile while the finger is down, plus 2.5 s after it lifts
  touch(kind, now) {
    if (kind === 'touch' && this.e.pomodoroActive) { this.e.flashTimer(Math.floor(now), 2000); return; }   // studying: show the time left
    if (kind === 'release' && !this.held) return;
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
    let rects = this.prev.concat(boxes);
    if (engine.fullVersion !== this.drawnVersion) {   // screen color / timer screen changed: redraw everything
      this.drawnVersion = engine.fullVersion;
      rects = [[0, 0, SCREEN - 1, SCREEN - 1]];
    }
    for (const [x0, y0, x1, y1] of rects) {
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
