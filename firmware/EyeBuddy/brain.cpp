#include "brain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha256.h"

namespace eyes {

constexpr uint32_t AFTER_TOUCH_MS = 2500;      // keep smiling this long after the finger lifts
constexpr uint32_t RESET_COUNTDOWN_MS = 5000;  // holding this long starts the on-screen countdown
constexpr uint32_t BOOT_ID_MS = 5000, PAIRED_SHOW_MS = 2000, RESET_SHOW_MS = 2500, NOTICE_EVERY_MS = 1000;

static float toLook(int v) { return (v - SCREEN / 2) / (float)(SCREEN / 2); }
static bool is(const char* line, const char* word) {   // line == word, or word followed by a space
  size_t n = strlen(word);
  return strncmp(line, word, n) == 0 && (line[n] == 0 || line[n] == ' ' || line[n] == '\r');
}
static const char* arg(const char* line, const char* word) {   // text after "word ", or "" (call after is())
  size_t n = strlen(word);
  return line[n] == ' ' ? line + n + 1 : "";
}

void Brain::begin(Engine* engine, const BrainIO& io, const char* id, int fw, const uint8_t* keys, size_t keysLen) {
  eng_ = engine;
  io_ = io;
  snprintf(id_, sizeof id_, "%s", id);
  fw_ = fw;
  keys_.load(keys, keysLen);
}

void Brain::sendAll(const char* line) {
  send(SRC_USB, line);
  if (bleAuthed_) send(SRC_BLE, line);
}

void Brain::notice(int src, const char* line, uint32_t now) {
  if (lastNotice_[src] && (uint32_t)(now - lastNotice_[src]) < NOTICE_EVERY_MS) return;
  lastNotice_[src] = now ? now : 1;
  send(src, line);
}

bool Brain::mayControl(int src, uint32_t now) {
  if (!authorized(src)) { notice(src, "AUTH need", now); return false; }
  if (linked(now) && linkSrc_ != src) { notice(src, "BUSY", now); return false; }   // one controller at a time
  return true;
}

void Brain::saveKeys() {
  uint8_t buf[Pairing::MAX_KEYS * Pairing::KEY_LEN];
  size_t n = keys_.save(buf);
  if (io_.saveKeys) io_.saveKeys(buf, n);
}

void Brain::onLine(int src, const char* line, uint32_t now) {
  if (src != SRC_USB && src != SRC_BLE) return;
  char buf[96];
  if (is(line, "P")) {
    uint32_t a = io_.random ? io_.random() : 0, b = io_.random ? io_.random() : 0;
    snprintf(nonce_[src], sizeof nonce_[src], "%08lx%08lx", (unsigned long)a, (unsigned long)b);
    nonceLive_[src] = true;
    snprintf(buf, sizeof buf, "HELLO EyeBuddy %d %s %d %s", PROTOCOL_VERSION, id_, fw_, nonce_[src]);
    send(src, buf);
    return;
  }
  if (is(line, "A")) {   // challenge answer; each nonce can be tried once
    uint8_t mac[Pairing::KEY_LEN];
    bool ok = nonceLive_[src] && fromHex(arg(line, "A"), mac, sizeof mac) && keys_.verify(nonce_[src], mac);
    nonceLive_[src] = false;
    if (ok && src == SRC_BLE) bleAuthed_ = true;
    if (ok) lastNotice_[src] = 0;   // new state: let the next complaint (e.g. BUSY) through right away
    send(src, ok ? "AUTH ok" : "AUTH no");
    return;
  }
  if (is(line, "PAIR")) {
    uint8_t key[Pairing::KEY_LEN];
    if (!fromHex(arg(line, "PAIR"), key, sizeof key)) { send(src, "AUTH no"); return; }
    if (src == SRC_USB) {   // plugged in: trusted, no touch needed
      if (keys_.add(key)) saveKeys();
      send(src, "AUTH ok");
      return;
    }
    // Bluetooth: someone has to touch the board (even for a known key: a sniffed PAIR line can't be replayed)
    memcpy(pairKey_, key, sizeof key);
    pairPending_ = true;
    pairUntil_ = now + PAIR_TIMEOUT_MS;
    send(src, "AUTH wait");
    return;
  }
  unsigned left = 0, total = 0, face = 0, eye = 0;
  if (is(line, "M") && sscanf(line, "M %u %u", &left, &total) >= 1) {   // pomodoro start / sync / stop
    if (mayControl(src, now)) eng_->setPomodoro(now, left, total);
    return;
  }
  if (is(line, "S")) {   // show the pomodoro time left now
    if (mayControl(src, now)) eng_->flashTimer(now, 2000);
    return;
  }
  if (is(line, "K") && sscanf(line, "K %6x %6x", &face, &eye) == 2) {   // colors from the website
    if (mayControl(src, now)) eng_->setColors(face, eye);
    return;
  }
  if (is(line, "B") || is(line, "B2")) {   // laptop's blink clock
    if (authorized(src) && linked(now) && linkSrc_ == src) eng_->blinkNow(now, line[1] == '2');
    return;
  }
  char mood[16];
  int x = 0, y = 0;
  Mood m;
  if (sscanf(line, "E %15s %d %d", mood, &x, &y) >= 1 && parseMood(mood, &m)) {
    if (!mayControl(src, now)) return;
    if (!linked(now)) linkBlink_ = true;   // a laptop just connected (or came back): blink hello
    haveLink_ = true;
    linkSrc_ = src;
    lastLink_ = now;
    linkMood_ = m;
    linkX_ = (x < -100 ? -100 : x > 100 ? 100 : x) / 100.0f;
    linkY_ = (y < -100 ? -100 : y > 100 ? 100 : y) / 100.0f;
  }
}

void Brain::onLinkDrop(int src) {
  if (src != SRC_BLE) return;
  bleAuthed_ = false;
  nonceLive_[SRC_BLE] = false;
  pairPending_ = false;
  lastNotice_[SRC_BLE] = 0;
  if (haveLink_ && linkSrc_ == SRC_BLE) haveLink_ = false;   // free the eyes for USB right away
}

void Brain::forgetAll(uint32_t now) {
  keys_.clear();
  saveKeys();
  sendAll("T reset");
  onLinkDrop(SRC_BLE);
  if (io_.dropBle) io_.dropBle();
  resetShowUntil_ = now + RESET_SHOW_MS;
  eng_->blinkNow(now, true);
}

void Brain::overrideFor(Mood m, float lx, float ly, uint32_t until) {
  override_ = true;
  overrideMood_ = m;
  overrideX_ = lx;
  overrideY_ = ly;
  overrideUntil_ = until;
}

// Any touch = the "welcome back" smile, right away, for as long as the finger stays plus 2.5 s.
// During a pomodoro a touch shows the time left instead. While a Bluetooth laptop is waiting to pair,
// a touch accepts it. Holding 10 s forgets all laptops (in any mode).
void Brain::onTouch(bool down, int x, int y, uint32_t now) {
  if (down && !down_) {
    down_ = true;
    downAt_ = now;
    heldMs_ = 0;
    resetFired_ = false;
    if (eng_->pomodoroActive() && !pairPending_) {   // studying: show the time left
      eng_->flashTimer(now, 2000);
    } else {
      overrideHeld_ = true;
      overrideFor(HAPPY, toLook(x), toLook(y), now);
    }
    if (pairPending_) {
      pairPending_ = false;
      if (keys_.add(pairKey_)) saveKeys();
      bleAuthed_ = true;
      lastNotice_[SRC_BLE] = 0;
      send(SRC_BLE, "AUTH ok");
      pairedShowUntil_ = now + PAIRED_SHOW_MS;
    }
    sendAll("T touch");
  } else if (down) {
    if (override_) {
      overrideX_ = toLook(x);   // eyes follow the finger (when no laptop is driving the gaze)
      overrideY_ = toLook(y);
    }
    heldMs_ = now - downAt_;
    if (!resetFired_ && heldMs_ >= RESET_HOLD_MS) { resetFired_ = true; forgetAll(now); }
  } else if (down_) {
    down_ = false;
    heldMs_ = 0;
    if (overrideHeld_) { overrideHeld_ = false; overrideUntil_ = now + AFTER_TOUCH_MS; }
    sendAll("T release");
  }
}

void Brain::update(uint32_t now) {
  if (!started_) { started_ = true; bootUntil_ = now + BOOT_ID_MS; }
  if (pairPending_ && (int32_t)(now - pairUntil_) >= 0) { pairPending_ = false; send(SRC_BLE, "AUTH timeout"); }

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
  } else if (pairPending_) {   // look down at "TAP TO PAIR"
    eng_->setWander(false);
    eng_->setMood(NEUTRAL);
    eng_->setLook(0, 0.8f);
  } else {   // nobody connected: just hang out, blink and look around
    eng_->setWander(true);
    eng_->setMood(NEUTRAL);
  }
}

const char* Brain::overlay(uint32_t now) {
  if (down_ && !resetFired_ && heldMs_ >= RESET_COUNTDOWN_MS) {
    snprintf(text_, sizeof text_, "RESET IN %d", (int)((RESET_HOLD_MS - heldMs_ + 999) / 1000));
    return text_;
  }
  if ((int32_t)(now - resetShowUntil_) < 0 && resetShowUntil_) return "RESET";
  if (pairPending_) return "TAP TO PAIR";
  if ((int32_t)(now - pairedShowUntil_) < 0 && pairedShowUntil_) return "PAIRED";
  if (started_ && (int32_t)(now - bootUntil_) < 0) {
    snprintf(text_, sizeof text_, "ID %s", id_);
    return text_;
  }
  return nullptr;
}

}  // namespace eyes
