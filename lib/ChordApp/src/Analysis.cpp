#include "Analysis.h"
#include <algorithm>

namespace chord {
namespace {
void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    size_t offset = 0;
    while ((offset = text.find(from, offset)) != std::string::npos) {
        text.replace(offset, from.size(), to);
        offset += to.size();
    }
}
std::string jazzQuality(std::string quality) {
    replaceAll(quality, "m11b5", "ø11");
    replaceAll(quality, "m9b5", "ø9");
    replaceAll(quality, "m7b5", "ø7");
    if (quality.rfind("m(maj", 0) == 0) {
        quality = "-" + quality.substr(2);
        const auto closing = quality.find(')');
        if (closing != std::string::npos) quality.erase(closing, 1);
    }
    replaceAll(quality, "maj", "Δ");
    replaceAll(quality, "dim", "°");
    replaceAll(quality, "aug", "+");
    if (!quality.empty() && quality.front() == 'm') quality.front() = '-';
    return quality;
}
template<class Candidate>
std::string namedChord(const Candidate& candidate, int bassPitch, const Settings& settings, bool inferred) {
    auto name = asciiMusic(candidate.chordName);
    const auto root = asciiMusic(candidate.rootName);
    name.replace(0, root.size(), pitchName(candidate.root, settings.flats));
    // A slash inside 6/9 belongs to the quality. Only replace a final note.
    if (bassPitch >= 0 && bassPitch != candidate.root) {
        const auto slash = name.find_last_of('/');
        if (slash != std::string::npos && slash + 1 < name.size() &&
            name[slash + 1] >= 'A' && name[slash + 1] <= 'G') {
            name.replace(slash + 1, std::string::npos, pitchName(bassPitch, settings.flats));
        }
    }
    if (settings.notation == ChordNotation::Jazz) {
        const auto rootLength = pitchName(candidate.root, settings.flats).size();
        std::string bass;
        const auto slash = name.find_last_of('/');
        if (slash != std::string::npos && slash + 1 < name.size() && name[slash + 1] >= 'A' && name[slash + 1] <= 'G') {
            bass = name.substr(slash);
            name.erase(slash);
        }
        name = name.substr(0, rootLength) + jazzQuality(name.substr(rootLength)) + bass;
    }
    if (inferred) name += "?";
    return name;
}
}

std::string asciiMusic(std::string text) {
    for (const auto& pair : {std::pair<const char*, const char*>{"♯", "#"}, {"♭", "b"}, {"°", "dim"}}) {
        size_t pos = 0;
        while ((pos = text.find(pair.first, pos)) != std::string::npos) {
            text.replace(pos, std::char_traits<char>::length(pair.first), pair.second);
            pos += std::char_traits<char>::length(pair.second);
        }
    }
    return text;
}

