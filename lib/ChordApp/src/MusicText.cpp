#include "MusicText.h"
#include <algorithm>
#include <cmath>

namespace chord {
namespace {
enum class Symbol { None, Major, Diminished, HalfDiminished };
Symbol symbolAt(const std::string& text, size_t offset) {
    if (text.compare(offset, 2, "Δ") == 0) return Symbol::Major;
    if (text.compare(offset, 2, "°") == 0) return Symbol::Diminished;
    if (text.compare(offset, 2, "ø") == 0) return Symbol::HalfDiminished;
    return Symbol::None;
}
int advance(Symbol symbol, Font font) {
    if (font == Font::Chord) return symbol == Symbol::Major ? 32 : symbol == Symbol::Diminished ? 18 : 28;
    if (font == Font::Body) return symbol == Symbol::Major ? 14 : symbol == Symbol::Diminished ? 9 : 12;
    return symbol == Symbol::Major ? 8 : symbol == Symbol::Diminished ? 6 : 8;
}
int scaled(float value, float scale) { return int(std::lround(value * scale)); }
void triangle(Canvas& c, int middle, int top, int width, int height, uint32_t color) {
    for (int row = 0; row <= height; ++row) {
        const int half = height ? width * row / (2 * height) : 0;
        c.line(middle - half, top + row, middle + half, top + row, color);
    }
}
void symbol(Canvas& c, Symbol kind, int x, int y, Font font, float scale, uint32_t color, uint32_t background) {
    const bool large = font == Font::Chord, body = font == Font::Body;
    const int stroke = std::max(1, scaled(large ? 2 : 1, scale));
    if (kind == Symbol::Major) {
        const int width = std::max(3, scaled(large ? 28 : body ? 12 : 6, scale));
        const int height = std::max(4, scaled(large ? 35 : body ? 12 : 7, scale));
        const int middle = x + scaled(large ? 16 : body ? 7 : 4, scale);
        const int top = y + scaled(font == Font::Label ? 0 : 2, scale);
        triangle(c, middle, top, width, height, color);
        triangle(c, middle, top + 2 * stroke, std::max(1, width - 4 * stroke), std::max(1, height - 3 * stroke), background);
    } else {
        const bool half = kind == Symbol::HalfDiminished;
        const int radius = std::max(2, scaled(large ? (half ? 10 : 6) : body ? (half ? 4 : 3) : 2, scale));
        const int centerX = x + scaled(large ? (half ? 13 : 8) : body ? (half ? 6 : 4) : 3, scale);
        const int centerY = y + scaled(large ? (half ? 25 : 10) : body ? (half ? 9 : 5) : 3, scale);
        c.ellipse(centerX, centerY, radius, radius, color);
        const int inner = std::max(0, radius - stroke);
        if (inner) c.ellipse(centerX, centerY, inner, inner, background);
        if (half) {
            const int slash = radius + std::max(1, scaled(large ? 2 : 1, scale));
            for (int i = 0; i < stroke; ++i)
                c.line(centerX - slash + i, centerY + slash, centerX + slash + i, centerY - slash, color);
        }
    }
}
}

int musicTextWidth(Canvas& canvas, const std::string& text, Font font, float scale) {
    int width = 0;
    size_t offset = 0;
    while (offset < text.size()) {
        const auto kind = symbolAt(text, offset);
        if (kind != Symbol::None) { width += scaled(advance(kind,font),scale); offset += 2; }
        else {
            const size_t start = offset++;
            while (offset < text.size() && symbolAt(text,offset) == Symbol::None) ++offset;
            width += canvas.textWidth(text.substr(start,offset-start),font,scale);
        }
    }
    return width;
}

void musicText(Canvas& canvas, const std::string& text, int x, int y, Font font, float scale, uint32_t color, uint32_t background) {
    size_t offset = 0;
    while (offset < text.size()) {
        const auto kind = symbolAt(text,offset);
        if (kind != Symbol::None) {
            symbol(canvas,kind,x,y,font,scale,color,background);
            x += scaled(advance(kind,font),scale);
            offset += 2;
        } else {
            const size_t start = offset++;
            while (offset < text.size() && symbolAt(text,offset) == Symbol::None) ++offset;
            const auto run = text.substr(start,offset-start);
            canvas.text(run,x,y,font,scale,color);
            x += canvas.textWidth(run,font,scale);
        }
    }
}
} // namespace chord
