#include "eyes.h"
#include <math.h>
#include <string.h>

namespace eyes {

static const char* const NAMES[MOOD_COUNT] = {"neutral", "happy", "side", "sad", "wow", "chill", "angry", "dizzy"};
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
    /* ANGRY  glare      */ {98, 40, 30, 72, 80},      // shape animates, see ANGRY_KEYS
    /* DIZZY  wobble     */ {103, 15, 10, 80, 80},     // shape animates, see DIZZY_KEYS
};

// ---- smooth keyframe curves for angry + dizzy ----
// Cubic Hermite through the keys with time-aware tangents, flattened at peaks and holds, so a value
// eases into each extreme (like a pendulum) and never overshoots.
template <int F>
static float smoothKeys(const float (*keys)[F], int n, int field, float t) {
  if (t <= keys[0][0]) return keys[0][field];
  if (t >= keys[n - 1][0]) return keys[n - 1][field];
  int i = 0;
  while (t >= keys[i + 1][0]) i++;
  auto tangent = [&](int k) -> float {
    if (k <= 0 || k >= n - 1) return 0;
    float a = keys[k][field] - keys[k - 1][field], b = keys[k + 1][field] - keys[k][field];
    if (a * b <= 0) return 0;   // a peak, a dip or a hold: arrive with zero speed
    return (keys[k + 1][field] - keys[k - 1][field]) / (keys[k + 1][0] - keys[k - 1][0]);
  };
  float h = keys[i + 1][0] - keys[i][0], u = (t - keys[i][0]) / h;
  float u2 = u * u, u3 = u2 * u;
  return (2 * u3 - 3 * u2 + 1) * keys[i][field] + (u3 - 2 * u2 + u) * h * tangent(i) +
         (-2 * u3 + 3 * u2) * keys[i + 1][field] + (u3 - u2) * h * tangent(i + 1);
}

// angry: the lids slide down over the focused ovals (A1), then squeeze into a squint (A2) and hold.
// t (s), spacing, centre y, rx, ry, lid y at the eye centre, lid slope toward the nose, corner rounding
static const float ANGRY_KEYS[][8] = {
    {0.00f, 103,   0, 55, 74, -115.0f, 0.55f, 0},   // = the focused ovals (lid above the eye)
    {0.40f,  98, -11, 72, 69,  -28.0f, 0.55f, 8},   // big glare
    {0.85f,  98, -12, 56, 54,  -13.5f, 0.60f, 8},   // narrowed squint (holds)
};
constexpr int ANGRY_N = sizeof(ANGRY_KEYS) / sizeof(ANGRY_KEYS[0]);

// dizzy: the pair of eyes rocks around the face, flattens, becomes spinning swirls for 1 s, then
// melts back into the focused ovals.  t (s), tilt (deg), spacing, centre x, centre y, rx, ry, swirl 0..1
static const float DIZZY_KEYS[][8] = {
    {0.00f,   0, 103,  0,  0, 55, 74, 0},   // = the focused ovals
    {0.35f,  36, 100, -3, -9, 58, 78, 0},
    {0.75f, -41, 104, 32, 27, 58, 78, 0},
    {1.15f,  47, 107, -1, -1, 64, 70, 0},
    {1.55f,  -9, 100,  0,  0, 74, 41, 0},   // flattening out
    {1.90f,   0, 104,  0, -2, 60, 60, 1},   // swirls...
    {2.90f,   0, 104,  0, -2, 60, 60, 1},   // ...spinning for 1 s
    {3.30f,   0, 103,  0,  0, 55, 74, 0},   // back to the focused ovals
};
constexpr int DIZZY_N = sizeof(DIZZY_KEYS) / sizeof(DIZZY_KEYS[0]);
constexpr uint32_t DIZZY_TOTAL_MS = 3300;
constexpr float SWIRL_R = 64, SWIRL_TURNS = 2.75f, SWIRL_HALF = 5.5f, SWIRL_MIN = 0.6f, SWIRL_SPIN = 12.566f;   // rad/s (2 turns/s)

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
constexpr float TWO_PI = 6.2831853f;

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