std::string pitchName(int pc, bool flats) {
    static const char* sharp[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* flat[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    pc = (pc % 12 + 12) % 12;
    return flats ? flat[pc] : sharp[pc];
}

std::string noteName(int midi, bool flats) {
    return pitchName(midi % 12, flats) + std::to_string(midi / 12 - 1);
}

std::string chordName(const Snapshot& snapshot, const Settings& settings) {
    if (!snapshot.chord) {
        if (snapshot.notes.size() == 1) return noteName(snapshot.notes.front(), settings.flats);
        return "--";
    }
    return namedChord(*snapshot.chord, snapshot.notes.empty() ? -1 : snapshot.notes.front() % 12, settings,
                      snapshot.chord->voicingType == ChordDetection::VoicingType::Rootless);
}

std::string alternativeName(const ChordDetection::ChordAlternative& alternative, const Settings& settings) {
    return namedChord(alternative, alternative.bass, settings, alternative.rootless);
}

std::string alternativeNames(const Snapshot& snapshot, const Settings& settings, size_t limit) {
    if (!snapshot.chord || !limit) return "";
    std::vector<const ChordDetection::ChordAlternative*> readings;
    const auto& primary = *snapshot.chord;
    const bool inferred = primary.voicingType == ChordDetection::VoicingType::Rootless;
    for (const auto& alternative : primary.alternatives) {
        const int degree = (alternative.root - settings.tonic + 12) % 12;
        // Show close literal alternatives and readings related to the chosen
        // tonic/V. Other supported roots remain available in diagnostics.
        if (inferred || alternative.score + 40 >= primary.score || degree == 0 || degree == 7)
            readings.push_back(&alternative);
    }
    auto priority = [&](const ChordDetection::ChordAlternative* reading) {
        const int degree = (reading->root - settings.tonic + 12) % 12;
        return degree == 0 ? 2 : degree == 7 ? 1 : 0;
    };
    std::stable_sort(readings.begin(), readings.end(), [&](const auto* a, const auto* b) {
        if (priority(a) != priority(b)) return priority(a) > priority(b);
        return a->score > b->score;
    });
    std::string result;
    for (size_t i = 0; i < std::min(limit, readings.size()); ++i) {
        if (!result.empty()) result += "  |  ";
        result += alternativeName(*readings[i], settings);
    }
    if (readings.size() > limit) result += "  +" + std::to_string(readings.size() - limit);
    return result;
}

std::string romanName(std::string roman, const Settings& settings) {
    if (settings.notation == ChordNotation::Jazz) {
        replaceAll(roman, "7b5", "ø7");
        replaceAll(roman, "dim", "°");
        replaceAll(roman, "maj", "Δ");
    }
    return roman;
}

std::string degreeName(int interval, const ChordDetection::ChordCandidate* candidate) {
    static const char* names[] = {"R", "b9", "9", "b3", "3", "11", "b5", "5", "b13", "13", "b7", "7"};
    const int pc = (interval % 12 + 12) % 12;
    if (candidate) {
        const auto& type = candidate->chordType;
        if (pc == 2 && type == "sus2") return "2";
        if (pc == 3 && (type.find("#9") != std::string::npos ||
            std::find(candidate->pattern.begin(), candidate->pattern.end(), 4) != candidate->pattern.end())) return "#9";
        if (pc == 5 && type.find("sus4") != std::string::npos) return "4";
        if (pc == 6 && type.find("#11") != std::string::npos) return "#11";
        if (pc == 8 && (type.find("#5") != std::string::npos || type.find("augmented") != std::string::npos)) return "#5";
        if (pc == 9 && type == "diminished7") return "bb7";
        if (pc == 9 && (type == "major6" || type == "minor6" || type.find("6/9") != std::string::npos)) return "6";
    }
    return names[pc];
}

std::string voicingName(const Snapshot& snapshot) {
    if (snapshot.notes.size() < 2) return "--";
    const auto type = snapshot.chord ? snapshot.chord->voicingType :
        ChordDetection::VoicingAnalyzer::classifyVoicing(snapshot.notes);
    using V = ChordDetection::VoicingType;
    switch (type) {
        case V::Close: return "Close";
        case V::Open: return "Open";
        case V::Drop2: return "Drop 2*";
        case V::Drop3: return "Drop 3*";
        case V::Rootless: return "Rootless*";
        default: return "--";
    }
}

SpelledNote spellNote(int midi, const ChordDetection::ChordCandidate* candidate, bool flats) {
    static const int sharpLetter[] = {0,0,1,1,2,3,3,4,4,5,5,6};
    static const int flatLetter[] = {0,1,1,2,2,3,4,4,5,5,6,6};
    static const int natural[] = {0,2,4,5,7,9,11};
    static const char letters[] = "CDEFGAB";
    int letter = (flats ? flatLetter : sharpLetter)[midi % 12];
    if (candidate) {
        const int rootLetter = (flats ? flatLetter : sharpLetter)[candidate->root];
        const int interval = (midi % 12 - candidate->root + 12) % 12;
        static const int offsets[] = {0,1,1,2,2,3,4,4,5,5,6,6};
        int offset = offsets[interval];
        const auto degree = degreeName(interval, candidate);
        if (degree == "#9") offset = 1;
        if (degree == "#11") offset = 3;
        if (degree == "#5") offset = 4;
        if (degree == "bb7") offset = 6;
        letter = (rootLetter + offset) % 7;
    }
    int accidental = (midi % 12 - natural[letter] + 18) % 12 - 6;
    // Very exotic enharmonic roots can require triple accidentals. Prefer a
    // readable chromatic spelling in that case.
    if (accidental < -2 || accidental > 2) {
        letter = (flats ? flatLetter : sharpLetter)[midi % 12];
        accidental = (midi % 12 - natural[letter] + 18) % 12 - 6;
    }
    const int octave = (midi - accidental - natural[letter]) / 12 - 1;
    return {(octave * 7) + letter, accidental, letters[letter], octave};
}

Snapshot captureNotes(const MidiState& state) {
    Snapshot result;
    result.notes = state.notes();
    for (int note : result.notes) result.pressed.push_back(state.pressed(note));
    result.pedal = state.pedalDown();
    return result;
}

Snapshot analyze(const MidiState& state, ChordDetection::ChordDetector& detector) {
    auto result = captureNotes(state);
    result.chord = detector.detectChord(result.notes);
    return result;
}

std::string spelledName(int midi, const ChordDetection::ChordCandidate* candidate, bool flats, bool octave) {
    const auto note = spellNote(midi, candidate, flats);
    std::string name(1, note.letter);
    if (note.accidental < 0) name.append(-note.accidental, 'b');
    if (note.accidental > 0) name.append(note.accidental, '#');
    if (octave) name += std::to_string(note.octave);
    return name;
}

void Settler::changed(uint32_t now) {
    if (!pending_) { pending_ = true; first_ = now; }
    last_ = now;
}
bool Settler::due(uint32_t now) const {
    return pending_ && (uint32_t(now - last_) >= 35 || uint32_t(now - first_) >= 85);
}

} // namespace chord
