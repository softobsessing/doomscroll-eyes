// Draws the eyes onto the panel: rotation, dirty boxes, DMA strips.
#pragma once
#include "eyes.h"

void render_begin();                                       // init panel, clear to black (brightness stays 0)
void render_frame(const eyes::Engine& engine);             // redraw only what changed around the eyes
void render_to_logical(int px, int py, int* lx, int* ly);  // panel/touch coords -> eye coords
