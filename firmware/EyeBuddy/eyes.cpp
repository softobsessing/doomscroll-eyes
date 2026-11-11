#include "eyes.h"
#include <math.h>
#include <string.h>

namespace eyes {

static const char* const NAMES[MOOD_COUNT] = {"neutral", "happy", "side", "sad"};
const char* moodName(Mood m) { return m < MOOD_COUNT ? NAMES[m] : "?"; }
bool parseMood(const char* s, Mood* out) {
  for (int i = 0; i < MOOD_COUNT; i++)
    if (strcmp(s, NAMES[i]) == 0) { *out = (Mood)i; return true; }
  return false;
}

// ---- look of each mood (logical px). Drawn from the four reference renders. ----
struct Style {
  float spacing;        // eye centre distance from screen centre
  float gazeX, gazeY;   // how far the eyes travel for look = +-1
  float halfW, halfH;   // half size of the drawn shape (for bounding boxes)
};
static const Style STYLES[MOOD_COUNT] = {
    /* NEUTRAL dots      */ {100, 70, 55, 36, 36},
    /* HAPPY  ^ ^        */ {105, 30, 20, 40, 27},
    /* SIDE   half-moons */ {98, 45, 25, 74, 37},
    /* SAD    heavy lids */ {100, 30, 15, 70, 48},
};

constexpr float DOT_R = 36;
constexpr float HAPPY_HALF_W = 34, HAPPY_HALF_H = 21, HAPPY_STROKE = 6.0f;
constexpr float SIDE_R = 74;                                // half circle, flat lid through the middle
constexpr float SAD_RX = 70, SAD_RY = 66, SAD_H = 76;       // boat shape under a heavy lid
constexpr float SAD_TILT = 7.0f * 3.14159265f / 180.0f;     // outer corners droop
constexpr float CREASE_DROP = 17, CREASE_R = 130, CREASE_HALF = 2.2f, CREASE_HALF_LEN = 50;
constexpr float MIN_CLOSED_H = 4;                           // a closed eye is a 4 px line

static const float CX = SCREEN / 2.0f, CY = SCREEN / 2.0f;

// ---- tiny SDF helpers (negative inside) ----
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float len(float x, float y) { return sqrtf(x * x + y * y); }

// Good-quality ellipse distance approximation (stays sharp when very flat, i.e. mid-blink).
static float sdEllipse(float x, float y, float rx, float ry) {
  float k0 = len(x / rx, y / ry);
  float k1 = len(x / (rx * rx), y / (ry * ry));
  return k1 > 1e-6f ? k0 * (k0 - 1.0f) / k1 : -(rx < ry ? rx : ry);
}
static float sdSegment(float px, float py, float ax, float ay, float bx, float by) {
  float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
  float d = bax * bax + bay * bay;
  float h = d > 1e-6f ? clamp01((pax * bax + pay * bay) / d) : 0;
  return len(pax - bax * h, pay - bay * h);
}
// Exact distance to a quadratic Bezier curve A-B-C (Inigo Quilez). Falls back to a segment when straight.
static float sdBezier(float px, float py, float ax, float ay, float bx, float by, float cx, float cy) {
  float a_x = bx - ax, a_y = by - ay;
  float b_x = ax - 2 * bx + cx, b_y = ay - 2 * by + cy;
  float bb = b_x * b_x + b_y * b_y;
  if (bb < 1e-3f) return sdSegment(px, py, ax, ay, cx, cy);
  float c_x = 2 * a_x, c_y = 2 * a_y, d_x = ax - px, d_y = ay - py;
  float kk = 1.0f / bb;
  float kx = kk * (a_x * b_x + a_y * b_y);
  float ky = kk * (2 * (a_x * a_x + a_y * a_y) + (d_x * b_x + d_y * b_y)) / 3.0f;
  float kz = kk * (d_x * a_x + d_y * a_y);
  float p = ky - kx * kx, p3 = p * p * p;
  float q = kx * (2 * kx * kx - 3 * ky) + kz;
  float h = q * q + 4 * p3;
  auto dist2 = [&](float t) {
    float x = d_x + (c_x + b_x * t) * t, y = d_y + (c_y + b_y * t) * t;
    return x * x + y * y;
  };
  if (h >= 0) {
    h = sqrtf(h);
    float x0 = (h - q) / 2, x1 = (-h - q) / 2;
    float t = cbrtf(x0) + cbrtf(x1) - kx;
    return sqrtf(dist2(clamp01(t)));
  }
  float z = sqrtf(-p);
  float v = acosf(q / (p * z * 2)) / 3;
  float m = cosf(v), n = sinf(v) * 1.732050808f;
  float t0 = clamp01((m + m) * z - kx), t1 = clamp01((-n - m) * z - kx);
  return sqrtf(fminf(dist2(t0), dist2(t1)));
}

// Distance for one eye shape around its visible centre. `crease` gets the lid-line coverage (sad only).
static float shapeDistance(Mood shape, float x, float y, float s, float side, float* crease) {
  *crease = 0;
  switch (shape) {
    case NEUTRAL: {
      float ry = fmaxf(DOT_R * s, MIN_CLOSED_H / 2);
      return sdEllipse(x, y, DOT_R, ry);
    }
    case HAPPY: {  // rounded arch (^), flattens to a bar when blinking
      float h = HAPPY_HALF_H * s;   // control point at -3h puts the top of the curve at -h
      return sdBezier(x, y, -HAPPY_HALF_W, h, 0, -3 * h, HAPPY_HALF_W, h) - HAPPY_STROKE;
    }
    case SIDE: {   // half circle with a flat lid on top
      float H = fmaxf(SIDE_R * s, MIN_CLOSED_H);
      float k = H / SIDE_R;
      float ry = SIDE_R * k, ecy = H / 2 - ry;
      float lid = -H / 2 - y;                       // > 0 above the lid
      return fmaxf(sdEllipse(x, y - ecy, SIDE_R, ry), lid);
    }
    case SAD: {    // heavy tilted lid + lid crease line
      float H = fmaxf(SAD_H * s, MIN_CLOSED_H);
      float k = H / SAD_H;
      float ry = SAD_RY * k, ecy = H / 2 - ry, L = -H / 2;
      static const float COS_T = cosf(SAD_TILT), SIN_T = sinf(SAD_TILT);   // not per pixel
      float ct = COS_T, st = side * SIN_T;
      // frame aligned with the lid: qx along the lid, qy = depth below the lid
      float qx = x * ct + (y - L) * st;
      float qy = -x * st + (y - L) * ct;
      float d = fmaxf(sdEllipse(x, y - ecy, SAD_RX, ry), -qy);
      float dc = fabsf(len(qx, qy - (CREASE_DROP * k - CREASE_R)) - CREASE_R) - CREASE_HALF;
      float ends = clamp01((CREASE_HALF_LEN - fabsf(qx)) / 8.0f);
      *crease = clamp01(0.5f - dc) * ends * clamp01((s - 0.5f) / 0.3f);
      return d;
    }
    default:
      return 1e9f;
  }
}

uint16_t Engine::shade(int px, int py) const {
  float alpha = 0, crease = 0;
  for (int i = 0; i < 2; i++) {
    const Eye& e = eye_[i];
    if (px < e.x0 || px > e.x1 || py < e.y0 || py > e.y1) continue;
    float x = (px + 0.5f - e.cx) / e.scale, y = (py + 0.5f - e.cy) / e.scale;
    float c;
    float d = shapeDistance(e.shape, x, y, e.squash, e.side, &c) * e.scale;  // screen px
    float a = clamp01(0.5f - d);
    if (a > alpha) { alpha = a; crease = c; }
  }
  if (alpha <= 0) return 0;
  // mix in 5/6-bit channel space, then quantise; alpha 1 gives the exact palette colour
  float r = 29 + (20 - 29) * crease, g = 49 + (30 - 49) * crease, b = 8 + (6 - 8) * crease;
  return rgb565((int)(r * alpha + 0.5f), (int)(g * alpha + 0.5f), (int)(b * alpha + 0.5f));
}

// ---- animation ----
uint32_t Engine::rand32() {   // xorshift32
  rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
  return rng_;
}
float Engine::randf(float lo, float hi) { return lo + (hi - lo) * (rand32() & 0xFFFFFF) / (float)0x1000000; }

void Engine::begin(uint32_t seed) { rng_ = seed ? seed : 1; started_ = false; }
void Engine::setMood(Mood m) { if (m < MOOD_COUNT) target_ = m; }
void Engine::setLook(float x, float y) {
  lookTX_ = x < -1 ? -1 : (x > 1 ? 1 : x);
  lookTY_ = y < -1 ? -1 : (y > 1 ? 1 : y);
}
void Engine::setWander(bool on) { wander_ = on; }

// Blink: close fast (70 ms), stay shut (30 ms), open a bit slower (130 ms). Double blink adds a second one.
constexpr uint32_t BLINK_CLOSE = 70, BLINK_SHUT = 100, BLINK_END = 230, DOUBLE_GAP = 80;

void Engine::startBlink(uint32_t now) {
  blinkPhase_ = 1;
  blinkStart_ = now;
  swappedThisBlink_ = false;
  doubleBlink_ = false;
}

float Engine::blinkOpenness(uint32_t now) {
  uint32_t t = now - blinkStart_;
  // Swap the mood at the first update after the eyes reach shut. Don't require landing inside the
  // 30 ms shut window: at 50-60 ms per frame every sample skipped it, the mood never changed and the
  // eyes re-blinked forever (looked like a permanent squint).
  if (!swappedThisBlink_ && t >= BLINK_CLOSE) { mood_ = target_; swappedThisBlink_ = true; }
  uint32_t total = doubleBlink_ ? 2 * BLINK_END + DOUBLE_GAP : BLINK_END;
  if (t >= total) { blinkPhase_ = 0; return 1; }
  if (t >= BLINK_END) {
    if (t < BLINK_END + DOUBLE_GAP) return 1;
    t -= BLINK_END + DOUBLE_GAP;
  }
  if (t < BLINK_CLOSE) { float u = t / (float)BLINK_CLOSE; return 1 - u * u; }
  if (t < BLINK_SHUT) return 0;
  float u = (t - BLINK_SHUT) / (float)(BLINK_END - BLINK_SHUT);
  return 1 - (1 - u) * (1 - u);
}

bool Engine::blinkNow(uint32_t now, bool twice) {
  if (blinkPhase_ || target_ != mood_) return false;
  startBlink(now);
  doubleBlink_ = twice;
  return true;
}

bool Engine::takeBlinkEvent(bool* twice) {
  if (!blinkEvent_) return false;
  blinkEvent_ = false;
  if (twice) *twice = blinkEventTwice_;
  return true;
}

static inline bool reached(uint32_t now, uint32_t when) { return (int32_t)(now - when) >= 0; }

void Engine::update(uint32_t now) {
  if (!started_) {
    started_ = true;
    lastMs_ = now;
    nextBlink_ = now + (uint32_t)randf(1500, 4000);
    nextJitter_ = nextWander_ = now;
  }
  float dt = (int32_t)(now - lastMs_) / 1000.0f;
  if (dt < 0) dt = 0;
  if (dt > 0.1f) dt = 0.1f;
  lastMs_ = now;

  // blinking: random 2-6 s, sometimes a double blink; a mood change forces a blink right away
  if (!blinkPhase_ && (target_ != mood_ || (autoBlink_ && reached(now, nextBlink_)))) {
    bool natural = target_ == mood_;
    startBlink(now);
    doubleBlink_ = natural && randf(0, 1) < 0.15f;
    if (natural) { blinkEvent_ = true; blinkEventTwice_ = doubleBlink_; }
  }
  float open = 1;
  if (blinkPhase_) {
    open = blinkOpenness(now);
    if (!blinkPhase_) nextBlink_ = now + (uint32_t)randf(2000, 6000);
  }

  // gaze: target (+ idle wandering) + tiny jitter, eased like quick eye movements
  if (wander_ && reached(now, nextWander_)) {
    if (randf(0, 1) < 0.3f) wanderX_ = wanderY_ = 0;
    else { wanderX_ = randf(-0.7f, 0.7f); wanderY_ = randf(-0.45f, 0.45f); }
    nextWander_ = now + (uint32_t)randf(900, 3200);
  }
  if (!autoBlink_) {
    jitX_ = jitY_ = 0;   // the clock's glances arrive baked into setLook()
  } else if (reached(now, nextJitter_)) {
    jitX_ = randf(-0.05f, 0.05f);
    jitY_ = randf(-0.04f, 0.04f);
    nextJitter_ = now + (uint32_t)randf(400, 1600);
  }
  float tx = (wander_ ? wanderX_ : lookTX_) + jitX_, ty = (wander_ ? wanderY_ : lookTY_) + jitY_;
  float a = 1 - expf(-dt / 0.055f);
  lookX_ += (tx - lookX_) * a;
  lookY_ += (ty - lookY_) * a;

  float breathe = 1 + 0.018f * sinf(now * (6.2831853f / 4200.0f));
  const Style& st = STYLES[mood_];
  for (int i = 0; i < 2; i++) {
    Eye& e = eye_[i];
    e.shape = mood_;
    e.side = i == 0 ? -1.0f : 1.0f;
    e.cx = CX + e.side * st.spacing + lookX_ * st.gazeX;
    e.cy = CY + lookY_ * st.gazeY;
    e.scale = breathe;
    e.squash = open;
    float hw = st.halfW * breathe + 3, hh = st.halfH * breathe + 3;
    e.x0 = (int)floorf(e.cx - hw); e.x1 = (int)ceilf(e.cx + hw);
    e.y0 = (int)floorf(e.cy - hh); e.y1 = (int)ceilf(e.cy + hh);
  }
}

}  // namespace eyes
