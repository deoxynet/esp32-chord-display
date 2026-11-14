#pragma once
#include "Display.h"

namespace chord {
// Full-size lead-sheet name, with raised/stacked parenthesized alterations.
int chordTextWidth(Canvas& canvas, const std::string& name, float scale, bool roman = false);
void chordText(Canvas& canvas, const std::string& name, int x, int y, float scale,
               uint32_t color, uint32_t background, bool roman = false);
}
