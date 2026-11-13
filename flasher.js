// flasher.js — "Update firmware" from the web page, over USB (esptool-js, runs in the browser).
//
// Writes the four parts separately (bootloader 0x0, partition table 0x8000, boot_app0 0xE000,
// app 0x10000) instead of one merged image, so the NVS area in between (0x9000-0xDFFF), where the
// board keeps its paired laptops, is left alone: updating doesn't unpair anything.
import { ESPLoader, Transport } from './vendor/esptool-js/bundle.js';
import { md5hex } from './md5.js';

export async function loadManifest() {
  const r = await fetch('fw/manifest.json', { cache: 'no-cache' });
  if (!r.ok) throw new Error(`firmware manifest: HTTP ${r.status}`);
  return r.json();   // {version, files: [{file, address, size, sha256}]}
}

const hex = (buf) => [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, '0')).join('');

async function fetchPart(f) {
  const r = await fetch(`fw/${f.file}`, { cache: 'no-cache' });
  if (!r.ok) throw new Error(`${f.file}: HTTP ${r.status}`);
  const data = new Uint8Array(await r.arrayBuffer());
  const sum = hex(await crypto.subtle.digest('SHA-256', data));
  if (data.length !== f.size || sum !== f.sha256) throw new Error(`${f.file} is damaged (checksum mismatch); reload the page`);
  return { data, address: f.address };
}

/**
 * @param {{port: SerialPort, manifest: object, onProgress?: (fraction:number, what:string)=>void, onLog?: (s:string)=>void}} o
 * The port must be closed (disconnect the app's USB link first).
 */
export async function flashLatest({ port, manifest, onProgress = () => {}, onLog = () => {} }) {
  onProgress(0, 'downloading firmware');
  const parts = await Promise.all(manifest.files.map(fetchPart));
  const total = parts.reduce((n, p) => n + p.data.length, 0);
  const transport = new Transport(port, false);
  const terminal = { clean() {}, writeLine: (s) => onLog(s), write: (s) => onLog(s) };
  const loader = new ESPLoader({ transport, baudrate: 921600, terminal, debugLogging: false });
  try {
    onProgress(0, 'connecting to the board');
    const chip = await loader.main();   // resets the board into its bootloader
    if (!/ESP32-S3/i.test(chip)) throw new Error(`this is a ${chip}, not an EyeBuddy (ESP32-S3)`);
    const before = parts.map((_, i) => parts.slice(0, i).reduce((n, p) => n + p.data.length, 0));
    await loader.writeFlash({
      fileArray: parts,
      flashMode: 'keep', flashFreq: 'keep', flashSize: 'keep',
      eraseAll: false,
      compress: true,
      reportProgress: (i, written, size) => onProgress((before[i] + (written / size) * parts[i].data.length) / total, 'writing'),
      calculateMD5Hash: (image) => md5hex(image),   // esptool-js re-reads the flash and compares
    });
    onProgress(1, 'restarting the board');
    await loader.after('hard_reset');
  } finally {
    try { await transport.disconnect(); } catch {}
  }
}
