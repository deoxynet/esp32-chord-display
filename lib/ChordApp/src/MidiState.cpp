#include "MidiState.h"
#include <algorithm>

namespace chord {

void MidiState::clear() {
    for (auto& channel : channels_) channel = Channel{};
    ++revision_;
}

void MidiState::setChannel(uint8_t channel) {
    channel_ = channel <= 16 ? channel : 0;
    ++revision_;
}

void MidiState::setUseSustain(bool enabled) {
    useSustain_ = enabled;
    if (!enabled) {
        for (auto& channel : channels_) channel.sustained.fill(false);
    }
    ++revision_;
}

void MidiState::receive(MidiMessage message) {
    if (message.status == 0xFF) { clear(); return; }
    if (message.status < 0x80 || message.status >= 0xF0 ||
        message.data1 > 127 || message.data2 > 127) return;
    auto& channel = channels_[message.status & 0x0F];
    auto& held = channel.held[message.data1];
    auto& sustained = channel.sustained[message.data1];
    const uint8_t type = message.status & 0xF0;
    if (type == 0x90 && message.data2 != 0) {
        if (held < 255) ++held;
        sustained = false;
    } else if (type == 0x80 || (type == 0x90 && message.data2 == 0)) {
        if (held == 0) return; // unmatched releases must not create sustained notes
        --held;
        if (held == 0) sustained = useSustain_ && channel.pedal;
    } else if (type == 0xB0) {
        const int cc = message.data1;
        if (cc == 64) {
            channel.pedal = message.data2 >= 64;
            if (!channel.pedal) channel.sustained.fill(false);
        } else if (cc == 120) {
            channel.held.fill(0);
            channel.sustained.fill(false);
        } else if (cc == 121) {
            channel.pedal = false;
            channel.sustained.fill(false);
        } else if (cc == 123 || (cc >= 124 && cc <= 127)) {
            for (int n = 0; n < 128; ++n) {
                if (channel.held[n]) channel.sustained[n] = useSustain_ && channel.pedal;
                channel.held[n] = 0;
            }
        } else return;
    } else return;
    ++revision_;
}

std::vector<int> MidiState::notes() const {
    std::vector<int> result;
    result.reserve(16);
    for (int note = 0; note < 128; ++note) {
        for (int ch = 0; ch < 16; ++ch) {
            if (selected(ch) && (channels_[ch].held[note] || channels_[ch].sustained[note])) {
                result.push_back(note);
                break;
            }
        }
    }
    return result;
}

bool MidiState::pressed(int note) const {
    if (note < 0 || note > 127) return false;
    for (int ch = 0; ch < 16; ++ch) {
        if (selected(ch) && channels_[ch].held[note]) return true;
    }
    return false;
}

bool MidiState::pedalDown() const {
    for (int ch = 0; ch < 16; ++ch) {
        if (selected(ch) && channels_[ch].pedal) return true;
    }
    return false;
}

} // namespace chord
