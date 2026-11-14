// ChordDetector.cpp
// Main chord detector implementation
//
// This file contains the core detection logic that orchestrates
// the specialized modules.

#include "ChordDetector.h"
#include <algorithm>
#include <array>
#include <set>

namespace ChordDetection {

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

ChordDetector::ChordDetector(bool enableContext, SlashChordMode slashMode)
    : enableContext_(enableContext)
    , slashChordMode_(slashMode)
{
    ChordPatterns::initializePatterns(chordPatterns_);
}

ChordDetector::~ChordDetector() {
    chordHistory_.clear();
}

// ============================================================================
// AMBIGUITY RESOLUTION
// ============================================================================

std::shared_ptr<ChordCandidate> ChordDetector::resolveAmbiguity(
    const std::vector<std::shared_ptr<ChordCandidate>>& candidates,
    int bassPitchClass) const
{
    if (candidates.empty()) {
        return nullptr;
    }

    if (candidates.size() < 2) {
        return candidates[0];
    }

    auto& top    = candidates[0];
    // Minor-sixth and half-diminished seventh have identical pitch sets.
    // In the absence of harmonic context, use the actually played bass.
    for (const auto& candidate : candidates) {
        const auto isMinorSixth = [](const std::string& type) { return type == "minor6" || type == "minor6/9"; };
        const auto isHalfDiminished = [](const std::string& type) {
            return type == "half-diminished7" || type == "minor7b5" || type == "minor9b5" || type == "minor11b5";
        };
        const bool minor6vsHalfDim =
            (isMinorSixth(candidate->chordType) && isHalfDiminished(top->chordType)) ||
            (isMinorSixth(top->chordType) && isHalfDiminished(candidate->chordType));
        if (minor6vsHalfDim && std::abs(top->score - candidate->score) <= 80) {
            if (candidate->root == bassPitchClass) return candidate;
            if (top->root == bassPitchClass) return top;
        }
        // Cm11 without 9 and F9sus4 share C-Eb-G-Bb-F. Cadd9/D and
        // D9sus4(no5) also coincide. Use the played bass for these readings.
        const auto isSusExtension = [](const std::string& type) {
            return type == "7sus4" || type == "7sus4b9" || type == "9sus4" || type == "13sus4";
        };
        const auto isMinorEleventh = [](const std::string& type) { return type == "minor11" || type == "minor11b5"; };
        const bool suspendedAlternative =
            (isMinorEleventh(candidate->chordType) && isSusExtension(top->chordType)) ||
            (isMinorEleventh(top->chordType) && isSusExtension(candidate->chordType)) ||
            (candidate->chordType == "add9" && top->chordType == "9sus4") ||
            (top->chordType == "add9" && candidate->chordType == "9sus4");
        if (suspendedAlternative && std::abs(top->score - candidate->score) <= 40) {
            if (candidate->root == bassPitchClass) return candidate;
            if (top->root == bassPitchClass) return top;
        }
    }
    auto& second = candidates[1];

    // Large score gap — no real ambiguity, trust the score.
    if (top->score - second->score > 40.0f) {
        return top;
    }

    const auto& typeA = top->chordType;
    const auto& typeB = second->chordType;

    // -------------------------------------------------------------------------
    // Case 1: C6 vs Am7 — same four notes, different root interpretation.
    //   C major6 : {C,E,G,A} — root=C, intervals={0,4,7,9}
    //   A minor7 : {A,C,E,G} — root=A, intervals={0,3,7,10}
    // Disambiguation: whichever root matches the bass wins.
    // -------------------------------------------------------------------------
    const bool isMajor6vsMinor7 = (typeA == "major6" && typeB == "minor7") ||
                                   (typeA == "minor7" && typeB == "major6");
    if (isMajor6vsMinor7) {
        if (top->root    == bassPitchClass) return top;
        if (second->root == bassPitchClass) return second;
        // No bass clue: prefer major6 (more common in root-position context).
        return (typeA == "major6") ? top : second;
    }

    // -------------------------------------------------------------------------
    // Case 2: Diminished7 enharmonics — all four inversions of a dim7 chord
    // share the same four pitch classes (e.g., Bdim7 = Ddim7 = Fdim7 = Abdim7).
    // Disambiguation: whichever root is the bass note wins.
    // -------------------------------------------------------------------------
    if (typeA == "diminished7" && typeB == "diminished7") {
        if (top->root    == bassPitchClass) return top;
        if (second->root == bassPitchClass) return second;
        return top; // fall back to highest score
    }

    // -------------------------------------------------------------------------
    // Case 3: Minor6 vs minor — e.g., Am6 {A,C,E,F#} vs Am {A,C,E}.
    // If the minor6 root is the bass, prefer it (strong evidence).
    // Otherwise trust the score.
    // -------------------------------------------------------------------------
    const bool isMinor6vsMinor = (typeA == "minor6" && typeB == "minor") ||
                                  (typeA == "minor"  && typeB == "minor6");
    if (isMinor6vsMinor) {
        auto& minor6cand = (typeA == "minor6") ? top : second;
        if (minor6cand->root == bassPitchClass) return minor6cand;
        return top; // trust score
    }

    // Default: highest score wins.
    return top;
}

// ============================================================================
// MAIN DETECTION METHOD
// ============================================================================

std::shared_ptr<ChordCandidate> ChordDetector::detectChord(
    const std::vector<int>& midiNotes)
{
    // Keep one compact reading per root. Only the three leading readings in
    // the primary group need full result objects; alternatives share the notes.
    std::vector<int> sortedNotes;
    sortedNotes.reserve(midiNotes.size());
    for (int note : midiNotes) {
        if (note >= kMinMidiNote && note <= kMaxMidiNote) sortedNotes.push_back(note);
    }
    std::sort(sortedNotes.begin(), sortedNotes.end());
    sortedNotes.erase(std::unique(sortedNotes.begin(), sortedNotes.end()), sortedNotes.end());
    std::vector<int> pitchClasses;
    for (int note : sortedNotes) pitchClasses.push_back(note % 12);
    std::sort(pitchClasses.begin(), pitchClasses.end());
    pitchClasses.erase(std::unique(pitchClasses.begin(), pitchClasses.end()), pitchClasses.end());
    if (pitchClasses.size() < kDefaultMinimumNotes) return nullptr;

    struct Ranked {
        const std::pair<const std::string, ChordPattern>* entry = nullptr;
        int root = 0;
        float score = 0;
        VoicingType voicing = VoicingType::Unknown;
        int alterations = 0;
    };
    std::array<Ranked, 12> ranked{};
    const int bass = sortedNotes.front() % 12;
    const auto voicing = VoicingAnalyzer::classifyVoicing(sortedNotes);
    std::vector<int> intervals;
    intervals.reserve(12);
    for (int root = 0; root < 12; ++root) {
        const bool present = std::binary_search(pitchClasses.begin(), pitchClasses.end(), root);
        if (!present && pitchClasses.size() < 3) continue;
        intervals.clear();
        for (int pc : pitchClasses) intervals.push_back(NoteUtils::intervalBetween(root, pc));
        std::sort(intervals.begin(), intervals.end());
        int alterations = 0;
        for (int interval : intervals) if (interval == 1 || interval == 6 || interval == 8) ++alterations;
        if (std::binary_search(intervals.begin(), intervals.end(), 4) &&
            std::binary_search(intervals.begin(), intervals.end(), 3)) ++alterations;
        for (const auto& entry : chordPatterns_) {
            const auto type = present ? voicing : VoicingType::Rootless;
            const float score = ChordScoring::computeScore(intervals, entry.second, bass, root, type);
            if (score <= kMinimumScoreThreshold || score <= ranked[root].score) continue;
            ranked[root] = {&entry, root, score, type, alterations};
        }
    }
    std::sort(ranked.begin(), ranked.end(), [bass](const Ranked& a, const Ranked& b) {
        if (!a.entry || !b.entry) return bool(a.entry) > bool(b.entry);
        // A complete reading with a sounding root stays the literal default.
        // Missing-root hypotheses remain available without replacing it.
        if ((a.voicing == VoicingType::Rootless) != (b.voicing == VoicingType::Rootless))
            return a.voicing != VoicingType::Rootless;
        if (a.score != b.score) return a.score > b.score;
        // With equal rootless evidence, prefer fewer altered tensions before
        // using a transposition-invariant root/bass tie-break.
        if (a.voicing == VoicingType::Rootless && a.alterations != b.alterations)
            return a.alterations < b.alterations;
        return NoteUtils::intervalBetween(bass, a.root) < NoteUtils::intervalBetween(bass, b.root);
    });

    std::vector<std::shared_ptr<ChordCandidate>> candidates;
    candidates.reserve(3);
    for (const auto& item : ranked) {
        if (!item.entry || candidates.size() == 3 || (item.voicing == VoicingType::Rootless &&
            ranked[0].voicing != VoicingType::Rootless)) break;
        auto candidate = std::make_shared<ChordCandidate>();
        const auto& pattern = item.entry->second;
        candidate->root = item.root;
        candidate->rootName = NoteUtils::getNoteName(item.root);
        candidate->chordType = item.entry->first;
        candidate->pattern = pattern.intervals;
        candidate->score = item.score;
        candidate->voicingType = item.voicing;
        candidate->noteNumbers = sortedNotes;
        candidate->pitchClasses = pitchClasses;
        for (int pc : pitchClasses) candidate->intervals.push_back(NoteUtils::intervalBetween(item.root, pc));
        std::sort(candidate->intervals.begin(), candidate->intervals.end());
        for (int interval : candidate->intervals) candidate->degrees.push_back(NoteUtils::getDegreeName(interval));
        for (int note : sortedNotes) candidate->noteNames.push_back(NoteUtils::getNoteName(note % 12));

        const int bassInterval = NoteUtils::intervalBetween(item.root, bass);
        std::string slash;
        if (bassInterval == 0) candidate->position = "Root Position";
        else {
            const auto bassName = NoteUtils::getNoteName(bass);
            if (item.voicing == VoicingType::Rootless) candidate->position = "Rootless";
            else if (bassInterval == 3 || bassInterval == 4) candidate->position = "1st Inversion";
            else if (bassInterval == 6 || bassInterval == 7 || bassInterval == 8) candidate->position = "2nd Inversion";
            else if (bassInterval == 10 || bassInterval == 11 ||
                     (bassInterval == 9 && candidate->chordType == "diminished7")) candidate->position = "3rd Inversion";
            else candidate->position = "Slash Chord";
            if (slashChordMode_ != SlashChordMode::Never) slash = "/" + bassName;
        }
        candidate->chordName = NoteUtils::replaceRootTemplate(pattern.display, candidate->rootName) + slash;
        candidates.push_back(candidate);
    }
    if (candidates.empty()) return nullptr;
    auto best = resolveAmbiguity(candidates, bass);
    float rivalScore = 0;
    for (const auto& item : ranked) {
        if (!item.entry || item.root == best->root) continue;
        rivalScore = std::max(rivalScore, item.score);
        const auto& pattern = item.entry->second;
        ChordAlternative alternative;
        alternative.root = item.root;
        alternative.bass = bass;
        alternative.rootName = NoteUtils::getNoteName(item.root);
        alternative.chordType = item.entry->first;
        alternative.pattern = pattern.intervals;
        alternative.score = item.score;
        alternative.rootless = item.voicing == VoicingType::Rootless;
        alternative.chordName = NoteUtils::replaceRootTemplate(pattern.display, alternative.rootName);
        if (bass != item.root && slashChordMode_ != SlashChordMode::Never)
            alternative.chordName += "/" + NoteUtils::getNoteName(bass);
        best->alternatives.push_back(std::move(alternative));
    }
    uint16_t playedMask = 0, patternMask = 0;
    for (int interval : best->intervals) playedMask |= uint16_t(1U << (interval % 12));
    for (int interval : best->pattern) patternMask |= uint16_t(1U << (interval % 12));
    best->confidence = ChordScoring::computeConfidence(best->score, rivalScore,
        static_cast<int>(pitchClasses.size()), playedMask == patternMask);
    if (best->voicingType == VoicingType::Rootless) best->confidence = std::min(best->confidence, 0.65f);
    if (enableContext_) {
        chordHistory_.push_back(best);
        if (chordHistory_.size() > kMaxChordHistorySize) chordHistory_.erase(chordHistory_.begin());
    }
    return best;
}

} // namespace ChordDetection

