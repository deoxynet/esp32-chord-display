#include <M5Unified.h>
#include <Preferences.h>
#include "Analysis.h"
#include "Harmony.h"
#include "UiLayout.h"
#include "Navigation.h"
#include "BleMidiInput.h"
#include "DeviceCanvas.h"

namespace {
M5Canvas framebuffer(&M5.Display);
DeviceCanvas canvas(framebuffer);
BleMidiInput midi;
Preferences preferences;
chord::MidiState liveNotes, demoNotes;
ChordDetection::ChordDetector detector;
chord::Settings settings;
chord::DisplayStatus displayStatus;
chord::Snapshot snapshot;
chord::Settler settler;
chord::HarmonyTrail harmonyTrail;
uint32_t observedRevision = 0;
uint32_t lastDraw = 0;
uint32_t lastBattery = 0;
uint32_t lastDemo = 0;
uint32_t overrunAt = 0;
uint32_t previousDropped = 0;
uint32_t lastBluetoothStatus = 0;
bool dirty = true;
bool forceAnalysis = true;
bool demo = false;
bool frozen = false;
int demoIndex = -1;
bool multiTouchGesture = false;

void saveSettings() {
    preferences.putBool("flats", settings.flats);
    preferences.putBool("sustain", settings.sustain);
    preferences.putUChar("channel", settings.channel);
    preferences.putUChar("brightness", settings.brightness);
    preferences.putUChar("mode", static_cast<uint8_t>(settings.mode));
    preferences.putUChar("bluetooth", static_cast<uint8_t>(settings.bluetooth));
    preferences.putUChar("tonic", settings.tonic);
    preferences.putUChar("scale", static_cast<uint8_t>(settings.scale));
    preferences.putUChar("notation", static_cast<uint8_t>(settings.notation));
}

void clearTrail() {
    harmonyTrail.clear();
    displayStatus.trail.clear();
    displayStatus.motion.clear();
    dirty = true;
}

void keyChanged() {
    clearTrail();
    preferences.putUChar("tonic", settings.tonic);
    preferences.putUChar("scale", static_cast<uint8_t>(settings.scale));
}

void applySettings() {
    liveNotes.setChannel(settings.channel);
    demoNotes.setChannel(settings.channel);
    liveNotes.setUseSustain(settings.sustain);
    demoNotes.setUseSustain(settings.sustain);
    M5.Display.setBrightness(settings.brightness);
    midi.setMode(settings.bluetooth);
    forceAnalysis = true;
    dirty = true;
}

void chooseMode(int index) {
    const chord::Mode modes[] = {chord::Mode::Chord, chord::Mode::Harmony, chord::Mode::Notation};
    if (index < 0 || index > 2) return;
    const auto mode = modes[index];
    if (settings.mode != mode) {
        settings.mode = mode;
        preferences.putUChar("mode", static_cast<uint8_t>(mode));
        dirty = true;
    }
}

void changeSetting() {
    switch (displayStatus.selectedSetting) {
        case 0: settings.channel = (settings.channel + 1) % 17; break;
        case 1: settings.sustain = !settings.sustain; break;
        case 2: settings.flats = !settings.flats; break;
        case 3:
            settings.brightness = settings.brightness < 100 ? 120 :
                settings.brightness < 150 ? 180 : settings.brightness < 220 ? 240 : 70;
            break;
        case 4:
            demo = !demo;
            demoNotes.clear();
            demoIndex = -1;
            frozen = false;
            clearTrail();
            break;
        case 5:
            settings.bluetooth = static_cast<chord::BluetoothMode>((static_cast<unsigned>(settings.bluetooth) + 1) % 3);
            break;
        case 6:
            displayStatus.keyPickerOpen = true;
            dirty = true;
            return;
        case 7:
            settings.notation = settings.notation == chord::ChordNotation::Jazz ? chord::ChordNotation::Standard : chord::ChordNotation::Jazz;
            break;
    }
    applySettings();
    saveSettings();
}

void action(int index) {
    if (displayStatus.keyPickerOpen) {
        if (index == 0) settings.tonic = (settings.tonic + 1) % 12;
        else if (index == 1) settings.scale = chord::ui::steppedScale(settings.scale, 1);
        else displayStatus.keyPickerOpen = false;
        if (index < 2) keyChanged();
        dirty = true;
    } else if (!displayStatus.settingsOpen) chooseMode(index);
    else if (index == 0) {
        displayStatus.selectedSetting = (displayStatus.selectedSetting + 1) % chord::ui::settingCount;
        dirty = true;
    } else if (index == 1) changeSetting();
    else { displayStatus.settingsOpen = false; dirty = true; }
}

void swipe(chord::ui::Swipe direction) {
    using namespace chord::ui;
    const auto layer = displayStatus.keyPickerOpen ? Layer::KeyPicker : displayStatus.settingsOpen ? Layer::Settings : Layer::Playing;
    switch (gestureAction(direction, layer)) {
        case GestureAction::NextView: chooseMode(steppedView(settings.mode, 1)); break;
        case GestureAction::PreviousView: chooseMode(steppedView(settings.mode, -1)); break;
        case GestureAction::OpenKeyPicker: displayStatus.keyPickerOpen = true; break;
        case GestureAction::OpenSettings: displayStatus.settingsOpen = true; break;
        case GestureAction::ClosePanel:
            if (displayStatus.keyPickerOpen) displayStatus.keyPickerOpen = false;
            else displayStatus.settingsOpen = false;
            break;
        case GestureAction::NextScale:
        case GestureAction::PreviousScale:
            settings.scale = steppedScale(settings.scale, direction == Swipe::Left ? 1 : -1);
            keyChanged();
            break;
        default: return;
    }
    dirty = true;
    const char* names[] = {"none", "left", "right", "up", "down"};
    Serial.printf("Swipe %s: view=%u; panel=%s; key=%s\n", names[static_cast<unsigned>(direction)], unsigned(settings.mode),
        displayStatus.keyPickerOpen ? "key" : displayStatus.settingsOpen ? "settings" : "playing", chord::keyName(settings).c_str());
}

void controls() {
    if (M5.BtnA.wasHold()) { frozen = !frozen; forceAnalysis = !frozen; dirty = true; }
    if (M5.BtnB.wasHold()) {
        liveNotes.clear();
        demoNotes.clear();
        frozen = false;
        clearTrail();
        forceAnalysis = true;
        dirty = true;
    }
    if (M5.BtnC.wasHold()) {
        displayStatus.keyPickerOpen = false;
        displayStatus.settingsOpen = !displayStatus.settingsOpen;
        dirty = true;
    }
    if (M5.BtnA.wasClicked()) action(0);
    if (M5.BtnB.wasClicked()) action(1);
    if (M5.BtnC.wasClicked()) action(2);
    const auto touch = M5.Touch.getDetail();
    if (M5.Touch.getCount() > 1) multiTouchGesture = true;
    if (touch.wasFlicked() || touch.wasDragged()) {
        if (!multiTouchGesture) swipe(chord::ui::swipeAt(touch.base_x, touch.base_y, touch.x, touch.y));
    } else if ((touch.wasClicked() || touch.state == m5::touch_state_t::hold_end) && !multiTouchGesture) {
        const int tab = chord::ui::tabAt(touch.x, touch.y);
        if (tab >= 0) action(tab);
        else if (displayStatus.keyPickerOpen) {
            const int tonic = chord::ui::tonicAt(touch.x, touch.y, settings.tonic), scaleIndex = chord::ui::scaleAt(touch.x, touch.y);
            if (tonic >= 0 && tonic != settings.tonic) {
                settings.tonic = static_cast<uint8_t>(tonic);
                keyChanged();
            } else if (scaleIndex >= 0) {
                const auto scale = chord::modesByBrightness[scaleIndex];
                if (scale != settings.scale) {
                    settings.scale = scale;
                    keyChanged();
                }
            }
        } else if (displayStatus.settingsOpen) {
            const int selected = chord::ui::settingAt(touch.x, touch.y);
            if (selected >= 0) {
                displayStatus.selectedSetting = static_cast<uint8_t>(selected);
                changeSetting();
            }
        } else if (chord::ui::keyButtonAt(settings.mode, touch.x, touch.y)) {
            displayStatus.keyPickerOpen = true;
            dirty = true;
        }
    }
    if (M5.Touch.getCount() == 0) multiTouchGesture = false;
}

void playDemo(uint32_t now) {
    if (!demo || (demoIndex >= 0 && uint32_t(now - lastDemo) < 2800)) return;
    static const int chords[][6] = {
        {50,60,64,65,69,-1}, // Dm9, home in D Dorian
        {50,55,59,62,-1,-1}, // G/D, Dorian 6 over the tonic pedal
        {50,55,60,65,69,-1}, // Dm11, fourth-based color
        {50,54,57,60,-1,-1}, // D7, applied dominant in C major
        {43,53,59,62,-1,-1}, // G7
        {48,60,64,67,71,-1}, // Cmaj7
        {48,60,63,67,70,-1}, // Cm7, borrowed color in C major
    };
    demoIndex = (demoIndex + 1) % 7;
    lastDemo = now;
    demoNotes.clear();
    const uint8_t channel = settings.channel ? settings.channel - 1 : 0;
    for (int note : chords[demoIndex]) {
        if (note >= 0) demoNotes.receive({static_cast<uint8_t>(0x90 | channel), static_cast<uint8_t>(note), 96});
    }
    forceAnalysis = true;
}
}

