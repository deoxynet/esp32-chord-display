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
uint16_t mask(const std::vector<int>& intervals) {
    uint16_t result = 0;
    for (int interval : intervals) result |= 1U << ((interval % 12 + 12) % 12);
    return result;
}
chord::Snapshot voicing(int root, std::initializer_list<int> intervals) {
    static ChordDetection::ChordDetector detector;
    chord::Snapshot result;
    for (int interval : intervals) result.notes.push_back(root + interval);
    std::sort(result.notes.begin(), result.notes.end());
    result.chord = detector.detectChord(result.notes);
    return result;
}
chord::Settings key(int tonic, chord::ScaleMode mode = chord::ScaleMode::Ionian) {
    chord::Settings result;
    result.tonic = tonic;
    result.scale = mode;
    return result;
}

void jazzVoicings() {
    struct Case { std::initializer_list<int> intervals; const char* type; };
    const Case cases[] = {
        {{0,4,10}, "dominant7"}, {{0,4,11}, "major7"}, {{0,3,10}, "minor7"},
        {{0,3,10,17}, "minor11"}, {{0,3,7,10,17}, "minor11"},
        {{0,3,7,10,14,17}, "minor11"}, {{0,3,6,10,17}, "minor11b5"},
        {{0,3,6,10,14}, "minor9b5"}, {{0,5,7,10,13}, "7sus4b9"},
        {{0,4,10,17}, "dominant11"}, {{0,4,11,17}, "major11"},
        {{0,5,7,10,14}, "9sus4"}, {{0,5,10,14}, "9sus4"},
        {{0,5,7,10,14,21}, "13sus4"}, {{0,5,10,21}, "13sus4"},
        {{0,3,7,10,21}, "minor13"}, {{0,4,11,21}, "major13"},
        {{0,4,7,10,14,17,21}, "dominant13"},
        {{0,4,7,10,14,18,21}, "dominant13#11"},
        {{0,4,7,11,14,18,21}, "major13#11"},
        {{0,4,6,8,10,13,15}, "altered"}, {{0,3,7,17}, "minor-add11"},
    };
    for (int transpose = 0; transpose < 12; ++transpose) for (const auto& c : cases) {
        auto frame = voicing(48 + transpose, c.intervals);
        const auto label = std::string(c.type) + " transposition " + std::to_string(transpose);
        check(frame.chord && frame.chord->root == transpose && frame.chord->chordType == c.type,
              label + " got " + (frame.chord ? chord::asciiMusic(frame.chord->chordName) : "--"));
    }
    ChordDetection::ChordDetector detector;
    // Every full pattern in every key retains its played bass root. Display
    // aliases such as aug7/7#5 and the quartal/m11 reading are allowed.
    for (const auto& entry : detector.getChordPatterns()) for (int transpose = 0; transpose < 12; ++transpose) {
        std::vector<int> notes;
        for (int interval : entry.second.intervals) notes.push_back(48 + transpose + interval);
        const auto result = detector.detectChord(notes);
        check(result && result->root == transpose, entry.first + " full-pattern root at " + std::to_string(transpose));
    }
    const auto& altered = detector.getChordPatterns().at("altered");
    using namespace ChordDetection;
    check(ChordScoring::computeScore({0,4,10}, altered, 0, 0, VoicingType::Close) == 0,
          "7alt requires a sounding alteration, not just a dominant shell");
    check(ChordScoring::computeScore({4,6,8,10,1,3}, altered, 4, 0, VoicingType::Rootless) > 0,
          "altered voicings may omit the root when guide tones are present");
    check(ChordScoring::computeScore({6,8,10,1,3}, altered, 6, 0, VoicingType::Rootless) == 0,
          "a rootless altered hypothesis must actually contain the major third");
    check(chord::degreeName(3, voicing(48, {0,4,6,8,10,13,15}).chord.get()) == "#9",
          "the altered chord's raised ninth is not labeled as a minor third");
    check(chord::spelledName(63, voicing(48, {0,4,6,8,10,13,15}).chord.get(), false) == "D#4",
          "an altered C chord spells its raised ninth as D#, not Eb");
}

