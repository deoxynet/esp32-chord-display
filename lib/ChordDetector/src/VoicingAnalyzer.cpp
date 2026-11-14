// VoicingAnalyzer.cpp
// Voicing classification implementation

#include "VoicingAnalyzer.h"
#include <algorithm>

namespace ChordDetection {
namespace VoicingAnalyzer {

VoicingType classifyVoicing(const std::vector<int>& midiNotes) {
    if (midiNotes.size() < 2) {
        return VoicingType::Unknown;
    }
    
    // Sort notes from low to high
    std::vector<int> sorted = midiNotes;
    std::sort(sorted.begin(), sorted.end());
    
    int span = sorted.back() - sorted.front();
    
    // Close voicing: within one octave
    if (span <= 12) {
        return VoicingType::Close;
    }
    
    // Open voicing analysis
    if (span > 12) {
        // For 4+ note chords, check for drop voicings
        if (sorted.size() == 4) {
            unsigned pcs = 0;
            for (int note : sorted) pcs |= 1U << (note % 12);
            auto raised = sorted;
            const int dropped = raised.front() + 12;
            raised.front() = dropped;
            std::sort(raised.begin(), raised.end());
            if (__builtin_popcount(pcs) == 4 && raised.back() - raised.front() < 12) {
                if (raised[2] == dropped) return VoicingType::Drop2;
                if (raised[1] == dropped) return VoicingType::Drop3;
            }
        }
        
        return VoicingType::Open;
    }
    
    return VoicingType::Unknown;
}

int calculateSpan(const std::vector<int>& midiNotes) {
    if (midiNotes.empty()) {
        return 0;
    }
    
    auto [minIt, maxIt] = std::minmax_element(midiNotes.begin(), midiNotes.end());
    return *maxIt - *minIt;
}

bool isCloseVoicing(const std::vector<int>& midiNotes) {
    return calculateSpan(midiNotes) <= 12;
}

std::vector<int> getAdjacentIntervals(const std::vector<int>& midiNotes) {
    std::vector<int> intervals;
    
    if (midiNotes.size() < 2) {
        return intervals;
    }
    
    // Ensure sorted
    std::vector<int> sorted = midiNotes;
    std::sort(sorted.begin(), sorted.end());
    
    intervals.reserve(sorted.size() - 1);
    for (size_t i = 0; i < sorted.size() - 1; ++i) {
        intervals.push_back(sorted[i + 1] - sorted[i]);
    }
    
    return intervals;
}

} // namespace VoicingAnalyzer
} // namespace ChordDetection

