// MIDI support for sound kind 2 (compact stream, moses.dll 0x10010a74 / MidiSetTempo 0x10010708).
//
// Stream = sequence of dwords: bit31 set -> delay (low 31 bits), otherwise a short MIDI
// event packed as status | d1<<8 | d2<<16 (program change / channel pressure have 2 bytes).
//
// TIMING (verified against the original MIDI thread, moses.dll 0x10012de3): the delay is in MILLISECONDS, the thread adds it
// to a timeGetTime() target.  Playback is done by the software sequencer (MidiSeq.h / MidiPlayer below).
// CompactMidiToSmf() (1 unit = 1 ms: division 1000, tempo 1,000,000 us/quarter) is only a conversion helper for tools and tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "platform/MidiSeq.h"

// Multiply delta times by this in CompactMidiToSmf (1.0 = 1 unit is 1 ms; does not affect the sequencer).
extern double MidiTempoScale;

// moses.dll CMidiPlayer::thread_proc 0x10012de3, ini [audio] RemapDrums: a short message whose low nibble is 9 (MIDI channel 10,
// the drums) is rewritten to channel 16 (`| 0xf`). The test is on the low nibble of the whole dword, exactly like the original.
inline uint32_t MidiRemapDrumsMsg(uint32_t dw) { return (dw & 0xf) == 9 ? (dw | 0xf) : dw; }

// Port of the per-dword part of the original MIDI thread on a compact stream (dwords, bit31 = delta): optional drum remap, THEN the
// muted-channel test on the (possibly remapped) channel (note-on 0x9n of a muted channel is dropped, moses 0x1001064f), and the
// tempo scale delta*num/den (moses 0x10010708; den == 0 or num == den: off). Returns the stream unchanged when nothing applies.
std::vector<uint8_t> MidiPrepareCompact(const std::vector<uint8_t>& in, bool remapDrums, uint32_t muteMask, uint32_t num, uint32_t den);

// Portable: builds a format-0 Standard MIDI File in memory.
std::vector<uint8_t> CompactMidiToSmf(const uint8_t* data, size_t size);

// Sequencer-driven playback of a compact stream (see MidiSeq.h for the timing semantics).  Replaces the old MCI "sequencer"
// device player: the stream is NOT converted to a Standard MIDI File any more, the dwords are scheduled one by one.
//
//  * default: one worker thread (started on the first Play) polls the MidiSequencer with a real millisecond clock;
//  * manual clock (tests): no thread, Update() polls with the virtual clock set by SetManualClock().
// The messages go to a MidiSink: winmm midiOutShortMsg on Windows, a no-op/logging sink elsewhere (or whatever the tests
// install with SetSink).
class MidiPlayer {
public:
    MidiPlayer();
    ~MidiPlayer();
    MidiPlayer(const MidiPlayer&) = delete;
    MidiPlayer& operator=(const MidiPlayer&) = delete;

    // Test / embedding hooks (call before the first Play).
    void SetSink(std::shared_ptr<MidiSink> sink);
    void SetManualClock(MidiClock* clock); // null = real clock + worker thread again

    // `compact` = the dword stream of the sound (bit31 = delay in ms).  remapDrums: ini [audio] RemapDrums.
    bool Play(const std::vector<uint8_t>& compact, bool loop, bool remapDrums = false);
    void Stop();
    bool IsPlaying();                               // running (a paused sequence still counts as running)
    void Update();                                  // call regularly from the main thread; polls only in manual-clock mode
    void BreakLoops();                              // CMidiPlayer::BreakLoops (moses.dll 0x10012fa9)
    void SetChannelMute(uint32_t channel, bool mute); // live; CMidiPlayer::SetChannelMute (0x10012ff9)
    void SetTempo(uint32_t num, uint32_t den);        // live; CMidiPlayer::SetTempo (0x100130ae)
    void Hold(bool on);                              // pause/resume keeping the position (not used by CAudioLib, see MidiSeq.h)
    uint64_t NowMs();

    MidiSequencer& Sequencer() { return seq_; } // tests: inspect only, not thread-safe while the worker runs

private:
    struct Impl;
    Impl* d_;
    MidiSequencer seq_;
};