void muChords() {
    // The added second is retained alongside the major third. MIDI spacing
    // does not change the chord quality; the display calls this add9.
    const std::initializer_list<int> shapes[] = {
        {0,2,4,7}, {0,2,4}, {0,7,14,16}, {0,4,7,14}, {4,14,19,24},
        {7,12,14,16}, {0,12,14,16,19,24},
    };
    for (int transpose = 0; transpose < 12; ++transpose) for (const auto& shape : shapes) {
        auto frame = voicing(48 + transpose, shape);
        check(frame.chord && frame.chord->root == transpose && frame.chord->chordType == "add9",
              "mu/add2 voicing remains add9 across inversions, spacing and doubling: " +
              std::to_string(transpose) + " got " + (frame.chord ? chord::asciiMusic(frame.chord->chordName) : "--"));
    }
    auto s = key(0);
    check(chord::chordName(voicing(60, {0,2,4,7}), s) == "Cadd9", "C-D-E-G displays as Cadd9 in Jazz mode");
    check(chord::chordName(voicing(60, {0,2,4}), s) == "Cadd9", "mu/add9 may omit the perfect fifth");
    s.notation = chord::ChordNotation::Standard;
    check(chord::chordName(voicing(60, {0,2,4,7}), s) == "Cadd9", "mu naming is also add9 in Standard mode");
    check(chord::chordName(voicing(48, {4,14,19,24}), s) == "Cadd9/E", "third-in-bass mu voicing preserves slash bass");
    check(chord::chordName(voicing(48, {2,4,7,12}), s) == "D9sus4",
          "Cadd9/D is also D9sus4(no5); the bass guides this ambiguous reading");
    check(voicing(48, {0,2,7}).chord->chordType == "sus2", "removing the third makes sus2, not mu major");
    check(voicing(48, {0,4,7,10,14}).chord->chordType == "dominant9", "adding b7 makes a dominant ninth");
    check(voicing(48, {0,4,7,11,14}).chord->chordType == "major9", "adding a major seventh makes maj9");
    check(chord::analyzeHarmony(voicing(48, {0,2,4,7}), key(0)).shortRole == "TONIC", "mu major retains tonic function");
}

void completePitchAccounting() {
    ChordDetection::ChordDetector detector;
    check(!detector.detectChord({60,61,62,63,64,65,66,67,68,69,70,71}),
          "a complete chromatic cluster cannot become an ordinary chord symbol");
    // Exhaust the twelve-note universe. Every returned symbol must account
    // for all played classes and all mandatory tones except an inferred root.
    for (int pitches = 0; pitches < 4096; ++pitches) {
        std::vector<int> notes;
        for (int pitch = 0; pitch < 12; ++pitch) if (pitches & (1 << pitch)) notes.push_back(60 + pitch);
        const auto result = detector.detectChord(notes);
        if (!result) continue;
        const auto& pattern = detector.getChordPatterns().at(result->chordType);
        const auto played = mask(result->intervals);
        const auto allowed = mask(pattern.intervals) | mask(pattern.optional);
        auto required = mask(pattern.required);
        const bool rootless = result->voicingType == ChordDetection::VoicingType::Rootless;
        if (rootless) required &= ~uint16_t(1);
        check(!(played & ~allowed), "recognized symbol explains every sounding pitch class");
        check((played & required) == required, "recognized symbol has evidence for every mandatory tone");
        check(pattern.requiredAny.empty() || (played & mask(pattern.requiredAny)), "7alt has at least one alteration");
        if (rootless) {
            const bool seventh = played & ((1 << 10) | (1 << 11));
            const bool sixNine = !(mask(pattern.intervals) & ((1 << 10) | (1 << 11))) &&
                (played & (1 << 9)) && (played & (1 << 2));
            check((played & ((1 << 3) | (1 << 4))) && (seventh || sixNine),
                  "rootless recognition contains its actual guide-tone evidence");
        }
    }
    for (const auto& entry : detector.getChordPatterns()) {
        check(__builtin_popcount(mask(entry.second.intervals)) == int(entry.second.intervals.size()),
              entry.first + " does not name a single pitch class twice (e.g. #5 and b13)");
    }
}

