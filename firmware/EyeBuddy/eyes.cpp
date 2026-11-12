#include "eyes.h"
#include <math.h>
#include <string.h>

namespace eyes {

static const char* const NAMES[MOOD_COUNT] = {"neutral", "happy", "side", "sad", "wow", "chill"};
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
    /* NEUTRAL tall ovals (focused, default) */ {103, 50, 40, 55, 74},
    /* HAPPY  ^ ^        */ {105, 30, 20, 40, 27},
    /* SIDE   half-moons */ {98, 45, 25, 74, 37},
    /* SAD    heavy lids */ {100, 30, 15, 70, 48},
    /* WOW    surprised  */ {105, 25, 15, 101, 140},   // spacing/size animate, see WOW_KEYS
    /* CHILL  dots       */ {100, 70, 55, 36, 36},     // "chilling": the original round dots
};

// ---- surprised: the eyes grow through the four reference frames (logical px), smallest to biggest ----
struct WowKey { float t, spacing, rx, ry, pupilR, pupilIn; };
static const WowKey WOW_KEYS[] = {
    // t (s)  spacing  rx   ry   pupil r  pupil toward nose
    {0.00f,  89,  53,  35, 3.0f,  0},   // small wide ovals
    {0.22f, 112,  95,  69, 5.0f, 58},   // wider, pupils glance toward the nose
    {0.50f,  98,  83, 110, 3.5f,  0},   // tall
    {0.85f, 105, 101, 140, 3.0f,  0},   // huge, nearly touching (then holds)
};
constexpr int WOW_N = sizeof(WOW_KEYS) / sizeof(WOW_KEYS[0]);

// Catmull-Rom through the keyframes: one continuous growth, no stops at the frames.
static float catmull(float p0, float p1, float p2, float p3, float u) {
  return 0.5f * (2 * p1 + (p2 - p0) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u + (3 * p1 - p0 - 3 * p2 + p3) * u * u * u);
}
static WowKey wowAt(float t) {
  if (t <= WOW_KEYS[0].t) return WOW_KEYS[0];
  if (t >= WOW_KEYS[WOW_N - 1].t) return WOW_KEYS[WOW_N - 1];
  int i = 0;
  while (t >= WOW_KEYS[i + 1].t) i++;
  const WowKey& k0 = WOW_KEYS[i > 0 ? i - 1 : 0];
  const WowKey& k1 = WOW_KEYS[i];
  const WowKey& k2 = WOW_KEYS[i + 1];
  const WowKey& k3 = WOW_KEYS[i + 2 < WOW_N ? i + 2 : WOW_N - 1];
  float u = (t - k1.t) / (k2.t - k1.t);
  WowKey r;
  r.t = t;
  r.spacing = catmull(k0.spacing, k1.spacing, k2.spacing, k3.spacing, u);
  r.rx = catmull(k0.rx, k1.rx, k2.rx, k3.rx, u);
  r.ry = catmull(k0.ry, k1.ry, k2.ry, k3.ry, u);
  r.pupilR = fmaxf(1.5f, catmull(k0.pupilR, k1.pupilR, k2.pupilR, k3.pupilR, u));
  r.pupilIn = fmaxf(0.f, catmull(k0.pupilIn, k1.pupilIn, k2.pupilIn, k3.pupilIn, u));
  return r;
}

