#include "platform/Adpcm.h"

#include "platform/AdpcmTables.h"

// moses.dll 0x10014790
void AdpcmDecode(const uint8_t* in, size_t size, bool stereo, std::vector<int16_t>& out) {
    out.resize(size * 2);
    int32_t pred[2] = {0, 0};
    int idx[2] = {0, 0};
    size_t o = 0;
    for (size_t i = 0; i < size; ++i) {
        for (int k = 0; k < 2; ++k) {
            int nib = k ? (in[i] >> 4) : (in[i] & 0xf);
            int c = stereo ? k : 0;
            int delta = kAdpcmStep[idx[c] + (nib & 7)];
            if (nib & 8) delta = -delta;
            int32_t p = pred[c] + delta;
            if (p > 32767) p = 32767;
            if (p < -32768) p = -32768;
            pred[c] = p;
            int ni = idx[c] + kAdpcmAdj[nib];
            idx[c] = ni < 0 ? 0 : (ni > 0x2c0 ? 0x2c0 : ni);
            out[o++] = int16_t(p);
        }
    }
}
