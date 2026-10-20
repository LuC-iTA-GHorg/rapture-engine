// Custom 4-bit ADPCM used by the wave resources (CAudioWave, moses.dll 0x10014710 / 0x10014790).
// Standard IMA/DVI ADPCM with the step table pre-expanded to 8 columns (see AdpcmTables.h;
// the tables are computed, nothing is read from the DLL).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Decodes `size` bytes -> 2*size int16 samples (interleaved L,R when stereo).
// Low nibble first; mono: both nibbles on the same channel; stereo: low = ch0, high = ch1.
void AdpcmDecode(const uint8_t* in, size_t size, bool stereo, std::vector<int16_t>& out);
