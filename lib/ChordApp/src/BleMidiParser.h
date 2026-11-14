#pragma once

#include "MidiState.h"
#include <cstddef>

namespace chord {

// BLE-MIDI v1.0 framing. Only SysEx state crosses a packet boundary.
class BleMidiParser {
public:
    using Sink = void (*)(void*, MidiMessage);
    bool parse(const uint8_t* bytes, size_t length, Sink sink, void* context);
    void reset() { inSysEx_ = false; }

private:
    bool inSysEx_ = false;
};

} // namespace chord
