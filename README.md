# EyeBuddy web app

**Live:** https://softobsessing.github.io/doomscroll-eyes/ (desktop Chrome or Edge; needs a webcam, plus the EyeBuddy board for USB/Bluetooth)

Watches you through the laptop webcam, decides if you're looking at your screen or away (phone,
doom scrolling), and sends a mood to the EyeBuddy board, which draws the eyes.
Plain HTML/CSS/JS ES modules, no build step. Desktop **Chrome** only (Web Serial + Web Bluetooth).

Privacy: everything runs in the browser. The face model (MediaPipe FaceLandmarker) and its wasm
are vendored in `vendor/`, so nothing is fetched from a CDN and no video is uploaded or stored.
(`@mediapipe/tasks-vision` is pinned to **0.10.35** on purpose: the 1.x releases include a
usage-telemetry logger that POSTs to `odml.pa.googleapis.com`.)

## Run

```sh
./serve.sh            # = python3 -m http.server 8000 (bound to 127.0.0.1)
```
Open <http://localhost:8000/> in Chrome and allow the camera. `localhost` counts as a secure
context, which the camera, Web Serial and Web Bluetooth all need. Keep the tab open. It keeps
working in the background: frames come from a `MediaStreamTrackProcessor` and the 20 Hz sender
runs on a Worker timer, so background-tab throttling doesn't apply.

The first time a face is seen it calibrates automatically ("look at your screen for 3 s").
Press **Calibrate** again any time you move the laptop or change how you sit.

If the camera is denied or missing, a demo panel with 4 mood buttons appears instead.

## Connect the board

* **Connect USB**: pick the Espressif device (VID 0x303A). If it isn't listed, use
  **Other port…** (no filter). 115200 baud. A port you granted before is reopened automatically on
  page load and when it's plugged back in. Close the Arduino serial monitor first, since only one
  program can hold the port at a time.
