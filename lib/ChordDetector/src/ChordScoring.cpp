// Adapted from Long Kelvin's MIT-licensed pattern scoring for the ESP32.
// Fold extensions into pitch classes so octave doubling cannot change identity.
#include "ChordScoring.h"
#include <algorithm>

namespace ChordDetection {
namespace ChordScoring {
namespace {
uint16_t mask(const std::vector<int>& values) {
    uint16_t result = 0;
    for (int value : values) result |= uint16_t(1U << (value % 12));
    return result;
}
int count(uint16_t value) { return __builtin_popcount(static_cast<unsigned>(value)); }
}

float computeScore(const std::vector<int>& intervals, const ChordPattern& pattern,
                   int bass, int root, VoicingType voicing) {
    const auto played = mask(intervals);
    const auto tones = mask(pattern.intervals);
    auto required = mask(pattern.required);
    const auto optional = mask(pattern.optional);
    const auto important = mask(pattern.importantIntervals);
    const auto requiredAny = mask(pattern.requiredAny);
    const bool rootless = voicing == VoicingType::Rootless;
    if (rootless) {
        // Require actual guide tones: third/seventh, or third/sixth/ninth
        // for a rootless 6/9 shape. Two-note fragments remain ineligible in
        // candidate generation; no missing quality-defining tone is invented.
        const bool third = played & tones & ((1U << 3) | (1U << 4));
        const bool seventh = played & tones & ((1U << 10) | (1U << 11));
        const bool sixNine = !(tones & ((1U << 10) | (1U << 11))) &&
            (played & tones & (1U << 9)) && (played & tones & (1U << 2));
        if (!third || (!seventh && !sixNine)) return 0;
        required &= ~uint16_t(1);
    }
    if ((played & required) != required) return 0;
    if (requiredAny && !(played & requiredAny)) return 0;
    // A symbol must account for every sounding pitch class. A small score
    // penalty used to let even a chromatic cluster become an ordinary chord.
    if (played & ~uint16_t(tones | optional)) return 0;
    float score = static_cast<float>(pattern.baseScore);
    const auto expected = rootless ? uint16_t(tones & ~uint16_t(1)) : tones;
    // Omitting a perfect fifth or a lower tension is a complete jazz voicing
    // when that tone is explicitly optional in the pattern.
    if (rootless) score += 125;
    else if ((played & expected) == expected) score += 150;
    else if ((played & ~optional) == (expected & ~optional)) score += 125;
    // Normalize evidence by the pattern's size. Raw counts systematically
    // favored long extension names over an equally supported bass-rooted
    // chord (e.g. C7alt versus Gb13#11 on the same seven pitch classes).
    if (required) score += 90.0f * count(played & required) / count(required);
    if (important) score += 60.0f * count(played & important) / count(important);
    if (bass == root) score += 25;
    // Inferred roots already require all guide/named tones. Do not prefer an
    // altered fifth over a ninth just because its template omits the optional
    // perfect fifth; that favors complicated enharmonic spellings by accident.
    if (rootless) score += 80;
    else if (tones) score += 80.0f * count(played & tones) / count(tones);
    if (rootless) score -= 110;
    else if (voicing == VoicingType::Close) score += 5;
    if (!(played & 1) && !rootless) score -= 40;
    if (!(played & ((1U << 2) | (1U << 3) | (1U << 4) | (1U << 5)))) score -= 25;
    return score;
}

float computeConfidence(float best, float second, int noteCount, bool exact) noexcept {
    const float margin = std::clamp((best - second) / 100.0f, 0.0f, 1.0f);
    return std::clamp(0.35f * margin + 0.25f * std::min(best / 250.0f, 1.0f) +
        0.15f * std::min(noteCount / 6.0f, 1.0f) + 0.25f * (exact ? 1.0f : 0.5f), 0.0f, 1.0f);
}
} // namespace ChordScoring
} // namespace ChordDetection

