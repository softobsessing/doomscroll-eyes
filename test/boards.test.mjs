// node test/boards.test.mjs — boards.js: HELLO parsing, the challenge answer (vs Python's hmac), the board store.
import assert from 'node:assert/strict';
import { parseHello, answer, newKey, BoardStore, bleName, idFromName, isBoardId, otherBoard } from '../boards.js';

let passed = 0, failed = 0;
async function test(name, fn) {
  try { await fn(); passed++; console.log('ok  ', name); } catch (e) { failed++; console.log('FAIL', name); console.log(e); }
}
const mem = () => { const m = new Map(); return { getItem: (k) => m.get(k) ?? null, setItem: (k, v) => m.set(k, String(v)) }; };

await test('parseHello: pairing board (protocol 4) and older boards', () => {
  assert.deepEqual(parseHello('HELLO EyeBuddy 4 3f2a 6 0123456789abcdef'), { proto: 4, id: '3F2A', fw: 6, nonce: '0123456789abcdef' });
  assert.deepEqual(parseHello('HELLO EyeBuddy 3'), { proto: 3, id: null, fw: null, nonce: null });   // firmware v5
  assert.deepEqual(parseHello('HELLO EyeBuddy 1'), { proto: 1, id: null, fw: null, nonce: null });
  assert.equal(parseHello('T touch'), null);
});

await test('otherBoard: a RoboEyes board is pointed to its own app; EyeBuddy and other lines are not', () => {
  const o = otherBoard('HELLO RoboEyes 2 3F2A 2');
  assert.equal(o.name, 'RoboEyes');
  assert.match(o.other.url, /RoboEyes/);
  assert.equal(otherBoard('HELLO RoboEyes 1').name, 'RoboEyes');   // old RoboEyes firmware
  assert.deepEqual(otherBoard('HELLO Gizmo 1'), { name: 'Gizmo', other: null });
  assert.equal(otherBoard('HELLO EyeBuddy 4 3F2A 8 0123456789abcdef'), null);
  assert.equal(otherBoard('T touch'), null);
  assert.equal(parseHello('HELLO RoboEyes 2 3F2A 2'), null);   // never treated as an EyeBuddy
});

await test('answer = first 16 bytes of HMAC-SHA256(key, nonce), same as Python hmac / the board', async () => {
  const key = [...Array(16).keys()].map((b) => b.toString(16).padStart(2, '0')).join('');
  assert.equal(await answer(key, '0123456789abcdef'), '30019ba06197c0ed181d45d34500c0b6');
});

await test('newKey: 32 hex chars, random', () => {
  const a = newKey(), b = newKey();
  assert.match(a, /^[0-9a-f]{32}$/); assert.notEqual(a, b);
});

await test('names and ids', () => {
  assert.equal(bleName('3F2A'), 'EyeBuddy-3F2A');
  assert.equal(idFromName('EyeBuddy-3f2a'), '3F2A');
  assert.equal(idFromName('EyeBuddy'), null);
  assert.ok(isBoardId('BEEF') && !isBoardId('beef') && !isBoardId('XYZ1') && !isBoardId(undefined));
});

await test('store: keys per board, persisted, rename/forget/active', () => {
  const s = mem();
  const a = new BoardStore(s);
  assert.equal(a.upsert('nope'), null);
  const k = a.keyFor('3F2A');
  assert.equal(a.keyFor('3F2A'), k);   // stable
  a.upsert('BEEF', { lastSeen: 5 });
  a.setActive('BEEF');
  a.rename('3F2A', 'desk');
  const b = new BoardStore(s);          // reload from storage
  assert.equal(b.get('3F2A').key, k);
  assert.equal(b.label('3F2A'), 'desk (EyeBuddy-3F2A)');
  assert.equal(b.active, 'BEEF');
  b.forget('BEEF');
  assert.equal(b.active, '3F2A');
  assert.equal(new BoardStore(mem()).active, null);
});

console.log(`\n${passed} passed${failed ? `, ${failed} FAILED` : ''}`);
process.exitCode = failed ? 1 : 0;
