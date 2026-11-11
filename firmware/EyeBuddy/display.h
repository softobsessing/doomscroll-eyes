// Bare panel driver for the 1.43" AMOLED (SH8601 or CO5300): no LVGL.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void display_init(void);              // panel on, brightness 0 (clear it, then raise brightness)
void display_brightness(uint8_t level);
// Send a block of big-endian RGB565 pixels to physical rect [x0..x1] x [y0..y1] (inclusive).
// x0, y0 must be even and x1, y1 odd (panel requirement). Returns once the transfer is queued;
// the buffer must stay untouched until the next display_draw / display_wait returns.
void display_draw(int x0, int y0, int x1, int y1, const uint16_t* pixels);
void display_wait(void);   // block until the last transfer finished

#ifdef __cplusplus
}
#endif
