// Decides what the eyes should do: touch beats the laptop, the laptop beats idle. Also guards who may
// drive the board. Pure C++ like eyes.h, so it can be tested on the Mac.
//
// Protocol v4 (text lines, '\n'-terminated, same over USB serial and Bluetooth):
//   laptop -> board   P                      hello? -> HELLO with a fresh one-time nonce
//                     A <hmac>               prove a saved key: hex of the first 16 bytes of
//                                            HMAC-SHA256(key, nonce)  -> AUTH ok | AUTH no
//                     PAIR <key>             32 hex chars (16 random bytes). USB: saved at once (USB = physical
//                                            access). Bluetooth: board shows "TAP TO PAIR"; a touch saves it.
//                                            -> AUTH ok | AUTH wait | AUTH timeout | AUTH no
//                     E <mood> <lookX -100..100> <lookY -100..100>   (mood names: eyes.cpp NAMES)
//                       (neutral = focused tall ovals, wow = surprised, chill = chilling round dots)
//                     K <screen RRGGBB> <eyes RRGGBB>   (colors, e.g. "K 000000 EFC742")
//                     M <secondsLeft> <totalSeconds>    (pomodoro start/sync; "M 0 0" = stop)
//                     S                      (show the pomodoro time left for 2 s)
//                     B | B2                 (blink now / double blink: the web app is the blink clock)
//   board -> laptop   HELLO EyeBuddy 2 <id> <fw> <nonce>      id = 4 hex from the chip's MAC
//                     AUTH ok|no|wait|timeout|need            need = E/B sent before AUTH ok
//                     BUSY                   another link (USB or Bluetooth) is driving the eyes
//                     T touch | T release | T reset           reset = 10 s hold forgot all paired laptops
//                                            (during a pomodoro a touch shows the time left instead of smiling)
// USB is always trusted (anyone who can plug in can also reflash). Bluetooth must pass AUTH first;
// E, B, K, M and S from an unpaired Bluetooth laptop are ignored. Protocol 3 was firmware v5 (no pairing).
#pragma once
#include "eyes.h"
#include "pairing.h"

namespace eyes {

enum Src : int { SRC_USB = 0, SRC_BLE = 1 };
constexpr int PROTOCOL_VERSION = 4;

struct BrainIO {
  void (*emit)(int src, const char* line) = nullptr;           // one line (without '\n') to that link
  void (*saveKeys)(const uint8_t* data, size_t len) = nullptr; // persist Pairing::save() output
  void (*dropBle)() = nullptr;                                 // disconnect Bluetooth (after a reset)
  uint32_t (*random)() = nullptr;                              // for nonces
};

class Brain {
 public:
  static constexpr uint32_t LINK_TIMEOUT_MS = 3000, PAIR_TIMEOUT_MS = 30000, RESET_HOLD_MS = 10000;

  void begin(Engine* engine, const BrainIO& io, const char* id, int fw, const uint8_t* keys, size_t keysLen);
  void onLine(int src, const char* line, uint32_t now);
  void onLinkDrop(int src);                               // Bluetooth disconnected
  void onTouch(bool down, int x, int y, uint32_t now);    // logical coords, call every poll
  void update(uint32_t now);                              // call before Engine::update
  const char* overlay(uint32_t now);                      // text under the eyes right now, or nullptr

  bool linked(uint32_t now) const { return haveLink_ && (uint32_t)(now - lastLink_) < LINK_TIMEOUT_MS; }
  bool authorized(int src) const { return src == SRC_USB || bleAuthed_; }
  int pairedCount() const { return keys_.count(); }

 private:
  void overrideFor(Mood m, float lx, float ly, uint32_t until);
  void send(int src, const char* line) { if (io_.emit) io_.emit(src, line); }
  void sendAll(const char* line);                         // USB + Bluetooth if authorized
  void notice(int src, const char* line, uint32_t now);   // rate-limited complaint (AUTH need / BUSY)
  bool mayControl(int src, uint32_t now);                 // authorized and not someone else's turn (else complains)
  void saveKeys();
  void forgetAll(uint32_t now);

  Engine* eng_ = nullptr;
  BrainIO io_;
  char id_[8] = "0000";
  int fw_ = 0;
  Pairing keys_;

  // auth
  bool bleAuthed_ = false;
  char nonce_[2][17] = {};
  bool nonceLive_[2] = {false, false};
  uint32_t lastNotice_[2] = {0, 0};
  bool pairPending_ = false;
  uint32_t pairUntil_ = 0, pairedShowUntil_ = 0;
  uint8_t pairKey_[Pairing::KEY_LEN] = {};

  // laptop link
  bool haveLink_ = false;
  int linkSrc_ = SRC_USB;
  uint32_t lastLink_ = 0;
  Mood linkMood_ = NEUTRAL;
  float linkX_ = 0, linkY_ = 0;
  bool linkBlink_ = false;   // just got linked: blink hello

  // touch
  bool down_ = false, resetFired_ = false;
  uint32_t downAt_ = 0, heldMs_ = 0, resetShowUntil_ = 0;
  bool override_ = false, overrideHeld_ = false;
  uint32_t overrideUntil_ = 0;
  Mood overrideMood_ = HAPPY;
  float overrideX_ = 0, overrideY_ = 0;

  bool started_ = false;
  uint32_t bootUntil_ = 0;
  char text_[24] = {};
};

}  // namespace eyes
