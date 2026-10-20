// Software MIDI sequencer for sound kind 2 (compact stream), a clean reimplementation of the original MIDI thread.
//
// ORIGINAL SEMANTICS (moses.dll CMidiPlayer::thread_proc 0x10012de3, SetTempo 0x100130ae, SetChannelMute 0x10012ff9,
// BreakLoops 0x10012fa9, Play 0x10013120, stop/close 0x100131e6 / 0x10012d00 / 0x10012d87):
//  * The stream is an array of dwords.  bit31 set = delay, the low 31 bits are MILLISECONDS (the thread adds them to a
//    timeGetTime() target, so the unit is verified: 1 delta unit = 1 ms).  Otherwise the dword is a short MIDI message
//    handed to midiOutShortMsg unchanged.
//  * The target time is ABSOLUTE and accumulated (start time + sum of the scaled delays): the thread never re-syncs to
//    "now", so if it is late it fires the following events at once (catch-up) and no drift builds up.
//  * Tempo: delay_ms = (delay * num) / den in 32-bit unsigned arithmetic, applied per delay when it is READ; den == 0
//    disables the scaling, SetTempo(n, n) stores (0, 0).  A change therefore affects the delays read afterwards, not
//    the one already being waited for.  It is live (done under the thread's mutex).
//  * Channel mute: one flag per channel (index = value % 16).  Only NOTE-ON messages (status & 0xf0 == 0x90) of a muted
//    channel are dropped (a note-on with velocity 0 included); note-offs and controllers still go out.  Live, and it
//    survives a new Play.
//  * RemapDrums (ini [audio]): a message with low nibble 9 becomes channel 16 (`| 0xf`) BEFORE the mute test, so the
//    drums are muted through channel 15.  Fixed at engine creation.
//  * Loop flag: at the end of the stream the index goes back to 0 and the target time simply continues (seamless loop)
//    while the flag is set; BreakLoops clears it live.  The last delay of the stream is waited for before the end/loop.
//  * End or Stop: 16 x "all notes off" (0xB0|ch, controller 0x7b) then midiOutReset, then close.
//  * CAudioLib::Pause / Activate do not pause: they STOP the player and the sound is started again from the beginning on
//    resume (moses.dll 0x10011be9, 0x10011df1, 0x10011dc9); see AudioLib.cpp.
//
// The port splits the thread into a pure state machine (MidiSequencer::Poll, driven by a clock value) and a thin driver
// (MidiPlayer, Midi.h) that polls it from its own thread with a real clock or, in tests, from a virtual clock.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Receiver of the short messages (winmm midiOutShortMsg on Windows, a logging no-op elsewhere, a recorder in tests).
class MidiSink {
public:
    virtual ~MidiSink() {}
    virtual bool Open() = 0;                                       // midiOutOpen (MIDI_MAPPER); false = no usable device
    virtual void ShortMsg(uint32_t msg, uint64_t scheduledMs) = 0; // midiOutShortMsg.  scheduledMs = the time the event was due
    virtual void Reset() = 0;                                      // 0x10012d87: all-notes-off on 16 channels + midiOutReset
    virtual void Close() = 0;                                      // midiOutClose
};

// Time source in milliseconds (timeGetTime in the original).
class MidiClock {
public:
    virtual ~MidiClock() {}
    virtual uint64_t NowMs() = 0;
};
class MidiRealClock : public MidiClock { // steady clock, ms since the first call
public:
    uint64_t NowMs() override;
};
class MidiManualClock : public MidiClock { // virtual clock for tests: moves only when told to
public:
    uint64_t now = 0;
    uint64_t NowMs() override { return now; }
};

// The state machine.  NOT thread-safe by itself (MidiPlayer serialises the calls with a mutex).
class MidiSequencer {
public:
    enum class Status { Idle, Waiting, Finished };

    explicit MidiSequencer(MidiSink* sink = nullptr) : sink_(sink) {}
    void SetSink(MidiSink* s) { sink_ = s; }

    // CMidiPlayer::Play 0x10013120: stops what runs, opens the sink, restarts at index 0 with target = nowMs.
    // `events` is copied.  false if the sink cannot be opened.  remapDrums = the ini option (engine creation time).
    bool Start(const uint32_t* events, size_t count, bool loop, uint64_t nowMs, bool remapDrums);
    // 0x100131e6: all notes off, close, forget the stream.  No "ended" status (the original stop path is silent).
    void Stop();
    // Extra (not in the original, which stops/restarts): hold the position.  Notes are silenced; Resume shifts the target
    // by the time spent paused.
    void Pause(uint64_t nowMs);
    void Resume(uint64_t nowMs);

    void SetChannelMute(uint32_t channel, bool mute); // 0x10012ff9 (channel % 16)
    bool ChannelMuted(uint32_t channel) const { return mute_[channel % 16] != 0; }
    void SetTempo(uint32_t num, uint32_t den);        // 0x100130ae
    void BreakLoops() { loop_ = false; }              // 0x10012fa9

    // Executes everything due at nowMs, like the thread loop.  Returns Waiting with the absolute time of the next wake-up in
    // *wakeMs (== nowMs when the per-call event budget ran out), Finished once when the stream ended (the sink was reset and
    // closed), Idle when nothing runs.
    Status Poll(uint64_t nowMs, uint64_t* wakeMs);

    bool Running() const { return running_; }
    bool Paused() const { return paused_; }
    bool Looping() const { return loop_; }
    size_t Position() const { return idx_; }
    size_t Count() const { return events_.size(); }
    uint64_t TargetMs() const { return target_; }
    uint32_t TempoNum() const { return num_; }
    uint32_t TempoDen() const { return den_; }

private:
    void Finish();

    MidiSink* sink_ = nullptr;
    std::vector<uint32_t> events_;
    size_t idx_ = 0;
    bool loop_ = false;
    bool running_ = false;
    bool paused_ = false;
    bool remap_ = false;
    bool waiting_ = false;
    uint64_t target_ = 0, pausedAt_ = 0;
    uint32_t num_ = 0, den_ = 0; // SetTempo: ms = delay * num_ / den_ (den_ == 0: off)
    uint8_t mute_[16] = {0};
};

// Test sink: records every message with its scheduled time and the clock value at which it was emitted.
class MidiRecordingSink : public MidiSink {
public:
    struct Event { uint32_t msg; uint64_t scheduledMs; uint64_t emittedMs; };
    std::vector<Event> events;
    int opens = 0, closes = 0, resets = 0;
    bool failOpen = false;
    MidiClock* clock = nullptr; // optional, fills emittedMs
    bool Open() override { if (failOpen) return false; ++opens; opened = true; return true; }
    void ShortMsg(uint32_t msg, uint64_t scheduledMs) override {
        events.push_back({msg, scheduledMs, clock ? clock->NowMs() : scheduledMs});
    }
    void Reset() override { ++resets; }
    void Close() override { ++closes; opened = false; }
    bool opened = false;
};

// Default backend: winmm midiOutShortMsg on Windows (Midi_win.cpp), otherwise a no-op that can log
// (env RAPTURE_MIDI_LOG=1 prints every message to stderr).  Never returns null.
std::unique_ptr<MidiSink> CreateDefaultMidiSink();
