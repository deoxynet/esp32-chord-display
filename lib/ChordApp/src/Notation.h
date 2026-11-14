#pragma once

#include "Analysis.h"

namespace chord {

inline constexpr int staffSpacing = 10;
inline constexpr int trebleBottom = 100;
inline constexpr int bassBottom = 180;
inline constexpr int noteheadRadius = 6;
inline constexpr int noteheadShift = 12;

struct StaffNote {
    int midi = 60;
    int x = 186;
    int y = 110;
    int accidental = 0;
    int accidentalX = 151;
    int diatonic = 28;
    bool showAccidental = false;
    bool treble = true;
    bool pressed = true;
    std::vector<int> ledgerLines;
};

struct StaffStem {
    int x = 0, top = 0, bottom = 0;
    bool treble = true;
    bool pressed = true;
};

struct StaffLayout {
    std::vector<StaffNote> notes;
    std::vector<StaffStem> stems;
    int left = 44, right = 276;
    int outside = 0;
    int trebleOctaves = 0;
    int bassOctaves = 0;
};

StaffLayout layoutStaff(const Snapshot& snapshot, bool flats);

} // namespace chord
