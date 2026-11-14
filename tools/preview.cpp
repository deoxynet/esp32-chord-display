#include "Display.h"
#include "ChordText.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

// Read the same bitmap font assets installed for the device. No Arduino/GFX
// runtime is needed by this native renderer.
namespace bitmap {
#define PROGMEM
struct GFXglyph {
    uint32_t bitmapOffset;
    uint8_t width, height, xAdvance;
    int8_t xOffset, yOffset;
};
struct GFXfont {
    uint8_t* bitmap;
    GFXglyph* glyph;
    uint16_t first, last;
    uint8_t yAdvance;
};
#include "GFXFF/FreeSansBold24pt7b.h"
#include "Font16.h"
#include "glcdfont.h"
#undef PROGMEM
}

namespace {
std::string color(uint32_t value) {
    std::ostringstream out;
    out << '#' << std::hex << std::setw(6) << std::setfill('0') << value;
    return out.str();
}
std::string escaped(std::string text) {
    for (const auto& pair : {std::pair<std::string,std::string>{"&","&amp;"}, {"<","&lt;"}, {">","&gt;"}, {"\"","&quot;"}}) {
        size_t at = 0;
        while ((at = text.find(pair.first, at)) != std::string::npos) {
            text.replace(at, pair.first.size(), pair.second);
            at += pair.second.size();
        }
    }
    return text;
}

class SvgCanvas final : public chord::Canvas {
public:
    explicit SvgCanvas(std::ostream& out) : out_(out) {}
    void rect(int x, int y, int w, int h, uint32_t fill, int radius) override {
        out_ << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << w << "\" height=\"" << h
             << "\" rx=\"" << radius << "\" fill=\"" << color(fill) << "\"/>\n";
    }
    void line(int x1, int y1, int x2, int y2, uint32_t stroke) override {
        if (y1 == y2) { rect(std::min(x1,x2), y1, std::abs(x2-x1)+1, 1, stroke, 0); return; }
        if (x1 == x2) { rect(x1, std::min(y1,y2), 1, std::abs(y2-y1)+1, stroke, 0); return; }
        // Rasterize strokes like the device instead of relying on SVG engines'
        // differing one-pixel stroke coverage.
        const int dx = std::abs(x2-x1), sx = x1 < x2 ? 1 : -1;
        const int dy = -std::abs(y2-y1), sy = y1 < y2 ? 1 : -1;
        int error = dx + dy;
        out_ << "<path fill=\"" << color(stroke) << "\" d=\"";
        while (true) {
            out_ << 'M' << x1 << ' ' << y1 << "h1v1h-1z";
            if (x1 == x2 && y1 == y2) break;
            const int twice = 2 * error;
            if (twice >= dy) { error += dy; x1 += sx; }
            if (twice <= dx) { error += dx; y1 += sy; }
        }
        out_ << "\"/>\n";
    }
    void ellipse(int x, int y, int rx, int ry, uint32_t fill) override {
        out_ << "<ellipse cx=\"" << x << "\" cy=\"" << y << "\" rx=\"" << rx << "\" ry=\"" << ry
             << "\" fill=\"" << color(fill) << "\"/>\n";
    }
    int textWidth(const std::string& text, chord::Font type, float scale) override {
        int width = 0;
        for (unsigned char c : text) width += static_cast<int>(advance(c, type) * scale);
        return width;
    }
    void text(const std::string& value, int x, int y, chord::Font type, float scale, uint32_t fill) override {
        const int width = textWidth(value, type, scale);
        if (x < 0 || x + width > 320) {
            std::cerr << "Text exceeds screen: " << value << " at " << x << " + " << width << '\n';
            ++overflows;
        }
        out_ << "<g aria-label=\"" << escaped(value) << "\" fill=\"" << color(fill) << "\"><title>" << escaped(value) << "</title>\n";
        std::ostringstream path;
        int baseline = 0;
        if (type == chord::Font::Chord) {
            for (const auto& glyph : bitmap::FreeSansBold24pt7bGlyphs) baseline = std::max(baseline, -int(glyph.yOffset));
        }
        for (unsigned char c : value) {
            if (c < 32 || c > 126) c = '?';
            int glyphWidth = 5, glyphHeight = 8, offsetX = 0, offsetY = 0;
            const auto& glyph = bitmap::FreeSansBold24pt7bGlyphs[c - 32];
            if (type == chord::Font::Chord) {
                glyphWidth = glyph.width; glyphHeight = glyph.height;
                offsetX = glyph.xOffset; offsetY = baseline + glyph.yOffset;
            } else if (type == chord::Font::Body) {
                glyphWidth = bitmap::widtbl_f16[c - 32] - 1; glyphHeight = 16;
            }
            auto on = [&](int column, int row) {
                if (type == chord::Font::Label) return (bitmap::font[c * 5 + column] & (1U << row)) != 0;
                if (type == chord::Font::Body) {
                    const int rowBytes = (glyphWidth + 7) / 8;
                    return (bitmap::chrtbl_f16[c - 32][row * rowBytes + column / 8] & (0x80U >> (column % 8))) != 0;
                }
                const int bit = row * glyphWidth + column;
                return (bitmap::FreeSansBold24pt7bBitmaps[glyph.bitmapOffset + bit / 8] & (0x80U >> (bit % 8))) != 0;
            };
            for (int row = 0; row < glyphHeight; ++row) {
                for (int col = 0; col < glyphWidth; ++col) {
                    if (!on(col, row)) continue;
                    const int left = x + static_cast<int>(std::floor((offsetX + col) * scale));
                    const int top = y + static_cast<int>(std::floor((offsetY + row) * scale));
                    const int right = x + static_cast<int>(std::floor((offsetX + col + 1) * scale));
                    const int bottom = y + static_cast<int>(std::floor((offsetY + row + 1) * scale));
                    if (right == left || bottom == top) continue;
                    path << 'M' << left << ' ' << top << 'h' << right-left << 'v' << bottom-top << 'h' << left-right << 'z';
                }
            }
            x += static_cast<int>(advance(c, type) * scale);
        }
        out_ << "<path d=\"" << path.str() << "\"/></g>\n";
    }
    int overflows = 0;
private:
    static int advance(unsigned char c, chord::Font type) {
        if (c < 32 || c > 126) c = '?';
        if (type == chord::Font::Label) return 6;
        if (type == chord::Font::Body) return bitmap::widtbl_f16[c - 32];
        return bitmap::FreeSansBold24pt7bGlyphs[c - 32].xAdvance;
    }
    std::ostream& out_;
};

void start(std::ostream& out, int width, int height, const std::string& title) {
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << width << ' ' << height << "\" width=\"" << width
        << "\" height=\"" << height << "\" shape-rendering=\"crispEdges\" role=\"img\"><title>" << title << "</title>\n";
}
}

