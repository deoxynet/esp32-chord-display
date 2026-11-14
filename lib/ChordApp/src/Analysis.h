#pragma once

#include "ChordDetector.h"
#include "MidiState.h"
#include <string>

namespace chord {

// Keep the original values so existing saved display preferences still work.
enum class Mode : uint8_t { Chord = 0, Notation = 2, Harmony = 3 }; // 1 was the retired Analyze view
enum class BluetoothMode : uint8_t { Auto, Group, Direct };
enum class ScaleMode : uint8_t { Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian, MelodicMinor, Count };
enum class ChordNotation : uint8_t { Jazz, Standard };

struct Settings {
    bool flats = false;
    bool sustain = true;
    uint8_t channel = 0;
    uint8_t brightness = 180;
    Mode mode = Mode::Chord;
    BluetoothMode bluetooth = BluetoothMode::Group;
    uint8_t tonic = 2;
    ScaleMode scale = ScaleMode::Dorian;
    ChordNotation notation = ChordNotation::Standard;
};

struct Snapshot {
    std::shared_ptr<ChordDetection::ChordCandidate> chord;
    std::vector<int> notes;
    std::vector<bool> pressed;
    bool pedal = false;
};

struct SpelledNote {
    int diatonic = 0; // C0=0, D0=1 ...
    int accidental = 0; // -2..2
    char letter = 'C';
    int octave = 4;
};

std::string asciiMusic(std::string text);
std::string pitchName(int pitchClass, bool flats = false);
std::string noteName(int midi, bool flats = false);
std::string chordName(const Snapshot& snapshot, const Settings& settings);
std::string alternativeName(const ChordDetection::ChordAlternative& alternative, const Settings& settings);
std::string alternativeNames(const Snapshot& snapshot, const Settings& settings, size_t limit = 2);
std::string romanName(std::string roman, const Settings& settings);
std::string degreeName(int interval, const ChordDetection::ChordCandidate* chord);
std::string voicingName(const Snapshot& snapshot);
SpelledNote spellNote(int midi, const ChordDetection::ChordCandidate* chord, bool flats);
std::string spelledName(int midi, const ChordDetection::ChordCandidate* chord, bool flats, bool octave = true);
Snapshot analyze(const MidiState& state, ChordDetection::ChordDetector& detector);
Snapshot captureNotes(const MidiState& state); // immediate note feedback without chord settling/detection

// Burst settling has a fixed upper bound, even with continuously arriving notes.
class Settler {
public:
    void changed(uint32_t now);
    bool due(uint32_t now) const;
    void settled() { pending_ = false; }
private:
    bool pending_ = false;
    uint32_t first_ = 0;
    uint32_t last_ = 0;
};

} // namespace chord