void setup() {
    Serial.begin(115200);
    auto config = M5.config();
    config.internal_spk = false;
    config.internal_mic = false;
    M5.begin(config);
    M5.Display.setRotation(0); // M5Unified/M5GFX 0.3.0: upright Core2 is landscape
    M5.BtnA.setHoldThresh(650);
    M5.BtnB.setHoldThresh(650);
    M5.BtnC.setHoldThresh(650);
    M5.Touch.setFlickThresh(14);
    framebuffer.setColorDepth(16);
    framebuffer.setPsram(true);
    if (!framebuffer.createSprite(320, 240)) {
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_RED);
        M5.Display.drawString("Display buffer failed. Restart.", 10, 100);
        while (true) delay(100);
    }
    framebuffer.setTextWrap(false);
    preferences.begin("chord-display", false);
    settings.flats = preferences.getBool("flats", false);
    settings.sustain = preferences.getBool("sustain", true);
    settings.channel = std::min<uint8_t>(16, preferences.getUChar("channel", 0));
    settings.brightness = std::max<uint8_t>(40, preferences.getUChar("brightness", 180));
    const auto savedMode = preferences.getUChar("mode", 0);
    settings.mode = savedMode == 2 ? chord::Mode::Notation : savedMode == 3 ? chord::Mode::Harmony : chord::Mode::Chord;
    settings.bluetooth = static_cast<chord::BluetoothMode>(std::min<uint8_t>(2, preferences.getUChar("bluetooth", 1)));
    settings.tonic = preferences.getUChar("tonic", 2) % 12;
    settings.scale = static_cast<chord::ScaleMode>(std::min<uint8_t>(static_cast<uint8_t>(chord::ScaleMode::Count) - 1, preferences.getUChar("scale", 1)));
    settings.notation = static_cast<chord::ChordNotation>(std::min<uint8_t>(1, preferences.getUChar("notation", 1)));
    applySettings();
