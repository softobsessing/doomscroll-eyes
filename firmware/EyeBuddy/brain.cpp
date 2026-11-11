#include "brain.h"
#include <stdio.h>
#include <stdlib.h>

namespace eyes {

constexpr uint32_t AFTER_TOUCH_MS = 2500;   // keep smiling this long after the finger lifts

static float toLook(int v) { return (v - SCREEN / 2) / (float)(SCREEN / 2); }

void Brain::onLine(const char* line, uint32_t now) {
  if (line[0] == 'P' && (line[1] == 0 || line[1] == '\r')) {
    if (emit_) emit_("HELLO EyeBuddy 1");
    return;
  }
  if (line[0] == 'B' && (line[1] == 0 || line[1] == '2' || line[1] == '\r')) {   // laptop's blink clock
    if (linked(now)) eng_->blinkNow(now, line[1] == '2');
    return;
  }
  char mood[16];
  int x = 0, y = 0;
  Mood m;
  if (sscanf(line, "E %15s %d %d", mood, &x, &y) >= 1 && parseMood(mood, &m)) {
    if (!linked(now)) linkBlink_ = true;   // a laptop just connected (or came back): blink hello
    haveLink_ = true;
    lastLink_ = now;
    linkMood_ = m;
    linkX_ = (x < -100 ? -100 : x > 100 ? 100 : x) / 100.0f;
    linkY_ = (y < -100 ? -100 : y > 100 ? 100 : y) / 100.0f;
  }
}

void Brain::overrideFor(Mood m, float lx, float ly, uint32_t until) {
  override_ = true;
  overrideMood_ = m;
  overrideX_ = lx;
  overrideY_ = ly;
  overrideUntil_ = until;
}

// Any touch = the "welcome back" smile, right away, for as long as the finger stays plus 2.5 s.
void Brain::onTouch(bool down, int x, int y, uint32_t now) {
  if (down && !down_) {
    down_ = true;
    overrideHeld_ = true;
    overrideFor(HAPPY, toLook(x), toLook(y), now);
    if (emit_) emit_("T touch");
  } else if (down) {
    overrideX_ = toLook(x);   // eyes follow the finger (when no laptop is driving the gaze)
    overrideY_ = toLook(y);
  } else if (down_) {
    down_ = false;
    overrideHeld_ = false;
    overrideUntil_ = now + AFTER_TOUCH_MS;
    if (emit_) emit_("T release");
  }
}

void Brain::update(uint32_t now) {
  eng_->setAutoBlink(!linked(now));   // connected: blink exactly when the website's preview blinks
  if (linkBlink_) { linkBlink_ = false; eng_->blinkNow(now, false); }   // the website blinks at the same moment
  if (override_ && !overrideHeld_ && (int32_t)(now - overrideUntil_) >= 0) override_ = false;
  if (override_) {
    eng_->setWander(false);
    eng_->setMood(overrideMood_);
    // connected: keep following the laptop's gaze so the website preview shows the same thing
    if (linked(now)) eng_->setLook(linkX_, linkY_);
    else eng_->setLook(overrideX_, overrideY_);
  } else if (linked(now)) {
    eng_->setWander(false);
    eng_->setMood(linkMood_);
    eng_->setLook(linkX_, linkY_);
  } else {   // nobody connected: just hang out, blink and look around
    eng_->setWander(true);
    eng_->setMood(NEUTRAL);
  }
}

}  // namespace eyes
