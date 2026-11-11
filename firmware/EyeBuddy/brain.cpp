#include "brain.h"
#include <stdio.h>
#include <stdlib.h>

namespace eyes {

constexpr uint32_t TAP_MAX_MS = 400, HOLD_MS = 550;
constexpr int TAP_SLOP = 40, SWIPE_MIN = 70;
constexpr uint32_t TAP_HAPPY_MS = 2500, AFTER_HOLD_MS = 1000, SWIPE_SHOW_MS = 6000;

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

void Brain::onTouch(bool down, int x, int y, uint32_t now) {
  if (down) {
    if (!down_) {
      down_ = true;
      holdFired_ = false;
      downAt_ = now;
      x0_ = lastX_ = x;
      y0_ = lastY_ = y;
      moved_ = 0;
    } else {
      lastX_ = x;
      lastY_ = y;
      int m = abs(x - x0_) + abs(y - y0_);
      if (m > moved_) moved_ = m;
    }
    if (!holdFired_ && moved_ < TAP_SLOP && (uint32_t)(now - downAt_) >= HOLD_MS) {
      holdFired_ = true;
      overrideHeld_ = true;   // being petted: happy for as long as the finger stays
      overrideFor(HAPPY, toLook(x0_), toLook(y0_), now);
      if (emit_) emit_("T hold");
    }
    return;
  }
  if (!down_) return;
  down_ = false;
  if (holdFired_) {
    overrideHeld_ = false;
    overrideUntil_ = now + AFTER_HOLD_MS;
    if (emit_) emit_("T release");
    return;
  }
  int dx = lastX_ - x0_, dy = lastY_ - y0_;
  if (abs(dx) >= SWIPE_MIN && abs(dx) > abs(dy)) {   // swipe: flip through the expressions by hand
    manual_ = (eng_->mood() + (dx > 0 ? 1 : MOOD_COUNT - 1)) % MOOD_COUNT;   // step from what is showing
    overrideFor((Mood)manual_, 0, 0, now + SWIPE_SHOW_MS);
    if (emit_) emit_(dx > 0 ? "T swipe_right" : "T swipe_left");
  } else if ((uint32_t)(now - downAt_) < TAP_MAX_MS && moved_ < TAP_SLOP) {   // boop
    overrideFor(HAPPY, toLook(x0_), toLook(y0_), now + TAP_HAPPY_MS);
    if (emit_) emit_("T tap");
  }
}

void Brain::update(uint32_t now) {
  eng_->setAutoBlink(!linked(now));   // connected: blink exactly when the website's preview blinks
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
