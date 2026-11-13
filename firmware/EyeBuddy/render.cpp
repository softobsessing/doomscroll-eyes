#include "render.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "display.h"
#include "text.h"

using namespace eyes;

// ---------- logical (what the eyes code draws) <-> physical (panel) coordinates ----------
// Same mapping LVGL's software rotation used in the Waveshare demo, so touch and drawing agree.
constexpr int N = SCREEN;
static inline void toPhysical(int lx, int ly, int* px, int* py) {
#if SCREEN_ROTATION == 90
  *px = ly; *py = N - 1 - lx;
#elif SCREEN_ROTATION == 180
  *px = N - 1 - lx; *py = N - 1 - ly;
#elif SCREEN_ROTATION == 270
  *px = N - 1 - ly; *py = lx;
#else
  *px = lx; *py = ly;
#endif
}
static inline void toLogical(int px, int py, int* lx, int* ly) {
#if SCREEN_ROTATION == 90
  *lx = N - 1 - py; *ly = px;
#elif SCREEN_ROTATION == 180
  *lx = N - 1 - px; *ly = N - 1 - py;
#elif SCREEN_ROTATION == 270
  *lx = py; *ly = N - 1 - px;
#else
  *lx = px; *ly = py;
#endif
}

// ---------- drawing ----------
// Two phases per frame so both eyes change on the panel at (almost) the same instant:
//   1. shade every changed box into `stage`, a full-screen copy of the panel in PSRAM (slow part)
//   2. push those boxes out in quick DMA strips (fast part, a few ms for both eyes together)
constexpr int BUF_PX = N * 36;
static uint16_t* bufs[2];      // internal DMA strips, ping-ponged
static int curBuf = 0;
static uint16_t* stage;        // N*N big-endian RGB565, physical layout

struct Box {
  int x0, y0, x1, y1;
  bool valid;
};
static Box prevBox[2];

// logical box -> physical rect, aligned the way the panel wants (even start, odd end)
static bool physicalRect(Box b, Box* p) {
  if (b.x0 < 0) b.x0 = 0;
  if (b.y0 < 0) b.y0 = 0;
  if (b.x1 > N - 1) b.x1 = N - 1;
  if (b.y1 > N - 1) b.y1 = N - 1;
  if (b.x0 > b.x1 || b.y0 > b.y1) return false;
  int ax, ay, bx, by;
  toPhysical(b.x0, b.y0, &ax, &ay);
  toPhysical(b.x1, b.y1, &bx, &by);
  *p = {min(ax, bx) & ~1, min(ay, by) & ~1, max(ax, bx) | 1, max(ay, by) | 1, true};
  return true;
}

// What logical pixel (x, y) shows: the text line on top of the eyes.
uint16_t render_pixel(const Engine& engine, const char* text, int x, int y) {
  if (text && textPixel(text, x, y)) {   // in the eye color
    uint32_t c = engine.eyeColor();
    return rgb565((c >> 19) & 31, (c >> 10) & 63, (c >> 3) & 31);
  }
  return engine.shade(x, y);
}

static void shadeRect(const Engine* engine, const char* text, const Box& p) {
  for (int py = p.y0; py <= p.y1; py++) {
    uint16_t* out = stage + py * N + p.x0;
    for (int px = p.x0; px <= p.x1; px++) {
      int lx, ly;
      toLogical(px, py, &lx, &ly);
      uint16_t c = engine ? render_pixel(*engine, text, lx, ly) : 0;
      *out++ = (uint16_t)((c >> 8) | (c << 8));   // panel takes big-endian RGB565
    }
  }
}

static void pushRect(const Box& p) {
  int w = p.x1 - p.x0 + 1;
  int rowsPer = (BUF_PX / w) & ~1;
  for (int py = p.y0; py <= p.y1; py += rowsPer) {
    int rows = min(rowsPer, p.y1 - py + 1);
    uint16_t* out = bufs[curBuf];
    for (int r = 0; r < rows; r++, out += w) memcpy(out, stage + (py + r) * N + p.x0, w * sizeof(uint16_t));
    display_draw(p.x0, py, p.x1, py + rows - 1, bufs[curBuf]);
    curBuf ^= 1;
  }
}

static Box unite(const Box& a, const Box& b) {
  if (!a.valid) return b;
  if (!b.valid) return a;
  return {min(a.x0, b.x0), min(a.y0, b.y0), max(a.x1, b.x1), max(a.y1, b.y1), true};
}
static bool overlaps(const Box& a, const Box& b) {
  return a.x0 <= b.x1 + 8 && b.x0 <= a.x1 + 8 && a.y0 <= b.y1 + 8 && b.y0 <= a.y1 + 8;
}

static char prevText[TEXT_MAX + 1];   // text currently on the panel ("" = none)

void render_frame(const Engine& engine, const char* text) {
  if (!text) text = "";
  // screen color changed, timer screen opened/closed/ticked (or first frame): redraw everything
  static uint32_t drawnVersion = 0;
  if (engine.fullVersion() != drawnVersion) {
    drawnVersion = engine.fullVersion();
    for (int i = 0; i < 2; i++) {
      const Eye& e = engine.eye(i);
      prevBox[i] = {e.x0, e.y0, e.x1, e.y1, true};
    }
    strncpy(prevText, text, TEXT_MAX);
    prevText[TEXT_MAX] = 0;
    Box all = {0, 0, N - 1, N - 1, true};
    shadeRect(&engine, text, all);
    pushRect(all);
    return;
  }
  Box region[3];
  for (int i = 0; i < 2; i++) {
    const Eye& e = engine.eye(i);
    Box now = {e.x0, e.y0, e.x1, e.y1, true};
    region[i] = unite(prevBox[i], now);   // erase where the eye was, draw where it is
    prevBox[i] = now;
  }
  Box logical[3];
  int n = 0;
  if (overlaps(region[0], region[1])) logical[n++] = unite(region[0], region[1]);
  else { logical[n++] = region[0]; logical[n++] = region[1]; }
  if (strncmp(text, prevText, TEXT_MAX) != 0) {   // text appeared / changed / went away: redraw its old + new box
    TextBox a = textBox(prevText), b = textBox(text);
    logical[n++] = unite({a.x0, a.y0, a.x1, a.y1, a.valid}, {b.x0, b.y0, b.x1, b.y1, b.valid});
    strncpy(prevText, text, TEXT_MAX);
    prevText[TEXT_MAX] = 0;
  }
  Box phys[3];
  int m = 0;
  for (int i = 0; i < n; i++)
    if (logical[i].valid && physicalRect(logical[i], &phys[m])) m++;
  for (int i = 0; i < m; i++) shadeRect(&engine, text, phys[i]);   // phase 1: both eyes (+ text)
  for (int i = 0; i < m; i++) pushRect(phys[i]);                   // phase 2: out together
}

void render_to_logical(int px, int py, int* lx, int* ly) { toLogical(px, py, lx, ly); }

void render_begin() {
  for (int i = 0; i < 2; i++) {
    bufs[i] = (uint16_t*)heap_caps_malloc(BUF_PX * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(bufs[i]);
  }
  stage = (uint16_t*)heap_caps_calloc(N * N, sizeof(uint16_t), MALLOC_CAP_SPIRAM);   // needs PSRAM: OPI
  assert(stage);
  display_init();
  pushRect({0, 0, N - 1, N - 1, true});   // all black (stage starts zeroed), while brightness is still 0
  display_wait();
}