int main(int argc, char** argv) {
    const std::filesystem::path directory = argc > 1 ? argv[1] : "docs/screens";
    std::filesystem::create_directories(directory);
    chord::MidiState state;
    ChordDetection::ChordDetector detector;
    std::vector<int> notes = {50,57,60,64,65,71}; // Dm13, including Dorian's natural 6
    if (argc > 2) {
        notes.clear();
        for (int i = 2; i < argc; ++i) {
            const int note = std::stoi(argv[i]);
            if (note < 0 || note > 127) { std::cerr << "MIDI note must be 0..127\n"; return 1; }
            notes.push_back(note);
        }
    }
    for (int note : notes) state.receive({0x90, static_cast<uint8_t>(note), 100});
    auto snapshot = chord::analyze(state, detector);
    chord::Settings settings;
    chord::DisplayStatus status;
    status.connected = true;
    status.battery = 82;
    status.address = "aa:bb:cc:dd:ee:ff";
    status.trail = "i7 > IV > i7";
    std::ofstream overview(directory / "overview.svg");
    start(overview, 1008, 240, "Core2 chord display: chord, harmony and enlarged staff at native resolution");
    const char* names[] = {"chord", "harmony", "notation", "settings", "key", "key-idle", "key-pedal", "key-note", "key-sharp", "key-flat",
        "chord-idle", "harmony-idle", "chord-major", "chord-dim", "chord-halfdim", "chord-standard", "chord-borrowed", "chord-applied"};
    const chord::Mode modes[] = {chord::Mode::Chord, chord::Mode::Harmony, chord::Mode::Notation};
    int overflows = 0;
    for (int i = 0; i < 18; ++i) {
        settings.mode = modes[i % 3];
        status.settingsOpen = i == 3;
        status.keyPickerOpen = i >= 4 && i < 10;
        auto frame = snapshot;
        if (i == 5) frame = {};
        if (i == 6) { frame = {}; frame.notes = {59,61,70}; frame.pressed = {true,true,false}; frame.pedal = true; }
        if (i == 7) { frame = {}; frame.notes = {59}; frame.pressed = {true}; }
        if (i == 8) { frame = {}; settings.tonic = 6; settings.scale = chord::ScaleMode::Lydian; }
        if (i == 9) { frame = {}; settings.tonic = 10; settings.scale = chord::ScaleMode::Dorian; settings.flats = true; }
        if (i >= 10) {
            frame = {}; settings = {}; settings.mode = i == 11 ? chord::Mode::Harmony : chord::Mode::Chord;
            status.trail.clear();
            if (i >= 12) {
                settings.tonic = 0; settings.scale = chord::ScaleMode::Ionian;
                chord::MidiState sample;
                const std::vector<std::vector<int>> examples = {{60,64,67,71},{60,63,66,69},{59,62,65,69},{50,53,57,60},{53,56,60,63},{50,54,57,60}};
                for (int note : examples[i-12]) sample.receive({0x90,static_cast<uint8_t>(note),96});
                frame = chord::analyze(sample,detector);
                if (i == 15) settings.notation = chord::ChordNotation::Standard;
            }
        }
        std::ofstream screen(directory / (std::string(names[i]) + ".svg"));
        start(screen, 320, 240, std::string("Core2 ") + names[i]);
        SvgCanvas canvas(screen);
        chord::render(canvas, frame, settings, status);
        screen << "</svg>\n";
        overflows += canvas.overflows;
        if (i < 3) {
            overview << "<g transform=\"translate(" << i * 344 << " 0)\">\n";
            SvgCanvas tile(overview);
            chord::render(tile, snapshot, settings, status);
            overview << "</g>\n";
        }
    }
    overview << "</svg>\n";
    std::ofstream symbols(directory / "chord-symbols.svg");
    start(symbols,640,384,"Standard chord symbols with raised and stacked alterations");
    const char* symbolExamples[] = {"Cmaj7","Gsus4b9","G7(b9,13)","F#13#11b9","Db13#11b9","Fmaj7/G"};
    for (int i = 0; i < 6; ++i) {
        symbols << "<g transform=\"translate(" << (i%2)*320 << ' ' << (i/2)*128 << ")\">\n";
        SvgCanvas tile(symbols);
        tile.rect(0,0,320,128,0x0D141C,0);
        float scale = 1.7f;
        while (chord::chordTextWidth(tile,symbolExamples[i],scale) > 296) scale -= 0.025f;
        chord::chordText(tile,symbolExamples[i],12,20,scale,0xF2F4EC,0x0D141C);
        symbols << "</g>\n";
        overflows += tile.overflows;
    }
    symbols << "</svg>\n";
    std::cout << "Rendered " << chord::chordName(snapshot, settings) << " to " << directory << '\n';
    return overflows ? 1 : 0;
}
