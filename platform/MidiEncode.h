// Standard MIDI File -> compact MIDI stream of sound kind 2 (WP-7). Inverse of CompactMidiToSmf (Midi.h).
//
// Compact stream (see Midi.h): dwords; bit31 set = delta time in "units" (1 unit = 1 ms, same assumption as
// CompactMidiToSmf / MidiTempoScale), otherwise a short channel event `status | d1<<8 | d2<<16`.
// A delta dword is emitted only when the time advances (never two in a row, none before events at the same
// time) and a final delta up to the end-of-track meta event is kept, exactly like the shipped streams.
// Sysex and meta events are dropped (the original stream cannot carry them); all tracks are merged.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Converts the tick times of the SMF (format 0/1/2, PPQN division) through the tempo map into milliseconds.
bool SmfToCompactMidi(const uint8_t* smf, size_t size, std::vector<uint8_t>& compact, std::string* err = nullptr);
