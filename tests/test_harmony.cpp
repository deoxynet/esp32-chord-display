#include "Harmony.h"
#include "Notation.h"
#include "UiLayout.h"
#include "Navigation.h"
#include <cstdlib>
#include <iostream>

namespace {
int checks = 0;
void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
chord::Snapshot snapshot(std::initializer_list<int> notes) {
    chord::MidiState state;
    ChordDetection::ChordDetector detector;
    for (int note : notes) state.receive({0x90, static_cast<uint8_t>(note), 100});
    return chord::analyze(state, detector);
}
void modalTests() {
    using chord::ScaleMode;
    chord::Settings s;
    auto home = chord::analyzeHarmony(snapshot({50,53,57,60}), s);
    check(home.roman == "i7" && home.role == "Modal center / home" && home.shortRole == "TONIC", "D Dorian treats Dm7 as home, not ii of C");
    check(home.inScale && !home.tones[5].played && home.colorPrompt == "COLOR 6 = B / try it", "Dorian teaches the natural sixth");
    auto color = chord::analyzeHarmony(snapshot({55,59,62,65}), s);
    check(color.roman == "IV7" && !color.uncertain && color.inScale, "Dorian IV7 is modal color, not automatically V of relative major");
    check(color.tones[5].played && color.colorPrompt == "COLOR 6 = B / heard", "color tone feedback follows actual pitches");
    auto pull = chord::analyzeHarmony(snapshot({57,61,64,67}), s);
    check(pull.roman == "V7?" && pull.uncertain && !pull.inScale, "raised-leading-tone A7 is a possible tonal pull to D");
    check(chord::analyzeHarmony(snapshot({51,55,58,61}), s).roman == "subV7/i?", "tritone substitute names a minor home with lowercase i");
    auto fourths = chord::analyzeHarmony(snapshot({50,55,60,65}), s);
    check(fourths.quartal && fourths.roman == "4ths" && fourths.uncertain, "stacked fourths do not assert a unique functional root");
    check(chord::analyzeHarmony(snapshot({50}), s).roman == "--", "single notes aren't assigned harmonic function");
    const auto idle = chord::analyzeHarmony({}, s);
    check(idle.playedMask == 0 && idle.role.empty(), "silence has no invented tones or idle coaching message");
    chord::Snapshot unmatched; unmatched.notes = {60,61};
    check(chord::chordName(unmatched, s) == "--", "unrecognized sets use a quiet dash placeholder");

    s.tonic = 0; s.scale = ScaleMode::Ionian;
    auto applied = chord::analyzeHarmony(snapshot({50,54,57,60}), s);
    check(applied.roman == "V7/V?" && applied.role.find("Applied dominant?") == 0, "D7 in C major can tonicize G");
    check(chord::analyzeHarmony(snapshot({49,53,56,59}), s).roman == "subV7/I?", "Db7 can be a tritone substitute in C");
    check(chord::analyzeHarmony(snapshot({53,56,60,63}), s).role == "Borrowed? / parallel Aeolian", "Fm7 suggests parallel-minor borrowing");
    check(chord::analyzeHarmony(snapshot({58,62,65,68}), s).role == "Backdoor color? / bVII7 to I", "Bb7 backdoor reading remains qualified");

    s.tonic = 6; s.scale = ScaleMode::Lydian;
    check(chord::scaleNoteName(s, 6) == "E#" && chord::scaleNoteName(s, 3) == "B#", "F# Lydian spells scale degrees by letter");
    s.flats = true; s.tonic = 1; s.scale = ScaleMode::Dorian;
    check(chord::scaleNoteName(s, 6) == "Cb", "Db Dorian uses Cb, not B");
    s.tonic = 11; s.flats = false; s.scale = ScaleMode::Locrian;
    check(chord::analyzeHarmony(snapshot({65,69,72}), s).roman == "bV", "Locrian lowered fifth isn't mislabeled #IV");
    s.tonic = 2; s.scale = ScaleMode::MelodicMinor;
    check(chord::analyzeHarmony(snapshot({50,53,57,61}), s).roman == "i(maj7)", "jazz minor tonic retains major seventh");

    const int majorSteps[] = {0,2,4,5,7,9,11};
    const int qualities[][4] = {{0,4,7,11},{0,3,7,10},{0,3,7,10},{0,4,7,11},{0,4,7,10},{0,3,7,10},{0,3,6,10}};
    const char* romans[] = {"Imaj7","ii7","iii7","IVmaj7","V7","vi7","vii7b5"};
    for (int tonic = 0; tonic < 12; ++tonic) {
        s.tonic = tonic; s.scale = ScaleMode::Ionian;
        for (int degree = 0; degree < 7; ++degree) {
            const int root = 60 + (tonic + majorSteps[degree]) % 12;
            const auto& q = qualities[degree];
            auto h = chord::analyzeHarmony(snapshot({root,root+q[1],root+q[2],root+q[3]}), s);
            check(h.roman == romans[degree] && h.inScale && !h.uncertain, "diatonic sevenths in all twelve major keys: " + std::to_string(tonic) + " " + romans[degree] + " got " + h.roman);
        }
        for (int mode = 0; mode < int(ScaleMode::Count); ++mode) {
            s.scale = static_cast<ScaleMode>(mode);
            auto h = chord::analyzeHarmony({}, s);
            int mask = 0;
            for (const auto& tone : h.tones) mask |= 1 << tone.pitch;
            check(__builtin_popcount(unsigned(mask)) == 7, "every selectable transposition has seven distinct tones");
            check(h.tones[0].pitch == tonic, "mode always retains the selected tonic");
        }
    }
}

void trailTests() {
    chord::Settings s; s.tonic = 0; s.scale = chord::ScaleMode::Ionian;
    chord::HarmonyTrail trail;
    auto dm = snapshot({50,53,57,60}), g = snapshot({55,59,62,65}), c = snapshot({48,52,55,59});
    check(!trail.observe(dm,s,100) && !trail.observe(dm,s,279), "rolled or brief voicings don't immediately enter history");
    check(trail.observe(dm,s,280) && trail.text() == "ii7", "stable chord enters trail");
    trail.observe({},s,400);
    check(trail.size() == 1, "releasing keys preserves the phrase");
    auto inverted = dm;
    inverted.notes = {53,57,60,62}; // Keep the identified harmony; F6/Dm7 itself is ambiguous to the detector.
    trail.observe(inverted,s,500);
    trail.observe(inverted,s,800);
    check(trail.size() == 1, "inversion of the same chord isn't a new harmony");
    trail.observe(g,s,900); trail.observe(g,s,1080);
    trail.observe(c,s,1200); trail.observe(c,s,1380);
    check(trail.text() == "ii7 > V7 > Imaj7" && trail.motion() == "ii - V - I / resolved", "real ii-V-I gets contextual feedback");
    trail.clear();
    auto d7 = snapshot({50,54,57,60});
    trail.observe(d7,s,1400); trail.observe(d7,s,1580);
    trail.observe(g,s,1600); trail.observe(g,s,1780);
    trail.observe(c,s,1800); trail.observe(c,s,1980);
    check(trail.motion().empty(), "V/V-V-I isn't mislabeled ii-V-I");
    s.tonic = 2;
    trail.observe({},s,2000);
    check(trail.size() == 0, "changing the reference clears old Roman numerals");
    trail.observe(dm,s,0xFFFFFF80U);
    check(trail.observe(dm,s,100), "trail settling survives millis rollover");
    for (int i = 0; i < 8; ++i) {
        const auto& snap = i % 2 ? g : dm;
        trail.observe(snap,s,500 + i*300); trail.observe(snap,s,680 + i*300);
    }
    check(trail.size() == 4, "history stays bounded");
}

void layoutTests() {
    using namespace chord::ui;
    int reachable = 0;
    const auto keyboard = pianoLayout(0);
    for (const auto& key : keyboard) {
        if (!key.inRange()) continue;
        const int y = keyboardY + (key.black ? 20 : 60);
        check(tonicAt(key.x + key.width/2, y,0) == key.pitch, "reference keyboard selects every white and black tonic");
        reachable |= 1 << key.pitch;
    }
    check(reachable == 0xFFF, "all twelve tonics are reachable from the piano");
    check(tonicAt(40,50,0) == 1 && tonicAt(40,100,0) == 0, "black key takes priority only within its actual height");
    check(tonicAt(80,50,0) == 3 && tonicAt(80,100,0) == 2, "D sharp and the exposed part of D select different tonics");
    for (int i = 0; i < 8; ++i) check(scaleAt(44 + i%4*77,150 + i/4*32) == i, "every mode is touch-selectable");
    check(tonicAt(-1,50,0) == -1 && tonicAt(312,50,0) == -1 && tonicAt(20,113,0) == -1, "screen edges and space below the piano don't change key");
    check(scaleAt(82,141) == -1 && scaleAt(20,196) == -1, "mode gaps don't select neighboring modes");
    check(keyButtonAt(chord::Mode::Harmony,0,43) && keyButtonAt(chord::Mode::Harmony,319,43), "the entire Harmony header is a generous key/mode target");
    check(!keyButtonAt(chord::Mode::Harmony,100,44) && !keyButtonAt(chord::Mode::Harmony,100,-1), "header touch doesn't overlap the chord below");
    check(keyButtonAt(chord::Mode::Chord,319,31) && !keyButtonAt(chord::Mode::Chord,100,32), "Chord header uses its full width");
    check(!keyButtonAt(chord::Mode::Notation,100,10), "touching notation's chord name doesn't unexpectedly change key");
    check(tabAt(319,239) == 2 && tabAt(320,239) == -1 && tabAt(30,219) == -1, "footer respects screen and staff bounds");
    check(settingAt(160,170) == 6 && settingAt(160,190) == 7 && settingAt(160,202) == -1, "key and notation settings fit above the footer");
    auto staff = chord::layoutStaff(snapshot({36,48,60,64,67,71}), false);
    check(chord::staffSpacing == 10 && staff.notes.size() == 6 && staff.outside == 0, "large staff retains ordinary two-handed piano register");
    check(staff.notes[2].y == 110 && staff.notes[3].y == 100, "middle C and E have full ten-pixel staff spacing");
    for (int note = 0; note < 128; ++note) {
        chord::Snapshot single; single.notes = {note};
        auto layout = chord::layoutStaff(single, false);
        check(layout.notes.size() + layout.outside == 1, "enlarged notation accounts for every MIDI pitch");
        for (const auto& head : layout.notes) check(head.y >= 40 && head.y <= 211, "large noteheads stay clear of tabs");
    }
}

void scrollingKeyboardTests() {
    using namespace chord::ui;
    for (int tonic = 0; tonic < 12; ++tonic) {
        const auto keyboard = pianoLayout(static_cast<uint8_t>(tonic));
        check(keyboard.count <= 15, "scrolled keyboard uses bounded visible geometry");
        auto center = [&](int offset) {
            for (const auto& key : keyboard) if (key.offset == offset) return key.x + key.width/2;
            return -1;
        };
        check(center(0) == tonicCenter && center(12) == octaveCenter, "all white and black tonics share the same two anchors");
        int reachable = 0, rangeCount = 0, previousWhite = -1;
        bool sawBlack = false;
        for (const auto& key : keyboard) {
            check(key.x >= keyboardLeft && key.x + key.width <= keyboardRight, "edge pieces are clipped to the piano viewport");
            if (key.black) sawBlack = true;
            else check(!sawBlack, "black keys overlay white keys consistently");
            const int x = key.x + key.width/2, y = keyboardY + (key.black ? 20 : 60);
            if (!key.inRange()) {
                check(tonicAt(x,y,tonic) == -1, "muted edge fragments never select a different tonic");
                continue;
            }
            ++rangeCount;
            reachable |= 1 << key.pitch;
            check(key.pitch == (tonic + key.offset) % 12, "scrolling keeps each label attached to the actual piano pitch");
            check(tonicAt(x,y,tonic) == key.pitch, "white and black touch targets follow the scrolled keyboard");
            if (key.black) check(key.width == 24, "black keys retain their natural width");
            else {
                check(key.width == 38 && (previousWhite < 0 || x - previousWhite == 38), "white-key spacing is preserved across semitone boundaries");
                previousWhite = x;
            }
        }
        check(rangeCount == 13 && reachable == 0xFFF, "tonic through octave includes all chromatic pitch classes");
        int previous = -1;
        for (int offset = 0; offset <= 12; ++offset) {
            check(center(offset) > previous, "chromatic notes ascend left to right without wrapping");
            previous = center(offset);
        }
        for (int mode = 0; mode < int(chord::ScaleMode::Count); ++mode) {
            previous = -1;
            for (int interval : chord::scaleDefinition(static_cast<chord::ScaleMode>(mode)).intervals) {
                check(center(interval) > previous && center(interval) < octaveCenter, "degrees 1 through 7 read left to right in every key and mode");
                previous = center(interval);
            }
        }
    }
}

void gestureTests() {
    using namespace chord::ui;
    check(swipeAt(240,100,90,110) == Swipe::Left, "left swipe tolerates normal vertical drift");
    check(swipeAt(90,100,240,90) == Swipe::Right, "right swipe direction");
    check(swipeAt(150,180,160,60) == Swipe::Up, "up swipe opens the key sheet");
    check(swipeAt(150,60,140,210) == Swipe::Down, "down swipe direction");
    check(swipeAt(150,60,140,260) == Swipe::Down, "down swipe can finish over the Core2 bezel");
    check(swipeAt(150,100,158,105) == Swipe::None, "tap jitter isn't navigation");
    check(swipeAt(240,160,180,100) == Swipe::None, "ambiguous diagonals don't change a page or key");
    check(swipeAt(240,230,80,230) == Swipe::None, "footer sliding doesn't also navigate");
    check(swipeAt(10,260,80,100) == Swipe::None, "bezel button motion isn't a screen swipe");
    check(swipeAt(150,100,320,110) == Swipe::None, "invalid touch coordinates are rejected");
    check(gestureAction(Swipe::Left, Layer::Playing) == GestureAction::NextView &&
          gestureAction(Swipe::Right, Layer::Playing) == GestureAction::PreviousView, "horizontal swipes navigate playing views");
    check(gestureAction(Swipe::Up, Layer::Playing) == GestureAction::OpenKeyPicker &&
          gestureAction(Swipe::Down, Layer::Playing) == GestureAction::OpenSettings, "key picker and settings are accessible from every playing view");
    check(gestureAction(Swipe::Left, Layer::KeyPicker) == GestureAction::NextScale &&
          gestureAction(Swipe::Right, Layer::KeyPicker) == GestureAction::PreviousScale, "picker compares modes without navigating away");
    check(gestureAction(Swipe::Down, Layer::KeyPicker) == GestureAction::ClosePanel &&
          gestureAction(Swipe::Down, Layer::Settings) == GestureAction::ClosePanel, "down dismisses either sheet");
    check(gestureAction(Swipe::Left, Layer::Settings) == GestureAction::None &&
          gestureAction(Swipe::Right, Layer::Settings) == GestureAction::None, "settings swipes cannot accidentally change MIDI or Bluetooth");
    check(steppedView(chord::Mode::Chord,1) == 1 && steppedView(chord::Mode::Harmony,1) == 2 &&
          steppedView(chord::Mode::Notation,1) == 0, "view swipes use visual order, not saved enum values");
    check(steppedView(chord::Mode::Chord,-1) == 2 && steppedView(chord::Mode::Notation,-1) == 1, "backward views wrap consistently");
    check(steppedScale(chord::ScaleMode::Locrian,1) == chord::ScaleMode::Lydian &&
          steppedScale(chord::ScaleMode::Lydian,-1) == chord::ScaleMode::Locrian, "brightness navigation wraps in both directions");
}

void brightnessTests() {
    using chord::ScaleMode;
    const ScaleMode expected[] = {ScaleMode::Lydian,ScaleMode::Ionian,ScaleMode::Mixolydian,ScaleMode::MelodicMinor,
        ScaleMode::Dorian,ScaleMode::Aeolian,ScaleMode::Phrygian,ScaleMode::Locrian};
    int seen = 0, previous = 100;
    for (int i = 0; i < 8; ++i) {
        const auto mode = chord::modesByBrightness[i];
        check(mode == expected[i] && chord::brightnessIndex(mode) == i, "picker and brightness index agree on the intended order");
        seen |= 1 << static_cast<int>(mode);
        int sum = 0;
        for (int interval : chord::scaleDefinition(mode).intervals) sum += interval;
        check(sum <= previous, "bright-to-dark order never raises the aggregate scale degrees");
        previous = sum;
        check(chord::ui::steppedScale(mode,1) == expected[(i+1)%8] &&
              chord::ui::steppedScale(mode,-1) == expected[(i+7)%8], "both swipe directions and Mode+ follow the button order");
        const int touched = chord::ui::scaleAt(44 + i%4*77,150 + i/4*32);
        check(touched == i && chord::modesByBrightness[touched] == mode, "touching a reordered card chooses its displayed mode");
    }
    check(seen == 0xFF, "every mode appears exactly once");
    check(static_cast<int>(ScaleMode::Ionian) == 0 && static_cast<int>(ScaleMode::Dorian) == 1 &&
          static_cast<int>(ScaleMode::Lydian) == 3 && static_cast<int>(ScaleMode::MelodicMinor) == 7,
          "saved mode IDs retain their original meaning after presentation reordering");
}

void referenceTests() {
    chord::Settings s; s.tonic = 0; s.scale = chord::ScaleMode::Ionian;
    chord::MidiState midi;
    chord::Settler settler; settler.changed(100);
    midi.receive({0x90,60,100});
    auto captured = chord::captureNotes(midi);
    auto r = chord::analyzeReference(captured,s);
    check(!settler.due(101) && !captured.chord && r.notes[0].held, "reference responds to note-on before chord settling or detection");
    check(r.readout == "C = 1 / Tonic / home", "single tonic note explains its degree and role");
    midi.receive({0x90,64,100}); midi.receive({0x90,67,100});
    r = chord::analyzeReference(chord::captureNotes(midi),s);
    check(r.active == std::vector<int>({0,4,7}) && r.readout == "C:1  E:3  G:5", "polyphonic notes each have a functional degree");
    midi.receive({0xB0,64,127});
    midi.receive({0x80,60,0}); midi.receive({0x80,64,0}); midi.receive({0x80,67,0});
    r = chord::analyzeReference(chord::captureNotes(midi),s);
    check(r.notes[0].sustained && !r.notes[0].held && r.notes[4].sustained, "pedal-sustained notes retain separate display state");
    midi.receive({0x90,76,90});
    r = chord::analyzeReference(chord::captureNotes(midi),s);
    check(r.notes[4].held && r.notes[4].sustained && r.active.size() == 3, "different octaves of one class merge without losing held/pedal state");
    midi.receive({0xB0,64,0});
    r = chord::analyzeReference(chord::captureNotes(midi),s);
    check(r.active == std::vector<int>({4}) && r.notes[4].held && !r.notes[4].sustained, "pedal lift clears deferred highlights but preserves a held octave");
    check(r.readout == "E = 3 / Major third", "a single third doesn't need a detected chord");
    midi.receive({0x80,76,0});
    check(chord::analyzeReference(chord::captureNotes(midi),s).active.empty(), "last note-off immediately clears the reference");
    midi.receive({0x91,67,90}); midi.setChannel(1);
    check(chord::analyzeReference(chord::captureNotes(midi),s).active.empty(), "reference follows the selected MIDI channel");

    s.tonic = 2; s.scale = chord::ScaleMode::Dorian;
    chord::Snapshot single; single.notes = {59}; single.pressed = {true};
    r = chord::analyzeReference(single,s);
    check(r.readout == "B = 6 / Dorian color" && r.notes[11].color, "Dorian natural sixth gets its modal function");
    single.notes = {61};
    r = chord::analyzeReference(single,s);
    check(r.readout == "C# = 7 / Outside Dorian" && !r.notes[1].inScale, "chromatic input still has a degree and consistent spelling");
    check(s.tonic == 2 && s.scale == chord::ScaleMode::Dorian, "playing the hardware keyboard never retunes the selected reference");
    single.notes = {70}; single.pressed = {false};
    r = chord::analyzeReference(single,s);
    check(r.readout == "Bb = b6 / Outside Dorian / pedal", "outside sustained notes explain the altered degree");
    single.notes = {68}; single.pressed = {true};
    r = chord::analyzeReference(single,s);
    check(r.notes[8].name == "Ab" && r.notes[8].degree == "b5", "minor chromatic fifth is spelled consistently");
    s.tonic = 5; s.scale = chord::ScaleMode::Lydian; single.notes = {59};
    check(chord::analyzeReference(single,s).readout == "B = #4 / Lydian color", "Lydian raised fourth is relative to the selected tonic");
    s.tonic = 1; s.scale = chord::ScaleMode::Ionian; single.notes = {60};
    check(chord::analyzeReference(single,s).readout == "B# = 7 / Leading tone", "enharmonic scale note names retain their function");

    chord::Snapshot dense;
    for (int i = 60; i < 72; ++i) dense.notes.push_back(i);
    r = chord::analyzeReference(dense,s);
    check(r.active.size() == 12 && r.readout.find("+6") != std::string::npos, "dense voicings keep all key highlights and bound the text summary");
    static const std::string letters = "CDEFGAB";
    static const int natural[] = {0,2,4,5,7,9,11};
    for (bool flats : {false,true}) for (int tonic = 0; tonic < 12; ++tonic) for (int mode = 0; mode < int(chord::ScaleMode::Count); ++mode) {
        s.flats = flats; s.tonic = tonic; s.scale = static_cast<chord::ScaleMode>(mode);
        auto idle = chord::analyzeReference({},s);
        int inScale = 0, colors = 0;
        for (int pitch = 0; pitch < 12; ++pitch) {
            const auto& note = idle.notes[pitch];
            int accidental = 0;
            for (size_t i = 1; i < note.name.size(); ++i) accidental += note.name[i] == '#' ? 1 : -1;
            const int actual = (natural[letters.find(note.name[0])] + accidental + 24) % 12;
            check(actual == pitch && !note.degree.empty(), "every note/degree preserves pitch in every selectable key and mode");
            inScale += note.inScale; colors += note.color;
        }
        check(inScale == 7 && colors == 1 && idle.active.empty(), "idle reference preserves its seven tones and one focus without false highlights");
    }
}
}

int main() {
    modalTests(); trailTests(); layoutTests(); gestureTests(); referenceTests(); scrollingKeyboardTests(); brightnessTests();
    std::cout << checks << " harmony/layout checks passed\n";
}