// Distance to an Archimedean spiral (r = b * theta) drawn with a thick stroke, rotated by `spin`.
static float sdSwirl(float x, float y, float spin) {
  const float thetaMax = SWIRL_TURNS * TWO_PI, b = SWIRL_R / thetaMax;
  float r = len(x, y);
  float phi = fmodf(atan2f(y, x) - spin, TWO_PI);
  if (phi < 0) phi += TWO_PI;
  float n = floorf((r / b - phi) / TWO_PI + 0.5f);   // nearest turn
  float best = 1e9f;
  for (int k = -1; k <= 1; k++) {
    float th = phi + TWO_PI * (n + k);
    float tc = th < SWIRL_MIN ? SWIRL_MIN : (th > thetaMax ? thetaMax : th);
    float dr = r - b * tc, da = (th - tc) * b * tc;   // past the ends: round caps
    best = fminf(best, sqrtf(dr * dr + da * da));
  }
  return best - SWIRL_HALF;
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
    case ANGRY: {  // oval under a slanted lid that drops toward the nose; corners rounded
      float sq = fmaxf(s, MIN_CLOSED_H / (2 * e.ry));   // blink: squash vertically
      float yy = y / sq, xi = x * -side;                // xi > 0 toward the nose
      float R = e.round, norm = sqrtf(1 + e.lidK * e.lidK);
      float body = sdEllipse(x, yy, e.rx - R, e.ry - R);
      float lid = (e.lidC + R * norm + e.lidK * xi - yy) / norm;   // > 0 above the lid
      return (fmaxf(body, lid) - R) * sq;
    }
    case DIZZY: {  // tilted oval, blending into a spinning swirl
      float rx = x * e.cosr + y * e.sinr, ry = -x * e.sinr + y * e.cosr;
      float d = e.swirl < 1 ? sdEllipse(rx, ry, e.rx, e.ry) : 0;
      if (e.swirl > 0) d = d * (1 - e.swirl) + sdSwirl(x, y, e.spin) * e.swirl;
      return d;
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

bool Engine::dizzyRunning(uint32_t now) const { return mood_ == DIZZY && (uint32_t)(now - moodSince_) < DIZZY_TOTAL_MS; }

bool Engine::blinkNow(uint32_t now, bool twice) {
  if (blinkPhase_ || target_ != mood_ || dizzyRunning(now)) return false;   // no blinking mid-wobble
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

  // Angry and dizzy grow straight out of the focused ovals (no blink), and a finished dizzy (which already
  // looks like the focused ovals) hands back to focused without one either.
  if (!blinkPhase_ && target_ != mood_) {
    bool fromFocus = mood_ == NEUTRAL && (target_ == ANGRY || target_ == DIZZY);
    bool dizzyDone = mood_ == DIZZY && !dizzyRunning(now) && target_ == NEUTRAL;
    if (fromFocus || dizzyDone) { mood_ = target_; moodSince_ = now; }
  }
  // blinking: random 2-6 s, sometimes a double blink; a mood change forces a blink right away
  bool quiet = dizzyRunning(now);   // no random blinks while dizzy
  if (!blinkPhase_ && (target_ != mood_ || (autoBlink_ && !quiet && reached(now, nextBlink_)))) {
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
  Mood shape = mood_;
  if (mood_ == DIZZY && !dizzyRunning(now)) shape = NEUTRAL;   // dizzy is over: plain focused ovals
  const Style& st = STYLES[shape];
  float spacing = st.spacing, halfW = st.halfW, halfH = st.halfH;
  float animT = (now - moodSince_) / 1000.0f;
  // angry / dizzy shape this frame
  float pairRot = 0, offX = 0, offY = 0, eyeCy = 0, rx = 0, ry = 0, lidC = 0, lidK = 0, rnd = 0, swirl = 0;
  if (shape == ANGRY) {
    spacing = smoothKeys(ANGRY_KEYS, ANGRY_N, 1, animT);
    eyeCy = smoothKeys(ANGRY_KEYS, ANGRY_N, 2, animT);
    rx = smoothKeys(ANGRY_KEYS, ANGRY_N, 3, animT);
    ry = smoothKeys(ANGRY_KEYS, ANGRY_N, 4, animT);
    lidC = smoothKeys(ANGRY_KEYS, ANGRY_N, 5, animT);
    lidK = smoothKeys(ANGRY_KEYS, ANGRY_N, 6, animT);
    rnd = smoothKeys(ANGRY_KEYS, ANGRY_N, 7, animT);
    halfW = rx; halfH = ry + fabsf(eyeCy);
  } else if (shape == DIZZY) {
    pairRot = smoothKeys(DIZZY_KEYS, DIZZY_N, 1, animT) * (3.14159265f / 180.0f);
    spacing = smoothKeys(DIZZY_KEYS, DIZZY_N, 2, animT);
    offX = smoothKeys(DIZZY_KEYS, DIZZY_N, 3, animT);
    offY = smoothKeys(DIZZY_KEYS, DIZZY_N, 4, animT);
    rx = smoothKeys(DIZZY_KEYS, DIZZY_N, 5, animT);
    ry = smoothKeys(DIZZY_KEYS, DIZZY_N, 6, animT);
    swirl = clamp01(smoothKeys(DIZZY_KEYS, DIZZY_N, 7, animT));
    float c = cosf(pairRot), s = sinf(pairRot);
    float hx = sqrtf(rx * c * rx * c + ry * s * ry * s), hy = sqrtf(rx * s * rx * s + ry * c * ry * c);
    float sw = swirl > 0 ? SWIRL_R + SWIRL_HALF : 0;
    halfW = fmaxf(hx, sw); halfH = fmaxf(hy, sw);
  }
  WowKey wk = {0, 0, 0, 0, 0, 0};
  if (mood_ == WOW) {
    wk = wowAt((now - moodSince_) / 1000.0f);
    spacing = wk.spacing; halfW = wk.rx; halfH = wk.ry;
  }
  for (int i = 0; i < 2; i++) {
    Eye& e = eye_[i];
    e.shape = shape;
    e.side = i == 0 ? -1.0f : 1.0f;
    e.cx = CX + e.side * spacing + lookX_ * st.gazeX;
    e.cy = CY + lookY_ * st.gazeY;
    e.rot = pairRot; e.cosr = cosf(pairRot); e.sinr = sinf(pairRot);
    e.lidC = lidC; e.lidK = lidK; e.round = rnd; e.swirl = swirl;
    e.spin = animT * SWIRL_SPIN;
    if (shape == DIZZY) {   // the pair rocks around the middle of the face
      e.cx = CX + offX + e.side * spacing * e.cosr + lookX_ * st.gazeX;
      e.cy = CY + offY + e.side * spacing * e.sinr + lookY_ * st.gazeY;
    }
    if (shape == ANGRY) e.cy += eyeCy;
    e.scale = breathe;
    e.squash = open;
    e.rx = shape == WOW ? wk.rx : rx; e.ry = shape == WOW ? wk.ry : ry;
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
