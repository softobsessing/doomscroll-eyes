// Decides what the eyes should do: touch beats the laptop, the laptop beats idle.
// Pure C++ like eyes.h, so it can be tested on the Mac.
//
// Protocol (text lines, '\n'-terminated, same over USB serial and Bluetooth):
//   laptop -> board   E <neutral|happy|side|sad> <lookX -100..100> <lookY -100..100>
//                     B | B2                 (blink now / double blink: the web app is the blink clock,
//                                             so board and website blink at the same moment)
//                     P                      (ping, board answers HELLO)
//   board -> laptop   HELLO EyeBuddy 1
//                     T tap | T hold | T release | T swipe_left | T swipe_right
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

  // touch
  bool down_ = false, holdFired_ = false;
  uint32_t downAt_ = 0;
  int x0_ = 0, y0_ = 0, lastX_ = 0, lastY_ = 0, moved_ = 0;
  bool override_ = false, overrideHeld_ = false;
  uint32_t overrideUntil_ = 0;
  Mood overrideMood_ = HAPPY;
  float overrideX_ = 0, overrideY_ = 0;
  int manual_ = NEUTRAL;   // expression picked by swiping
};

}  // namespace eyes
