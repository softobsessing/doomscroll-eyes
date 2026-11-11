// Eye engine: animation state + per-pixel shading for the two eyes.
// Pure C++ (no Arduino / ESP-IDF), so the exact same code runs on the board and in
// tools/eyes_preview on the Mac.
//
// Coordinates are "logical" screen pixels: 466x466, (0,0) top-left, y down.
#pragma once
#include <stdint.h>

namespace eyes {

enum Mood : uint8_t { NEUTRAL, HAPPY, SIDE, SAD, MOOD_COUNT };
const char* moodName(Mood m);              // "neutral", "happy", "side", "sad"
bool parseMood(const char* s, Mood* out);

constexpr int SCREEN = 466;

constexpr uint16_t rgb565(int r5, int g6, int b5) { return (uint16_t)((r5 << 11) | (g6 << 5) | b5); }
// Picked as exact RGB565 values so the panel shows exactly this (no rounding drift).
constexpr uint16_t COLOR_EYE    = rgb565(29, 49, 8);   // warm yellow  ~#EFC642
constexpr uint16_t COLOR_CREASE = rgb565(20, 30, 6);   // lid line on "sad" ~#A57931

// One eye, ready to draw this frame.
struct Eye {
  Mood shape;
  float cx, cy;     // visible centre of the eye (logical px)
  float scale;      // breathing
  float squash;     // 1 = open, 0 = fully closed (blink)
  float side;       // -1 = left eye on screen, +1 = right eye
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
};

}  // namespace eyes
