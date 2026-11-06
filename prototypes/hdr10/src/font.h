// Own 5x7 bitmap font (no dependencies). Lowercase renders as uppercase.
#pragma once
#include <cstdint>
// Draws into a CPU RGBA8 buffer (row 0 = top). Returns advance width in pixels.
int font_draw(uint32_t* px, int w, int h, int x, int y, const char* s, uint32_t rgba, int scale);
constexpr int FONT_W = 6, FONT_H = 8; // cell incl. spacing, at scale 1
