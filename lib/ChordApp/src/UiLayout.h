#pragma once

#include <array>
#include <algorithm>
#include "Analysis.h"

namespace chord::ui {
inline constexpr int footerY = 220;
inline constexpr int settingY = 42;
inline constexpr int settingHeight = 20;
inline constexpr int settingCount = 8;
struct PianoKey {
    int pitch, offset, x, width;
    bool black;
    bool inRange() const { return offset >= 0 && offset <= 12; }
};
inline constexpr int keyboardY = 43, keyboardHeight = 70, blackKeyHeight = 42;
inline constexpr int keyboardLeft = 8, keyboardRight = 312;
inline constexpr int tonicCenter = 27, octaveCenter = 293;
struct PianoLayout {
    std::array<PianoKey, 18> keys{};
    size_t count = 0;
    const PianoKey* begin() const { return keys.data(); }
    const PianoKey* end() const { return keys.data() + count; }
};
inline PianoLayout pianoLayout(uint8_t tonic) {
    // Scroll a real piano beneath a fixed tonic anchor. The muted edge pieces
    // preserve black/white geometry without repeating degree 7 before 1.
    static constexpr bool black[] = {false,true,false,true,false,false,true,false,true,false,true,false};
    static constexpr int whiteIndex[] = {0,0,1,1,2,3,3,4,4,5,5,6};
    const auto position = [&](int midi) {
        return (midi / 12 * 7 + whiteIndex[midi % 12]) * 38 + (black[midi % 12] ? 19 : 0);
    };
    const int base = 60 + tonic % 12;
    const int shift = tonicCenter - position(base);
    PianoLayout result;
    for (bool blackPass : {false,true}) {
        for (int midi = base - 2; midi <= base + 14; ++midi) {
            const int pitch = midi % 12;
            if (black[pitch] != blackPass) continue;
            const int width = blackPass ? 24 : 38;
            const int x = position(midi) + shift - width / 2;
            const int left = std::max(keyboardLeft, x), right = std::min(keyboardRight, x + width);
            if (right <= left) continue;
            result.keys[result.count++] = {pitch, midi - base, left, right - left, blackPass};
        }
    }
    return result;
}
inline int tabAt(int x, int y) {
    return x >= 0 && x < 320 && y >= footerY && y < 240 ? (x < 107 ? 0 : x < 214 ? 1 : 2) : -1;
}
inline int settingAt(int x, int y) {
    return x >= 0 && x < 320 && y >= settingY && y < settingY + settingCount * settingHeight ? (y - settingY) / settingHeight : -1;
}
inline int tonicAt(int x, int y, uint8_t tonic) {
    if (y < keyboardY || y >= keyboardY + keyboardHeight) return -1;
    const auto keyboard = pianoLayout(tonic);
    for (int i = int(keyboard.count) - 1; i >= 0; --i) {
        const auto& key = keyboard.keys[i];
        if (x >= key.x && x < key.x + key.width && (!key.black || y < keyboardY + blackKeyHeight))
            return key.inRange() ? key.pitch : -1;
    }
    return -1;
}
inline int scaleAt(int x, int y) {
    if (x < 8 || x >= 312 || y < 136 || y >= 196) return -1;
    const int column = (x - 8) / 77, row = (y - 136) / 32;
    return column < 4 && row < 2 && (x - 8) % 77 < 73 && (y - 136) % 32 < 28 ? row * 4 + column : -1;
}
inline bool keyButtonAt(Mode mode, int x, int y) {
    return mode != Mode::Notation && x >= 0 && x < 320 && y >= 0 && y < (mode == Mode::Harmony ? 44 : 32);
}
} // namespace chord::ui
