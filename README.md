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
| laptop → board | `E <mood> <lookX> <lookY>` | 20 Hz while connected + immediately on mood change. mood ∈ `neutral happy side sad` |
| laptop → board | `P` | ping |
| board → laptop | `HELLO EyeBuddy 1` | on boot and in reply to `P` |
| board → laptop | `T tap` / `T hold` / `T release` / `T swipe_left` / `T swipe_right` | touch |

`lookX`, `lookY` are integers from −100 to 100, with 0,0 at the centre (and when no face is seen).
Directions are on the board's screen **as the user sees it**: +X = pupils toward the right edge,
+Y = pupils toward the bottom edge. The value is the mirror image of your position, so the eyes
point at you. Move to your left and the pupils move to your left, too. The longest line,
`E neutral -100 -100\n`, is 20 bytes, so it fits one BLE packet. While a write is in flight,
older queued `E` frames are dropped in favour of the newest.

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

Mood timing: AWAY for 0.4 s → `side`, still AWAY 2.0 s later → `sad` (counts one "caught"; the
count resets at local midnight). After `side`/`sad`, ON for 0.3 s → `happy` for 1.2 s → `neutral`.

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
link.js                           Link interface: SerialLink (USB) / BleLink (Nordic UART)
test/attention.test.mjs           node test/attention.test.mjs
vendor/                           tasks-vision 0.10.35 bundle + wasm, face_landmarker.task (float16 v1)
```

## Screen preview
The round "EyeBuddy's screen" card shows exactly what the board shows, live, even with no board connected.
`eyes.js` is a line-for-line port of the firmware's `EyeBuddy/eyes.cpp`, fed the same mood/look values the app sends
(and it reacts to the board's touch events too). If you change shapes or timings in `eyes.cpp`, change `eyes.js` too and run
`node test/eyes_port.test.mjs`, which checks the two pixel by pixel (after `cd tools/eyebuddy_preview && ./preview stills out/stills`).
