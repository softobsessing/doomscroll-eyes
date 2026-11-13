#!/usr/bin/env python3
"""After `arduino-cli compile --output-dir EyeBuddy/build EyeBuddy`: package the firmware for the website.

  ../fw/{bootloader,partitions,boot_app0,app}.bin + manifest.json   (Update firmware button: NVS stays untouched)
  EyeBuddy/EyeBuddy-firmware.bin                                    (manual flash at 0x0; trimmed merged image)

The parts are cut from the merged image arduino-cli made, so they are byte-identical to a manual flash.
"""
import hashlib, json, os, re, struct, sys

here = os.path.dirname(os.path.abspath(__file__))
build = os.path.join(here, 'EyeBuddy', 'build')
merged = open(os.path.join(build, 'EyeBuddy.ino.merged.bin'), 'rb').read()
app = open(os.path.join(build, 'EyeBuddy.ino.bin'), 'rb').read()
boot_len = len(open(os.path.join(build, 'EyeBuddy.ino.bootloader.bin'), 'rb').read())
ptable = open(os.path.join(build, 'EyeBuddy.ino.partitions.bin'), 'rb').read()
version = int(re.search(r'#define FW_VERSION (\d+)', open(os.path.join(here, 'EyeBuddy', 'config.h')).read()).group(1))

# Partition table sanity: the layout the updater assumes.
parts = {}
for off in range(0, len(ptable), 32):
    e = ptable[off:off + 32]
    if e[:2] != b'\xaa\x50':
        break
    name = e[12:28].split(b'\0')[0].decode()
    parts[name] = struct.unpack('<II', e[4:12])
assert parts['nvs'][0] == 0x9000 and parts['otadata'][0] == 0xE000 and parts['app0'][0] == 0x10000, parts
assert parts['nvs'][0] + parts['nvs'][1] <= 0xE000

pieces = [('bootloader.bin', 0x0, boot_len), ('partitions.bin', 0x8000, len(ptable)),
          ('boot_app0.bin', 0xE000, 0x2000), ('app.bin', 0x10000, len(app))]
assert merged[0] == 0xE9 and merged[0x10000:0x10000 + len(app)] == app and merged[0x8000:0x8000 + len(ptable)] == ptable
assert set(merged[0x9000:0xE000]) == {0xFF}, 'NVS region in the merged image should be empty'

out = os.path.normpath(os.path.join(here, '..', 'fw'))
os.makedirs(out, exist_ok=True)
files = []
for name, addr, size in pieces:
    data = merged[addr:addr + size]
    assert addr + size <= len(merged)
    open(os.path.join(out, name), 'wb').write(data)
    files.append({'file': name, 'address': addr, 'size': size, 'sha256': hashlib.sha256(data).hexdigest()})
json.dump({'version': version, 'files': files}, open(os.path.join(out, 'manifest.json'), 'w'), indent=2)

end = (len(merged.rstrip(b'\xff')) + 4095) // 4096 * 4096
manual = merged[:end]
open(os.path.join(here, 'EyeBuddy', 'EyeBuddy-firmware.bin'), 'wb').write(manual)
print(f'firmware v{version}: web parts in {out}; manual image {len(manual)} bytes sha256 {hashlib.sha256(manual).hexdigest()}')
