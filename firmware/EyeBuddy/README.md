# EyeBuddy firmware

Yellow cartoon eyes for the **Waveshare ESP32-S3-Touch-AMOLED-1.43** that blink and look around by themselves, react
to touch, and react to you through the `eyebuddy-web` app (webcam, over USB or Bluetooth).

![expressions](preview/expressions.png)

**Ready-made firmware:** download `EyeBuddy-firmware.bin` from the
[releases](https://github.com/softobsessing/doomscroll-eyes/releases) and flash it at **0x0** with
https://espressif.github.io/esptool-js/ (Chrome). No Arduino needed.

`preview/eyebuddy_session.gif` is a simulated session rendered on the Mac from this exact code.

| Expression | When |
|---|---|
| dots (neutral) | default; always blinking (every 2–6 s, sometimes double), drifting slightly, following your face |
| `^ ^` happy | you come back to the screen, or you touch it |
| half-moons (side-eye) | your eyes leave the laptop screen (right away) |
| droopy lids (disappointed) | still looking away 2 s later |

Expression changes always happen mid-blink, so the eyes close in one mood and open in the next.

## Touch
- **any touch**: the welcome-back smile right away, for as long as your finger stays plus 2.5 s after.
  With no laptop connected the eyes look at your finger.

Connecting the web app (USB or Bluetooth) makes the eyes blink hello.

## Upload

### Arduino IDE
1. Boards Manager: install **esp32 by Espressif Systems** (tested with 3.3.12).
2. Open `EyeBuddy/EyeBuddy.ino`. Tools menu:
   - Board: **ESP32S3 Dev Module**
   - USB CDC On Boot: **Enabled** (needed for the web app's USB connection)
   - Flash Size: **16MB (128Mb)**
   - PSRAM: **OPI PSRAM**
   - Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)**
3. Upload. No extra libraries are needed (no LVGL).

If upload can't find the board: hold **BOOT**, tap **RESET**, release BOOT, then upload again.

### Command line (from this folder's parent)
```
~/.local/arduino-cli/arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" --output-dir EyeBuddy/build EyeBuddy
~/.local/arduino-cli/arduino-cli upload  --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" -p /dev/cu.usbmodem* --input-dir EyeBuddy/build EyeBuddy
```
To go back to the Waveshare factory clock: `esp32-tools/venv/bin/esptool.py --chip esp32s3 write_flash 0x0 ESP32-S3-Touch-AMOLED-1.43-factory.bin`.

## Settings (`config.h`)
- `SCREEN_ROTATION`: 270 matches the Waveshare demo. If the eyes are sideways on your mount, try 0 / 90 / 180.
- `SCREEN_BRIGHTNESS`: 0–255.
- `BLE_NAME`: keep it `EyeBuddy` unless you change it in the web app too.

## Protocol (USB serial and Bluetooth Nordic UART, text lines)
```
laptop -> board   E <neutral|happy|side|sad> <lookX -100..100> <lookY -100..100>    (~20 per second)
                  B | B2                                                           (blink now / double blink)
                  P                                                                (ping)
board -> laptop   HELLO EyeBuddy 1
                  T touch | T release                (finger down / finger up)
                  D fps <n> worst_ms <n>     (USB only, every 10 s, for debugging)
```
lookX + = toward the right side of the board's screen, lookY + = down. If no `E` line arrives for 3 s the board
goes back to idle (neutral, looking around).

**Lockstep with the website.** While connected, the web app's screen preview is the clock: the board turns off its own
random blinks and glances, blinks when it gets `B`, and its glances arrive inside the `E` look values. So the board shows
the same thing as the preview, at the same moment (within a frame plus the USB/Bluetooth delay). On its own it blinks by itself.

Each frame both eyes are shaded first (into a full-screen copy in PSRAM) and then sent to the panel together, so the left
and right eye always change at the same time. PSRAM must be enabled (OPI PSRAM).

## Code
- `eyes.cpp`: shapes (signed-distance drawing with anti-aliasing), blinking, gaze. Plain C++.
- `brain.cpp`: touch beats laptop beats idle; protocol parsing. Plain C++.
- `render.cpp`: rotation, redraws only the boxes around the eyes, two DMA buffers.
- `display.c`: panel init (Waveshare's init commands, no LVGL). `touch.cpp`: FT3168 over Wire.
- `ble_link.cpp`: Bluetooth Nordic UART service.

Mac checks (in the original project folder, not in this repo) `tools/eyebuddy_preview/`: `test_brain` (logic), `test_render` (panel output pixel-exact vs the eye
engine), `preview stills|anim|bench` (images).
