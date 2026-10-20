// Encoder for the custom 4-bit ADPCM of the wave resources (WP-7). Exact inverse of AdpcmDecode (Adpcm.h,
// moses.dll 0x10014790): at each sample the nibble whose decoded value is closest to the input is chosen
// (ties -> lowest nibble). Re-encoding the decoded PCM of any shipped wave gives back the original bytes
// (measured, see docs/WP7.md), so the greedy rule is at least consistent with the original encoder.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// pcm: `count` int16 samples (interleaved L,R when stereo). Output: ceil(count/2) bytes (low nibble first).
// For mono an odd `count` is padded with a repeat of the last sample; for stereo `count` must be even
// (a trailing lone sample is padded with a copy of itself).
void AdpcmEncode(const int16_t* pcm, size_t count, bool stereo, std::vector<uint8_t>& out);

// Signal-to-noise ratio in dB of `test` against `ref` (n samples): 10*log10(sum ref^2 / sum (ref-test)^2).
// Returns 999 for an exact match.
double SnrDb(const int16_t* ref, const int16_t* test, size_t n);
