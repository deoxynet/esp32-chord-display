#include "Harmony.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {
int checks = 0;
void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
uint16_t mask(const std::vector<int>& values) {
    uint16_t result = 0;
    for (int value : values) result |= 1U << ((value % 12 + 12) % 12);
    return result;
}
chord::Snapshot snapshot(const std::vector<int>& notes, ChordDetection::ChordDetector& detector) {
    chord::Snapshot result;
    result.notes = notes;
    std::sort(result.notes.begin(), result.notes.end());
    result.chord = detector.detectChord(notes);
    return result;
}
bool reading(const chord::Snapshot& frame, int root, const std::vector<std::string>& types) {
    if (!frame.chord) return false;
    auto matches = [&](int r, const std::string& type) {
        return r == root && std::find(types.begin(), types.end(), type) != types.end();
    };
    if (matches(frame.chord->root, frame.chord->chordType)) return true;
    for (const auto& other : frame.chord->alternatives) if (matches(other.root, other.chordType)) return true;
    return false;
}
void rootlessAndOmittedToneShapes() {
    ChordDetection::ChordDetector detector;
    struct Case { std::vector<int> notes; std::vector<std::string> types; };
    const Case cases[] = {
        {{52,59,62}, {"major9"}}, {{52,58,62}, {"dominant9"}},
        {{52,58,63}, {"dominant7#9", "altered"}},
        {{58,64,68}, {"dominant7b13", "dominant7#5", "augmented7", "altered"}},
        {{59,64,69}, {"major13"}}, {{52,57,59,62}, {"major13"}}, {{52,57,62}, {"6/9"}},
        {{51,57,62}, {"minor6/9"}},
        {{48,52,57}, {"major6"}}, {{48,51,57}, {"minor6"}},
        {{52,58,61}, {"dominant7b9", "altered"}},
        {{58,64,66}, {"dominant7b5", "dominant7#11"}},
        {{52,58,63,68}, {"dominant7#5#9", "dominant7#9b13", "altered"}},
        {{53,58,63,67,72}, {"minor11"}},
    };
    for (int transpose = 0; transpose < 12; ++transpose) for (const auto& c : cases) {
        auto notes = c.notes;
        for (int& note : notes) note += transpose;
        auto frame = snapshot(notes, detector);
        check(reading(frame, transpose, c.types), "supported chord reading available at transposition " + std::to_string(transpose));
        // Spacing and doubling cannot make a supported root unreachable.
        notes.push_back(notes.front()+12);
        for (size_t i = 1; i < notes.size(); i += 2) notes[i] += 12;
        frame = snapshot(notes, detector);
        check(reading(frame, transpose, c.types), "supported chord reading survives spacing and octave doubling");
    }
}
void literalAndInferred() {
    ChordDetection::ChordDetector detector;
    chord::Settings s; s.tonic = 0; s.scale = chord::ScaleMode::Ionian;
    s.notation = chord::ChordNotation::Jazz;
    auto frame = snapshot({52,59,62}, detector);
    check(frame.chord && frame.chord->root == 0 && frame.chord->voicingType == ChordDetection::VoicingType::Rootless,
          "E-B-D reaches Cmaj9 without a sounding C");
    check(chord::chordName(frame,s) == "CΔ9/E?", "the main name qualifies an inferred root");
    auto harmony = chord::analyzeHarmony(frame,s);
    check(harmony.roman == "Imaj7?" && harmony.uncertain && !harmony.tones[0].played,
          "an inferred tonic is qualified and never highlighted as played");
    check(frame.chord->confidence <= 0.65f, "rootless confidence does not imply a strong unique identification");
    frame = snapshot({48,52,57},detector);
    check(frame.chord->root == 9 && frame.chord->chordType == "minor", "C-E-A retains its complete Am/C reading");
    check(chord::alternativeNames(frame,s).find("C6") != std::string::npos, "the sixth-shell interpretation is visible");
    frame = snapshot({52,55,59},detector);
    check(frame.chord->root == 4 && frame.chord->chordType == "minor", "an ordinary E minor triad stays literal");
    check(chord::alternativeNames(frame,s).find("CΔ7/E?") != std::string::npos, "rootless Cmaj7 remains a qualified alternative in C");
    frame = snapshot({60,64,67},detector);
    check(chord::alternativeNames(frame,s).empty(), "a clear C triad does not acquire unrelated small-print speculation");
    frame = snapshot({52,58,62},detector);
    check(chord::chordName(frame,s) == "C9/E?", "a rootless ninth is not penalized for omitting an optional fifth");
    check(!chord::alternativeNames(frame,s).empty(), "the rootless ninth retains its tritone alternative");
    s.flats = true; s.notation = chord::ChordNotation::Standard;
    frame = snapshot({58,64,66},detector);
    check(chord::chordName(frame,s) == "Gb7/Bb", "literal root and actual bass honor flat spelling");
    check(chord::alternativeNames(frame,s).find("C7#11/Bb?") != std::string::npos,
          "the C7#11/b5 alternative preserves the actual Bb bass and inferred-root marker");
    frame = snapshot({52,57,62},detector);
    check(chord::alternativeNames(frame,s).find("C6/9/E?") != std::string::npos, "6/9 is not mistaken for a slash bass");
    const auto retained = frame.chord;
    const auto name = retained->alternatives.back().chordName;
    (void)detector.detectChord({60,64,67});
    check(retained->alternatives.back().chordName == name, "later analysis cannot mutate a retained snapshot");
}
void insufficientEvidence() {
    ChordDetection::ChordDetector detector;
    auto frame = snapshot({48,51,58},detector);
    check(reading(frame,0,{"minor7"}) && !reading(frame,0,{"minor7b5","half-diminished7"}),
          "omitting the fifth does not establish a half-diminished quality");
    frame = snapshot({36,45,51},detector);
    check(reading(frame,0,{"minor6"}) && !reading(frame,0,{"diminished7"}),
          "C-A-Eb supports Cm6, but cannot establish its missing altered fifth as Cdim7");
    frame = snapshot({48,57,60,64},detector);
    check(!reading(frame,0,{"major7"}) && reading(frame,0,{"major6"}),
          "a printed maj7 over a sixth shell never invents its unplayed major seventh");
    frame = snapshot({48,54,59},detector);
    check(!reading(frame,0,{"major7#11"}), "a root and #11/7 without the third do not determine maj7#11");
    for (const auto& notes : {std::vector<int>{52,58}, {48,59}, {60,61,62}, {60,61,63},
                             {60,61,62,63,64,65,66,67,68,69,70,71}})
        check(!detector.detectChord(notes), "underdefined dyads and unexplained clusters stay unlabeled");
}
void exhaustiveEvidence() {
    ChordDetection::ChordDetector detector;
    for (int pitches = 0; pitches < 4096; ++pitches) {
        std::vector<int> notes;
        for (int pitch = 0; pitch < 12; ++pitch) if (pitches & (1 << pitch)) notes.push_back(60+pitch);
        const auto result = detector.detectChord(notes);
        uint16_t expectedRoots = 0, emittedRoots = 0;
        if (notes.size() >= 2) for (int root = 0; root < 12; ++root) {
            const bool present = pitches & (1 << root);
            if (!present && notes.size() < 3) continue;
            std::vector<int> intervals;
            for (int note : notes) intervals.push_back((note-root+12)%12);
            for (const auto& entry : detector.getChordPatterns()) {
                const auto score = ChordDetection::ChordScoring::computeScore(intervals,entry.second,
                    notes.front()%12,root,present ? ChordDetection::VoicingType::Close : ChordDetection::VoicingType::Rootless);
                if (score > ChordDetection::kMinimumScoreThreshold) { expectedRoots |= 1U << root; break; }
            }
        }
        if (result) {
            emittedRoots |= 1U << result->root;
            for (const auto& other : result->alternatives) emittedRoots |= 1U << other.root;
        }
        check(emittedRoots == expectedRoots, "every eligible root is reachable and retained, including three-note hypotheses");
        if (!result) continue;
        auto verify = [&](int root, const std::string& type, bool inferred) {
            const auto& pattern = detector.getChordPatterns().at(type);
            std::vector<int> intervals;
            for (int note : notes) intervals.push_back((note-root+12)%12);
            const auto played = mask(intervals), tones = mask(pattern.intervals);
            auto required = mask(pattern.required);
            if (inferred) required &= ~uint16_t(1);
            check(!(played & ~uint16_t(tones | mask(pattern.optional))), "every reading explains every sounding class");
            check((played & required) == required, "every reading has all mandatory evidence");
            check(pattern.requiredAny.empty() || (played & mask(pattern.requiredAny)), "every altered reading has an alteration");
            check(inferred == !(played & 1), "rootless metadata matches actual notes");
            if (inferred) {
                const bool third = played & tones & ((1 << 3) | (1 << 4));
                const bool seventh = played & tones & ((1 << 10) | (1 << 11));
                const bool sixNine = !(tones & ((1 << 10) | (1 << 11))) && (played & tones & (1 << 9)) && (played & tones & (1 << 2));
                check(notes.size() >= 3 && third && (seventh || sixNine), "rootless readings have actual seventh or 6/9 guide-tone evidence");
            }
        };
        const bool inferred = result->voicingType == ChordDetection::VoicingType::Rootless;
        verify(result->root,result->chordType,inferred);
        check(result->alternatives.size() <= 11, "alternatives use a bounded set of distinct roots");
        uint16_t roots = 1U << result->root;
        for (const auto& other : result->alternatives) {
            verify(other.root,other.chordType,other.rootless);
            check(!(roots & (1U << other.root)), "display aliases do not crowd out distinct harmonic roots");
            roots |= 1U << other.root;
            check(!inferred || other.rootless, "an inferred root never displaces an available literal root");
            check(other.bass == notes.front()%12, "every alternative preserves the actual lowest MIDI pitch");
        }
    }
}
void inferredCadence() {
    ChordDetection::ChordDetector detector;
    chord::Settings s; s.tonic=0; s.scale=chord::ScaleMode::Ionian;
    chord::HarmonyTrail trail;
    uint32_t now=0;
    for (const auto& notes : {std::vector<int>{50,53,57,60}, {43,47,50,53}, {52,59,62}}) {
        const auto frame=snapshot(notes,detector);
        trail.observe(frame,s,now); trail.observe(frame,s,now+180); now+=300;
    }
    check(trail.text() == "ii7 > V7 > Imaj7?" && trail.motion() == "ii - V - I / possible",
          "an inferred tonic cannot produce an unqualified resolved-cadence claim");
}
}
int main() {
    rootlessAndOmittedToneShapes(); literalAndInferred(); insufficientEvidence(); exhaustiveEvidence(); inferredCadence();
    std::cout << checks << " candidate-evidence checks passed\n";
}
