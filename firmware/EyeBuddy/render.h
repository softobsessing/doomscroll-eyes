// Draws the eyes (and an optional text line under them) onto the panel: rotation, dirty boxes, DMA strips.
#pragma once
#include "eyes.h"

void render_begin();                                       // init panel, clear to black (brightness stays 0)
// Redraw only what changed: the boxes around the eyes, plus the text line when it changes. text may be null.
void render_frame(const eyes::Engine& engine, const char* text = nullptr);
uint16_t render_pixel(const eyes::Engine& engine, const char* text, int x, int y);   // what logical (x, y) shows
void render_to_logical(int px, int py, int* lx, int* ly);  // panel/touch coords -> eye coords
