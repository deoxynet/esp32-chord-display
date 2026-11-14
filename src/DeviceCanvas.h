#pragma once

#include <M5Unified.h>
#include "Display.h"

class DeviceCanvas : public chord::Canvas {
public:
    explicit DeviceCanvas(M5Canvas& sprite) : sprite_(sprite) {}
    void rect(int x, int y, int w, int h, uint32_t color, int radius) override {
        if (radius) sprite_.fillRoundRect(x, y, w, h, radius, rgb(color));
        else sprite_.fillRect(x, y, w, h, rgb(color));
    }
    void line(int x1, int y1, int x2, int y2, uint32_t color) override {
        sprite_.drawLine(x1, y1, x2, y2, rgb(color));
    }
    void ellipse(int x, int y, int rx, int ry, uint32_t color) override {
        sprite_.fillEllipse(x, y, rx, ry, rgb(color));
    }
    void text(const std::string& value, int x, int y, chord::Font font, float scale, uint32_t color) override {
        selectFont(font, scale);
        sprite_.setTextColor(rgb(color));
        sprite_.setTextDatum(lgfx::textdatum_t::top_left);
        sprite_.drawString(value.c_str(), x, y);
    }
    int textWidth(const std::string& value, chord::Font font, float scale) override {
        selectFont(font, scale);
        return sprite_.textWidth(value.c_str());
    }

private:
    static uint16_t rgb(uint32_t color) {
        return lgfx::color565(static_cast<uint8_t>(color >> 16),
            static_cast<uint8_t>(color >> 8), static_cast<uint8_t>(color));
    }
    void selectFont(chord::Font font, float scale) {
        const lgfx::IFont* selected = &fonts::Font0;
        if (font == chord::Font::Body) selected = &fonts::Font2;
        else if (font == chord::Font::Chord) selected = &fonts::FreeSansBold24pt7b;
        sprite_.setFont(selected);
        sprite_.setTextSize(scale);
    }
    M5Canvas& sprite_;
};
