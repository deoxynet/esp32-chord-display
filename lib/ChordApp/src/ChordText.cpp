#include "ChordText.h"
#include "MusicText.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace chord {
namespace {
struct Parts { std::string base, bass; std::vector<std::string> tensions; };
bool digit(char c) { return c >= '0' && c <= '9'; }
Parts parse(std::string name, bool roman) {
    Parts p;
    const auto slash = name.find_last_of('/');
    if (!roman && slash != std::string::npos && slash+1 < name.size() && name[slash+1] >= 'A' && name[slash+1] <= 'G') {
        p.bass = name.substr(slash); name.erase(slash);
    }
    size_t root = 0;
    if (roman) {
        if (name.rfind("sub",0) == 0) root = 3;
        while (root < name.size() && (name[root] == '#' || name[root] == 'b')) ++root;
        while (root < name.size() && std::string("IVXivx").find(name[root]) != std::string::npos) ++root;
    } else if (!name.empty() && name[0] >= 'A' && name[0] <= 'G') {
        root = 1;
        while (root < name.size() && (name[root] == '#' || name[root] == 'b')) ++root;
    }
    p.base = name.substr(0,root);
    for (size_t i = root; i < name.size();) {
        if ((name[i] == 'b' || name[i] == '#') && i+1 < name.size() && digit(name[i+1])) {
            const size_t start = i++;
            while (i < name.size() && digit(name[i])) ++i;
            p.tensions.push_back(name.substr(start,i-start));
        } else if (name[i] == '(') {
            const auto end = name.find(')',i);
            const auto inside = end == std::string::npos ? "" : name.substr(i+1,end-i-1);
            const bool numeric = !inside.empty() && inside.find_first_not_of("b#0123456789,/ ") == std::string::npos;
            if (numeric) {
                size_t at = 0;
                while (at < inside.size()) {
                    const auto next = inside.find_first_of(",/ ",at);
                    if ((next == std::string::npos ? inside.size() : next) > at)
                        p.tensions.push_back(inside.substr(at,next == std::string::npos ? next : next-at));
                    if (next == std::string::npos) break;
                    at = next+1;
                }
                i = end+1;
            } else if (end != std::string::npos) {
                p.base += name.substr(i,end-i+1); i = end+1;
            } else p.base += name[i++];
        } else p.base += name[i++];
    }
    return p;
}
struct Metrics { Font font; float script; int gap, bracket, height, row, width; };
Metrics metrics(Canvas& c, const Parts& p, float scale) {
    const int count = std::max(1,int(p.tensions.size()));
    const int gap = std::max(1,int(std::lround(scale)));
    float script = std::max(0.1f,std::min(1.1f*scale,(36*scale-gap*(count-1))/(16*count)));
    // Upscale the small bitmap face rather than losing strokes by reducing
    // the sixteen-pixel face below its native size.
    const Font font = script < 1 ? Font::Label : Font::Body;
    if (font == Font::Label) script *= 2;
    const int row = int(std::ceil((font == Font::Label ? 8 : 16)*script));
    int width = 0;
    for (const auto& tension : p.tensions) width = std::max(width,musicTextWidth(c,tension,font,script));
    const int bracket = std::max(3,int(std::lround(4*scale)));
    return {font,script,gap,bracket,row*count+gap*(count-1),row,width};
}
void parenthesis(Canvas& c, int x, int y, int w, int h, bool right, int stroke, uint32_t color) {
    for (int row = 0; row < h; ++row) {
        const float t = h > 1 ? 2.f*row/(h-1)-1 : 0;
        const int offset = int(std::lround(w*(1-std::sqrt(std::max(0.f,1-t*t)))));
        const int at = x + (right ? w-offset : offset);
        c.line(at,y+row,at+stroke-1,y+row,color);
    }
}
}
int chordTextWidth(Canvas& c, const std::string& name, float scale, bool roman) {
    const auto p = parse(name,roman);
    int width = musicTextWidth(c,p.base,Font::Chord,scale);
    if (!p.tensions.empty()) {
        const auto m = metrics(c,p,scale);
        width += m.width + 2*m.bracket + 4*m.gap;
    }
    return width + musicTextWidth(c,p.bass,Font::Chord,scale);
}
void chordText(Canvas& c, const std::string& name, int x, int y, float scale, uint32_t color, uint32_t background, bool roman) {
    const auto p = parse(name,roman);
    musicText(c,p.base,x,y,Font::Chord,scale,color,background);
    x += musicTextWidth(c,p.base,Font::Chord,scale);
    if (!p.tensions.empty()) {
        const auto m = metrics(c,p,scale);
        const int stroke = std::max(1,int(std::lround(scale)));
        x += m.gap;
        parenthesis(c,x,y,m.bracket,m.height,false,stroke,color);
        for (size_t i = 0; i < p.tensions.size(); ++i) {
            const auto& text = p.tensions[i];
            const int width = musicTextWidth(c,text,m.font,m.script);
            musicText(c,text,x+m.bracket+m.gap+(m.width-width)/2,y+int(i)*(m.row+m.gap),m.font,m.script,color,background);
        }
        parenthesis(c,x+m.bracket+m.width+2*m.gap,y,m.bracket,m.height,true,stroke,color);
        x += m.width+2*m.bracket+3*m.gap;
    }
    musicText(c,p.bass,x,y,Font::Chord,scale,color,background);
}
}
