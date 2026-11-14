#include "Display.h"
#include "Harmony.h"
#include "Notation.h"
#include "UiLayout.h"
#include "ModeOrder.h"
#include "MusicText.h"
#include "ChordText.h"
#include "NotationGlyphs.h"
#include <algorithm>
#include <cmath>

namespace chord {
namespace {
constexpr uint32_t bg = 0x0D141C, panel = 0x17232F, rule = 0x2D3C48;
constexpr uint32_t ink = 0xF2F4EC, muted = 0x91A5B5, accent = 0xD1F875;
constexpr uint32_t blue = 0x8FC8FF, amber = 0xF7C77A;
constexpr uint32_t chromatic = 0xF29DAC, sustained = 0x526D91;

void label(Canvas& c, const std::string& value, int x, int y, uint32_t color = muted) {
    c.text(value, x, y, Font::Label, 1, color);
}
void fitted(Canvas& c, const std::string& value, int x, int y, int width,
            Font font, float scale, uint32_t color = ink, bool centered = false) {
    while (c.textWidth(value, font, scale) > width && scale > 0.3f) scale -= 0.025f;
    const int offset = centered ? (width - c.textWidth(value, font, scale)) / 2 : 0;
    c.text(value, x + offset, y, font, scale, color);
}
void fittedMusic(Canvas& c, const std::string& value, int x, int y, int width,
                 Font font, float scale, uint32_t color = ink, bool centered = false, uint32_t background = bg) {
    while (musicTextWidth(c,value,font,scale) > width && scale > 0.3f) scale -= 0.025f;
    const int offset = centered ? (width - musicTextWidth(c,value,font,scale)) / 2 : 0;
    musicText(c,value,x+offset,y,font,scale,color,background);
}
void fittedChord(Canvas& c, const std::string& value, int x, int y, int width,
                 float scale, uint32_t color = ink, bool centered = false, uint32_t background = bg, bool roman = false) {
    while (chordTextWidth(c,value,scale,roman) > width && scale > 0.3f) scale -= 0.025f;
    const int offset = centered ? (width-chordTextWidth(c,value,scale,roman))/2 : 0;
    chordText(c,value,x+offset,y,scale,color,background,roman);
}
void statusLight(Canvas& c, const DisplayStatus& status) {
    const auto color = status.demo ? blue : status.connected ? accent : amber;
    c.ellipse(314, 10, status.midiActive ? 3 : 2, status.midiActive ? 3 : 2, color);
    if (status.frozen) label(c, "HOLD", 280, 20, amber);
    else if (!status.connected && !status.demo) label(c, "PAIRING", 268, 20, amber);
    else if (status.demo) label(c, "DEMO", 280, 20, blue);
}
void context(Canvas& c, const Settings& settings, bool large = false) {
    const int height = large ? 40 : 27;
    c.rect(6, 2, 308, height, rule, 4);
    c.rect(7, 3, 306, height - 2, panel, 4);
    fitted(c, keyName(settings), 14, large ? 10 : 5, large ? 207 : 268, Font::Body, 1.2f, blue);
    if (large) label(c, "KEY / MODE", 230, 16, ink);
    fitted(c, ">", 298, large ? 12 : 7, 10, Font::Body, 1, blue);
}
void footer(Canvas& c, const Settings& settings, const DisplayStatus& status) {
    c.line(0, ui::footerY, 319, ui::footerY, rule);
    const char* tabs[] = {"CHORD", "HARMONY", "STAFF"};
    const char* menu[] = {"NEXT", "CHANGE", "DONE"};
    const char* picker[] = {"TONIC +", "MODE +", "DONE"};
    const Mode modes[] = {Mode::Chord, Mode::Harmony, Mode::Notation};
    for (int i = 0; i < 3; ++i) {
        const int left = i * 107;
        const bool active = !status.settingsOpen && !status.keyPickerOpen && settings.mode == modes[i];
        if (active) c.rect(left + 8, ui::footerY + 1, 91, 2, accent);
        const auto* name = status.keyPickerOpen ? picker[i] : status.settingsOpen ? menu[i] : tabs[i];
        fitted(c, name, left + 4, 223, 99, Font::Body, 1, active ? accent : muted, true);
    }
}
void intervalChips(Canvas& c, const Snapshot& snapshot, const Settings& settings) {
    std::vector<int> pcs;
    for (int note : snapshot.notes) if (std::find(pcs.begin(), pcs.end(), note % 12) == pcs.end()) pcs.push_back(note % 12);
    if (pcs.empty()) return;
    const int stride = std::min(49,304 / int(pcs.size()));
    const int left = (320 - int(pcs.size()) * stride) / 2;
    for (size_t i = 0; i < pcs.size(); ++i) {
        const int x = left + int(i) * stride, y = 191;
        c.rect(x,y,stride-3,28,panel,3);
        const auto name = spelledName(60 + pcs[i],snapshot.chord.get(),settings.flats,false);
        const auto degree = snapshot.chord ? degreeName(pcs[i] - snapshot.chord->root, snapshot.chord.get()) : "";
        fitted(c,name,x+2,y,stride-7,Font::Body,1,ink,true);
        fitted(c,degree,x+2,y+16,stride-7,Font::Label,1.5f,accent,true);
    }
}
void chordView(Canvas& c, const Snapshot& snapshot, const Settings& settings) {
    context(c, settings);
    fittedChord(c, chordName(snapshot, settings), 8, 33, 304, 1.9f, ink, true);
    const auto harmony = analyzeHarmony(snapshot, settings);
    if (!harmony.shortRole.empty()) {
        c.rect(8,125,304,48,panel,5);
        const auto roman = romanName(harmony.roman,settings);
        const auto slash = roman.find('/');
        const auto color = harmony.uncertain ? amber : accent;
        if (slash != std::string::npos) {
            fittedChord(c,roman.substr(0,slash),14,125,112,0.7f,color,false,panel,true);
            fittedMusic(c,roman.substr(slash),14,155,112,Font::Body,1,color,false,panel);
        } else fittedChord(c,roman,14,128,112,0.88f,color,false,panel,true);
        fitted(c,harmony.shortRole,136,127,170,Font::Body,1.5f,ink);
        fitted(c,harmony.detail,136,153,170,Font::Body,1,muted);
    }
    std::string detail = snapshot.chord ? snapshot.chord->position + " / " + voicingName(snapshot) : "";
    if (snapshot.chord && snapshot.chord->voicingType == ChordDetection::VoicingType::Rootless)
        detail = "Rootless / root inferred";
    if (snapshot.pedal) detail += detail.empty() ? "PEDAL" : " / PEDAL";
    const auto alternatives = alternativeNames(snapshot, settings,1);
    if (!alternatives.empty()) {
        const bool rootless = snapshot.chord && snapshot.chord->voicingType == ChordDetection::VoicingType::Rootless;
        detail = std::string(rootless ? "Rootless / ALSO " : "ALSO ")+alternatives;
        if (snapshot.pedal) detail += " / PEDAL";
    }
    fittedMusic(c,detail,8,174,304,Font::Body,1,alternatives.empty() ? muted : amber,true);
    intervalChips(c, snapshot, settings);
}
void harmonyView(Canvas& c, const Snapshot& snapshot, const Settings& settings, const DisplayStatus& status) {
    context(c, settings, true);
    const auto harmony = analyzeHarmony(snapshot, settings);
    const auto alternatives = harmony.quartal || harmony.quintal ? "" : alternativeNames(snapshot, settings);
    fittedChord(c, harmony.quintal ? "Quintal" : harmony.quartal ? "Quartal" : chordName(snapshot, settings), 8, 45, 210,
                alternatives.empty() ? 0.9f : 0.7f);
    if (harmony.roman != "--") {
        c.rect(229, 49, 83, 32, panel, 4);
        fittedMusic(c,romanName(harmony.roman,settings),232,56,77,Font::Body,1.25f,harmony.uncertain ? amber : accent,true,panel);
    }
    if (!alternatives.empty()) fittedMusic(c,"ALSO " + alternatives,8,82,304,Font::Label,1,amber);
    fitted(c, harmony.role, 8, 92, 304, Font::Body, 1, harmony.uncertain ? amber : ink);
    for (int i = 0; i < 7; ++i) {
        const auto& tone = harmony.tones[i];
        const int x = 8 + i * 44;
        c.rect(x, 112, 40, 47, tone.played ? accent : panel, 3);
        if (tone.played) c.rect(x + 1, 113, 38, 45, 0x263A2B, 3);
        fitted(c, tone.note, x + 2, 119, 36, Font::Body, 1, tone.played ? ink : muted, true);
        fitted(c, tone.degree, x + 2, 142, 36, Font::Label, 1, tone.color ? blue : i == 0 ? accent : muted, true);
        if (tone.color) c.rect(x + 9, 156, 22, 2, blue);
    }
    fitted(c, harmony.colorPrompt, 8, 167, 304, Font::Label, 1, blue);
    if (!harmony.outsideNotes.empty()) {
        fitted(c, "OUTSIDE MODE: " + harmony.outsideNotes, 8, 180, 304, Font::Label, 1, amber);
        fitted(c, status.motion.empty() ? harmony.exercise : status.motion, 8, 193, 304, Font::Label, 1, muted);
    } else {
        fitted(c, status.motion.empty() ? harmony.exercise : status.motion, 8, 182, 304, Font::Label, 1, muted);
    }
    if (!status.trail.empty()) fittedMusic(c,romanName(status.trail,settings),8,207,304,Font::Label,1,accent);
}
void staffGlyph(Canvas& c, const staffglyph::Glyph& glyph, int x, int y, uint32_t color) {
    for (int row = 0; row < glyph.height; ++row) {
        int start = -1;
        for (int column = 0; column <= glyph.width; ++column) {
            const bool on = column < glyph.width && (glyph.rows[row] & (uint32_t(1) << column));
            if (on && start < 0) start = column;
            if (!on && start >= 0) {
                c.line(x+glyph.left+start,y+glyph.top+row,x+glyph.left+column-1,y+glyph.top+row,color);
                start = -1;
            }
        }
    }
}
void notationView(Canvas& c, const Snapshot& snapshot, const Settings& settings) {
    fittedChord(c,chordName(snapshot,settings),8,2,295,0.65f);
    const auto staff = layoutStaff(snapshot, settings.flats);
    for (int bottom : {trebleBottom, bassBottom})
        for (int i = 0; i < 5; ++i) c.line(staff.left,bottom-i*staffSpacing,staff.right,bottom-i*staffSpacing,0x687F91);
    c.line(staff.left,trebleBottom-4*staffSpacing,staff.left,bassBottom,muted);
    staffGlyph(c,staffglyph::trebleClef,50,trebleBottom-staffSpacing,ink);
    staffGlyph(c,staffglyph::bassClef,50,bassBottom-3*staffSpacing,ink);
    if (staff.trebleOctaves) fitted(c,staff.trebleOctaves == 1 ? "8va" : "15ma",83,38,52,Font::Body,1,blue);
    if (staff.bassOctaves) fitted(c,staff.bassOctaves == -1 ? "8vb" : "15mb",83,201,52,Font::Body,1,blue);
    for (const auto& note : staff.notes) {
        for (int y : note.ledgerLines) c.line(note.x-9,y,note.x+9,y,muted);
    }
    for (const auto& stem : staff.stems) c.line(stem.x,stem.top,stem.x,stem.bottom,stem.pressed ? accent : blue);
    for (const auto& note : staff.notes) {
        const auto color = note.pressed ? accent : blue;
        for (const auto& stem : staff.stems) if (stem.treble == note.treble && std::abs(note.x-stem.x) > noteheadRadius) {
            const int y = note.y+(note.x > stem.x ? 2 : -2);
            c.line(stem.x,y,note.x+(note.x > stem.x ? -noteheadRadius : noteheadRadius-1),y,color);
        }
        staffGlyph(c,staffglyph::notehead,note.x-noteheadRadius,note.y,color);
        if (note.showAccidental) staffGlyph(c,staffglyph::accidental(note.accidental),note.accidentalX,note.y,color);
    }
    if (staff.outside) label(c, "+" + std::to_string(staff.outside) + " OUTSIDE", 243, 207, amber);
    if (snapshot.pedal) label(c, "PEDAL", 279, 38, blue);
}
void menu(Canvas& c, const Settings& settings, const DisplayStatus& status) {
    fitted(c, "Settings", 10, 7, 250, Font::Body, 1.4f);
    const char* bluetoothNames[] = {"Auto", "WIDI group", "Direct WIDI"};
    const std::string values[] = {settings.channel == 0 ? "Omni" : "Ch " + std::to_string(settings.channel),
        settings.sustain ? "Follow CC64" : "Keys only", settings.flats ? "Flats" : "Sharps",
        std::to_string(int(std::round(settings.brightness * 100.0 / 255))) + "%", status.demo ? "On" : "Off",
        bluetoothNames[static_cast<unsigned>(settings.bluetooth)], keyName(settings) + " >",
        settings.notation == ChordNotation::Jazz ? "Jazz" : "Standard"};
    const char* names[] = {"MIDI channel", "Sustain", "Note names", "Brightness", "Demo", "Bluetooth", "Key / mode", "Chord symbols"};
    for (int i = 0; i < ui::settingCount; ++i) {
        const int y = ui::settingY + i * ui::settingHeight;
        const bool active = status.selectedSetting == i;
        if (active) c.rect(8, y - 1, 304, ui::settingHeight - 1, panel, 3);
        fitted(c, names[i], 12, y + 2, 137, Font::Body, 1, active ? ink : muted);
        fitted(c, values[i], 152, y + 2, 156, Font::Body, 1, active ? accent : muted);
    }
    label(c, status.address, 10, 209);
    if (status.dropped || status.malformed)
        fitted(c, "DROP " + std::to_string(status.dropped) + " BAD " + std::to_string(status.malformed), 143, 209, 164, Font::Label, 1, amber);
    else if (status.battery >= 0) label(c, "BAT " + std::to_string(status.battery) + "%", 254, 209);
}
void keyPicker(Canvas& c, const Snapshot& snapshot, const Settings& settings) {
    fitted(c, keyName(settings), 8, 6, 296, Font::Body, 1.4f, ink);
    label(c, "TAP A KEY FOR TONIC", 8, 31);
    label(c, "TONIC", 178, 31, accent);
    label(c, "MODE", 222, 31, blue);
    label(c, "COLOR", 269, 31, amber);
    const auto reference = analyzeReference(snapshot, settings);
    const auto keyboard = ui::pianoLayout(settings.tonic);
    for (const auto& key : keyboard) {
        const int height = key.black ? ui::blackKeyHeight : ui::keyboardHeight;
        if (!key.inRange()) {
            c.rect(key.x, ui::keyboardY, key.width - 1, height, key.black ? bg : 0x566470, 2);
            continue;
        }
        const auto& note = reference.notes[key.pitch];
        const auto base = key.pitch == settings.tonic ? accent : note.color ? amber : note.inScale ? blue : key.black ? panel : 0xB6C1CC;
        const auto fill = note.held ? ink : note.sustained ? sustained : base;
        const auto textColor = note.held ? bg : note.sustained ? ink : note.inScale || !key.black ? bg : muted;
        c.rect(key.x, ui::keyboardY, key.width - 1, height, fill, 2);
        const auto name = note.inScale || note.sounding() ? note.name : pitchName(key.pitch, settings.flats);
        fitted(c, name, key.x + 2, ui::keyboardY + (key.black ? 5 : height - 30), key.width - 5,
               key.black ? Font::Label : Font::Body, 1, textColor, true);
        if (note.inScale || note.sounding())
            fitted(c, key.offset == 12 ? "8" : note.degree, key.x + 2, ui::keyboardY + height - (key.black ? 15 : 13), key.width - 5,
                   Font::Label, 1, textColor, true);
        if (note.sounding()) c.rect(key.x + 2, ui::keyboardY + height - 4, key.width - 5, 2, note.inScale ? base : chromatic);
    }
    if (reference.active.empty()) label(c, "BRIGHT -> DARK", 8, 121);
    else {
        bool outside = false, held = false;
        for (int pitch : reference.active) { outside |= !reference.notes[pitch].inScale; held |= reference.notes[pitch].held; }
        fitted(c, reference.readout, 8, reference.active.size() > 4 ? 121 : 117, 304,
               reference.active.size() > 4 ? Font::Label : Font::Body, 1, outside ? chromatic : held ? ink : blue);
    }
    for (int i = 0; i < static_cast<int>(ScaleMode::Count); ++i) {
        const auto mode = modesByBrightness[i];
        const int x = 8 + (i % 4) * 77, y = 136 + (i / 4) * 32;
        const bool active = settings.scale == mode;
        c.rect(x, y, 73, 28, active ? blue : panel, 3);
        fitted(c, scaleDefinition(mode).shortName, x + 2, y + 6, 69, Font::Body, 1, active ? bg : ink, true);
    }
    std::string formula;
    for (const auto* degree : scaleDefinition(settings.scale).degrees) {
        if (!formula.empty()) formula += " ";
        formula += degree;
    }
    fitted(c, formula, 8, 201, 304, Font::Label, 1, blue, true);
}
}

void render(Canvas& canvas, const Snapshot& snapshot, const Settings& settings, const DisplayStatus& status) {
    canvas.rect(0, 0, 320, 240, bg);
    if (status.keyPickerOpen) keyPicker(canvas, snapshot, settings);
    else if (status.settingsOpen) menu(canvas, settings, status);
    else if (settings.mode == Mode::Harmony) harmonyView(canvas, snapshot, settings, status);
    else if (settings.mode == Mode::Notation) notationView(canvas, snapshot, settings);
    else chordView(canvas, snapshot, settings);
    statusLight(canvas, status);
    if (status.overrun) {
        canvas.rect(8, 203, 304, 16, bg);
        label(canvas, "INPUT OVERRUN - NOTES CLEARED", 8, 207, amber);
    }
    footer(canvas, settings, status);
}

} // namespace chord
