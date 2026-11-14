#include "Notation.h"
#include "NotationGlyphs.h"
#include <algorithm>
#include <cstdlib>

namespace chord {

StaffLayout layoutStaff(const Snapshot& snapshot, bool flats) {
    StaffLayout result;
    std::vector<SpelledNote> spellings;
    int trebleMin = 128, trebleMax = -1, bassMin = 128, bassMax = -1;
    for (int midi : snapshot.notes) {
        const auto spelled = spellNote(std::clamp(midi,0,127),snapshot.chord.get(),flats);
        spellings.push_back(spelled);
        if (midi < 0 || midi > 127) continue;
        // Written octave matters for Cb4 and B#3 at the clef boundary.
        if (spelled.diatonic >= 28) { trebleMin = std::min(trebleMin,midi); trebleMax = std::max(trebleMax,midi); }
        else { bassMin = std::min(bassMin,midi); bassMax = std::max(bassMax,midi); }
    }
    if (trebleMax > 83 && trebleMin >= 72) result.trebleOctaves = 1;
    if (trebleMax > 95 && trebleMin >= 84) result.trebleOctaves = 2;
    if (bassMin < 36 && bassMax < 48) result.bassOctaves = -1;
    if (bassMin < 24 && bassMax < 36) result.bassOctaves = -2;

    for (size_t i = 0; i < snapshot.notes.size(); ++i) {
        const int midi = snapshot.notes[i];
        auto spelled = spellings[i];
        const bool treble = spelled.diatonic >= 28;
        spelled.diatonic -= 7 * (treble ? result.trebleOctaves : result.bassOctaves);
        const int bottom = treble ? trebleBottom : bassBottom;
        const int bottomStep = treble ? 30 : 18; // E4 / G2
        const int y = bottom - (spelled.diatonic - bottomStep) * (staffSpacing / 2);
        const auto& glyph = staffglyph::accidental(spelled.accidental);
        if (midi < 0 || midi > 127 || y < (treble ? 40 : 132) || y > (treble ? 130 : 211) ||
            (spelled.accidental && (y+glyph.top < 34 || y+glyph.top+glyph.height > 219)) || result.notes.size() >= 20) {
            ++result.outside; continue;
        }
        StaffNote note;
        note.midi = midi;
        note.x = 0;
        note.y = y;
        note.diatonic = spelled.diatonic;
        note.treble = treble;
        note.accidental = spelled.accidental;
        note.showAccidental = spelled.accidental != 0;
        note.pressed = i < snapshot.pressed.size() ? snapshot.pressed[i] : true;
        const int top = bottom - 4 * staffSpacing;
        for (int line = bottom + staffSpacing; line <= y; line += staffSpacing) note.ledgerLines.push_back(line);
        for (int line = top - staffSpacing; line >= y; line -= staffSpacing) note.ledgerLines.push_back(line);
        result.notes.push_back(note);
    }
    // An altered and natural pitch sharing a written position both need signs.
    for (auto& note : result.notes) if (!note.accidental)
        for (const auto& other : result.notes)
            if (note.treble == other.treble && note.diatonic == other.diatonic && other.accidental)
                note.showAccidental = true;

    int otherTop[2] = {220,220}, otherBottom[2] = {0,0};
    for (const auto& note : result.notes) {
        const int staff = note.treble ? 0 : 1;
        otherTop[staff] = std::min(otherTop[staff],note.y);
        otherBottom[staff] = std::max(otherBottom[staff],note.y);
    }
    for (bool treble : {true,false}) {
        std::vector<size_t> indices;
        for (size_t i = 0; i < result.notes.size(); ++i) if (result.notes[i].treble == treble) indices.push_back(i);
        if (indices.empty()) continue;
        int top = 220, bottom = 0;
        for (size_t i : indices) { top = std::min(top,result.notes[i].y); bottom = std::max(bottom,result.notes[i].y); }
        const int middle = (treble ? trebleBottom : bassBottom) - 2*staffSpacing;
        bool up = top+bottom >= 2*middle;
        if (treble && top-28 < 35) up = false;
        if (treble && !up && bottom+28 > std::min(136,otherTop[1]-9) && top-28 >= 35) up = true;
        std::sort(indices.begin(),indices.end(),[&](size_t a,size_t b) {
            if (result.notes[a].diatonic == result.notes[b].diatonic)
                return up ? result.notes[a].midi < result.notes[b].midi : result.notes[a].midi > result.notes[b].midi;
            return up ? result.notes[a].diatonic < result.notes[b].diatonic : result.notes[a].diatonic > result.notes[b].diatonic;
        });
        std::vector<int> headRows;
        int minOffset = 0;
        for (size_t i : indices) {
            auto& note = result.notes[i];
            size_t column = 0;
            for (; column < headRows.size(); ++column) if (std::abs(note.y-headRows[column]) >= staffSpacing) break;
            if (column == headRows.size()) headRows.push_back(note.y); else headRows[column] = note.y;
            note.x = int(column)*(up ? noteheadShift : -noteheadShift);
            minOffset = std::min(minOffset,note.x);
        }
        struct Column { int width = 0; std::vector<std::pair<int,int>> ranges; };
        std::vector<Column> columns;
        std::vector<int> lanes(result.notes.size(),-1);
        for (size_t i : indices) {
            const auto& note = result.notes[i];
            if (!note.showAccidental) continue;
            const auto& glyph = staffglyph::accidental(note.accidental);
            const int a = note.y+glyph.top, b = a+glyph.height;
            size_t lane = 0;
            for (; lane < columns.size(); ++lane) {
                bool collision = false;
                for (const auto& range : columns[lane].ranges) if (a < range.second+3 && b+3 > range.first) collision = true;
                if (!collision) break;
            }
            if (lane == columns.size()) columns.emplace_back();
            columns[lane].width = std::max(columns[lane].width,glyph.width);
            columns[lane].ranges.emplace_back(a,b);
            lanes[i] = int(lane);
        }
        int accidentalWidth = 0;
        for (const auto& column : columns) accidentalWidth += column.width+4;
        const int base = std::max(186,84+accidentalWidth+noteheadRadius-minOffset+5);
        for (size_t i : indices) {
            auto& note = result.notes[i];
            note.x += base;
            if (note.showAccidental) {
                const auto& glyph = staffglyph::accidental(note.accidental);
                int right = base+minOffset-noteheadRadius-5;
                for (int lane = 0; lane < lanes[i]; ++lane) right -= columns[lane].width+4;
                note.accidentalX = right-glyph.width-glyph.left;
            }
            result.right = std::max(result.right,note.x+noteheadRadius+22);
        }
        StaffStem stem;
        stem.x = base+(up ? noteheadRadius : -noteheadRadius);
        stem.treble = treble;
        stem.pressed = false;
        int attachTop = 220, attachBottom = 0;
        for (size_t i : indices) {
            const auto& note = result.notes[i];
            const int attach = note.y + (up ? (note.x == base ? -2 : 2) : (note.x == base ? 2 : -2));
            attachTop = std::min(attachTop,attach);
            attachBottom = std::max(attachBottom,attach);
            stem.pressed |= note.pressed;
        }
        stem.top = up ? std::max(top-28,treble ? 35 : std::max(117,otherBottom[0]+9)) : attachTop;
        stem.bottom = up ? attachBottom : std::min(bottom+28,treble ? std::min(136,otherTop[1]-9) : 214);
        result.stems.push_back(stem);
    }
    return result;
}

} // namespace chord
