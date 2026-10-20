// MIDI support for sound kind 2 (compact stream, moses.dll 0x10010a74 / MidiSetTempo 0x10010708).
//
// Stream = sequence of dwords: bit31 set -> delta time (low 31 bits), otherwise a short MIDI
// event packed as status | d1<<8 | d2<<16 (program change / channel pressure have 2 bytes).
//
// TEMPO UNCERTAINTY: the unit of the delta values was never verified in the original code.
// CompactMidiToSmf() assumes 1 delta unit = 1 ms (division 1000, tempo 1,000,000 us/quarter),
// identical to engine/tools/rrs_extract.py. If the music sounds too fast/slow, change
// kMsPerDeltaUnit-equivalent in the SMF header (MidiTempoScale below).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Multiply delta times by this when converting (1.0 = 1 unit is 1 ms).
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

// Windows-only playback (MCI "sequencer" device; writes the SMF to a temp file). On other
// platforms every call is a harmless no-op returning false. Isolated in Midi_win.cpp.
class MidiPlayer {
public:
    ~MidiPlayer() { Stop(); }
    bool Play(const std::vector<uint8_t>& smf, bool loop);
    void Stop();
    bool IsPlaying();
    void Update(); // call regularly; restarts the sequence when looping and it has ended
    void BreakLoops() { loop_ = false; } // WP-5: CMidiPlayer::BreakLoops (moses.dll 0x10629): finish the current run, do not restart

private:
    bool open_ = false;
    bool loop_ = false;
    std::string tmpPath_;
};
