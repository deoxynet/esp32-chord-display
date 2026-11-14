#include "BleMidiParser.h"

namespace chord {

bool BleMidiParser::parse(const uint8_t* bytes, size_t length, Sink sink, void* context) {
    if (!bytes || !sink || length < 2 || (bytes[0] & 0xC0) != 0x80) return false;
    size_t i = 1;
    uint8_t running = 0;
    bool haveTimestamp = false;
    while (i < length) {
        if (inSysEx_ && bytes[i] < 0x80) { ++i; continue; }

        // At a message boundary, every high-bit byte is a timestamp.
        if (bytes[i] & 0x80) {
            ++i;
            haveTimestamp = true;
            if (i == length) return false;
        }
        if (inSysEx_) {
            if (bytes[i] == 0xF7) { inSysEx_ = false; ++i; continue; }
            if (bytes[i] >= 0xF8) {
                sink(context, {bytes[i++], 0, 0});
                continue;
            }
            if (bytes[i] < 0x80) continue;
            inSysEx_ = false; // recover from an unterminated SysEx
        }
        uint8_t status = running;
        if (bytes[i] & 0x80) {
            status = bytes[i++];
            if (status < 0xF0) running = status;
        } else if (!haveTimestamp || running == 0) return false;

        if (status == 0xF0) { inSysEx_ = true; continue; }
        if (status < 0x80) return false;
        uint8_t count = 0;
        if (status < 0xF0) {
            const uint8_t type = status & 0xF0;
            count = (type == 0xC0 || type == 0xD0) ? 1 : 2;
        } else if (status == 0xF1 || status == 0xF3) count = 1;
        else if (status == 0xF2) count = 2;
        else if (status == 0xF4 || status == 0xF5 || status == 0xF7) return false;

        if (i + count > length) return false;
        MidiMessage message{status, 0, 0};
        if (count >= 1) message.data1 = bytes[i++];
        if (count == 2) message.data2 = bytes[i++];
        if (message.data1 >= 0x80 || message.data2 >= 0x80) return false;
        sink(context, message);
    }
    return true;
}

} // namespace chord
