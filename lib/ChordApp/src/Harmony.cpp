#include "Harmony.h"
#include <algorithm>
#include <cctype>

namespace chord {
namespace {
constexpr ScaleDefinition scales[] = {
    {"Ionian", "Major", {0,2,4,5,7,9,11}, {"1","2","3","4","5","6","7"}, 6},
    {"Dorian", "Dorian", {0,2,3,5,7,9,10}, {"1","2","b3","4","5","6","b7"}, 5},
    {"Phrygian", "Phrygian", {0,1,3,5,7,8,10}, {"1","b2","b3","4","5","b6","b7"}, 1},
    {"Lydian", "Lydian", {0,2,4,6,7,9,11}, {"1","2","3","#4","5","6","7"}, 3},
    {"Mixolydian", "Mixolyd.", {0,2,4,5,7,9,10}, {"1","2","3","4","5","6","b7"}, 6},
    {"Aeolian", "Minor", {0,2,3,5,7,8,10}, {"1","2","b3","4","5","b6","b7"}, 5},
    {"Locrian", "Locrian", {0,1,3,5,6,8,10}, {"1","b2","b3","4","b5","b6","b7"}, 4},
    {"Melodic minor", "Jazz min.", {0,2,3,5,7,9,11}, {"1","2","b3","4","5","6","7"}, 6},
};
int pc(int value) { return (value % 12 + 12) % 12; }
uint16_t bit(int pitch) { return uint16_t(1U << pc(pitch)); }

uint16_t patternMask(const ChordDetection::ChordCandidate& candidate) {
    uint16_t result = 0;
    for (int interval : candidate.pattern) result |= bit(interval);
    return result;
}
bool dominant(const ChordDetection::ChordCandidate& candidate) {
    const auto mask = patternMask(candidate);
    return (mask & bit(10)) && ((mask & bit(4)) ||
        (candidate.chordType.find("sus4") != std::string::npos && (mask & bit(5))));
}
bool minorQuality(uint16_t mask) {
    return (mask & bit(3)) && !(mask & bit(4));
}
bool diminishedQuality(uint16_t mask) {
    return minorQuality(mask) && (mask & bit(6)) && !(mask & bit(7));
}
bool majorTonic(uint16_t mask) {
    return (mask & bit(4)) && (mask & bit(7)) && !(mask & bit(10));
}
bool minorTonic(uint16_t mask) {
    return minorQuality(mask) && (mask & bit(7));
}
uint16_t coreMask(const ChordDetection::ChordCandidate& candidate) {
    // Functional quality comes from the root, third, fifth and seventh (or
    // sixth). Tensions such as #11 must not turn Imaj7 into a borrowed chord.
    const auto mask = patternMask(candidate);
    uint16_t core = bit(0);
    if (mask & bit(4)) core |= bit(4);
    else if (mask & bit(3)) core |= bit(3);
    else if (mask & bit(5)) core |= bit(5);
    else if (mask & bit(2)) core |= bit(2);
    if (mask & bit(7)) core |= bit(7);
    else if (diminishedQuality(mask)) core |= bit(6);
    else if (candidate.chordType != "altered" && (mask & bit(8))) core |= bit(8);
    if (mask & bit(11)) core |= bit(11);
    else if (mask & bit(10)) core |= bit(10);
    else if (mask & bit(9)) core |= bit(9);
    return core;
}
bool coreInCollection(const ChordDetection::ChordCandidate& candidate, int tonic, const std::array<int, 7>& intervals) {
    uint16_t collection = 0;
    for (int interval : intervals) collection |= bit(tonic + interval - candidate.root);
    return !(coreMask(candidate) & ~collection);
}
bool coreInScale(const ChordDetection::ChordCandidate& candidate, const Settings& settings) {
    return coreInCollection(candidate, settings.tonic, scaleDefinition(settings.scale).intervals);
}
std::string degreeRoman(int interval) {
    static const char* names[] = {"I","bII","II","bIII","III","IV","#IV","V","bVI","VI","bVII","VII"};
    return names[pc(interval)];
}
std::string modeRoman(int interval, const Settings& settings) {
    const auto& scale = scaleDefinition(settings.scale);
    static const char* numerals[] = {"I","II","III","IV","V","VI","VII"};
    for (int i = 0; i < 7; ++i) if (scale.intervals[i] == pc(interval)) {
        const std::string degree = scale.degrees[i];
        return degree.substr(0, degree.size() - 1) + numerals[i];
    }
    return degreeRoman(interval);
}
std::string roman(const ChordDetection::ChordCandidate& candidate, const Settings& settings) {
    auto result = modeRoman(candidate.root - settings.tonic, settings);
    const auto mask = patternMask(candidate);
    const bool minor = (mask & bit(3)) && !(mask & bit(4));
    const bool diminished = minor && (mask & bit(6)) && !(mask & bit(7));
    if (minor) for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (diminished && (mask & bit(10))) return result + "7b5";
    if (diminished) return result + ((mask & bit(9)) ? "dim7" : "dim");
    if (candidate.chordType != "altered" && (mask & bit(4)) && (mask & bit(8)) && !(mask & bit(7))) result += "+";
    if (mask & bit(11)) result += minor ? "(maj7)" : "maj7";
    else if (mask & bit(10)) result += "7";
    else if (mask & bit(9)) result += "6";
    if (candidate.chordType.find("sus2") != std::string::npos) result += "sus2";
    if (candidate.chordType.find("sus4") != std::string::npos) result += "sus4";
    if (candidate.chordType == "power5") result += "5";
    return result;
}
int scaleDegree(int pitch, const Settings& settings) {
    const auto& intervals = scaleDefinition(settings.scale).intervals;
    const auto found = std::find(intervals.begin(), intervals.end(), pc(pitch - settings.tonic));
    return found == intervals.end() ? -1 : int(found - intervals.begin());
}
std::string targetRoman(int degree, const Settings& settings) {
    if (settings.scale == ScaleMode::Aeolian && degree == 4) return "V";
    const auto& intervals = scaleDefinition(settings.scale).intervals;
    const int third = pc(intervals[(degree + 2) % 7] - intervals[degree]);
    auto result = modeRoman(intervals[degree], settings);
    if (third == 3) for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
bool viableTarget(int degree, const Settings& settings) {
    if (degree < 0) return false;
    const auto& intervals = scaleDefinition(settings.scale).intervals;
    return pc(intervals[(degree + 4) % 7] - intervals[degree]) == 7;
}
std::string approachDiminishedRoman(int root, int targetDegree, const Settings& settings) {
    // Spell the leading root on the letter below its target: C#dim7 -> Dm
    // is #idim7, rather than an enharmonic biidim7 reading.
    static const int naturalDegrees[] = {0,2,4,5,7,9,11};
    static const char* numerals[] = {"i","ii","iii","iv","v","vi","vii"};
    const int degree = (targetDegree + 6) % 7;
    const int alteration = pc(root - settings.tonic - naturalDegrees[degree] + 6) - 6;
    return std::string(alteration < 0 ? -alteration : alteration, alteration < 0 ? 'b' : '#') + numerals[degree] + "dim7?";
}
bool stacked(const Snapshot& snapshot, int interval) {
    // Literal fourth/fifth stacks are deliberately not assigned a unique root.
    if (snapshot.notes.size() < 3) return false;
    for (size_t i = 1; i < snapshot.notes.size(); ++i)
        if (snapshot.notes[i] - snapshot.notes[i - 1] != interval) return false;
    return true;
}
std::string exercise(const Settings& settings) {
    const auto root = scaleNoteName(settings, 0);
    switch (settings.scale) {
        case ScaleMode::Ionian: return "TRY ii - V - I: " + scaleNoteName(settings, 1) + "m7 " + scaleNoteName(settings, 4) + "7 " + root + "maj7";
        case ScaleMode::Dorian: return "VAMP " + root + "m7 - " + scaleNoteName(settings, 3) + "/" + root;
        case ScaleMode::Phrygian: return "VAMP " + root + "m7 - " + scaleNoteName(settings, 1) + "/" + root;
        case ScaleMode::Lydian: return "VAMP " + root + "maj7 - " + scaleNoteName(settings, 1) + "/" + root;
        case ScaleMode::Mixolydian: return "VAMP " + root + "7 - " + scaleNoteName(settings, 6) + "/" + root;
        case ScaleMode::Aeolian: return "TRY ii7b5 - V - i: " + scaleNoteName(settings, 1) + "m7b5 " + scaleNoteName(settings, 4) + "7 " + root + "m(maj7)";
        case ScaleMode::Locrian: return "VAMP " + root + "m7b5 - " + scaleNoteName(settings, 1) + "/" + root;
        default: return "VAMP " + root + "m(maj7) - " + scaleNoteName(settings, 3) + "/" + root;
    }
}
}

const ScaleDefinition& scaleDefinition(ScaleMode mode) {
    const auto index = static_cast<unsigned>(mode);
    return scales[index < static_cast<unsigned>(ScaleMode::Count) ? index : 0];
}

std::string keyName(const Settings& settings) {
    return pitchName(settings.tonic, settings.flats) + " " + scaleDefinition(settings.scale).name;
}

std::string scaleNoteName(const Settings& settings, int degree) {
    const auto root = pitchName(settings.tonic, settings.flats);
    static const std::string letters = "CDEFGAB";
    static const int natural[] = {0,2,4,5,7,9,11};
    degree = (degree % 7 + 7) % 7;
    const int letter = (int(letters.find(root[0])) + degree) % 7;
    const int pitch = pc(settings.tonic + scaleDefinition(settings.scale).intervals[degree]);
    const int accidental = pc(pitch - natural[letter] + 6) - 6;
    std::string result(1, letters[letter]);
    if (accidental > 0) result.append(accidental, '#');
    if (accidental < 0) result.append(-accidental, 'b');
    return result;
}

ScaleReference analyzeReference(const Snapshot& snapshot, const Settings& settings) {
    ScaleReference result;
    const auto& scale = scaleDefinition(settings.scale);
    static const char* chromaticDegrees[] = {"1","b2","2","b3","3","4","#4","5","b6","6","b7","7"};
    static const int chromaticSteps[] = {0,1,1,2,2,3,3,4,5,5,6,6};
    static const std::string letters = "CDEFGAB";
    static const int natural[] = {0,2,4,5,7,9,11};
    const auto root = pitchName(settings.tonic, settings.flats);
    const int rootLetter = int(letters.find(root[0]));
    for (int pitch = 0; pitch < 12; ++pitch) {
        auto& note = result.notes[pitch];
        const int interval = pc(pitch - settings.tonic);
        int degree = -1;
        for (int i = 0; i < 7; ++i) if (scale.intervals[i] == interval) degree = i;
        note.inScale = degree >= 0;
        note.color = degree == scale.colorIndex;
        if (note.inScale) {
            note.name = scaleNoteName(settings, degree);
            note.degree = scale.degrees[degree];
            switch (degree) {
                case 0: note.role = "Tonic / home"; break;
                case 1: note.role = scale.intervals[1] == 1 ? "Minor second / b9" : "Second / 9th"; break;
                case 2: note.role = scale.intervals[2] == 3 ? "Minor third" : "Major third"; break;
                case 3: note.role = scale.intervals[3] == 6 ? "Raised fourth / #11" : "Fourth / 11th"; break;
                case 4: note.role = scale.intervals[4] == 6 ? "Diminished fifth" : "Fifth / support"; break;
                case 5: note.role = scale.intervals[5] == 8 ? "Minor sixth / b13" : "Sixth / 13th"; break;
                case 6: note.role = scale.intervals[6] == 11 ? "Leading tone" : "Minor seventh"; break;
            }
            if (note.color && settings.scale != ScaleMode::Ionian)
                note.role = std::string(settings.scale == ScaleMode::MelodicMinor ? "Jazz minor" : scale.name) + " color";
        } else {
            // Keep chromatic degrees and their note names enharmonically
            // consistent: e.g. C# is 7 in D Dorian, not Db labelled as 7.
            const bool flatFifth = interval == 6 && scale.intervals[2] == 3;
            note.degree = flatFifth ? "b5" : chromaticDegrees[interval];
            const int letter = (rootLetter + (flatFifth ? 4 : chromaticSteps[interval])) % 7;
            const int accidental = pc(pitch - natural[letter] + 6) - 6;
            note.name = std::string(1, letters[letter]);
            if (accidental > 0) note.name.append(accidental, '#');
            if (accidental < 0) note.name.append(-accidental, 'b');
            note.role = "Outside " + std::string(scale.name);
        }
    }
    uint16_t seen = 0;
    for (size_t i = 0; i < snapshot.notes.size(); ++i) {
        const int midi = snapshot.notes[i];
        if (midi < 0 || midi > 127) continue;
        const int pitch = midi % 12;
        auto& note = result.notes[pitch];
        if (i >= snapshot.pressed.size() || snapshot.pressed[i]) note.held = true;
        else note.sustained = true;
        if (!(seen & bit(pitch))) { result.active.push_back(pitch); seen |= bit(pitch); }
    }
    if (result.active.size() == 1) {
        const auto& note = result.notes[result.active[0]];
        result.readout = note.name + " = " + note.degree + " / " + note.role;
        if (!note.held) result.readout += " / pedal";
    } else {
        for (size_t i = 0; i < std::min(result.active.size(), size_t(6)); ++i) {
            if (!result.readout.empty()) result.readout += "  ";
            const auto& note = result.notes[result.active[i]];
            result.readout += note.name + ":" + note.degree;
        }
        if (result.active.size() > 6) result.readout += "  +" + std::to_string(result.active.size() - 6);
    }
    return result;
}

HarmonyResult analyzeHarmony(const Snapshot& snapshot, const Settings& settings) {
    HarmonyResult result;
    const auto& scale = scaleDefinition(settings.scale);
    uint16_t scaleMask = 0;
    for (int note : snapshot.notes) result.playedMask |= bit(note);
    for (int i = 0; i < 7; ++i) {
        const int pitch = pc(settings.tonic + scale.intervals[i]);
        scaleMask |= bit(pitch);
        result.tones[i] = {pitch, scaleNoteName(settings, i), scale.degrees[i],
                           bool(result.playedMask & bit(pitch)), i == scale.colorIndex};
    }
    result.outsideMask = uint16_t(result.playedMask & ~scaleMask);
    result.inScale = result.outsideMask == 0;
    for (int i = 0; i < 12; ++i) if (result.outsideMask & bit(i)) {
        if (!result.outsideNotes.empty()) result.outsideNotes += " ";
        result.outsideNotes += snapshot.chord ? spelledName(60 + i, snapshot.chord.get(), settings.flats, false) :
            pitchName(i, settings.flats);
    }
    const auto& color = result.tones[scale.colorIndex];
    result.colorPrompt = "COLOR " + color.degree + " = " + color.note + (color.played ? " / heard" : " / try it");
    result.exercise = exercise(settings);
    result.quartal = stacked(snapshot, 5) || (snapshot.chord &&
        snapshot.chord->chordType.find("quartal") == 0);
    result.quintal = stacked(snapshot, 7);
    if (result.quartal || result.quintal) {
        const std::string name = result.quintal ? "Quintal" : "Quartal";
        result.uncertain = true;
        result.roman = result.quintal ? "5ths" : "4ths";
        result.role = name + (result.inScale ? " color / root is ambiguous" : " color / outside tones");
        result.shortRole = result.quintal ? "QUINTAL" : "QUARTAL";
        result.detail = "root ambiguous";
        return result;
    }
    if (!snapshot.chord) return result;
    result.hasChord = true;
    const auto& candidate = *snapshot.chord;
    result.roman = roman(candidate, settings);
    if (candidate.voicingType == ChordDetection::VoicingType::Rootless) {
        result.uncertain = true;
        result.roman += "?";
    }
    const int interval = pc(candidate.root - settings.tonic);
    const auto quality = patternMask(candidate);
    const bool minorKey = settings.scale == ScaleMode::Aeolian;
    const bool tonal = settings.scale == ScaleMode::Ionian || minorKey;
    if (tonal && interval == 0 && (minorKey ? minorTonic(quality) : majorTonic(quality))) {
        result.role = "Tonic / home";
        result.shortRole = "TONIC";
        result.detail = keyName(settings);
        return result;
    }
    // Major and composite minor both use a major-quality V, with or without
    // altered tensions. The selected seven-note reference remains literal.
    if (tonal && interval == 7 && (dominant(candidate) ||
        ((quality & bit(4)) && (quality & (bit(6) | bit(7) | bit(8))) && !(quality & bit(11))))) {
        result.role = "Dominant / pull toward " + std::string(minorKey ? "i" : "I");
        result.shortRole = "DOMINANT";
        result.detail = minorKey ? "toward i" : "toward I";
        return result;
    }
    if (tonal && interval == 11 && diminishedQuality(quality)) {
        result.role = "Dominant / leading tone to " + std::string(minorKey ? "i" : "I");
        result.shortRole = "DOMINANT";
        result.detail = minorKey ? "toward i" : "toward I";
        return result;
    }
    constexpr std::array<int, 7> harmonicMinor = {0,2,3,5,7,8,11};
    const bool compositeMinor = minorKey &&
        (coreInCollection(candidate, settings.tonic, harmonicMinor) ||
         coreInCollection(candidate, settings.tonic, scaleDefinition(ScaleMode::MelodicMinor).intervals));
    if (coreInScale(candidate, settings) || compositeMinor) {
        if (!tonal && interval == 0) {
            result.role = "Modal center / home";
            result.shortRole = "TONIC";
            result.detail = keyName(settings);
        }
        else if (tonal) {
            if ((interval == 2 && minorQuality(quality)) ||
                (interval == 5 && (minorQuality(quality) || (quality & bit(4)))) ||
                (minorKey && interval == 8 && (quality & bit(4))) ||
                (minorKey && interval == 9 && diminishedQuality(quality))) {
                result.role = "Predominant / leads toward V";
                result.shortRole = "PRE-DOMINANT";
                result.detail = "toward V";
            } else if ((!minorKey && (interval == 4 || interval == 9) && minorQuality(quality)) ||
                       (minorKey && interval == 3 && (quality & bit(4)))) {
                result.role = "Tonic family / gentle contrast";
                result.shortRole = "TONIC FAMILY";
                result.detail = "gentle contrast";
            } else if (minorKey && interval == 10 && dominant(candidate)) {
                result.role = "Subdominant minor / bVII7 color";
                result.shortRole = "SUBDOM MINOR";
                result.detail = "bVII7 color";
            } else {
                result.role = "Diatonic color / function needs context";
                result.shortRole = "IN THE KEY";
                result.detail = keyName(settings);
            }
        } else {
            result.role = color.played ? "Diatonic color / " + color.degree + " is present" : "Diatonic neighbor / keep tonic home";
            result.shortRole = color.played ? "MODAL COLOR" : "IN THE MODE";
            result.detail = color.played ? color.degree + " = " + color.note : keyName(settings);
        }
        return result;
    }
    // Chromatic chords can have several explanations. A question mark marks
    // these as possible readings, never a claimed intention from one voicing.
    result.uncertain = true;
    if (dominant(candidate)) {
        const int target = pc(candidate.root + 5);
        const int degree = scaleDegree(target, settings);
        if (viableTarget(degree, settings)) {
            result.roman = "V7" + (degree == 0 ? std::string() : "/" + targetRoman(degree, settings)) + "?";
            result.role = degree == 0 ? "Tonal pull? / dominant to home" : "Applied dominant? / to " + scaleNoteName(settings, degree);
            result.shortRole = degree == 0 ? "DOMINANT?" : "APPLIED DOM?";
            result.detail = "to " + scaleNoteName(settings, degree);
            result.exercise = "TRY resolving to " + scaleNoteName(settings, degree) + " / listen to the pull";
            return result;
        }
        if (interval == 10 && settings.scale == ScaleMode::Ionian) {
            result.role = "Backdoor color? / bVII7 to I";
            result.shortRole = "BACKDOOR?";
            result.detail = "to " + scaleNoteName(settings, 0);
            return result;
        }
        // A substitute dominant sits a semitone above its target, including
        // secondary targets (e.g. Ab7 = subV7/V in C).
        // A sus chord lacks the guide-tone tritone needed for this reading.
        const int subDegree = scaleDegree(pc(candidate.root - 1), settings);
        if ((quality & bit(4)) && viableTarget(subDegree, settings)) {
            result.roman = "subV7/" + targetRoman(subDegree, settings) + "?";
            result.role = subDegree == 0 ? "Tritone substitute? / to home" :
                "Tritone substitute? / to " + scaleNoteName(settings, subDegree);
            result.shortRole = "TRITONE SUB?";
            result.detail = "to " + scaleNoteName(settings, subDegree);
            result.exercise = "TRY bass down a half step to " + scaleNoteName(settings, subDegree);
            return result;
        }
    }
    if (diminishedQuality(quality) && (quality & bit(9))) {
        const int degree = scaleDegree(pc(candidate.root + 1), settings);
        if (viableTarget(degree, settings)) {
            result.roman = approachDiminishedRoman(candidate.root, degree, settings);
            result.role = "Approach diminished? / to " + scaleNoteName(settings, degree);
            result.shortRole = "DIM APPROACH?";
            result.detail = "to " + scaleNoteName(settings, degree);
            return result;
        }
    }
    // Prefer a source collection containing the whole voicing, then consider
    // structural borrowing with chromatic tensions. Include parallel major.
    uint16_t played = 0;
    for (int note : snapshot.notes) played |= bit(note - candidate.root);
    for (bool includeTensions : {true, false}) {
        for (auto mode : {ScaleMode::Ionian, ScaleMode::Aeolian, ScaleMode::Dorian, ScaleMode::Mixolydian,
                          ScaleMode::Phrygian, ScaleMode::Lydian, ScaleMode::MelodicMinor}) {
            if (mode == settings.scale) continue;
            uint16_t mask = 0;
            for (int value : scaleDefinition(mode).intervals) mask |= bit(settings.tonic + value - candidate.root);
            if (!(coreMask(candidate) & ~mask) && (!includeTensions || !(played & ~mask))) {
                result.role = "Borrowed? / parallel " + std::string(scaleDefinition(mode).name);
                result.shortRole = "BORROWED?";
                result.detail = "from " + std::string(scaleDefinition(mode).shortName);
                return result;
            }
        }
    }
    result.role = "Chromatic color / several readings";
    result.shortRole = "CHROMATIC";
    result.detail = "several readings";
    return result;
}

void HarmonyTrail::clear() {
    count_ = 0;
    pending_ = {};
}

bool HarmonyTrail::observe(const Snapshot& snapshot, const Settings& settings, uint32_t now) {
    bool changed = false;
    if (tonic_ != settings.tonic || scale_ != settings.scale) {
        changed = count_ != 0;
        clear();
        tonic_ = settings.tonic;
        scale_ = settings.scale;
    }
    if (!snapshot.chord || snapshot.notes.size() < 3 || stacked(snapshot, 5) || stacked(snapshot, 7) ||
        snapshot.chord->chordType.find("quartal") == 0) { pending_ = {}; return changed; }
    const auto& candidate = *snapshot.chord;
    if (count_ && steps_[count_ - 1].root == candidate.root && steps_[count_ - 1].type == candidate.chordType) {
        pending_ = {};
        return changed;
    }
    if (pending_.root != candidate.root || pending_.type != candidate.chordType) {
        pending_ = {candidate.root, candidate.chordType, "", patternMask(candidate), dominant(candidate),
                    candidate.voicingType == ChordDetection::VoicingType::Rootless};
        pendingAt_ = now;
        return changed;
    }
    if (uint32_t(now - pendingAt_) < 180) return changed;
    pending_.roman = analyzeHarmony(snapshot, settings).roman;
    if (count_ == steps_.size()) {
        for (size_t i = 1; i < count_; ++i) steps_[i - 1] = steps_[i];
        --count_;
    }
    steps_[count_++] = pending_;
    pending_ = {};
    return true;
}

std::string HarmonyTrail::text() const {
    std::string result;
    for (size_t i = 0; i < count_; ++i) {
        if (!result.empty()) result += " > ";
        result += steps_[i].roman;
    }
    return result;
}

std::string HarmonyTrail::motion() const {
    if (count_ < 3) return "";
    const auto& ii = steps_[count_ - 3];
    const auto& v = steps_[count_ - 2];
    const auto& i = steps_[count_ - 1];
    const bool minorHome = scaleDefinition(scale_).intervals[2] == 3;
    const bool halfDiminishedII = diminishedQuality(ii.quality);
    if (pc(ii.root - tonic_) != 2 || !minorQuality(ii.quality) || !(ii.quality & bit(10)) ||
        pc(v.root - tonic_) != 7 || !v.dominant ||
        i.root != tonic_ || !(minorHome ? minorTonic(i.quality) : majorTonic(i.quality))) return "";
    const std::string phrase = std::string(halfDiminishedII ? "ii7b5" : "ii") + " - V - " + (minorHome ? "i" : "I");
    if (ii.inferred || v.inferred || i.inferred) return phrase + " / possible";
    return phrase + (scale_ == ScaleMode::Ionian || scale_ == ScaleMode::Aeolian ? " / resolved" : " / tonal cadence");
}

} // namespace chord
