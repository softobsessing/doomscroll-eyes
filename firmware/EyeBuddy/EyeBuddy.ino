// EyeBuddy: expressive eyes for the Waveshare ESP32-S3-Touch-AMOLED-1.43.
//
// On its own it blinks and looks around. Touch it: tap = happy, hold = happy while petted,
// swipe left/right = flip through the expressions. Connect the eyebuddy-web app (USB or
// Bluetooth) and it reacts to whether you're looking at your laptop or doom scrolling.
//
// Arduino IDE board settings: "ESP32S3 Dev Module", USB CDC On Boot: Enabled,
// Flash Size: 16MB, PSRAM: OPI PSRAM, Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS).
// No extra libraries needed.
#include <Arduino.h>
#include <Preferences.h>
#include "ble_link.h"
#include "brain.h"
#include "config.h"
#include "display.h"
#include "eyes.h"
#include "render.h"
#include "touch.h"

using namespace eyes;

static Engine engine;
static Brain brain;

// ---------- colors: set from the website, remembered across restarts ----------
static Preferences prefs;
static uint32_t seenFace, seenEye, colorChangedAt = 0;
static bool colorsDirty = false;

static void loadColors() {
  prefs.begin("eyebuddy", false);
  engine.setColors(prefs.getUInt("face", DEFAULT_FACE_COLOR), prefs.getUInt("eye", DEFAULT_EYE_COLOR));
  seenFace = engine.faceColor();
  seenEye = engine.eyeColor();
}

// Save 1.5 s after the last change, so dragging the color wheel doesn't wear out the flash.
static void saveColorsWhenSettled(uint32_t now) {
  if (engine.faceColor() != seenFace || engine.eyeColor() != seenEye) {
    seenFace = engine.faceColor();
    seenEye = engine.eyeColor();
    colorChangedAt = now;
    colorsDirty = true;
  }
  if (colorsDirty && now - colorChangedAt > 1500) {
    prefs.putUInt("face", seenFace);
    prefs.putUInt("eye", seenEye);
    colorsDirty = false;
  }
}

// ---------- talking to the web app ----------
static void emit(const char* line) {
  Serial.println(line);
  ble_send_line(line);
}

struct LineReader {
  char buf[96];
  size_t len = 0;
  void feed(char c, uint32_t now) {
    if (c == '\n' || c == '\r') {
      if (len) {
        buf[len] = 0;
        brain.onLine(buf, now);
        len = 0;
      }
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    } else {
      len = 0;   // garbage / overlong line: drop it
    }
  }
};
static LineReader usbLine, bleLine;

static void pollLinks(uint32_t now) {
  while (Serial.available() > 0) usbLine.feed((char)Serial.read(), now);
  char tmp[64];
  size_t n;
  while ((n = ble_read(tmp, sizeof tmp)) > 0)
    for (size_t i = 0; i < n; i++) bleLine.feed(tmp[i], now);
}

static uint32_t lastTouchPoll = 0;
static void pollTouch(uint32_t now) {
  if ((int32_t)(now - lastTouchPoll) < 20) return;
  lastTouchPoll = now;
  uint16_t x, y;
  bool down = touch_read(&x, &y);
  int lx = 0, ly = 0;
  if (down) render_to_logical(x, y, &lx, &ly);
  brain.onTouch(down, lx, ly, now);
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // never stall the animation when no laptop is listening

  render_begin();   // panel on and cleared to black, then brightness up
  display_brightness(SCREEN_BRIGHTNESS);

  touch_begin();
  ble_begin(BLE_NAME);
  engine.begin(esp_random());
  loadColors();
  brain.begin(&engine, emit);
  emit("HELLO EyeBuddy 3");
}

void loop() {
  const uint32_t frameMs = 1000 / TARGET_FPS;
  uint32_t start = millis();
  pollLinks(start);
  pollTouch(start);
  brain.update(start);
  engine.update(start);
  render_frame(engine);
  saveColorsWhenSettled(start);
  uint32_t spent = millis() - start;

  // every 10 s, over USB only: "D fps <frames/s> worst_ms <slowest frame>" (the web app ignores D lines)
  static uint32_t statStart = 0, frames = 0, worst = 0;
  frames++;
  if (spent > worst) worst = spent;
  if ((int32_t)(start - statStart) >= 10000) {
    if (statStart) Serial.printf("D fps %lu worst_ms %lu\n", (unsigned long)(frames / 10), (unsigned long)worst);
    statStart = start;
    frames = worst = 0;
  }
  if (spent < frameMs) delay(frameMs - spent);
}
