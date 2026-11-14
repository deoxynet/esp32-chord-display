#include "Analysis.h"
#include "BleMidiParser.h"
#include "Notation.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
int checks = 0;
void check(bool condition, const std::string& description) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}
void collect(void* context, chord::MidiMessage message) {
    static_cast<std::vector<chord::MidiMessage>*>(context)->push_back(message);
}
void parserTests() {
    chord::BleMidiParser parser;
    std::vector<chord::MidiMessage> messages;
    auto packet = [&](std::initializer_list<uint8_t> bytes) {
        std::vector<uint8_t> data(bytes);
        return parser.parse(data.data(), data.size(), collect, &messages);
    };
    check(packet({0x80,0x80,0x90,60,100,64,90,0x81,67,80,0x82,0x80,60,0}), "running status and new timestamps");
    check(messages.size() == 4 && messages[1].data1 == 64 && messages[2].data1 == 67 && messages[3].status == 0x80, "decode batched notes");
    messages.clear();
    check(packet({0x80,0xFF,0x9F,72,127,0x80,0xF8,0x81,74,90,0x82,0xF2,1,2,0x83,76,80}), "timestamp rollover and system events");
    check(messages.size() == 5 && messages[0].status == 0x9F && messages[2].status == 0x9F && messages[4].data1 == 76, "real-time/common preserve packet running status");
    messages.clear();
    check(packet({0x80,0x80,0xC0,5,6,0x81,0xD0,40,0x82,0xB0,64,127}), "one-byte message lengths");
    check(messages.size() == 4 && messages.back().data1 == 64, "CC64 after program change");
    messages.clear();
    check(packet({0x80,0x80,0xF0,1,2,3}), "SysEx start");
    check(packet({0x81,4,5,0x82,0xF8,6,0x83,0xF7,0x84,0x90,60,100}), "SysEx continuation and ending");
    check(messages.size() == 2 && messages.back().status == 0x90, "skip SysEx without swallowing following notes");
    check(!packet({}), "reject empty packet");
    check(!packet({0x80}), "reject header-only packet");
    check(!packet({0x40,0x80,0x90,60,100}), "reject invalid header");
    check(!packet({0x80,0x80,60,100}), "running status cannot cross packets");
    check(!packet({0x80,0x80,0x90,60}), "reject truncated note");
    check(!packet({0x80,0x80,0x90,60,0xFF}), "reject status in MIDI data");
    check(!packet({0x80,0x80,0xF4}), "reject undefined common status");
    uint32_t random = 0x12345678;
    std::vector<uint8_t> fuzz(128);
    for (int n = 0; n < 20000; ++n) {
        for (auto& value : fuzz) { random = random * 1664525U + 1013904223U; value = random >> 24; }
        messages.clear();
        parser.parse(fuzz.data(), n % fuzz.size(), collect, &messages);
        if (n % 5 == 0) parser.reset();
    }
    check(true, "20,000 bounded parser fuzz inputs");
}

void midiTests() {
    chord::MidiState state;
    state.receive({0x90,60,100});
    state.receive({0x91,60,100});
    state.receive({0x80,60,0});
    check(state.notes() == std::vector<int>{60}, "independent channels hold a shared pitch");
    state.setChannel(1);
    check(state.notes().empty(), "channel filter");
    state.setChannel(0);
    state.receive({0xB1,64,127});
    state.receive({0x91,60,0});
    check(state.notes() == std::vector<int>{60} && !state.pressed(60), "velocity-zero release follows sustain");
    state.receive({0x91,60,100});
    state.receive({0xB1,64,0});
    check(state.notes() == std::vector<int>{60} && state.pressed(60), "pedal lift preserves a retriggered held key");
    state.receive({0x81,60,0});
    check(state.notes().empty(), "retrigger releases cleanly");
    state.receive({0xB0,64,127});
    state.receive({0x80,62,0});
    check(state.notes().empty(), "unmatched note off cannot add a sustained pitch");
    state.receive({0x90,64,100});
    state.receive({0x80,64,0});
    state.setUseSustain(false);
    check(state.notes().empty(), "keys-only mode flushes deferred releases");
    state.setUseSustain(true);
    state.receive({0x90,64,100});
    state.receive({0xB0,123,0});
    check(state.notes() == std::vector<int>{64} && !state.pressed(64), "all notes off respects the pedal");
    state.receive({0xB0,121,0});
    check(state.notes().empty() && !state.pedalDown(), "reset controllers releases sustain");
    state.receive({0x90,67,100});
    state.receive({0x90,67,100});
    state.receive({0x80,67,0});
    check(state.pressed(67), "overlapping note-ons are counted");
    state.receive({0xB0,120,0});
    check(state.notes().empty(), "all sound off is immediate");
    state.receive({0x90,67,100});
    state.receive({0xFF,0,0});
    check(state.notes().empty(), "system reset clears all channels");
}

