#include "Analysis.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
void quoted(const std::string& value) {
    std::cout << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') std::cout << '\\' << c;
        else if (c < 32) std::cout << "\\u00" << std::hex << std::setw(2)
                                 << std::setfill('0') << int(c) << std::dec;
        else std::cout << c;
    }
    std::cout << '"';
}
void integers(const std::vector<int>& values) {
    std::cout << '[';
    bool first = true;
    for (int value : values) {
        if (!first) std::cout << ',';
        std::cout << value;
        first = false;
    }
    std::cout << ']';
}
}

// A deliberately small batch interface to the production analyzer. Each input
// line is an integer request ID followed by MIDI pitches; output is JSONL.
int main(int argc, char** argv) {
    ChordDetection::ChordDetector detector;
    if (argc == 2 && std::string(argv[1]) == "--events") {
        chord::MidiState state;
        std::string line;
        while (std::getline(std::cin, line)) {
            std::istringstream input(line);
            int id, status, data1, data2;
            if (!(input >> id >> status >> data1 >> data2) || status < 128 || status > 255 ||
                data1 < 0 || data1 > 127 || data2 < 0 || data2 > 127) return 2;
            state.receive({static_cast<uint8_t>(status), static_cast<uint8_t>(data1), static_cast<uint8_t>(data2)});
            std::cout << "{\"id\":" << id << ",\"notes\":"; integers(state.notes());
            std::cout << ",\"pedal\":" << (state.pedalDown() ? "true" : "false") << "}\n";
        }
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--patterns") {
        for (const auto& [name, pattern] : detector.getChordPatterns()) {
            std::cout << "{\"type\":"; quoted(name);
            std::cout << ",\"display\":"; quoted(chord::asciiMusic(pattern.display));
            std::cout << ",\"quality\":"; quoted(pattern.quality);
            std::cout << ",\"intervals\":"; integers(pattern.intervals);
            std::cout << ",\"required\":"; integers(pattern.required);
            std::cout << ",\"optional\":"; integers(pattern.optional);
            std::cout << ",\"required_any\":"; integers(pattern.requiredAny);
            std::cout << "}\n";
        }
        return 0;
    }
    if (argc != 1) { std::cerr << "usage: chord_audit [--patterns|--events]\n"; return 2; }
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        int id, pitch;
        if (!(input >> id)) { std::cerr << "missing integer request ID\n"; return 2; }
        chord::MidiState state;
        while (input >> pitch) {
            if (pitch < 0 || pitch > 127) { std::cerr << "invalid MIDI pitch\n"; return 2; }
            state.receive({0x90, static_cast<uint8_t>(pitch), 100});
        }
        if (!input.eof()) { std::cerr << "invalid MIDI pitch token\n"; return 2; }
        const auto snapshot = chord::analyze(state, detector);
        chord::Settings settings;
        settings.notation = chord::ChordNotation::Standard;
        std::cout << "{\"id\":" << id << ",\"notes\":"; integers(snapshot.notes);
        std::cout << ",\"name\":"; quoted(chord::chordName(snapshot, settings));
        settings.flats = true;
        std::cout << ",\"flat_name\":"; quoted(chord::chordName(snapshot, settings));
        std::cout << ",\"voicing\":"; quoted(chord::voicingName(snapshot));
        if (snapshot.chord) {
            const auto& result = *snapshot.chord;
            std::cout << ",\"root\":" << result.root << ",\"type\":"; quoted(result.chordType);
            std::cout << ",\"score\":" << result.score << ",\"confidence\":" << result.confidence;
            std::cout << ",\"position\":"; quoted(result.position);
            std::cout << ",\"root_present\":" << (result.voicingType == ChordDetection::VoicingType::Rootless ? "false" : "true");
            std::cout << ",\"pattern\":"; integers(result.pattern);
            std::cout << ",\"intervals\":"; integers(result.intervals);
        } else std::cout << ",\"root\":null,\"type\":null";
        std::cout << ",\"alternatives\":[";
        bool first = true;
        if (snapshot.chord) for (const auto& alternative : snapshot.chord->alternatives) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"root\":" << alternative.root << ",\"type\":"; quoted(alternative.chordType);
            settings.flats = false;
            std::cout << ",\"name\":"; quoted(chord::alternativeName(alternative, settings));
            settings.flats = true;
            std::cout << ",\"flat_name\":"; quoted(chord::alternativeName(alternative, settings));
            std::cout << ",\"score\":" << alternative.score;
            std::cout << ",\"root_present\":" << (alternative.rootless ? "false" : "true");
            std::cout << ",\"pattern\":"; integers(alternative.pattern);
            std::vector<int> intervals;
            for (int pitchClass : snapshot.chord->pitchClasses)
                intervals.push_back((pitchClass - alternative.root + 12) % 12);
            std::sort(intervals.begin(), intervals.end());
            std::cout << ",\"intervals\":"; integers(intervals);
            std::cout << '}';
        }
        // Backward-compatible diagnostic: candidate generation now visits all
        // eligible three-note roots, so none are blocked by a four-note guard.
        std::cout << "],\"blocked_rootless_candidates\":[]";
        std::cout << "}\n";
    }
}
