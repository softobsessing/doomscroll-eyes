// Decides what the eyes should do: touch beats the laptop, the laptop beats idle.
// Pure C++ like eyes.h, so it can be tested on the Mac.
//
// Protocol (text lines, '\n'-terminated, same over USB serial and Bluetooth):
//   laptop -> board   E <neutral|happy|side|sad|wow|chill> <lookX -100..100> <lookY -100..100>
//                       (neutral = focused tall ovals, wow = surprised, chill = chilling round dots)
//                     K <screen RRGGBB> <eyes RRGGBB>   (colors, e.g. "K 000000 EFC742")
//                     M <secondsLeft> <totalSeconds>    (pomodoro start/sync; "M 0 0" = stop)
//                     S                      (show the pomodoro time left for 2 s)
//                     B | B2                 (blink now / double blink: the web app is the blink clock,
//                                             so board and website blink at the same moment)
//                     P                      (ping, board answers HELLO)
//   board -> laptop   HELLO EyeBuddy 3
//                     T touch | T release    (finger down / finger up; during a pomodoro a touch shows the time left)
#pragma once
#include "eyes.h"

namespace eyes {

class Brain {
 public:
  typedef void (*Emit)(const char* line);   // send one line (without '\n') to the laptop
  static constexpr uint32_t LINK_TIMEOUT_MS = 3000;

  void begin(Engine* engine, Emit emit) { eng_ = engine; emit_ = emit; }
  void onLine(const char* line, uint32_t now);
  void onTouch(bool down, int x, int y, uint32_t now);   // logical coords, call every poll
  void update(uint32_t now);                             // call before Engine::update

  bool linked(uint32_t now) const { return haveLink_ && (uint32_t)(now - lastLink_) < LINK_TIMEOUT_MS; }

 private:
  void overrideFor(Mood m, float lx, float ly, uint32_t until);

  Engine* eng_ = nullptr;
  Emit emit_ = nullptr;

  bool haveLink_ = false;
  uint32_t lastLink_ = 0;
  Mood linkMood_ = NEUTRAL;
  float linkX_ = 0, linkY_ = 0;

  bool linkBlink_ = false;   // just got linked: blink hello

  // touch
  bool down_ = false;
  bool override_ = false, overrideHeld_ = false;
  uint32_t overrideUntil_ = 0;
  Mood overrideMood_ = HAPPY;
  float overrideX_ = 0, overrideY_ = 0;
};

}  // namespace eyes