void detectorTests() {
    ChordDetection::ChordDetector detector;
    struct Case { std::vector<int> notes; const char* name; };
    const Case cases[] = {
        {{60,64,67},"C"}, {{60,63,67},"Cm"}, {{60,63,66},"Cdim"},
        {{60,64,68},"Caug"}, {{60,62,67},"Csus2"}, {{60,65,67},"Csus4"},
        {{60,67},"C5"}, {{60,64,67,71},"Cmaj7"}, {{60,63,67,70},"Cm7"},
        {{60,64,67,70},"C7"}, {{60,63,66,69},"Cdim7"}, {{60,63,66,70},"Cm7b5"},
        {{60,63,67,71},"Cm(maj7)"}, {{60,64,67,69},"C6"}, {{57,60,64,67},"Am7"},
        {{60,63,67,69},"Cm6"}, {{60,64,67,71,74},"Cmaj9"},
        {{60,63,67,70,74},"Cm9"}, {{60,64,67,70,74},"C9"},
        {{60,64,67,70,73},"C7b9"}, {{60,64,67,70,75},"C7#9"},
        {{60,64,67,71,78},"Cmaj7#11"}, {{60,64,67,70,74,81},"C13"},
        {{60,64,67,74},"Cadd9"}, {{64,67,72},"C/E"}, {{55,60,64},"C/G"},
    };
    for (const auto& entry : cases) {
        auto result = detector.detectChord(entry.notes);
        const std::string actual = result ? chord::asciiMusic(result->chordName) : "<none>";
        check(actual == entry.name, std::string(entry.name) + " detected as " + actual);
        check(result->confidence >= 0 && result->confidence <= 1, "bounded match score");
    }
    check(!detector.detectChord({}), "no chord for silence");
    check(!detector.detectChord({60}), "no chord for one note");
    check(!detector.detectChord({48,60,72}), "octave unisons are not a chord");
    check(!detector.detectChord({-50,200}), "out-of-range inputs are ignored");
    for (int transpose = 0; transpose < 12; ++transpose) {
        for (const auto& entry : std::vector<Case>{{{60,64,67},"major"},{{60,63,67},"minor"},{{60,64,67,71},"major7"},{{60,64,67,70},"dominant7"}}) {
            auto notes = entry.notes;
            for (auto& note : notes) note += transpose;
            for (size_t inversion = 0; inversion < notes.size(); ++inversion) {
                auto result = detector.detectChord(notes);
                check(result && result->root == transpose && result->chordType == entry.name,
                    "transposition/inversion " + std::string(entry.name) + " root " + std::to_string(transpose) +
                    " inversion " + std::to_string(inversion) + " got " + (result ? chord::asciiMusic(result->chordName) : "<none>"));
                notes[inversion] += 12;
            }
        }
    }
    auto doubled = detector.detectChord({48,60,64,67,71,76});
    check(doubled && doubled->chordType == "major7" && doubled->root == 0, "doubling notes preserves identity");
    auto inversion = detector.detectChord({64,67,72});
    check(inversion->getBassPitchClass() == 4, "bass uses MIDI order, not sorted pitch classes");
    using V = ChordDetection::VoicingType;
    check(ChordDetection::VoicingAnalyzer::classifyVoicing({60,64,67,71}) == V::Close, "close voicing");
    check(ChordDetection::VoicingAnalyzer::classifyVoicing({55,60,64,71}) == V::Drop2, "reconstruct drop 2");
    check(ChordDetection::VoicingAnalyzer::classifyVoicing({52,60,67,71}) == V::Drop3, "reconstruct drop 3");
    check(ChordDetection::VoicingAnalyzer::classifyVoicing({36,60,64,67}) == V::Open, "wide bass is not automatically drop 2");
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) (void)detector.detectChord({48,60,64,67,70,74,81});
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count();
    std::cout << "Host detection mean: " << micros / 1000.0 << " us (not an ESP32 measurement)\n";
}

