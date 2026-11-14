// The restart after "Update firmware": the board's built-in USB (ESP32-S3 USB-Serial-JTAG) resets the chip
// when RTS goes on while DTR is off. esptool-js 0.7.1's own after('hard_reset') only switched RTS off (it
// already was), so the board never restarted into the new firmware. This checks our replacement.
// Run: node test/flasher.test.mjs
import assert from 'node:assert';
import { resetIntoApp } from '../flasher.js';

// Stand-in for esptool-js's Transport: setRTS also re-sends the current DTR (like the real one).
function fakeTransport({ dropAfterReset = false } = {}) {
  const log = [];
  let dtr = true, rts = false, gone = false;   // DTR left on, as after esptool-js's connect sequence
  const signals = (s) => {
    if (gone) throw new Error('device lost');
    if ('dataTerminalReady' in s) dtr = s.dataTerminalReady;
    if ('requestToSend' in s) rts = s.requestToSend;
    log.push({ dtr, rts });
    if (dropAfterReset && rts && !dtr) gone = true;   // the chip resets and leaves USB right away
  };
  return {
    log,
    async setDTR(v) { this._DTR_state = v; signals({ dataTerminalReady: v }); },
    async setRTS(v) { signals({ requestToSend: v }); signals({ dataTerminalReady: this._DTR_state }); },
  };
}

let passed = 0;
const resetPulse = (log) => log.some((s) => s.rts && !s.dtr);              // chip reset, normal boot
// USB-Serial-JTAG: stepping from (DTR on, RTS off) straight to (DTR off, RTS on) = reset into DOWNLOAD mode
const downloadMode = (log) => log.some((s, i) => i > 0 && s.rts && !s.dtr && log[i - 1].dtr && !log[i - 1].rts);

{
  const t = fakeTransport();
  await resetIntoApp(t);
  assert.ok(resetPulse(t.log), 'RTS must go on with DTR off (that is what restarts the chip)');
  assert.ok(!downloadMode(t.log), 'must not make the DTR-on -> RTS-on step (that boots into download mode)');
  const last = t.log[t.log.length - 1];
  assert.deepStrictEqual(last, { dtr: false, rts: false }, 'ends with both lines released');
  passed++;
}
{
  const t = fakeTransport({ dropAfterReset: true });
  await resetIntoApp(t);   // must not throw when the board vanishes from USB mid-reset
  assert.ok(resetPulse(t.log));
  passed++;
}
console.log(`${passed} passed`);
