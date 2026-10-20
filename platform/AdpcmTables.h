#pragma once
// ADPCM tables used by the RRS 4:1 wave codec (CAudioWave).
//
// The codec is the standard IMA/DVI ADPCM algorithm, so the tables are computed here from the
// published 89-entry IMA step-size sequence (no data is copied from any game binary):
//   kAdpcmStep[i*8 + m] = the dequantised difference for step-size i and 3-bit magnitude m
//                          = (s>>3) + (m&1 ? s>>2 : 0) + (m&2 ? s>>1 : 0) + (m&4 ? s : 0),  s = IMA_STEP[i]
//   kAdpcmAdj[n]        = 8 * the IMA index adjustment for nibble n (-1,-1,-1,-1,2,4,6,8, twice);
//                          the index is stored premultiplied by 8 (0 .. 0x2C0).
// The values were verified to be identical to the tables of the original moses.dll
// (0x1001bba0 / 0x1001c130).
#include <cstdint>

struct AdpcmTables {
    uint16_t step[712];
    int16_t adj[16];
    constexpr AdpcmTables() : step(), adj() {
        constexpr uint16_t ima[89] = {
            7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
            31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
            130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
            544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
            2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
            9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
        for (int i = 0; i < 89; ++i) {
            const int s = ima[i];
            for (int m = 0; m < 8; ++m) {
                int d = s >> 3;
                if (m & 1) d += s >> 2;
                if (m & 2) d += s >> 1;
                if (m & 4) d += s;
                step[i * 8 + m] = static_cast<uint16_t>(d);
            }
        }
        const int adjIma[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
        for (int n = 0; n < 16; ++n) adj[n] = static_cast<int16_t>(adjIma[n & 7] * 8);
    }
};

inline constexpr AdpcmTables kAdpcmTables{};
static constexpr const uint16_t (&kAdpcmStep)[712] = kAdpcmTables.step;
static constexpr const int16_t (&kAdpcmAdj)[16] = kAdpcmTables.adj;