* **Connect EyeBuddy-3F2A** (Bluetooth): once you have a board, the button connects to *that* board only
  (Chrome's chooser is filtered to its name). **Other board…** shows every EyeBuddy nearby. If the link drops,
  it retries 5 times (0.5/1/2/4/8 s backoff), then waits for you to click again.

Only one link is active at a time. The pill shows which board and what the handshake is doing. Touch events from
the board show up as a toast.

This app only drives **EyeBuddy** boards. A **RoboEyes** board (pill eyes on a 3D ball, same Waveshare screen)
has its own app at https://softobsessing.github.io/RoboEyes/. If you plug one in here, the page says so, links
there and leaves the port alone (over USB it moves on to the next port this site may use). RoboEyes boards
advertise as `RoboEyes-XXXX`, so the Bluetooth chooser here never lists them.

## One board per computer (pairing)

Every board has its own ID, the last 4 hex digits of its chip's MAC address. The board shows it on screen for 5 s after
power-on (`ID 3F2A`), and its Bluetooth name is `EyeBuddy-3F2A`. The same firmware file works for every board.

* **Your board only obeys computers it has paired with.** Pair by plugging it in with USB once (instant), or by
  connecting over Bluetooth and touching the board when it shows **TAP TO PAIR** (30 s). Until then a Bluetooth
  computer can't move the eyes (`AUTH need`).
* **How it proves it:** this browser keeps a random 16-byte key per board (localStorage, `eyebuddy.boards`); the
  board keeps up to 8 keys in flash. On every connection the board sends a one-time nonce and the page answers with
  HMAC-SHA256(key, nonce), so the key never travels over the air again after pairing, and a recorded answer can't be
  replayed.
* **One controller at a time:** while one computer drives the eyes, others get `BUSY` (the page says so) until it
  stops for 3 s. The board also accepts only one Bluetooth connection and stops advertising while connected.
* **Giving a board away / starting over:** hold the board for 10 s (it counts down `RESET IN 5…1`). It forgets every
  computer. **Forget** in My boards only forgets the board in this browser.
* **My boards** lists your boards (rename, forget, pick which one the Bluetooth button uses).
  **Setup link** shows a link + QR code like `…/doomscroll-eyes/?board=3F2A` that opens the site set up for that
  board (good for a sticker or the box). It is not a password: pairing still needs USB or a touch.
* Old boards (firmware v5 and earlier) still connect, without the lock; the page suggests updating.

Security notes: USB is trusted because anyone who can plug in can also reflash the board. The weakest moment is
Bluetooth pairing itself (the key is sent once, unencrypted); pairing over USB avoids that. Bluetooth isn't
encrypted, so someone with a radio sniffer could still *watch* the expressions, but not control the board.

## Update the firmware from the page

**My boards → Update firmware (USB)**: plug the board in, pick it, done (about 30 s). It flashes the firmware this
site ships (`fw/`, with `manifest.json` checksums) using esptool-js (vendored, `vendor/esptool-js/`). It writes the
bootloader, partition table, boot_app0 and app separately and leaves the NVS area alone, so **pairings survive the
update**. (Flashing the single `EyeBuddy-firmware.bin` at 0x0 by hand erases them.) The flash is verified with MD5
afterwards. To publish a new firmware: bump `FW_VERSION` in `config.h`, compile, then run
`python3 firmware/make_web_fw.py`, which refreshes `fw/` and the manual `.bin`.

### Protocol v4 (ASCII lines, `\n` terminated, same on USB and BLE)
| direction | line | notes |
|---|---|---|
| laptop → board | `P` | hello? The page sends it right after connecting |
| board → laptop | `HELLO EyeBuddy 4 <id> <fw> <nonce>` | on boot (USB) and in reply to `P`; fresh 16-hex nonce each time. Protocol 3 (`HELLO EyeBuddy 3`, firmware v5) had no pairing; 1 = v4 and older |
| laptop → board | `A <hmac>` | 32 hex: first 16 bytes of HMAC-SHA256(key, nonce). One try per nonce |
| laptop → board | `PAIR <key>` | 32 hex. USB: saved at once. Bluetooth: board shows TAP TO PAIR |
| board → laptop | `AUTH ok` / `no` / `wait` / `timeout` / `need` | `need` = a control line (E/B/K/M/S) sent before `AUTH ok` |
| board → laptop | `BUSY` | another link is driving the eyes |
| laptop → board | `E <mood> <lookX> <lookY>` | 20 Hz once authorised + immediately on mood change. mood ∈ `neutral happy side sad wow chill angry dizzy love excite left right think over` (`neutral` = focused, `wow` = surprised, `chill` = chilling, `excite` = excited, `left`/`right` = darting, `think` = deep in thought, `over` = overwhelmed) |
| laptop → board | `K <screen> <eyes>` | colors as `RRGGBB`, e.g. `K 000000 EFC742`; sent on connect and live while you drag the color wheel. The board saves them |
| laptop → board | `B` / `B2` | blink now / double blink (the preview is the blink clock) |
| laptop → board | `M <left> <total>` | pomodoro: seconds left / session length; sent at start, on connect and every minute. `M 0 0` = stop |
| laptop → board | `S` | show the pomodoro time left for 2 s |
| board → laptop | `T touch` / `T release` / `T reset` | finger down / up (smiles while touched + 2.5 s; during a pomodoro it shows the time left instead); reset = 10 s hold |

Lines longer than 20 bytes (HELLO, A, PAIR) are sent in 20-byte Bluetooth packets in both directions and joined
back into lines on the other side.

`lookX`, `lookY` are integers from −100 to 100, with 0,0 at the centre (and when no face is seen).
Directions are on the board's screen **as the user sees it**: +X = pupils toward the right edge,
+Y = pupils toward the bottom edge. The value is the mirror image of your position, so the eyes
point at you. Move to your left and the pupils move to your left, too. The longest line,
`E neutral -100 -100\n`, is 20 bytes, so it fits one BLE packet. While a write is in flight,
older queued `E` frames (and `K` color lines) are dropped in favour of the newest.

When **Pause tracking** is on, the camera is switched off and the app keeps sending `E neutral 0 0`.

## Tune

Open **Tuning**. The live table shows every signal, your calibrated baseline, the difference (Δ),
and the limit. A row turns red when that signal alone would count as "away".

| rule (per frame) | default |
|---|---|
| no face | AWAY |
| blink > 0.5 (eyes closed) | HOLD: keep the previous call, so blinks never count as away |
| \|yaw − yaw₀\| > 25° | AWAY (head turned) |
| pitch − pitch₀ < −15° | AWAY (head tilted **down**) |
| gazeDown − gazeDown₀ > 0.35 | AWAY (eyes down, e.g. phone in lap) |
| \|gazeSide − gazeSide₀\| > 0.5 | AWAY (eyes sideways) |

Mood timing: AWAY → `side` on the first away frame, still AWAY 2.0 s later → `sad` (counts one "caught"; the
count resets at local midnight). When you blink, EyeBuddy blinks too (preview right away, board via `B`); its own random blinks only fill in gaps. After `side`/`sad`, ON for 0.3 s → `happy` for 1.2 s → `neutral`.

Tips:
* If glancing down at the keyboard trips "away", raise **eyes down** or **head down**.
* If it misses the phone, lower **eyes down** first (phones are usually an eyes-down + head-down
  motion), and recalibrate while looking at the middle of the screen.
* **follow gain** sets how far the board's pupils move. **smoothing** is the eye-follow filter
  time constant (150 ms).
* Settings, baseline and the caught count live in `localStorage`. **Reset to defaults** resets
  the sliders.

Sign conventions (from MediaPipe's facial transformation matrix, column-major, camera space
+X right / +Y up / camera looking down −Z): pitch is positive when the nose is up and negative
when it's down. Yaw is positive when you turn to your own left. These were checked with a test
portrait: a frontal face gives a forward vector ≈ (0,0,+1), and rotating the image clockwise gives
positive roll. To check pitch yourself, tilt your head down and watch the pitch readout go negative.

## Files
```
index.html  style.css  app.js     UI + wiring
vision.js                         FaceLandmarker (GPU, CPU fallback) + signal extraction
attention.js                      pure classifier + mood state machine (no DOM, time passed in)
colors.js                         colors card: color wheel, brightness, hex, saved color pairs
link.js                           Link interface: SerialLink (USB) / BleLink (Nordic UART)
boards.js                         your boards + keys (localStorage), HELLO parsing, HMAC answer (WebCrypto)
session.js                        per-connection handshake: hello -> auth/pair -> ready / busy / failed
flasher.js, md5.js, fw/           Update firmware button (esptool-js), firmware parts + manifest
test/boards.test.mjs              node test/boards.test.mjs
test/attention.test.mjs           node test/attention.test.mjs
vendor/                           tasks-vision 0.10.35 bundle + wasm, face_landmarker.task (float16 v1)
```

## Expression buttons
Under the status: **Auto** (the camera decides) or one of **focused / welcome back / side-eye / disappointed /
surprised / chilling / angry / dizzy / love**. Focused (the default) is two tall ovals; chilling is the original round dots.
**angry** grows out of the focused ovals: the lids slide down into a glare, then squeeze into a squint and hold.
**dizzy** plays once: the eyes wobble around the face, flatten, spin as swirls for 1 s, then melt back into the
focused ovals and your previous expression comes back.
**love** turns the focused ovals into hearts: they melt into shape, pop a little bigger, settle, then beat
(lub-dub) about once a second.

**Reactions** (their own row): **excited** (the eyes pop bigger and hop, squashing as they land),
**dart left / dart right** (a quick sideways dart, stretched while moving, then they hold and now and then glance back;
left ↔ right darts straight across), **thinking** (the eyes drift up to the side with flat bottoms and sway while three
thought dots pop in toward the top right), **overwhelmed** (squeezed shut into `> <`, trembling, with a sweat drop
sliding down). All of them grow straight out of the focused ovals. Pressing one holds it on the board (and the preview) until you press Auto; the eyes still follow you.
**surprised** is button-only: the eyes blink, then grow from small wide ovals to huge tall ones with tiny pupils.

## Pomodoro
Under the expressions: **Start 25-min pomodoro**. The eyes go surprised, the board shows the timer for 2 s (big
`MM:SS` and a ring that empties as time runs out), then focused eyes until time's up. During the session, touch the
board to see the time left for 2 s. When time's up the board shows `00:00`, the eyes go surprised, then back to Auto,
and the page says "Pomodoro done!". **Stop pomodoro** ends it early. The board keeps its own countdown, so touching it
still shows the time if the page is closed; the page remembers a running session across a reload.

**Look away during a pomodoro** (camera on) for more than 1 s and the eyes turn **angry**: the lids slide down into a
glare, then a squint, and stay that way. Look back at the screen and they smoothly settle into the focused ovals again.

## Colors
The **colors** card sets the eye color and the screen color. Pick **Eyes** or **Screen**, then drag on the wheel
(hue around, saturation outward), use the brightness slider, or type a hex code. The preview and the board change
live. **Save these colors** keeps the pair (up to 8) as a little face; click one to use it again, × to remove it.
**Reset** goes back to yellow on black. The current colors and the saved list live in `localStorage`; the board
stores the last colors it got, so they survive a restart.

## Board firmware
Source in [`firmware/EyeBuddy`](firmware/EyeBuddy). First time (or a board with other firmware): flash
`EyeBuddy-firmware.bin` from the [releases page](https://github.com/softobsessing/doomscroll-eyes/releases) at 0x0 with
https://espressif.github.io/esptool-js/ in Chrome. After that, use **Update firmware** on this page.

## Screen preview
The round "EyeBuddy's screen" card shows exactly what the board shows, live, even with no board connected.
`eyes.js` is a line-for-line port of the firmware's `EyeBuddy/eyes.cpp`, fed the same mood/look values the app sends
(and it reacts to the board's touch events too). While connected the preview is the clock: its blinks are sent as `B` /
`B2` and its small glances ride along in the `E` look values, so the board blinks and moves at the same moment. If you change shapes or timings in `eyes.cpp`, change `eyes.js` too and run the pixel check:
`clang++ -O2 -std=c++17 -o /tmp/parity firmware/EyeBuddy/extras/parity.cpp firmware/EyeBuddy/eyes.cpp && /tmp/parity /tmp/parity_out && node test/eyes_port.test.mjs /tmp/parity_out`.
