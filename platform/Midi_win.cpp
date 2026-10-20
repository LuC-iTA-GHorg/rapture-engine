// MidiSink backend for Windows: winmm midiOutShortMsg, exactly the calls of the original MIDI thread (moses.dll 0x10012d3b
// open, 0x10012de3 short messages, 0x10012d87 all-notes-off + reset, 0x10012d00 close).  Only compiled when RAPTURE_WIN32.
#include "platform/Config.h"

#ifdef RAPTURE_WIN32
#include <windows.h>
#include <mmsystem.h>

#include "platform/MidiSeq.h"

namespace {
class WinmmMidiSink : public MidiSink {
public:
    ~WinmmMidiSink() override { Close(); }

    // moses.dll 0x10012d3b: midiOutOpen(&h, MIDI_MAPPER, 0, 0, 0)
    bool Open() override {
        if (h_) return true;
        if (midiOutOpen(&h_, MIDI_MAPPER, 0, 0, 0) != MMSYSERR_NOERROR) {
            h_ = nullptr;
            return false;
        }
        timeBeginPeriod(1); // the sequencer sleeps with millisecond targets: ask for a 1 ms timer
        timer_ = true;
        Reset();
        return true;
    }
    void ShortMsg(uint32_t msg, uint64_t) override {
        if (h_) midiOutShortMsg(h_, msg);
    }
    // moses.dll 0x10012d87: CC 0x7b (all notes off) on the 16 channels, then midiOutReset
    void Reset() override {
        if (!h_) return;
        for (uint32_t ch = 0; ch < 16; ++ch) midiOutShortMsg(h_, 0xb0u | ch | (0x7bu << 8));
        midiOutReset(h_);
    }
    // moses.dll 0x10012d00
    void Close() override {
        if (h_) {
            midiOutClose(h_);
            h_ = nullptr;
        }
        if (timer_) {
            timeEndPeriod(1);
            timer_ = false;
        }
    }

private:
    HMIDIOUT h_ = nullptr;
    bool timer_ = false;
};
} // namespace

std::unique_ptr<MidiSink> CreateDefaultMidiSink() { return std::unique_ptr<MidiSink>(new WinmmMidiSink()); }
#endif