void tonalFunction() {
    using chord::ScaleMode;
    for (int transpose = 0; transpose < 12; ++transpose) {
        const int root = 48 + transpose;
        auto major = key(transpose), minor = key(transpose, ScaleMode::Aeolian);
        auto h = chord::analyzeHarmony(voicing(root, {0,4,7,11,18}), major);
        check(h.shortRole == "TONIC" && !h.uncertain && !h.inScale,
              "Imaj7#11 is tonic while its chromatic #11 stays outside the major reference");
        for (const auto& v : {voicing(root+7, {0,4,7,10,13}), voicing(root+7, {0,4,7,10,20}),
                              voicing(root+7, {0,4,6,8,10,13,15}), voicing(root+7, {0,5,7,10,14}),
                              voicing(root+7, {0,5,7,10,14,21}), voicing(root+7, {0,4,8})}) {
            check(chord::analyzeHarmony(v, major).shortRole == "DOMINANT" &&
                  chord::analyzeHarmony(v, minor).shortRole == "DOMINANT",
                  "primary V retains its function with alterations, suspensions or augmentation in major/minor");
        }
        h = chord::analyzeHarmony(voicing(root+7, {0,4,6,8,10,13,15}), major);
        check(h.roman == "V7", "7alt alternatives do not invent an augmented triad in the Roman numeral");
        h = chord::analyzeHarmony(voicing(root+7, {0,5,7,10,14}), major);
        check(h.roman == "V7sus4", "Roman numerals preserve suspension type");
        for (const auto& i : {voicing(root, {0,3,7,9}), voicing(root, {0,3,7,11}), voicing(root, {0,3,7,9,14})}) {
            h = chord::analyzeHarmony(i, minor);
            check(h.shortRole == "TONIC" && !h.uncertain && !h.inScale,
                  "minor 6, minor-major 7 and minor 6/9 are composite-minor tonics");
        }
        check(chord::analyzeHarmony(voicing(root+2, {0,3,6,10}), minor).shortRole == "PRE-DOMINANT",
              "ii half-diminished is a minor-key predominant");
        check(chord::analyzeHarmony(voicing(root+5, {0,3,7,10}), minor).shortRole == "PRE-DOMINANT",
              "iv minor seventh is a minor-key predominant");
        check(chord::analyzeHarmony(voicing(root+5, {0,4,7,10}), minor).shortRole == "PRE-DOMINANT",
              "melodic-minor IV7 retains subdominant function in a minor key");
        check(chord::analyzeHarmony(voicing(root+2, {0,3,7,10}), minor).shortRole == "PRE-DOMINANT",
              "melodic-minor ii7 also has predominant function");
        check(chord::analyzeHarmony(voicing(root+9, {0,3,6,10}), minor).shortRole == "PRE-DOMINANT",
              "melodic-minor vi half-diminished belongs to the subdominant family");
        check(chord::analyzeHarmony(voicing(root+3, {0,4,8,11}), minor).shortRole == "TONIC FAMILY",
              "harmonic/melodic-minor bIII augmented-major seventh retains tonic-family function");
        check(chord::analyzeHarmony(voicing(root+8, {0,4,7,11}), minor).shortRole == "PRE-DOMINANT",
              "bVImaj7 belongs to the subdominant-minor family");
        check(chord::analyzeHarmony(voicing(root+7, {0,3,7,10}), minor).shortRole != "DOMINANT",
              "minor v does not acquire a leading-tone dominant function");
        check(chord::analyzeHarmony(voicing(root+11, {0,3,6,9}), minor).shortRole == "DOMINANT",
              "harmonic-minor leading-tone diminished seventh has dominant function");
        const auto dorian = key(transpose, ScaleMode::Dorian), mixolydian = key(transpose, ScaleMode::Mixolydian);
        check(chord::analyzeHarmony(voicing(root+5, {0,4,7,10}), dorian).shortRole == "MODAL COLOR",
              "Dorian IV7 stays modal instead of becoming an applied dominant");
        check(chord::analyzeHarmony(voicing(root, {0,4,7,10}), mixolydian).role == "Modal center / home",
              "Mixolydian I7 stays home");
        check(chord::analyzeHarmony(voicing(root, {0,3,7,11}), dorian).uncertain,
              "an explicitly selected Dorian collection does not silently become tonal minor");
        check(chord::analyzeHarmony(voicing(root+2, {0,4,7,10}), minor).roman == "V7/V?",
              "secondary dominants in tonal minor may target the major-quality V");
    }
    const auto cMinor = key(0, ScaleMode::Aeolian);
    const auto h = chord::analyzeHarmony(voicing(48, {0,3,7,11}), cMinor);
    check(h.tones[6].pitch == 10 && !h.tones[6].played && h.outsideMask == (1 << 11),
          "composite-minor function does not falsify the natural-minor scale highlights");
    check(h.exercise.find("ii7b5 - V - i") != std::string::npos, "minor-key exercise teaches the minor cadence");
    check(chord::analyzeHarmony(voicing(55, {0,4,7,10,13}), key(0)).outsideNotes == "Ab",
          "G7b9 spells its outside tension as Ab, not G#");
    auto flats = key(0); flats.flats = true;
    check(chord::analyzeHarmony(voicing(48, {0,4,7,11,18}), flats).outsideNotes == "F#",
          "Cmaj7#11 keeps F# in the outside-note readout even with flat root preference");
    check(chord::analyzeHarmony(voicing(48, {0,4,7,11}), key(0, ScaleMode::Dorian)).detail == "from Major",
          "modal interchange includes parallel major, rather than misidentifying it as Lydian");
    check(chord::analyzeHarmony(voicing(48, {0,4,7,11,18}), key(0, ScaleMode::Dorian)).detail == "from Lydian",
          "a borrowing source that also contains the played tensions is preferred");
}

