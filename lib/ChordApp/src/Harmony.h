#pragma once

#include "Analysis.h"
#include <array>

namespace chord {

struct ScaleDefinition {
    const char* name;
    const char* shortName;
    std::array<int, 7> intervals;
    std::array<const char*, 7> degrees;
    int colorIndex;
};

const ScaleDefinition& scaleDefinition(ScaleMode mode);
std::string keyName(const Settings& settings);
std::string scaleNoteName(const Settings& settings, int degree);

struct HarmonyTone {
    int pitch = 0;
    std::string note;
    std::string degree;
    bool played = false;
    bool color = false;
};

struct HarmonyResult {
    bool hasChord = false;
    bool inScale = true;
    bool uncertain = false;
    bool quartal = false;
    bool quintal = false;
    uint16_t playedMask = 0;
    uint16_t outsideMask = 0;
    std::array<HarmonyTone, 7> tones;
    std::string roman = "--";
    std::string role;
    std::string shortRole, detail;
    std::string colorPrompt;
    std::string exercise;
    std::string outsideNotes;
};

HarmonyResult analyzeHarmony(const Snapshot& snapshot, const Settings& settings);

struct ReferenceNote {
    std::string name, degree, role;
    bool inScale = false, color = false, held = false, sustained = false;
    bool sounding() const { return held || sustained; }
};
struct ScaleReference {
    std::array<ReferenceNote, 12> notes;
    std::vector<int> active; // unique sounding pitch classes, in voicing order
    std::string readout;
};
ScaleReference analyzeReference(const Snapshot& snapshot, const Settings& settings);

// A chord must persist for 180 ms to enter the trail. Octave/inversion changes
// and silent key releases don't create extra entries or erase the phrase.
class HarmonyTrail {
public:
    bool observe(const Snapshot& snapshot, const Settings& settings, uint32_t now);
    void clear();
    std::string text() const;
    std::string motion() const;
    size_t size() const { return count_; }
private:
    struct Step { int root = -1; std::string type, roman; uint16_t quality = 0; bool dominant = false, inferred = false; };
    std::array<Step, 4> steps_;
    size_t count_ = 0;
    Step pending_;
    uint32_t pendingAt_ = 0;
    int tonic_ = -1;
    ScaleMode scale_ = ScaleMode::Dorian;
};

} // namespace chord
