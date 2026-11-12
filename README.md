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
* **Connect Bluetooth**: pick `EyeBuddy` (Nordic UART service). If the link drops, it retries
  5 times (0.5/1/2/4/8 s backoff), then gives up and waits for you to click again.

Only one link is active at a time. The pill shows the state and `EyeBuddy v1` once the board's
`HELLO` arrives (the app sends `P` right after connecting). Touch events from the board show up
as a toast ("you booped it").

### Protocol (ASCII lines, `\n` terminated, same on USB and BLE)
| direction | line | notes |
|---|---|---|
| laptop → board | `E <mood> <lookX> <lookY>` | 20 Hz while connected + immediately on mood change. mood ∈ `neutral happy side sad wow` (`wow` = surprised) |
| laptop → board | `K <screen> <eyes>` | colors as `RRGGBB`, e.g. `K 000000 EFC742`; sent on connect and live while you drag the color wheel. The board saves them |
| laptop → board | `B` / `B2` | blink now / double blink (the preview is the blink clock) |
| laptop → board | `M <left> <total>` | pomodoro: seconds left / session length; sent at start, on connect and every minute. `M 0 0` = stop |
| laptop → board | `S` | show the pomodoro time left for 2 s |
| laptop → board | `P` | ping |
| board → laptop | `HELLO EyeBuddy 2` | on boot and in reply to `P` (v2 = has surprised + colors) |
| board → laptop | `T touch` / `T release` | finger down / up (board smiles while touched + 2.5 s; during a pomodoro it shows the time left instead) |

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
test/attention.test.mjs           node test/attention.test.mjs
vendor/                           tasks-vision 0.10.35 bundle + wasm, face_landmarker.task (float16 v1)
```

## Expression buttons
Under the status: **Auto** (the camera decides) or one of **focused / welcome back / side-eye / disappointed /
surprised**. Pressing one holds it on the board (and the preview) until you press Auto; the eyes still follow you.
**surprised** is button-only: the eyes blink, then grow from small wide ovals to huge tall ones with tiny pupils.

## Pomodoro
Under the expressions: **Start 25-min pomodoro**. The eyes go surprised, the board shows the timer for 2 s (big
`MM:SS` and a ring that empties as time runs out), then focused eyes until time's up. During the session, touch the
board to see the time left for 2 s. When time's up the board shows `00:00`, the eyes go surprised, then back to Auto,
and the page says "Pomodoro done!". **Stop pomodoro** ends it early. The board keeps its own countdown, so touching it
still shows the time if the page is closed; the page remembers a running session across a reload.

## Colors
The **colors** card sets the eye color and the screen color. Pick **Eyes** or **Screen**, then drag on the wheel
(hue around, saturation outward), use the brightness slider, or type a hex code. The preview and the board change
live. **Save these colors** keeps the pair (up to 8) as a little face; click one to use it again, × to remove it.
**Reset** goes back to yellow on black. The current colors and the saved list live in `localStorage`; the board
stores the last colors it got, so they survive a restart.

## Board firmware
Source in [`firmware/EyeBuddy`](firmware/EyeBuddy). Ready-made `EyeBuddy-firmware.bin` is on the
[releases page](https://github.com/softobsessing/doomscroll-eyes/releases): flash it at 0x0 with
https://espressif.github.io/esptool-js/ in Chrome.

## Screen preview
The round "EyeBuddy's screen" card shows exactly what the board shows, live, even with no board connected.
`eyes.js` is a line-for-line port of the firmware's `EyeBuddy/eyes.cpp`, fed the same mood/look values the app sends
(and it reacts to the board's touch events too). While connected the preview is the clock: its blinks are sent as `B` /
`B2` and its small glances ride along in the `E` look values, so the board blinks and moves at the same moment. If you change shapes or timings in `eyes.cpp`, change `eyes.js` too and run the pixel check:
`clang++ -O2 -std=c++17 -o /tmp/parity firmware/EyeBuddy/extras/parity.cpp firmware/EyeBuddy/eyes.cpp && /tmp/parity /tmp/parity_out && node test/eyes_port.test.mjs /tmp/parity_out`.
