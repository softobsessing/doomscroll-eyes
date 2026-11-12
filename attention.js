// attention.js — pure logic: per-frame classifier + mood state machine.
// No DOM, no clocks: every time value is passed in explicitly (seconds),
// so this file runs unchanged in the browser and in `node test/attention.test.mjs`.

export const MOODS = /** @type {const} */ (['neutral', 'happy', 'side', 'sad']);

export const MOOD_LABELS = {
  neutral: 'focused',
  happy: 'welcome back',
  side: 'side-eye',
  sad: 'disappointed',
  wow: 'surprised',   // button only (the camera never picks it); short name keeps E lines in one BLE packet
  chill: 'chilling',  // button only: the original round-dot eyes
};

export const DEFAULT_THRESHOLDS = {
  yawDeg: 25,      // |yaw - yaw0| above this => AWAY
  pitchDeg: 15,    // (pitch - pitch0) below -this (head tilted DOWN) => AWAY
  gazeDown: 0.35,  // (gazeDown - gazeDown0) above this => AWAY
  gazeSide: 0.5,   // |gazeSide - gazeSide0| above this => AWAY
  blink: 0.5,      // blink above this => eyes closed => HOLD
};

export const DEFAULT_TIMINGS = {
  sideDelay: 0,       // s of continuous AWAY before side-eye (0 = on the first away frame)
  sadDelay: 2.0,      // s still AWAY after entering side before sad
  returnDelay: 0.3,   // s of continuous ON (from side/sad) before happy
  happyDuration: 1.2, // s of happy before falling back to neutral
};

// Tolerance for float time arithmetic (t0 + 0.3) - t0 < 0.3.
const EPS = 1e-6;

export const ZERO_BASELINE = { yaw: 0, pitch: 0, gazeDown: 0, gazeSide: 0 };

/**
 * Classify one frame.
 * @param {{face:boolean, yaw:number, pitch:number, gazeDown:number, gazeSide:number, blink:number}} s
 * @param {{yaw:number, pitch:number, gazeDown:number, gazeSide:number} | null} baseline
 * @param {typeof DEFAULT_THRESHOLDS} th
 * @returns {{cls:'ON'|'AWAY'|'HOLD', reasons:string[]}}
 */
export function classifyFrame(s, baseline, th = DEFAULT_THRESHOLDS) {
  if (!s || !s.face) return { cls: 'AWAY', reasons: ['no face'] };
  if (s.blink > th.blink) return { cls: 'HOLD', reasons: ['blink'] };
  const b = baseline || ZERO_BASELINE;
  const reasons = [];
  if (Math.abs(s.yaw - b.yaw) > th.yawDeg) reasons.push('head turned');
  // Pitch convention (see vision.js): positive = nose up, negative = nose DOWN.
  if (s.pitch - b.pitch < -th.pitchDeg) reasons.push('head down');
  if (s.gazeDown - b.gazeDown > th.gazeDown) reasons.push('eyes down');
  if (Math.abs(s.gazeSide - b.gazeSide) > th.gazeSide) reasons.push('eyes sideways');
  return { cls: reasons.length ? 'AWAY' : 'ON', reasons };
}

/**
 * Mood state machine. Feed it the raw per-frame classification (ON/AWAY/HOLD)
 * with a monotonically increasing time in seconds.
 *
 *   ON steadily                         -> neutral
 *   AWAY continuously >= sideDelay      -> side
 *   still AWAY sadDelay after side      -> sad   (counts one "caught")
 *   side/sad, ON continuously returnDelay -> happy for happyDuration -> neutral
 *   HOLD (blink) keeps the previous effective classification.
 */
export class MoodMachine {
  constructor(timings = DEFAULT_TIMINGS) {
    this.timings = { ...DEFAULT_TIMINGS, ...timings };
    this.reset();
  }

  reset() {
    this.mood = 'neutral';
    this.effective = 'ON';    // last non-HOLD classification
    this.awaySince = null;    // start of current continuous AWAY run
    this.onSince = null;      // start of current continuous ON run
    this.sideAt = null;       // when side was entered
    this.happyUntil = null;
    this.caught = 0;          // sad entries since construction/reset
  }

  setTimings(t) { this.timings = { ...this.timings, ...t }; }

  /**
   * @param {'ON'|'AWAY'|'HOLD'} cls
   * @param {number} t seconds
   * @returns {{mood:string, effective:'ON'|'AWAY', changed:boolean, caught:boolean}}
   */
  update(cls, t) {
    const T = this.timings;
    const prev = this.mood;
    let caught = false;

    if (cls === 'ON' || cls === 'AWAY') this.effective = cls;
    const eff = this.effective;

    if (eff === 'AWAY') {
      this.onSince = null;
      if (this.awaySince === null) this.awaySince = t;
    } else {
      this.awaySince = null;
      if (this.onSince === null) this.onSince = t;
    }

    // happy times out on its own, whatever is happening.
    if (this.mood === 'happy' && t >= this.happyUntil - EPS) this._enter('neutral', t);

    if (eff === 'AWAY') {
      if ((this.mood === 'neutral' || this.mood === 'happy') && t - this.awaySince >= T.sideDelay - EPS) {
        this._enter('side', t);
      }
      if (this.mood === 'side' && t - this.sideAt >= T.sadDelay - EPS) {
        this._enter('sad', t);
        this.caught++;
        caught = true;
      }
    } else if ((this.mood === 'side' || this.mood === 'sad') && t - this.onSince >= T.returnDelay - EPS) {
      this._enter('happy', t);
    }

    return { mood: this.mood, effective: eff, changed: this.mood !== prev, caught };
  }

  _enter(mood, t) {
    this.mood = mood;
    if (mood === 'side') this.sideAt = t;
    if (mood === 'happy') this.happyUntil = t + this.timings.happyDuration;
  }
}

/** Median of a numeric array (NaN-free). */
export function median(xs) {
  const a = xs.filter(Number.isFinite).sort((p, q) => p - q);
  if (!a.length) return 0;
  const m = a.length >> 1;
  return a.length % 2 ? a[m] : (a[m - 1] + a[m]) / 2;
}

/** Baseline from calibration samples (frames with a face and open eyes). */
export function computeBaseline(samples) {
  return {
    yaw: median(samples.map((s) => s.yaw)),
    pitch: median(samples.map((s) => s.pitch)),
    gazeDown: median(samples.map((s) => s.gazeDown)),
    gazeSide: median(samples.map((s) => s.gazeSide)),
  };
}

/** Exponential smoothing step with time constant tau (seconds). */
export function smooth(prev, target, dt, tau) {
  if (!(dt > 0)) return prev;
  const a = 1 - Math.exp(-dt / tau);
  return prev + (target - prev) * a;
}

export function clamp(v, lo, hi) { return Math.min(hi, Math.max(lo, v)); }

/** Local-calendar day key, e.g. "2026-11-10"; the caught counter resets when it changes. */
export function dayKey(date) {
  const p = (n) => String(n).padStart(2, '0');
  return `${date.getFullYear()}-${p(date.getMonth() + 1)}-${p(date.getDate())}`;
}

/** Protocol line for the board. lookX/lookY are clamped integers in -100..100. */
export function eLine(mood, lookX, lookY) {
  const x = Math.round(clamp(lookX, -100, 100));
  const y = Math.round(clamp(lookY, -100, 100));
  return `E ${mood} ${x | 0} ${y | 0}\n`; // `| 0` turns -0 into 0
}