constexpr float FOCUS_RX = 55, FOCUS_RY = 74;               // focused: tall ovals
constexpr float DOT_R = 36;                                 // chilling: round dots
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
static float shapeDistance(const Eye& e, float x, float y, float* crease) {
  *crease = 0;
  const float s = e.squash, side = e.side;
  switch (e.shape) {
    case NEUTRAL: {   // tall oval, squashes flat when blinking
      float ry = fmaxf(FOCUS_RY * s, MIN_CLOSED_H / 2);
      return sdEllipse(x, y, FOCUS_RX, ry);
    }
    case CHILL: {     // round dot
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
    case WOW: {    // big oval (the pupil is cut out in shade())
      float ry = fmaxf(e.ry * s, MIN_CLOSED_H / 2);
      return sdEllipse(x, y, e.rx, ry);
    }
    default:
      return 1e9f;
  }
}

// ---- timer screen: MM:SS in seven-segment digits + a ring that empties as time runs out ----
constexpr float DIGIT_W = 64, DIGIT_H = 120, DIGIT_T = 14, DIGIT_GAP = 22, COLON_W = 34, SEG_INSET = 11;
constexpr float RING_R = 212, RING_HALF = 4, RING_DIM = 0.22f;
static const uint8_t SEGMENTS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   // bits a..g

// coverage of one digit centred at x = cx (y = 0 is the middle line)
static float digitCoverage(int digit, float x, float y, float cx) {
  const float xl = cx - DIGIT_W / 2 + DIGIT_T / 2, xr = cx + DIGIT_W / 2 - DIGIT_T / 2;
  const float yt = -DIGIT_H / 2 + DIGIT_T / 2, yb = DIGIT_H / 2 - DIGIT_T / 2, s = SEG_INSET;
  uint8_t m = SEGMENTS[digit];
  float d = 1e9f;
  if (m & 0x01) d = fminf(d, sdSegment(x, y, xl + s, yt, xr - s, yt));   // a top
  if (m & 0x02) d = fminf(d, sdSegment(x, y, xr, yt + s, xr, -s));       // b upper right
  if (m & 0x04) d = fminf(d, sdSegment(x, y, xr, s, xr, yb - s));        // c lower right
  if (m & 0x08) d = fminf(d, sdSegment(x, y, xl + s, yb, xr - s, yb));   // d bottom
  if (m & 0x10) d = fminf(d, sdSegment(x, y, xl, s, xl, yb - s));        // e lower left
  if (m & 0x20) d = fminf(d, sdSegment(x, y, xl, yt + s, xl, -s));       // f upper left
  if (m & 0x40) d = fminf(d, sdSegment(x, y, xl + s, 0, xr - s, 0));     // g middle
  return clamp01(0.5f - (d - DIGIT_T / 2));
}

uint16_t Engine::shadeTimer(int px, int py) const {
  float x = px + 0.5f - CX, y = py + 0.5f - CY;
  float a = 0;
  float r = len(x, y);
  float ring = clamp01(0.5f - (fabsf(r - RING_R) - RING_HALF));
  if (ring > 0) {   // bright for the time left (clockwise from 12 o'clock), dim for the rest
    float ang = atan2f(x, -y);
    if (ang < 0) ang += 6.2831853f;
    a = ring * (ang <= timerFrac_ * 6.2831853f ? 1.0f : RING_DIM);
  }
  const float inner = COLON_W / 2 + DIGIT_W / 2, outer = COLON_W / 2 + DIGIT_W + DIGIT_GAP + DIGIT_W / 2;
  if (fabsf(y) <= DIGIT_H / 2 + 2 && fabsf(x) <= outer + DIGIT_W / 2 + 2) {
    uint32_t mm = timerSecs_ / 60, ss = timerSecs_ % 60;
    if (mm > 99) mm = 99;
    const int digits[4] = {(int)(mm / 10), (int)(mm % 10), (int)(ss / 10), (int)(ss % 10)};
    const float centers[4] = {-outer, -inner, inner, outer};
    int i = x < -(inner + outer) / 2 ? 0 : x < 0 ? 1 : x < (inner + outer) / 2 ? 2 : 3;   // the digit cell under x
    a = fmaxf(a, digitCoverage(digits[i], x, y, centers[i]));
    if (fabsf(x) < COLON_W / 2) {   // colon
      float dc = fminf(len(x, y + DIGIT_H * 0.2f), len(x, y - DIGIT_H * 0.2f)) - DIGIT_T * 0.6f;
      a = fmaxf(a, clamp01(0.5f - dc));
    }
  }
  return blend(a, 0);
}

uint16_t Engine::blend(float alpha, float crease) const {
  if (alpha <= 0) return face565_;
  // mix in 5/6-bit channel space, then quantise; alpha 1 gives the exact eye colour
  float er = eyeR_ + (eyeR_ * CREASE_DARKEN - eyeR_) * crease;
  float eg = eyeG_ + (eyeG_ * CREASE_DARKEN - eyeG_) * crease;
  float eb = eyeB_ + (eyeB_ * CREASE_DARKEN - eyeB_) * crease;
  float r = faceR_ + (er - faceR_) * alpha, g = faceG_ + (eg - faceG_) * alpha, b = faceB_ + (eb - faceB_) * alpha;
  return rgb565((int)(r + 0.5f), (int)(g + 0.5f), (int)(b + 0.5f));
}

uint16_t Engine::shade(int px, int py) const {
  if (timerShown_) return shadeTimer(px, py);
  float alpha = 0, crease = 0;
  for (int i = 0; i < 2; i++) {
    const Eye& e = eye_[i];
    if (px < e.x0 || px > e.x1 || py < e.y0 || py > e.y1) continue;
    float x = (px + 0.5f - e.cx) / e.scale, y = (py + 0.5f - e.cy) / e.scale;
    float c;
    float d = shapeDistance(e, x, y, &c) * e.scale;  // screen px
    float a = clamp01(0.5f - d);
    if (e.shape == WOW && a > 0) {   // tiny pupil: a hole showing the screen color; hides as the eye shuts
      float pr = e.pupilR * clamp01((e.squash - 0.35f) / 0.35f);
      if (pr > 0) {
        float dp = (len(x - (-e.side * e.pupilIn + e.pupilDX), y - e.pupilDY * e.squash) - pr) * e.scale;
        a *= 1 - clamp01(0.5f - dp);
      }
    }
    if (a > alpha) { alpha = a; crease = c; }
  }
  return blend(alpha, crease);
}

void Engine::setColors(uint32_t face, uint32_t eye) {
  face &= 0xFFFFFF; eye &= 0xFFFFFF;
  if (face != face_ || (timerShown_ && eye != eyeRgb_)) fullVersion_++;
  face_ = face; eyeRgb_ = eye;
  faceR_ = ((face >> 16) & 0xFF) * 31 / 255.0f; faceG_ = ((face >> 8) & 0xFF) * 63 / 255.0f; faceB_ = (face & 0xFF) * 31 / 255.0f;
  eyeR_ = ((eye >> 16) & 0xFF) * 31 / 255.0f;   eyeG_ = ((eye >> 8) & 0xFF) * 63 / 255.0f;   eyeB_ = (eye & 0xFF) * 31 / 255.0f;
  face565_ = rgb565((int)(faceR_ + 0.5f), (int)(faceG_ + 0.5f), (int)(faceB_ + 0.5f));
}

// ---- animation ----
uint32_t Engine::rand32() {   // xorshift32
  rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
  return rng_;
}
float Engine::randf(float lo, float hi) { return lo + (hi - lo) * (rand32() & 0xFFFFFF) / (float)0x1000000; }

void Engine::begin(uint32_t seed) {
  rng_ = seed ? seed : 1;
  started_ = false;
  setColors(face_, eyeRgb_);
}
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
  if (!swappedThisBlink_ && t >= BLINK_CLOSE) { mood_ = target_; swappedThisBlink_ = true; moodSince_ = now; }
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

void Engine::setPomodoro(uint32_t now, uint32_t secondsLeft, uint32_t totalSeconds) {
  if (!secondsLeft) { pomoActive_ = false; timerUntil_ = 0; return; }
  pomoActive_ = true;
  pomoEnd_ = now + secondsLeft * 1000;
  pomoTotalMs_ = (totalSeconds ? totalSeconds : secondsLeft) * 1000;
}

void Engine::flashTimer(uint32_t now, uint32_t ms) {
  if (!pomoActive_) return;
  timerUntil_ = (now + ms) | 1;   // never 0 (0 = not showing)
}

uint32_t Engine::pomodoroSecondsLeft(uint32_t now) const {
  if (!pomoActive_ || reached(now, pomoEnd_)) return 0;
  return (pomoEnd_ - now + 999) / 1000;
}

void Engine::update(uint32_t now) {
  if (!started_) {
    started_ = true;
    lastMs_ = now;
    nextBlink_ = now + (uint32_t)randf(1500, 4000);
    nextJitter_ = nextWander_ = now;
    moodSince_ = now;
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
  float spacing = st.spacing, halfW = st.halfW, halfH = st.halfH;
  WowKey wk = {0, 0, 0, 0, 0, 0};
  if (mood_ == WOW) {
    wk = wowAt((now - moodSince_) / 1000.0f);
    spacing = wk.spacing; halfW = wk.rx; halfH = wk.ry;
  }
  for (int i = 0; i < 2; i++) {
    Eye& e = eye_[i];
    e.shape = mood_;
    e.side = i == 0 ? -1.0f : 1.0f;
    e.cx = CX + e.side * spacing + lookX_ * st.gazeX;
    e.cy = CY + lookY_ * st.gazeY;
    e.scale = breathe;
    e.squash = open;
    e.rx = wk.rx; e.ry = wk.ry;
    e.pupilR = wk.pupilR; e.pupilIn = wk.pupilIn;
    e.pupilDX = lookX_ * wk.rx * 0.25f;   // the pupils look where the eyes look
    e.pupilDY = lookY_ * wk.ry * 0.2f;
    float hw = halfW * breathe + 3, hh = halfH * breathe + 3;
    e.x0 = (int)floorf(e.cx - hw); e.x1 = (int)ceilf(e.cx + hw);
    e.y0 = (int)floorf(e.cy - hh); e.y1 = (int)ceilf(e.cy + hh);
  }

  // pomodoro: when time's up, show 00:00 for 3 s
  if (pomoActive_ && reached(now, pomoEnd_)) { pomoActive_ = false; timerUntil_ = (now + 3000) | 1; }
  bool shown = timerUntil_ != 0 && !reached(now, timerUntil_);
  if (!shown) timerUntil_ = 0;
  uint32_t secs = pomodoroSecondsLeft(now);
  if (shown != timerShown_ || (shown && secs != timerSecs_)) fullVersion_++;   // redraw on open/close/tick
  timerShown_ = shown;
  timerSecs_ = secs;
  timerFrac_ = pomoTotalMs_ ? secs * 1000.0f / pomoTotalMs_ : 0;
  if (timerFrac_ > 1) timerFrac_ = 1;
}

}  // namespace eyes
