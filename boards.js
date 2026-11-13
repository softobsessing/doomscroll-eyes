// boards.js — which EyeBuddy boards this browser owns, and the keys that prove it.
//
// Every board has a 4-hex ID from its chip (shown on its screen at power-on, Bluetooth name
// "EyeBuddy-<ID>"). For each board this browser keeps a random 16-byte key. The board stores the
// same key when you pair (touch it, or plug it in), then asks for proof on every connection:
// it sends a one-time nonce in HELLO and we answer with HMAC-SHA256(key, nonce). The key itself
// only crosses the air once, at pairing time.

export const BLE_PREFIX = 'EyeBuddy';
export const bleName = (id) => `${BLE_PREFIX}-${id}`;
export const idFromName = (name) => (/^EyeBuddy-([0-9A-F]{4})$/i.exec(name || '') || [])[1]?.toUpperCase() || null;
export const isBoardId = (id) => /^[0-9A-F]{4}$/.test(id || '');

/** First protocol with board IDs + pairing (firmware v6). 1 = firmware v1-v4, 3 = firmware v5. */
export const PAIRING_PROTOCOL = 4;

/** "HELLO EyeBuddy 4 3F2A 6 0123456789abcdef" -> {proto, id, fw, nonce}; older boards ("HELLO EyeBuddy 3") -> no id. */
export function parseHello(line) {
  const p = line.trim().split(/\s+/);
  if (p[0] !== 'HELLO' || p[1] !== 'EyeBuddy') return null;
  const proto = parseInt(p[2], 10) || 1;
  if (proto < PAIRING_PROTOCOL) return { proto, id: null, fw: null, nonce: null };
  return { proto, id: (p[3] || '').toUpperCase(), fw: parseInt(p[4], 10) || 0, nonce: p[5] || '' };
}

const hex = (bytes) => [...bytes].map((b) => b.toString(16).padStart(2, '0')).join('');
const unhex = (s) => new Uint8Array(s.match(/../g).map((h) => parseInt(h, 16)));

export function newKey() { return hex(crypto.getRandomValues(new Uint8Array(16))); }

/** Answer to the board's challenge: hex of the first 16 bytes of HMAC-SHA256(key, nonce). */
export async function answer(keyHex, nonce) {
  const k = await crypto.subtle.importKey('raw', unhex(keyHex), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', k, new TextEncoder().encode(nonce)));
  return hex(mac.slice(0, 16));
}

/** Boards this browser knows, in localStorage: { boards: {ID: {id, name, key, paired, fw, lastSeen}}, active }. */
export class BoardStore {
  constructor(storage = globalThis.localStorage, key = 'eyebuddy.boards') {
    this.storage = storage;
    this.lsKey = key;
    let data = null;
    try { data = JSON.parse(storage?.getItem(key)); } catch {}
    this.data = data && typeof data === 'object' && data.boards ? data : { boards: {}, active: null };
  }
  _save() { try { this.storage?.setItem(this.lsKey, JSON.stringify(this.data)); } catch {} }
  list() { return Object.values(this.data.boards).sort((a, b) => (b.lastSeen || 0) - (a.lastSeen || 0)); }
  get(id) { return this.data.boards[id] || null; }
  get active() { return this.data.active && this.data.boards[this.data.active] ? this.data.active : null; }
  setActive(id) { if (this.data.boards[id]) { this.data.active = id; this._save(); } }
  upsert(id, patch = {}) {
    if (!isBoardId(id)) return null;
    const b = this.data.boards[id] || { id, name: '', key: null, paired: false, fw: 0, lastSeen: 0 };
    Object.assign(b, patch, { id });
    this.data.boards[id] = b;
    this._save();
    return b;
  }
  /** This browser's key for the board (made on first use). */
  keyFor(id) {
    const b = this.upsert(id);
    if (!b.key) { b.key = newKey(); this._save(); }
    return b.key;
  }
  rename(id, name) { if (this.data.boards[id]) { this.data.boards[id].name = String(name).slice(0, 40); this._save(); } }
  forget(id) {
    delete this.data.boards[id];
    if (this.data.active === id) this.data.active = this.list()[0]?.id || null;
    this._save();
  }
  label(id) { const b = this.get(id); return b?.name ? `${b.name} (${bleName(id)})` : bleName(id); }
}