#ifdef CHORD_DISPLAY_DEMO
    demo = true;
#endif
    if (!midi.begin()) {
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_RED);
        M5.Display.drawString("BLE MIDI failed. Restart.", 10, 100);
        while (true) delay(100);
    }
    displayStatus.address = midi.address();
    Serial.println("A/B/C: chord/harmony/staff. Tap the key to choose a tonic/mode. Hold A: freeze, B: clear, C: settings.");
    Serial.println("Swipe left/right: views. Up: key/mode. Down: settings/close panel. Picker: left/right compares modes.");
    Serial.println("Bluetooth: Group waits for the WIDI central. Serial: ? status, s scan, g group, a auto, d direct.");
    Serial.println("Views: C chord, H harmony, N staff, K key picker. Symbols: t standard, j jazz. Key: " + String(chord::keyName(settings).c_str()));
}

void loop() {
    M5.update();
    const uint32_t now = millis();
    const bool recovered = midi.poll(liveNotes);
    if (recovered && !demo) {
        // Recovery and disconnects must clear the screen immediately, even if
        // it was frozen, so a lost release cannot masquerade as a live chord.
        frozen = false;
        clearTrail();
        forceAnalysis = true;
    }
    controls();
    playDemo(now);
    auto& state = demo ? demoNotes : liveNotes;
    if (observedRevision != state.revision()) {
        observedRevision = state.revision();
        settler.changed(now);
        if (displayStatus.keyPickerOpen && !frozen) dirty = true;
    }
    if (!frozen && (forceAnalysis || settler.due(now))) {
        snapshot = chord::analyze(state, detector);
        settler.settled();
        forceAnalysis = false;
        dirty = true;
    }
    if (!frozen && harmonyTrail.observe(snapshot, settings, now)) {
        displayStatus.trail = harmonyTrail.text();
        displayStatus.motion = harmonyTrail.motion();
        dirty = true;
    }
    if (midi.dropped() != previousDropped) {
        previousDropped = midi.dropped();
        overrunAt = now;
    }
    const bool connected = midi.connected();
    if (connected != displayStatus.connected) dirty = true;
    displayStatus.connected = connected;
    displayStatus.demo = demo;
    displayStatus.frozen = frozen;
    displayStatus.dropped = midi.dropped();
    displayStatus.malformed = midi.malformed();
    displayStatus.overrun = overrunAt && uint32_t(now - overrunAt) < 2500;
    displayStatus.midiActive = midi.lastPacketMs() && uint32_t(now - midi.lastPacketMs()) < 120;
    if (uint32_t(now - lastBattery) > 10000 || displayStatus.battery < 0) {
        lastBattery = now;
        displayStatus.battery = M5.Power.getBatteryLevel();
    }
    if ((dirty && uint32_t(now - lastDraw) >= 16) || uint32_t(now - lastDraw) >= 250) {
        if (displayStatus.keyPickerOpen && !frozen) {
            const auto reference = chord::captureNotes(state);
            chord::render(canvas, reference, settings, displayStatus);
        } else chord::render(canvas, snapshot, settings, displayStatus);
        framebuffer.pushSprite(0, 0);
        M5.Display.waitDMA();
        lastDraw = now;
        dirty = false;
    }
    bool requestedStatus = false;
    while (Serial.available()) {
        const char command = Serial.read();
        if (command == '?') requestedStatus = true;
        else if (command == 't' || command == 'j') {
            settings.notation = command == 't' ? chord::ChordNotation::Standard : chord::ChordNotation::Jazz;
            preferences.putUChar("notation",static_cast<uint8_t>(settings.notation));
            dirty = true;
            requestedStatus = true;
        }
        else if (command == 'C' || command == 'H' || command == 'N') chooseMode(command == 'C' ? 0 : command == 'H' ? 1 : 2);
        else if (command == 'K') { displayStatus.keyPickerOpen = !displayStatus.keyPickerOpen; dirty = true; }
        else if (command == 'L' || command == 'R' || command == 'U' || command == 'D') {
            swipe(command == 'L' ? chord::ui::Swipe::Left : command == 'R' ? chord::ui::Swipe::Right :
                  command == 'U' ? chord::ui::Swipe::Up : chord::ui::Swipe::Down);
        }
        else if (command == 's') midi.scanForDiagnostics();
        else if (command == 'g' || command == 'a' || command == 'd') {
            settings.bluetooth = command == 'g' ? chord::BluetoothMode::Group :
                command == 'd' ? chord::BluetoothMode::Direct : chord::BluetoothMode::Auto;
            midi.setMode(settings.bluetooth);
            saveSettings();
            dirty = true;
            requestedStatus = true;
        }
    }
    if (requestedStatus || uint32_t(now - lastBluetoothStatus) >= 10000) {
        midi.printStatus();
        if (requestedStatus) {
            const auto harmony = chord::analyzeHarmony(snapshot, settings);
            Serial.printf("Key: %s; symbols=%s; view=%u; panel=%s; chord=%s; degree=%s; role=%s; trail=%s\n",
                chord::keyName(settings).c_str(), settings.notation == chord::ChordNotation::Standard ? "Standard" : "Jazz", unsigned(settings.mode),
                displayStatus.keyPickerOpen ? "key" : displayStatus.settingsOpen ? "settings" : "playing", chord::chordName(snapshot, settings).c_str(),
                harmony.roman.c_str(), harmony.role.c_str(), displayStatus.trail.c_str());
            if (snapshot.chord) for (const auto& alternative : snapshot.chord->alternatives) {
                Serial.printf("Also: %s; root=%s\n", chord::alternativeName(alternative,settings).c_str(),
                              alternative.rootless ? "inferred" : "played");
            }
            if (displayStatus.keyPickerOpen) {
                const auto reference = chord::analyzeReference(frozen ? snapshot : chord::captureNotes(state), settings);
                Serial.printf("Live reference: %s\n", reference.readout.c_str());
            }
        }
        lastBluetoothStatus = now;
    }
    delay(2);
}
