#include "Analysis.h"
#include "MusicText.h"
#include "ChordText.h"
#include "Notation.h"
#include "NotationGlyphs.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
int checks = 0;
void check(bool condition, const std::string& name) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
chord::Snapshot notes(std::initializer_list<int> pitches) {
    chord::MidiState state; ChordDetection::ChordDetector detector;
    for (int pitch : pitches) state.receive({0x90,static_cast<uint8_t>(pitch),90});
    return chord::analyze(state,detector);
}
void formatting() {
    chord::Settings jazz, standard;
    check(standard.notation == chord::ChordNotation::Standard,"Standard symbols are the default");
    jazz.notation = chord::ChordNotation::Jazz;
    struct Case { chord::Snapshot frame; const char* jazz; const char* standard; };
    const Case cases[] = {
        {notes({60,64,67}),"C","C"}, {notes({60,63,67}),"C-","Cm"},
        {notes({60,63,66}),"C°","Cdim"}, {notes({60,64,68}),"C+","Caug"},
        {notes({60,64,67,71}),"CΔ7","Cmaj7"}, {notes({60,63,67,70}),"C-7","Cm7"},
        {notes({60,64,67,70}),"C7","C7"}, {notes({60,63,66,69}),"C°7","Cdim7"},
        {notes({60,63,66,70}),"Cø7","Cm7b5"}, {notes({60,63,67,71}),"C-Δ7","Cm(maj7)"},
        {notes({60,63,67,70,74}),"C-9","Cm9"}, {notes({60,64,67,71,74}),"CΔ9","Cmaj9"},
        {notes({60,63,67,69}),"C-6","Cm6"}, {notes({60,64,67,69}),"C6","C6"},
        {notes({60,64,67,74}),"Cadd9","Cadd9"}, {notes({60,64,67,70,73}),"C7b9","C7b9"},
        {notes({60,65,67}),"Csus4","Csus4"}, {notes({55,60,64,71}),"CΔ7/G","Cmaj7/G"},
        {notes({61,64,68,71}),"C#-7","C#m7"},
    };
    for (const auto& entry : cases) {
        check(chord::chordName(entry.frame,jazz) == entry.jazz,"Jazz form of " + std::string(entry.standard) + ": " + chord::chordName(entry.frame,jazz));
        check(chord::chordName(entry.frame,standard) == entry.standard,"Standard option retains " + std::string(entry.standard));
    }
    jazz.flats = true;
    check(chord::chordName(notes({61,64,68,71}),jazz) == "Db-7","enharmonic roots survive Jazz formatting");
    auto sixNine = notes({60,64,67,69,74});
    check(chord::chordName(sixNine,jazz) == "C6/9","6/9 quality isn't mistaken for slash bass");
    check(chord::romanName("Imaj7 > viidim7 > ii7b5",jazz) == "IΔ7 > vii°7 > iiø7","Roman numerals use compact quality signs too");
    check(chord::romanName("Imaj7",standard) == "Imaj7","standard Roman formatting remains available");
}
class Probe final : public chord::Canvas {
public:
    struct Text { std::string value; int x,y; chord::Font font; float scale; };
    std::vector<Text> texts;
    int minX = 100000, maxX = -100000, minY = 100000, maxY = -100000;
    int lines = 0, ellipses = 0;
    bool nonAscii = false;
    void point(int x,int y) { minX=std::min(minX,x); maxX=std::max(maxX,x); minY=std::min(minY,y); maxY=std::max(maxY,y); }
    void rect(int x,int y,int w,int h,uint32_t,int) override { point(x,y); point(x+w-1,y+h-1); }
    void line(int x,int y,int xx,int yy,uint32_t) override { ++lines; point(x,y); point(xx,yy); }
    void ellipse(int x,int y,int rx,int ry,uint32_t) override { ++ellipses; point(x-rx,y-ry); point(x+rx,y+ry); }
    int textWidth(const std::string& text,chord::Font font,float scale) override {
        return int(std::lround(text.size() * (font == chord::Font::Chord ? 24 : font == chord::Font::Body ? 8 : 6) * scale));
    }
    void text(const std::string& value,int x,int y,chord::Font font,float scale,uint32_t) override {
        if (!value.empty()) texts.push_back({value,x,y,font,scale});
        for (unsigned char c : value) if (c >= 128) nonAscii=true;
        const int h = int(std::lround((font == chord::Font::Chord ? 48 : font == chord::Font::Body ? 16 : 8) * scale));
        point(x,y); point(x+textWidth(value,font,scale)-1,y+h-1);
    }
};
void glyphs() {
    for (auto font : {chord::Font::Label,chord::Font::Body,chord::Font::Chord}) for (float scale : {0.65f,0.78f,1.f,1.9f}) {
        for (const auto* text : {"CΔ7","C°7","Cø7","C-Δ9/G","viiø7"}) {
            Probe c;
            const int width = chord::musicTextWidth(c,text,font,scale);
            chord::musicText(c,text,10,20,font,scale,0xFFFFFF,0x17232F);
            check(!c.nonAscii,"unsupported Unicode never reaches the bitmap font");
            check(c.lines + c.ellipses > 0,"music signs render as actual shapes");
            check(c.minX >= 9 && c.maxX <= 10+width+1,"music signs fit the measured text advance");
            check(c.minY >= 19 && c.maxY < 20+int(std::lround(49*scale))+2,"symbols stay within their text height");
        }
    }
}
void chordTypography() {
    const char* examples[] = {"Cmaj7","Cm(maj7)","C6/9","Fmaj7/G","Gsus4b9","G7(b9,13)","F#13#11b9","Db13#11b9","Cm7b5","CΔ9/E?"};
    for (const auto* name : examples) for (float scale : {0.4f,0.65f,1.f,1.9f}) {
        Probe c;
        const int width = chord::chordTextWidth(c,name,scale);
        chord::chordText(c,name,10,20,scale,0xFFFFFF,0);
        check(!c.nonAscii,"composed chord symbols support device bitmap fonts");
        check(c.minX >= 10 && c.maxX <= 10+width+2,"alterations and parentheses fit the measured width");
        check(c.minY >= 20 && c.maxY <= 20+std::ceil(49*scale)+2,"stacked alterations stay within the chord height");
    }
    for (const auto* name : {"Cmaj7","Cm(maj7)","C6/9","Dbmaj7","F#7"}) {
        Probe c;
        chord::chordText(c,name,0,0,1,0xFFFFFF,0);
        check(c.texts.size() == 1 && c.texts[0].value == name && c.texts[0].font == chord::Font::Chord && c.texts[0].scale == 1,
              "root accidentals, qualities and 6/9 stay full size: " + std::string(name));
    }
    Probe single;
    chord::chordText(single,"Gsus4b9",0,0,1.9f,0xFFFFFF,0);
    check(single.texts.size() == 2 && single.texts[0].value == "Gsus4" && single.texts[1].value == "b9" &&
          single.texts[0].font == chord::Font::Chord && single.texts[1].font == chord::Font::Body,"single alteration follows a full-size suspended name");
    Probe stacked;
    chord::chordText(stacked,"F#13#11b9",0,0,1.9f,0xFFFFFF,0);
    check(stacked.texts.size() == 3 && stacked.texts[0].value == "F#13" && stacked.texts[1].value == "#11" &&
          stacked.texts[2].value == "b9" && stacked.texts[2].y > stacked.texts[1].y,"multiple alterations stack without swallowing the root sharp");
    Probe bass;
    chord::chordText(bass,"Fmaj7/G",0,0,1,0xFFFFFF,0);
    check(bass.texts.size() == 2 && bass.texts[0].value == "Fmaj7" && bass.texts[1].value == "/G" &&
          bass.texts[1].font == chord::Font::Chord,"slash bass retains the reference's inline full-size style");
}
void staffEngraving() {
    chord::Snapshot boundary;
    boundary.chord = std::make_shared<ChordDetection::ChordCandidate>();
    boundary.chord->root = 6;
    boundary.notes = {59}; // Cb4 in a Gb chord, rather than B3.
    auto written = chord::layoutStaff(boundary,true);
    check(written.notes.size() == 1 && written.notes[0].treble && written.notes[0].y == 110 && written.notes[0].accidental == -1,
          "Cb4 uses the middle-C ledger position in treble clef");
    boundary.chord->root = 8;
    boundary.notes = {60}; // B#3 in a G# chord, rather than C4.
    written = chord::layoutStaff(boundary,false);
    check(written.notes.size() == 1 && !written.notes[0].treble && written.notes[0].y == 135 && written.notes[0].accidental == 1,
          "B#3 uses its written bass-clef position");
    // Dense pitch sets expose collisions that ordinary triads cannot reveal.
    for (bool flats : {false,true}) for (int start = 36; start <= 84; ++start) {
        chord::Snapshot frame;
        for (int pitch = start; pitch < start+12; ++pitch) frame.notes.push_back(pitch);
        const auto staff = chord::layoutStaff(frame,flats);
        check(staff.notes.size()+staff.outside == frame.notes.size(),"compact staff accounts for dense input");
        check(staff.left >= 36 && staff.right <= 310 && staff.right-staff.left < 270,"staff remains compact with dense accidentals");
        for (size_t a = 0; a < staff.notes.size(); ++a) {
            const auto& note = staff.notes[a];
            check(note.x-chord::noteheadRadius >= 80 && note.x+chord::noteheadRadius <= staff.right,"heads stay clear of clefs and staff edges");
            if (note.showAccidental) {
                const auto& glyph = chord::staffglyph::accidental(note.accidental);
                check(note.accidentalX+glyph.left >= 80 && note.y+glyph.top >= 34 &&
                      note.y+glyph.top+glyph.height <= 219,"complete accidental glyph stays inside staff viewport");
            }
            for (size_t b = a+1; b < staff.notes.size(); ++b) {
                const auto& other = staff.notes[b];
                if (note.treble != other.treble) continue;
                check(std::abs(note.x-other.x) >= 12 || std::abs(note.y-other.y) >= 10,"neighboring pitch heads never overlap");
                if (note.showAccidental && other.showAccidental) {
                    const auto& g = chord::staffglyph::accidental(note.accidental);
                    const auto& h = chord::staffglyph::accidental(other.accidental);
                    check(note.accidentalX+g.left+g.width <= other.accidentalX+h.left ||
                          other.accidentalX+h.left+h.width <= note.accidentalX+g.left ||
                          note.y+g.top+g.height <= other.y+h.top || other.y+h.top+h.height <= note.y+g.top,
                          "accidental columns do not collide");
                }
            }
        }
        for (const auto& stem : staff.stems) check(stem.top >= 35 && stem.bottom <= 214 && stem.top <= stem.bottom,"shared stems respect adjacent staves and labels");
        std::reverse(frame.notes.begin(),frame.notes.end());
        const auto reversed = chord::layoutStaff(frame,flats);
        for (const auto& note : staff.notes) {
            const auto found = std::find_if(reversed.notes.begin(),reversed.notes.end(),[&](const auto& n){return n.midi == note.midi;});
            check(found != reversed.notes.end() && found->x == note.x && found->y == note.y,"input ordering does not change notation placement");
        }
    }
}
}
int main() { formatting(); glyphs(); chordTypography(); staffEngraving(); std::cout << checks << " notation checks passed\n"; }
