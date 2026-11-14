#pragma once

#include "Analysis.h"
#include <cstdint>

namespace chord {

enum class Font { Label, Body, Chord };

// The device and native SVG preview use the same rendering code.
class Canvas {
public:
    virtual ~Canvas() = default;
    virtual void rect(int x, int y, int w, int h, uint32_t color, int radius = 0) = 0;
    virtual void line(int x1, int y1, int x2, int y2, uint32_t color) = 0;
    virtual void ellipse(int x, int y, int rx, int ry, uint32_t color) = 0;
    virtual void text(const std::string& value, int x, int y, Font font, float scale, uint32_t color) = 0;
    virtual int textWidth(const std::string& value, Font font, float scale) = 0;
};

struct DisplayStatus {
    bool connected = false;
    bool demo = false;
    bool frozen = false;
    bool settingsOpen = false;
    bool keyPickerOpen = false;
    bool overrun = false;
    bool midiActive = false;
    uint8_t selectedSetting = 0;
    int battery = -1;
    uint32_t dropped = 0;
    uint32_t malformed = 0;
    std::string address;
    std::string trail;
    std::string motion;
};

void render(Canvas& canvas, const Snapshot& snapshot, const Settings& settings, const DisplayStatus& status);

} // namespace chord
