// Renders a scripted scenario with the board's eye engine (eyes.cpp), so the website's
// preview (eyes.js) can be checked against it pixel by pixel. From the repo root:
//   clang++ -O2 -std=c++17 -o /tmp/parity firmware/EyeBuddy/extras/parity.cpp firmware/EyeBuddy/eyes.cpp
//   /tmp/parity /tmp/parity_out && node test/eyes_port.test.mjs /tmp/parity_out
// The same script is in test/eyes_port.test.mjs. Keep the two in sync.
// (Arduino ignores this folder; it only builds the sketch folder itself.)
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include "../eyes.h"
using namespace eyes;

static void dump(const Engine &e, const std::string &path) {
  std::vector<uint16_t> f(SCREEN * SCREEN);
  for (int y = 0; y < SCREEN; y++)
    for (int x = 0; x < SCREEN; x++) f[y * SCREEN + x] = e.shade(x, y);
  FILE *fp = fopen(path.c_str(), "wb");
  fwrite(f.data(), 2, f.size(), fp);
  fclose(fp);
}

static const uint32_t SHOTS[] = { 700, 910, 980, 1060, 1150, 1260, 1400, 1580, 1800, 2100, 2470, 3300, 4100, 4900, 5650, 5990 };

int main(int argc, char **argv) {
  std::string dir = argc > 1 ? argv[1] : "parity_out";
  int n = 0;
  // ---- driven by the website: moods (incl. surprised), look, blinks, colors ----
  {
    Engine e; e.begin(7);
    int k = 0;
    for (uint32_t t = 0; t <= 6000; t += 10) {
      e.setWander(false); e.setAutoBlink(false);
      Mood m = t < 800 ? NEUTRAL : t < 3000 ? WOW : t < 3800 ? HAPPY : t < 4600 ? SIDE : t < 5400 ? SAD : t < 5800 ? CHILL : WOW;
      e.setMood(m);
      e.setLook((float)(0.5 * sin(t / 600.0)), (float)(0.4 * cos(t / 800.0)));
      if (t == 2000) e.setColors(0x14213D, 0xFF5FA2);   // navy screen, pink eyes
      if (t == 2400) e.blinkNow(t, false);
      if (t == 4200) e.setColors(0xFFFFFF, 0x2A9D8F);   // white screen, teal eyes
      e.update(t);
      for (uint32_t s : SHOTS) if (s == t) { dump(e, dir + "/linked_" + std::to_string(k++) + ".bin"); n++; }
    }
  }
  // ---- on its own: random blinks and wandering (same seed) ----
  {
    Engine e; e.begin(3);
    e.setWander(true);
    int k = 0;
    for (uint32_t t = 0; t <= 6000; t += 10) {
      e.update(t);
      for (uint32_t s : SHOTS) if (s == t) { dump(e, dir + "/solo_" + std::to_string(k++) + ".bin"); n++; }
    }
  }
  // ---- pomodoro: timer screen (ticking), flash on demand, stop, and the 00:00 ending ----
  {
    Engine e; e.begin(5); e.setWander(false); e.setAutoBlink(false);
    e.setColors(0x14213D, 0xFF5FA2);
    const uint32_t pshots[] = { 200, 1300, 2500, 3500, 4300, 6100 };
    int k = 0;
    for (uint32_t t = 0; t <= 6200; t += 10) {
      if (t == 0) e.setPomodoro(t, 65, 1500);       // 01:05 left of 25:00
      if (t == 100 || t == 3000) e.flashTimer(t, 2000);
      if (t == 3800) e.setPomodoro(t, 0, 0);        // stopped: the flash ends right away
      if (t == 4000) e.setPomodoro(t, 2, 10);       // runs out at 6000 -> 00:00 for 3 s
      e.update(t);
      for (uint32_t s : pshots) if (s == t) { dump(e, dir + "/pomo_" + std::to_string(k++) + ".bin"); n++; }
    }
  }
  // ---- angry and dizzy evolving out of the focused ovals (and dizzy melting back) ----
  {
    Engine e; e.begin(9); e.setWander(false); e.setAutoBlink(false);
    const uint32_t eshots[] = { 600, 700, 800, 900, 1100, 1500, 2700, 2950, 3300, 3700, 4200, 4500, 4800, 5300, 5800, 6000, 6400, 7200 };
    int k = 0;
    for (uint32_t t = 0; t <= 7300; t += 10) {
      e.setMood(t < 500 ? NEUTRAL : t < 2000 ? ANGRY : t < 2600 ? NEUTRAL : t < 7000 ? DIZZY : NEUTRAL);
      e.setLook((float)(0.3 * sin(t / 500.0)), (float)(0.2 * cos(t / 700.0)));
      e.update(t);
      for (uint32_t s : eshots) if (s == t) { dump(e, dir + "/evolve_" + std::to_string(k++) + ".bin"); n++; }
    }
  }
  // ---- angry settling back into focused: full release, release mid-glare, angry again mid-release ----
  {
    Engine e; e.begin(11); e.setWander(false); e.setAutoBlink(false);
    const uint32_t sshots[] = { 1700, 1900, 2100, 2250, 2400, 2800, 3000, 3300, 3500, 3650, 3800, 4200 };
    int k = 0;
    for (uint32_t t = 0; t <= 4300; t += 10) {
      e.setMood(t < 200 ? NEUTRAL : t < 1600 ? ANGRY : t < 2600 ? NEUTRAL : t < 2900 ? ANGRY : t < 3200 ? NEUTRAL : ANGRY);
      e.setLook((float)(0.3 * sin(t / 450.0)), (float)(0.2 * cos(t / 650.0)));
      e.update(t);
      for (uint32_t s : sshots) if (s == t) { dump(e, dir + "/settle_" + std::to_string(k++) + ".bin"); n++; }
    }
  }
  // ---- love: ovals melt into hearts, pop, beat; blink while in love; back to focused ----
  {
    Engine e; e.begin(13); e.setWander(false); e.setAutoBlink(false);
    const uint32_t lshots[] = { 400, 500, 600, 700, 850, 1300, 1450, 1560, 2100, 2600, 2680, 3200 };
    int k = 0;
    for (uint32_t t = 0; t <= 3300; t += 10) {
      e.setMood(t < 300 ? NEUTRAL : t < 2550 ? LOVE : NEUTRAL);
      e.setLook((float)(0.3 * sin(t / 450.0)), (float)(0.2 * cos(t / 650.0)));
      if (t == 2050) e.blinkNow(t, false);
      e.update(t);
      for (uint32_t s : lshots) if (s == t) { dump(e, dir + "/love_" + std::to_string(k++) + ".bin"); n++; }
    }
  }
  printf("wrote %d frames to %s\n", n, dir.c_str());
  return 0;
}
