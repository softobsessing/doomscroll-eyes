// A line of chunky pixel text under the eyes (board ID, "TAP TO PAIR", ...). Pure C++, tested on the Mac.
#pragma once
#include <stdint.h>

namespace eyes {

constexpr int TEXT_SCALE = 4;      // each font pixel is 4x4 screen pixels (5x7 font -> 20x28 per letter)
constexpr int TEXT_CY = 360;       // centre line, below the eyes even when they look all the way down
constexpr int TEXT_MAX = 23;

struct TextBox { int x0, y0, x1, y1; bool valid; };
TextBox textBox(const char* s);              // logical pixels covered by s (inclusive); !valid for null/empty
bool textPixel(const char* s, int x, int y); // is logical pixel (x, y) lit by s?

}  // namespace eyes
