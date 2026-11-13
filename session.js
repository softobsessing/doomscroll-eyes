// session.js — the handshake on one connection (USB or Bluetooth), independent of the page.
//
//   board says HELLO (link.js sends P on connect)
//     old firmware (protocol < 4) -> 'ready' (legacy: anyone can drive it; suggest an update)
//     we have a key for it       -> A <hmac(key, nonce)>  -> AUTH ok -> 'ready'
//                                                         -> AUTH no -> PAIR <key> (board was reset / new laptop)
//     no key yet                 -> PAIR <key>  -> USB: AUTH ok at once.  Bluetooth: AUTH wait -> 'pairing'
//                                                  (board shows TAP TO PAIR) -> AUTH ok | AUTH timeout -> 'failed'
//   'ready': the app may send E/B.  BUSY -> 'busy' for a moment (another computer drives it), then try again.
//   AUTH need -> start over (board lost our session, e.g. it restarted).
import { PAIRING_PROTOCOL, answer, parseHello } from './boards.js';

export const BUSY_RETRY_MS = 2500;

export class Session {
  /**
   * @param {{store: import('./boards.js').BoardStore, kind: 'usb'|'ble', send: (line:string)=>void,
   *          onChange?: (s: Session)=>void, now?: ()=>number, expectId?: string|null}} o
   */
  constructor({ store, kind, send, onChange = () => {}, now = () => performance.now(), expectId = null }) {
    Object.assign(this, { store, kind, send, onChange, now, expectId });
    this.state = 'hello';   // hello | auth | pairing | ready | busy | failed
    this.board = null;      // {proto, id, fw}
    this.reason = '';
    this.busyUntil = 0;
    this._phase = '';       // what our last request was: 'A' | 'PAIR'
  }

  get legacy() { return !!this.board && this.board.proto < PAIRING_PROTOCOL; }
  canDrive() {
    if (this.state === 'busy' && this.now() >= this.busyUntil) this._set('ready');   // try again
    return this.state === 'ready';
  }

  _set(state, reason = '') {
    if (this.state === state && this.reason === reason) return;
    this.state = state;
    this.reason = reason;
    this.onChange(this);
  }

  /** Feed every line from the board. Returns true if it was a handshake line. */
  async onLine(line) {
    const hello = parseHello(line);
    if (hello) { await this._hello(hello); return true; }
    const w = line.trim().split(/\s+/);
    if (w[0] === 'BUSY') {
      this.busyUntil = this.now() + BUSY_RETRY_MS;
      if (this.state === 'ready' || this.state === 'busy') this._set('busy');
      return true;
    }
    if (w[0] !== 'AUTH') return false;
    const id = this.board?.id;
    switch (w[1]) {
      case 'ok':
        if (id) this.store.upsert(id, { paired: true, lastSeen: Date.now() });
        this._set('ready');
        break;
      case 'no':
        if (this._phase === 'A' && id) {   // the board doesn't know our key (it was reset, or never paired)
          this.store.upsert(id, { paired: false });
          this._pair();
        } else this._set('failed', 'refused');
        break;
      case 'wait': this._set('pairing'); break;
      case 'timeout': this._set('failed', 'timeout'); break;
      case 'need':   // we thought we were in, the board disagrees (it restarted): start over
        if (this.state === 'ready' || this.state === 'busy') { this._set('hello'); this.send('P'); }
        break;
    }
    return true;
  }

  async _hello(h) {
    this.board = { proto: h.proto, id: h.id, fw: h.fw };
    if (h.proto < PAIRING_PROTOCOL) { this._set('ready'); return; }   // old firmware: no locks
    if (this.expectId && h.id !== this.expectId) { this._set('failed', 'wrong-board'); return; }
    const b = this.store.upsert(h.id, { fw: h.fw, lastSeen: Date.now() });
    this.store.setActive(h.id);
    if (b.key) {
      this._phase = 'A';
      this._set('auth');
      this.send(`A ${await answer(b.key, h.nonce)}`);
    } else this._pair();
  }

  _pair() {
    this._phase = 'PAIR';
    this._set(this.kind === 'ble' ? 'pairing' : 'auth');
    this.send(`PAIR ${this.store.keyFor(this.board.id)}`);
  }
}
