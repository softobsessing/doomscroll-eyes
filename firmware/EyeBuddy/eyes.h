// Eye engine: animation state + per-pixel shading for the two eyes.
// Pure C++ (no Arduino / ESP-IDF), so the exact same code runs on the board and in
// tools/eyes_preview on the Mac.
//
// Coordinates are "logical" screen pixels: 466x466, (0,0) top-left, y down.
#pragma once
#include <stdint.h>

namespace eyes {

// NEUTRAL = "focused" (tall ovals, the default), WOW = "surprised", CHILL = "chilling" (round dots),
// ANGRY and DIZZY evolve out of the focused ovals (DIZZY ends in swirls, then melts back into focused).
// Short names so the longest E line still fits one Bluetooth packet.
enum Mood : uint8_t { NEUTRAL, HAPPY, SIDE, SAD, WOW, CHILL, ANGRY, DIZZY, LOVE, MOOD_COUNT };
const char* moodName(Mood m);              // "neutral", "happy", "side", "sad", "wow", "chill", "angry", "dizzy", "love"
bool parseMood(const char* s, Mood* out);

constexpr int SCREEN = 466;

constexpr uint16_t rgb565(int r5, int g6, int b5) { return (uint16_t)((r5 << 11) | (g6 << 5) | b5); }
// Default colors (0xRRGGBB). The website can change both; the board remembers them.
// The yellow maps exactly onto RGB565 (29, 49, 8), so the panel shows it with no rounding drift.
constexpr uint32_t DEFAULT_EYE_COLOR  = 0xEFC742;   // warm yellow
constexpr uint32_t DEFAULT_FACE_COLOR = 0x000000;   // black screen
constexpr float CREASE_DARKEN = 0.66f;              // lid line on "sad" = eye color * this

// One eye, ready to draw this frame.
struct Eye {
  Mood shape;
  float cx, cy;     // visible centre of the eye (logical px)
  float scale;      // breathing
  float squash;     // 1 = open, 0 = fully closed (blink)
  float side;       // -1 = left eye on screen, +1 = right eye
  // surprised only: the oval (half sizes) and its tiny pupil (radius, offset toward the nose, look shift)
  float rx, ry, pupilR, pupilIn, pupilDX, pupilDY;
  // angry / dizzy: tilt, slanted lid (y = lidC + lidK * x toward the nose), corner rounding, swirl blend + spin
  float rot, cosr, sinr, lidC, lidK, round, swirl, spin;
  // love: oval -> heart blend (0..1) and the heart's size (px per heart unit, incl. pop + heartbeat)
  float morph, heartS;
  int x0, y0, x1, y1;  // bounding box incl. anti-aliasing, inclusive
};

class Engine {
 public:
  void begin(uint32_t seed);
  void setMood(Mood m);              // switches on the next blink (starts one right away)
  void setLook(float x, float y);    // -1..1 each; where the eyes should look
  void setWander(bool on);           // nobody is driving the gaze: look around by itself
  // Auto blink off = something else is the clock (the web app): no random blinks or glances of
  // its own; blink only when blinkNow() is called. Keeps the board in lockstep with the website.
  void setAutoBlink(bool on) { autoBlink_ = on; }
  bool blinkNow(uint32_t nowMs, bool twice);   // start a blink now (ignored mid-blink / mid-switch)
  bool takeBlinkEvent(bool* twice);            // true once each time this engine blinked on its own
  void update(uint32_t nowMs);

  // Colors as 0xRRGGBB. face = the screen (and the pupils), eye = the eyes.
  void setColors(uint32_t face, uint32_t eye);
  uint32_t faceColor() const { return face_; }
  uint32_t eyeColor() const { return eyeRgb_; }
  uint16_t faceRgb565() const { return face565_; }

  // Pomodoro. The countdown runs here, so the board keeps time even with no website connected.
  void setPomodoro(uint32_t nowMs, uint32_t secondsLeft, uint32_t totalSeconds);   // secondsLeft 0 = stop
  void flashTimer(uint32_t nowMs, uint32_t ms = 2000);   // show the time left for ms, then the eyes again
  bool pomodoroActive() const { return pomoActive_; }
  uint32_t pomodoroSecondsLeft(uint32_t nowMs) const;
  bool timerShown() const { return timerShown_; }

  // Bumps whenever the whole screen must be redrawn (screen color, timer screen on/off/tick).
  uint32_t fullVersion() const { return fullVersion_; }

  float jitterX() const { return jitX_; }   // current small glance offset (the web app sends it along)
  float jitterY() const { return jitY_; }
  Mood mood() const { return mood_; }
  bool blinking() const { return blinkPhase_ != 0; }
  const Eye& eye(int i) const { return eye_[i]; }
  uint16_t shade(int x, int y) const;   // RGB565 colour of logical pixel (x, y)

 private:
  uint32_t rand32();
  float randf(float lo, float hi);
  void startBlink(uint32_t now);
  float blinkOpenness(uint32_t now);

  uint32_t rng_ = 1;
  bool started_ = false;
  uint32_t lastMs_ = 0;
  Mood mood_ = NEUTRAL, target_ = NEUTRAL;

  // blink
  int blinkPhase_ = 0;          // 0 none, 1 running
  uint32_t blinkStart_ = 0, nextBlink_ = 0;
  bool doubleBlink_ = false, swappedThisBlink_ = false;
  bool autoBlink_ = true, blinkEvent_ = false, blinkEventTwice_ = false;

  // gaze
  float lookTX_ = 0, lookTY_ = 0;   // requested
  float lookX_ = 0, lookY_ = 0;     // smoothed
  float jitX_ = 0, jitY_ = 0;
  uint32_t nextJitter_ = 0;
  bool wander_ = false;
  float wanderX_ = 0, wanderY_ = 0;
  uint32_t nextWander_ = 0;

  Eye eye_[2] = {};

  // colors (channel values in RGB565 units: 0..31 / 0..63 / 0..31)
  uint32_t face_ = DEFAULT_FACE_COLOR, eyeRgb_ = DEFAULT_EYE_COLOR, fullVersion_ = 1;
  float faceR_ = 0, faceG_ = 0, faceB_ = 0, eyeR_ = 29, eyeG_ = 49, eyeB_ = 8;
  uint16_t face565_ = 0;

  uint32_t moodSince_ = 0;   // when the current mood took over (drives the surprised/angry/dizzy animations)
  bool dizzyRunning(uint32_t now) const;
  // angry -> focused settles smoothly (lids lift, eyes relax) instead of swapping inside a blink
  bool releasing_ = false;
  uint32_t releaseStart_ = 0;
  float releaseFromT_ = 0;   // how far into the angry animation we were when the release began
  float angryParam(int field, float animT, uint32_t now) const;

  // pomodoro + timer screen
  bool pomoActive_ = false, timerShown_ = false;
  uint32_t pomoEnd_ = 0, pomoTotalMs_ = 0, timerUntil_ = 0, timerSecs_ = 0;
  float timerFrac_ = 0;
  uint16_t shadeTimer(int px, int py) const;
  uint16_t blend(float alpha, float crease) const;
};

}  // namespace eyes