void intervalStacks() {
    for (int transpose = 0; transpose < 12; ++transpose) {
        const auto s = key(transpose);
        const int root = 48 + transpose;
        for (const auto& thirds : {voicing(root, {0,4,7}), voicing(root, {0,3,7}), voicing(root, {0,4,8}),
                                    voicing(root, {0,3,6,9}), voicing(root, {0,4,7,11,14,17,21})}) {
            const auto h = chord::analyzeHarmony(thirds, s);
            check(thirds.chord && !h.quartal && !h.quintal, "third stacks retain their ordinary triad/extension names");
        }
        for (const auto& fourths : {voicing(root, {0,5,10}), voicing(root, {0,5,10,15})}) {
            const auto h = chord::analyzeHarmony(fourths, s);
            check(h.quartal && !h.quintal && h.roman == "4ths" && h.shortRole == "QUARTAL" && h.uncertain,
                  "literal perfect-fourth stacks have an ambiguous quartal reading");
        }
        for (const auto& fifths : {voicing(root, {0,7,14}), voicing(root, {0,7,14,21}),
                                   voicing(root, {0,7,14,21,28}), voicing(root, {0,7,14,21,28,35,42})}) {
            const auto h = chord::analyzeHarmony(fifths, s);
            check(fifths.chord && h.quintal && !h.quartal && h.roman == "5ths" && h.shortRole == "QUINTAL" && h.uncertain,
                  "literal fifth stacks are recognized as ambiguous quintal harmony");
            chord::HarmonyTrail trail;
            trail.observe(fifths, s, 0); trail.observe(fifths, s, 180);
            check(trail.size() == 0, "a quintal stack does not enter the trail with a speculative functional root");
        }
        const auto h = chord::analyzeHarmony(voicing(root, {0,7,16,23}), s);
        check(!h.quintal && !h.quartal && h.shortRole == "TONIC", "an open major seventh is not a literal interval stack");
    }
    auto s = key(0);
    s.notation = chord::ChordNotation::Jazz;
    check(chord::chordName(voicing(48, {0,3,6,10,14}), s) == "Cø9", "half-diminished ninth uses the Jazz quality glyph");
    check(chord::chordName(voicing(48, {0,4,7,11,14,18,21}), s) == "CΔ13#11", "Lydian major thirteenth retains #11");
}

void chromaticFunctions() {
    const int targets[] = {2,4,5,7,9};
    const char* numerals[] = {"ii","iii","IV","V","vi"};
    for (int transpose = 0; transpose < 12; ++transpose) {
        const auto s = key(transpose);
        const int root = 48 + transpose;
        for (int i = 0; i < 5; ++i) {
            auto h = chord::analyzeHarmony(voicing(root + (targets[i]+7)%12, {0,4,7,10}), s);
            check(h.roman == "V7/" + std::string(numerals[i]) + "?" && h.uncertain,
                  "secondary dominants target the correct major/minor degree");
            if (targets[i] == 9) continue; // bVII7 also has the common backdoor reading.
            h = chord::analyzeHarmony(voicing(root + targets[i]+1, {0,4,7,10}), s);
            check(h.roman == "subV7/" + std::string(numerals[i]) + "?" && h.uncertain,
                  "tritone substitutes can target secondary degrees");
        }
        auto h = chord::analyzeHarmony(voicing(root+1, {0,4,7,10}), s);
        check(h.roman == "subV7/I?", "primary tritone substitute still resolves down a half step");
        h = chord::analyzeHarmony(voicing(root+6, {0,3,6,9}), s);
        check(h.shortRole == "DIM APPROACH?" && h.roman == "#ivdim7?", "#IVdim7 has a qualified approach to V reading");
        h = chord::analyzeHarmony(voicing(root+1, {0,3,6,9}), s);
        check(h.roman == "#idim7?", "leading diminished root below ii is #i, not an enharmonic bii");
        h = chord::analyzeHarmony(voicing(root+10, {0,4,7,10}), s);
        check(h.shortRole == "BACKDOOR?", "bVII7 backdoor remains a hypothesis");
    }
}