void notationTests() {
    static const int natural[] = {0,2,4,5,7,9,11};
    ChordDetection::ChordCandidate candidate;
    for (bool flats : {false, true}) {
        for (int root = 0; root < 12; ++root) {
            candidate.root = root;
            for (int midi = 0; midi < 128; ++midi) {
                const auto note = chord::spellNote(midi, &candidate, flats);
                const int letter = ((note.diatonic % 7) + 7) % 7;
                check((note.octave + 1) * 12 + natural[letter] + note.accidental == midi, "notation must preserve pitch at every octave");
                check(std::abs(note.accidental) <= 2, "readable accidentals");
            }
        }
    }
    chord::Snapshot snapshot;
    snapshot.notes = {48,60,61,64,67,71};
    const auto staff = chord::layoutStaff(snapshot, false);
    check(staff.outside == 0 && staff.notes.size() == snapshot.notes.size(), "normal chord fits a grand staff");
    check(staff.notes[1].ledgerLines == std::vector<int>{110}, "middle C ledger line on enlarged staff");
    check(staff.notes[1].x != staff.notes[2].x, "adjacent notes have separated note heads");
    snapshot.notes = {0,127};
    const auto extremes = chord::layoutStaff(snapshot, false);
    check(extremes.notes.size() + extremes.outside == snapshot.notes.size(), "outside-staff pitches are accounted for");
    check(extremes.trebleOctaves == 2 && extremes.bassOctaves == -2, "extreme register octave signs");
    for (const auto& note : extremes.notes) check(note.y >= 40 && note.y <= 211, "notation stays inside its enlarged viewport");
    chord::Settler settler;
    settler.changed(0xFFFFFFF0U);
    check(!settler.due(0xFFFFFFF5U) && settler.due(25), "settling timer survives millis rollover");
    settler.settled();
    settler.changed(100);
    for (uint32_t time = 110; time <= 180; time += 10) settler.changed(time);
    check(settler.due(185), "continuous playing cannot indefinitely postpone recognition");
}

void integrationTests() {
    chord::BleMidiParser parser;
    chord::MidiState state;
    ChordDetection::ChordDetector detector;
    const auto sink = [](void* context, chord::MidiMessage message) {
        static_cast<chord::MidiState*>(context)->receive(message);
    };
    const uint8_t chordPacket[] = {0x80,0x80,0x90,60,100,64,100,67,100,70,100,73,100};
    check(parser.parse(chordPacket, sizeof(chordPacket), sink, &state), "receive an entire chord over BLE");
    auto snapshot = chord::analyze(state, detector);
    check(chord::chordName(snapshot, {}) == "C7b9", "BLE packet to displayed chord name");
    check(chord::spelledName(73, snapshot.chord.get(), false) == "Db5", "flat-nine pitch is spelled consistently with the chord");
    const auto staff = chord::layoutStaff(snapshot, false);
    check(staff.notes.size() == 5 && staff.notes.back().accidental == -1, "BLE packet to staff accidental");
    const uint8_t sustainPacket[] = {0x80,0x80,0xB0,64,127,0x81,0x80,60,0,64,0,67,0,70,0,73,0};
    check(parser.parse(sustainPacket, sizeof(sustainPacket), sink, &state), "release chord under pedal over BLE");
    snapshot = chord::analyze(state, detector);
    check(snapshot.notes.size() == 5 && snapshot.pedal && !snapshot.pressed.front(), "sustained chord remains visible");
    const uint8_t pedalUp[] = {0x80,0x80,0xB0,64,0};
    check(parser.parse(pedalUp, sizeof(pedalUp), sink, &state), "pedal lift over BLE");
    check(chord::analyze(state, detector).notes.empty(), "pedal lift clears the displayed notes");
}
}

int main() {
    parserTests();
    midiTests();
    detectorTests();
    notationTests();
    integrationTests();
    std::cout << checks << " checks passed\n";
}
