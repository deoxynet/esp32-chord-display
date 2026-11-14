#pragma once

#include "Display.h"

namespace chord {
// The installed bitmap fonts lack these musical glyphs. Shared vector drawing
// gives the device and SVG preview the same delta, diminished and half-dim signs.
int musicTextWidth(Canvas& canvas, const std::string& text, Font font, float scale);
void musicText(Canvas& canvas, const std::string& text, int x, int y, Font font, float scale,
               uint32_t color, uint32_t background);
} // namespace chord
