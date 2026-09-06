#pragma once

// MidiStreamSplit: a MIDI byte stream cut into whole messages.
//
// WinMM and WinRT hand over messages. A Kernel Streaming pin hands over bytes,
// because that is what the wire carries, and everything above IMidiInput is
// specified to receive one complete message per callback. This is the piece in
// between.
//
// It lives in its own header for the same reason the Wooting poll step does:
// the interesting cases here are running status, a realtime byte arriving in
// the middle of another message, and system exclusive, none of which can be
// produced on demand from a real device. Driven directly, all three are three
// lines of test.
//
// Header only and free of project references, so consuming it costs no change
// to either vcxproj.

#include <cstddef>
#include <cstdint>

namespace midi_stream {

// How many data bytes follow a status byte.
inline int DataBytesFor(uint8_t status) {
    if (status >= 0xF8) return 0;              // realtime: status only
    switch (status & 0xF0) {
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    case 0xC0: case 0xD0: return 1;
    case 0xF0:
        switch (status) {
        case 0xF1: case 0xF3: return 1;        // quarter frame, song select
        case 0xF2: return 2;                   // song position
        default: return 0;                     // tune request, and the rest
        }
    default: return 0;
    }
}

class Splitter {
public:
    // emit(const uint8_t* message, size_t length) is called once per complete
    // message, in arrival order. The pointer is valid only for that call.
    template <class Emit>
    void feed(const uint8_t* data, size_t length, Emit&& emit) {
        for (size_t i = 0; i < length; ++i) {
            const uint8_t byte = data[i];

            // Realtime bytes are allowed to appear between any two bytes of
            // another message, and must not disturb it. Passed straight out
            // without touching the message being assembled.
            if (byte >= 0xF8) { emit(&byte, size_t{1}); continue; }

            if (byte & 0x80) {
                // System exclusive is dropped rather than delivered in pieces:
                // the callers take one short message and have nowhere to put a
                // fragment. F7 ends it; any other status ends it too, because a
                // new message means the dump was abandoned.
                if (byte == 0xF0) { inSysex_ = true; pending_ = 0; continue; }
                inSysex_ = false;
                if (byte == 0xF7) { pending_ = 0; continue; }

                // A System Common status cancels running status; a channel
                // status becomes the new one.
                status_ = byte < 0xF0 ? byte : uint8_t{0};
                message_[0] = byte;
                pending_ = 1;
                expected_ = static_cast<size_t>(DataBytesFor(byte)) + 1;
                if (pending_ == expected_) { emit(message_, pending_); pending_ = 0; }
                continue;
            }

            if (inSysex_) continue;             // sysex payload
            if (pending_ == 0) {
                // Running status: a data byte with no status in front of it
                // repeats the last channel status.
                if (!status_) continue;         // data before any status at all
                message_[0] = status_;
                pending_ = 1;
                expected_ = static_cast<size_t>(DataBytesFor(status_)) + 1;
            }
            message_[pending_++] = byte;
            if (pending_ >= expected_) { emit(message_, pending_); pending_ = 0; }
        }
    }

    void reset() { status_ = 0; pending_ = 0; expected_ = 0; inSysex_ = false; }

private:
    uint8_t message_[3]{};
    uint8_t status_ = 0;     // last channel status, for running status
    size_t pending_ = 0;
    size_t expected_ = 0;
    bool inSysex_ = false;
};

} // namespace midi_stream
