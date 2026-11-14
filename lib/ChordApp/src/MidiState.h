#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace chord {

struct MidiMessage {
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
};

class MidiState {
public:
    void receive(MidiMessage message);
    void clear();
    void setChannel(uint8_t channel); // 0 = omni, 1..16 = one channel
    void setUseSustain(bool enabled);
    std::vector<int> notes() const;
    bool pressed(int note) const;
    bool pedalDown() const;
    uint32_t revision() const { return revision_; }
    uint8_t channel() const { return channel_; }
    bool useSustain() const { return useSustain_; }

private:
    struct Channel {
        std::array<uint8_t, 128> held{};
        std::array<bool, 128> sustained{};
        bool pedal = false;
    };
    bool selected(int channel) const { return channel_ == 0 || channel_ == channel + 1; }
    std::array<Channel, 16> channels_{};
    uint32_t revision_ = 0;
    uint8_t channel_ = 0;
    bool useSustain_ = true;
};

} // namespace chord
