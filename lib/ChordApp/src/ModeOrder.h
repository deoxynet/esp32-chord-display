#pragma once

#include "Analysis.h"
#include <array>

namespace chord {
// Presentation order is separate from the persisted ScaleMode IDs.
// Jazz minor and Mixolydian each lower one major-scale degree. The major-third
// mode comes first at that tie, followed by jazz minor's raised-7 Dorian color.
inline constexpr std::array<ScaleMode, static_cast<size_t>(ScaleMode::Count)> modesByBrightness = {
    ScaleMode::Lydian, ScaleMode::Ionian, ScaleMode::Mixolydian, ScaleMode::MelodicMinor,
    ScaleMode::Dorian, ScaleMode::Aeolian, ScaleMode::Phrygian, ScaleMode::Locrian,
};

constexpr int brightnessIndex(ScaleMode mode) {
    for (size_t i = 0; i < modesByBrightness.size(); ++i) if (modesByBrightness[i] == mode) return int(i);
    return 1; // Ionian fallback for an invalid value
}
} // namespace chord
