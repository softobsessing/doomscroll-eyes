// Compares eyes.js with the board's C++ (EyeBuddy/eyes.cpp) using frames from the Mac preview tool:
//   (cd ../tools/eyebuddy_preview && ./preview stills out/stills)   then   node test/eyes_port.test.mjs
import { readFileSync, existsSync } from 'node:fs';
import assert from 'node:assert';
import { fileURLToPath } from 'node:url';
import { Engine, MOODS, SCREEN } from '../eyes.js';

const dir = process.argv[2] || fileURLToPath(new URL('../../tools/eyebuddy_preview/out/stills/', import.meta.url));
if (!existsSync(dir)) { console.log(`skip: no C++ frames in ${dir}`); process.exit(0); }

function readPPM(path) {
  const buf = readFileSync(path);
  const header = buf.subarray(0, 20).toString('latin1').match(/^P6\s+(\d+)\s+(\d+)\s+255\s/);
  return buf.subarray(header[0].length);
}
const expand = (c) => [(((c >> 11) & 31) * 255 / 31) | 0, (((c >> 5) & 63) * 255 / 63) | 0, ((c & 31) * 255 / 31) | 0];

let total = 0;
for (let m = 0; m < MOODS.length; m++) {
  // same sequence as `preview stills`
  const e = new Engine(7);
  e.setMood(m);
  let t = 0;
  for (; t < 600; t += 10) e.update(t);
  while (e.blinking) e.update(t += 10);
  const frames = [['mood', e.eyes.map((x) => ({ ...x }))]];
  const open = new Map();
  const shadeNow = () => { const f = new Uint16Array(SCREEN * SCREEN); for (let y = 0; y < SCREEN; y++) for (let x = 0; x < SCREEN; x++) f[y * SCREEN + x] = e.shade(x, y); return f; };
  open.set('mood', shadeNow());
  while (!e.blinking) e.update(t += 5);
  for (let k = 0; k < 3; k++) e.update(t += 25);
  open.set('blink', shadeNow());
  for (const [kind, js] of open) {
    const ref = readPPM(`${dir}/${kind}_${MOODS[m]}.ppm`);
    let bad = 0, maxd = 0, lit = 0;
    for (let y = 0; y < SCREEN; y++) for (let x = 0; x < SCREEN; x++) {
      const dx = x + 0.5 - SCREEN / 2, dy = y + 0.5 - SCREEN / 2;
      if (dx * dx + dy * dy > (SCREEN / 2) ** 2) continue;   // preview paints the outside grey
      const i = y * SCREEN + x, rgb = expand(js[i]);
      if (js[i]) lit++;
      const d = Math.max(...rgb.map((v, c) => Math.abs(v - ref[i * 3 + c])));
      if (d) { bad++; maxd = Math.max(maxd, d); }
    }
    console.log(`${kind.padEnd(5)} ${MOODS[m].padEnd(8)} lit ${String(lit).padStart(6)}  differing ${bad}  max diff ${maxd}`);
    total += bad;
    assert(lit > 50, 'nothing drawn');
    assert(bad <= lit * 0.01 && maxd <= 9, 'JS port drifted from eyes.cpp');   // only float rounding at edges allowed
  }
}
console.log(total === 0 ? 'eyes.js matches eyes.cpp exactly' : `eyes.js matches eyes.cpp (${total} edge pixels differ by rounding)`);
