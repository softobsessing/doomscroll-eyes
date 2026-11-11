// Run: node test/attention.test.mjs   (no dependencies)
import assert from 'node:assert/strict';
import {
  MoodMachine, classifyFrame, computeBaseline, eLine, dayKey, smooth,
  DEFAULT_THRESHOLDS,
} from '../attention.js';

const DT = 1 / 30; // 30 fps synthetic frames
let passed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log(`ok   ${name}`); }
  catch (e) { console.error(`FAIL ${name}\n${e.stack}`); process.exitCode = 1; }
}

/** Feed `cls` frames from t to t+dur (exclusive end handled by including the endpoint). */
function run(m, cls, t0, dur) {
  let last;
  const n = Math.round(dur / DT);
  for (let i = 0; i <= n; i++) last = m.update(cls, t0 + Math.min(i * DT, dur));
  return { t: t0 + dur, last };
}

test('ON -> neutral', () => {
  const m = new MoodMachine();
  const { last } = run(m, 'ON', 0, 5);
  assert.equal(last.mood, 'neutral');
  assert.equal(last.effective, 'ON');
});

test('AWAY -> side on the very first away frame', () => {
  const m = new MoodMachine();
  let { t } = run(m, 'ON', 0, 1);
  assert.equal(m.update('AWAY', t + DT).mood, 'side');
});

test('sideDelay still works when set', () => {
  const m = new MoodMachine({ sideDelay: 0.4 });
  let { t } = run(m, 'ON', 0, 1);
  t += DT;
  assert.equal(run(m, 'AWAY', t, 0.39).last.mood, 'neutral');
  assert.equal(m.update('AWAY', t + 0.4).mood, 'side');
});

test('AWAY 2.0 s total -> sad (not before)', () => {
  const m = new MoodMachine();
  let { t } = run(m, 'ON', 0, 1);
  t += DT;
  assert.equal(run(m, 'AWAY', t, 1.95).last.mood, 'side');
  const r = m.update('AWAY', t + 2.0);
  assert.equal(r.mood, 'sad');
  assert.equal(r.caught, true);
});

test('blink HOLD frames do not trigger side', () => {
  const m = new MoodMachine();
  let { t } = run(m, 'ON', 0, 1);
  const r = run(m, 'HOLD', t + DT, 1.5); // long blink run, far beyond sideDelay
  assert.equal(r.last.mood, 'neutral');
  assert.equal(r.last.effective, 'ON');
  // and blinks interleaved with ON never trigger anything
  let tt = r.t;
  for (let i = 0; i < 300; i++) {
    tt += DT;
    assert.equal(m.update(i % 3 ? 'ON' : 'HOLD', tt).mood, 'neutral');
  }
});

test('HOLD while AWAY keeps the AWAY run going', () => {
  const m = new MoodMachine();
  let { t } = run(m, 'AWAY', 0, 0.2);
  ({ t } = run(m, 'HOLD', t + DT, 0.3));
  assert.equal(m.mood, 'side');
});

test('return ON 0.3 s -> happy, then after 1.2 s -> neutral', () => {
  const m = new MoodMachine();
  let { t } = run(m, 'AWAY', 0, 3); // -> sad
  assert.equal(m.mood, 'sad');
  t += DT;
  assert.equal(run(m, 'ON', t, 0.28).last.mood, 'sad');
  const h = m.update('ON', t + 0.3);
  assert.equal(h.mood, 'happy');
  assert.equal(h.changed, true);
  const happyAt = t + 0.3;
  assert.equal(run(m, 'ON', happyAt + DT, 1.15).last.mood, 'happy');
  assert.equal(m.update('ON', happyAt + 1.2).mood, 'neutral');
});

test('return from side (not sad) also -> happy', () => {
  const m = new MoodMachine();
  let { t } = run(m, 'AWAY', 0, 1); // side
  assert.equal(m.mood, 'side');
  assert.equal(run(m, 'ON', t + DT, 0.3).last.mood, 'happy');
});

test('brief ON glance (< returnDelay) does not leave side', () => {
  const m = new MoodMachine();
  let { t } = run(m, 'AWAY', 0, 1);
  ({ t } = run(m, 'ON', t + DT, 0.2));
  assert.equal(m.mood, 'side');
});

test('caught counter increments once per sad entry', () => {
  const m = new MoodMachine();
  let events = 0;
  let t = 0;
  for (let round = 0; round < 3; round++) {
    for (let i = 0; i < 150; i++) { t += DT; if (m.update('AWAY', t).caught) events++; } // 5 s away
    assert.equal(m.mood, 'sad');
    for (let i = 0; i < 90; i++) { t += DT; if (m.update('ON', t).caught) events++; }  // 3 s back
    assert.equal(m.mood, 'neutral');
  }
  assert.equal(events, 3);
  assert.equal(m.caught, 3);
});

test('classifier: no face -> AWAY, blink -> HOLD, thresholds and pitch sign', () => {
  const base = { yaw: 2, pitch: -5, gazeDown: 0.1, gazeSide: 0 };
  const f = (o) => classifyFrame({ face: true, yaw: 2, pitch: -5, gazeDown: 0.1, gazeSide: 0, blink: 0.1, ...o }, base, DEFAULT_THRESHOLDS).cls;
  assert.equal(classifyFrame({ face: false }, base).cls, 'AWAY');
  assert.equal(f({}), 'ON');
  assert.equal(f({ blink: 0.8, yaw: 90 }), 'HOLD');
  assert.equal(f({ yaw: 2 + 26 }), 'AWAY');
  assert.equal(f({ yaw: 2 - 26 }), 'AWAY');
  assert.equal(f({ yaw: 2 + 24 }), 'ON');
  assert.equal(f({ pitch: -5 - 16 }), 'AWAY');  // head DOWN (negative pitch)
  assert.equal(f({ pitch: -5 + 30 }), 'ON');    // head up is not "away"
  assert.equal(f({ gazeDown: 0.1 + 0.4 }), 'AWAY');
  assert.equal(f({ gazeSide: 0.6 }), 'AWAY');
  assert.equal(f({ gazeSide: -0.6 }), 'AWAY');
  assert.equal(f({ gazeSide: 0.4 }), 'ON');
  assert.equal(classifyFrame({ face: true, yaw: 0, pitch: 0, gazeDown: 0, gazeSide: 0, blink: 0 }, null).cls, 'ON');
});

test('helpers: baseline median, protocol line, day key, smoothing', () => {
  const b = computeBaseline([
    { yaw: 1, pitch: 10, gazeDown: 0.1, gazeSide: 0 },
    { yaw: 3, pitch: 12, gazeDown: 0.2, gazeSide: 0.1 },
    { yaw: 100, pitch: 11, gazeDown: 0.9, gazeSide: -0.1 },
  ]);
  assert.deepEqual(b, { yaw: 3, pitch: 11, gazeDown: 0.2, gazeSide: 0 });
  assert.equal(eLine('side', -40.4, 10.6), 'E side -40 11\n');
  assert.equal(eLine('neutral', -0.2, 500), 'E neutral 0 100\n');
  assert.ok(eLine('neutral', -100, -100).length <= 20); // fits one BLE packet
  assert.equal(dayKey(new Date(2026, 0, 5, 23, 59)), '2026-01-05');
  const s = smooth(0, 100, 0.15, 0.15);
  assert.ok(Math.abs(s - 63.2) < 0.1);
});

console.log(`\n${passed} passed${process.exitCode ? ', some FAILED' : ''}`);
