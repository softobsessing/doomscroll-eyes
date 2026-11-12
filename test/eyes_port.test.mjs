// Compares eyes.js with the board's eye engine (firmware/EyeBuddy/eyes.cpp), pixel by pixel.
// From the repo root:
//   clang++ -O2 -std=c++17 -o /tmp/parity firmware/EyeBuddy/extras/parity.cpp firmware/EyeBuddy/eyes.cpp
//   /tmp/parity /tmp/parity_out && node test/eyes_port.test.mjs /tmp/parity_out
// The scripted scenario below must match firmware/EyeBuddy/extras/parity.cpp.
import { readFileSync, existsSync } from 'node:fs';
import assert from 'node:assert';
import { Engine, SCREEN } from '../eyes.js';

const dir = process.argv[2] || '/tmp/parity_out';
if (!existsSync(dir)) { console.log(`skip: no firmware frames in ${dir}`); process.exit(0); }

const N = SCREEN;
const SHOTS = [700, 910, 980, 1060, 1150, 1260, 1400, 1580, 1800, 2100, 2470, 3300, 4100, 4900, 5650, 5990];
const expand = (c) => [((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31];

let worst = 0, frames = 0;
function compare(name, e) {
  const buf = readFileSync(`${dir}/${name}.bin`);
  const ref = new Uint16Array(buf.buffer, buf.byteOffset, N * N);
  let diff = 0, big = 0, eyePx = 0;
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const dx = x + 0.5 - N / 2, dy = y + 0.5 - N / 2;
    if (dx * dx + dy * dy > (N / 2) ** 2) continue;   // outside the round glass
    const i = y * N + x, js = e.shade(x, y);
    if (ref[i] !== e.face565) eyePx++;
    if (js === ref[i]) continue;
    diff++;
    const a = expand(js), b = expand(ref[i]);
    // more than one RGB565 step apart = a real difference (one step = float rounding on an edge)
    if (Math.abs(a[0] - b[0]) > 9 || Math.abs(a[1] - b[1]) > 5 || Math.abs(a[2] - b[2]) > 9) big++;
  }
  assert.ok(eyePx > 50, `${name}: nothing drawn`);
  worst = Math.max(worst, big); frames++;
  console.log(`${name.padEnd(10)} ${(e.timerShown ? `timer ${e.timerSecs}s` : e.moodName).padEnd(10)} eye px ${String(eyePx).padStart(6)}   ${diff ? `${diff} px off by a rounding step, ${big} by more` : 'identical'}`);
}

// ---- driven by the website: moods (incl. surprised), look, blinks, colors ----
{
  const e = new Engine(7);
  let k = 0;
  for (let t = 0; t <= 6000; t += 10) {
    e.setWander(false); e.setAutoBlink(false);
    e.setMood(t < 800 ? 'neutral' : t < 3000 ? 'wow' : t < 3800 ? 'happy' : t < 4600 ? 'side' : t < 5400 ? 'sad' : t < 5800 ? 'chill' : 'wow');
    e.setLook(Math.fround(0.5 * Math.sin(t / 600)), Math.fround(0.4 * Math.cos(t / 800)));
    if (t === 2000) e.setColors(0x14213D, 0xFF5FA2);
    if (t === 2400) e.blinkNow(t, false);
    if (t === 4200) e.setColors(0xFFFFFF, 0x2A9D8F);
    e.update(t);
    if (SHOTS.includes(t)) compare(`linked_${k++}`, e);
  }
}
// ---- on its own: random blinks and wandering (same seed) ----
{
  const e = new Engine(3);
  e.setWander(true);
  let k = 0;
  for (let t = 0; t <= 6000; t += 10) {
    e.update(t);
    if (SHOTS.includes(t)) compare(`solo_${k++}`, e);
  }
}
// ---- pomodoro: timer screen (ticking), flash on demand, stop, and the 00:00 ending ----
{
  const e = new Engine(5);
  e.setWander(false); e.setAutoBlink(false);
  e.setColors(0x14213D, 0xFF5FA2);
  const PSHOTS = [200, 1300, 2500, 3500, 4300, 6100];
  let k = 0;
  for (let t = 0; t <= 6200; t += 10) {
    if (t === 0) e.setPomodoro(t, 65, 1500);
    if (t === 100 || t === 3000) e.flashTimer(t, 2000);
    if (t === 3800) e.setPomodoro(t, 0, 0);
    if (t === 4000) e.setPomodoro(t, 2, 10);
    e.update(t);
    if (PSHOTS.includes(t)) compare(`pomo_${k++}`, e);
  }
}
// ---- angry and dizzy evolving out of the focused ovals (and dizzy melting back) ----
{
  const e = new Engine(9);
  e.setWander(false); e.setAutoBlink(false);
  const ESHOTS = [600, 700, 800, 900, 1100, 1500, 2700, 2950, 3300, 3700, 4200, 4500, 4800, 5300, 5800, 6000, 6400, 7200];
  let k = 0;
  for (let t = 0; t <= 7300; t += 10) {
    e.setMood(t < 500 ? 'neutral' : t < 2000 ? 'angry' : t < 2600 ? 'neutral' : t < 7000 ? 'dizzy' : 'neutral');
    e.setLook(Math.fround(0.3 * Math.sin(t / 500)), Math.fround(0.2 * Math.cos(t / 700)));
    e.update(t);
    if (ESHOTS.includes(t)) compare(`evolve_${k++}`, e);
  }
}
console.log(`\n${frames} frames compared; worst frame has ${worst} pixels off by more than a rounding step`);
assert.ok(worst <= 40, 'eyes.js no longer matches eyes.cpp');
console.log('ok: the website preview matches the board');