void cushInterchange() {
    // Cush is a progression technique. Keep the chosen major tonic while
    // checking the individual parallel-minor colors and the return home.
    for (int transpose = 0; transpose < 12; ++transpose) {
        const auto s = key(transpose);
        const int root = 48 + transpose;
        for (const auto& borrowed : {voicing(root+3, {4,14,19,24}), voicing(root+5, {0,3,7,10}),
                                     voicing(root+8, {0,4,7,11}), voicing(root+10, {0,4,7})}) {
            const auto h = chord::analyzeHarmony(borrowed, s);
            check(borrowed.chord && h.shortRole == "BORROWED?" && h.uncertain && !h.inScale,
                  "Cush parallel-minor colors retain ordinary chord names and qualified borrowing");
        }
        chord::HarmonyTrail trail;
        uint32_t now = 0;
        for (const auto& frame : {voicing(root, {0,4,7,11}), voicing(root+8, {0,4,7,11}),
                                  voicing(root+10, {0,4,7,10,14}), voicing(root, {0,4,7,11})}) {
            check(frame.chord != nullptr, "Cush progression chord is recognized");
            trail.observe(frame, s, now);
            trail.observe(frame, s, now+180);
            now += 300;
        }
        check(trail.text() == "Imaj7 > bVImaj7 > bVII7 > Imaj7" && trail.motion().empty(),
              "Cush trail preserves the chosen tonic and does not invent ii-V-I motion");
        check(s.tonic == transpose && s.scale == chord::ScaleMode::Ionian, "modal interchange leaves the selected key intact");
    }
}

std::string cadence(const chord::Settings& s, const chord::Snapshot& ii,
                    const chord::Snapshot& v, const chord::Snapshot& i) {
    chord::HarmonyTrail trail;
    uint32_t now = 0;
    for (const auto& frame : {ii,v,i}) {
        trail.observe(frame, s, now);
        trail.observe(frame, s, now+180);
        now += 300;
    }
    return trail.motion();
}
void cadenceQuality() {
    using chord::ScaleMode;
    for (int transpose = 0; transpose < 12; ++transpose) {
        const int root = 48 + transpose;
        const auto major = key(transpose), minor = key(transpose, ScaleMode::Aeolian);
        const auto ii = voicing(root+2, {0,3,7,10}), ii6 = voicing(root+2, {0,3,7,9});
        const auto iiHalf = voicing(root+2, {0,3,6,10});
        const auto v = voicing(root+7, {0,4,7,10}), vAltered = voicing(root+7, {0,4,7,10,13});
        const auto iMajor = voicing(root, {0,4,7,11}), iMinor = voicing(root, {0,3,7,9});
        check(cadence(major, ii, v, iMajor) == "ii - V - I / resolved", "major ii-V-I resolves in every key");
        check(cadence(minor, iiHalf, vAltered, iMinor) == "ii7b5 - V - i / resolved", "minor ii half-diminished-V-i6 resolves in every key");
        check(cadence(major, iiHalf, vAltered, iMajor) == "ii7b5 - V - I / resolved", "borrowed ii half-diminished may also resolve to major");
        check(cadence(major, ii6, v, iMajor).empty(), "ii6 is not automatically a jazz ii7 cadence");
        check(cadence(major, ii, v, voicing(root, {0,4,7,10})).empty(), "arrival on I7 does not falsely confirm tonic resolution");
        check(cadence(major, ii, v, voicing(root, {0,5,7})).empty(), "arrival on a suspended tonic does not confirm major quality");
        check(cadence(major, ii, v, iMinor).empty(), "a minor tonic does not confirm a selected major-key cadence");
        check(cadence(key(transpose, ScaleMode::Lydian), ii, v, iMajor) == "ii - V - I / tonal cadence",
              "a tonal cadence in a major mode retains uppercase I");
    }
}
}

int main() {
    jazzVoicings(); muChords(); completePitchAccounting(); tonalFunction(); chromaticFunctions(); intervalStacks(); cushInterchange(); cadenceQuality();
    std::cout << checks << " jazz-theory checks passed\n";
}
