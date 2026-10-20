#include "platform/AdpcmEncode.h"

#include "platform/AdpcmTables.h"

#include <cmath>
#include <cstdlib>

// Inverse of moses.dll 0x10014790.
void AdpcmEncode(const int16_t* pcm, size_t count, bool stereo, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve((count + 1) / 2);
    int32_t pred[2] = {0, 0};
    int idx[2] = {0, 0};
    size_t total = (count + 1) & ~size_t(1);
    for (size_t i = 0; i < total; ++i) {
        int c = stereo ? int(i & 1) : 0;
        int x;
        if (i < count) x = pcm[i];
        else x = count ? pcm[count - 1] : 0;
        int bestN = 0, bestErr = 0x7fffffff;
        int32_t bestP = 0;
        for (int n = 0; n < 16; ++n) {
            int delta = kAdpcmStep[idx[c] + (n & 7)];
            if (n & 8) delta = -delta;
            int32_t p = pred[c] + delta;
            if (p > 32767) p = 32767;
            if (p < -32768) p = -32768;
            int e = std::abs(x - p);
            if (e < bestErr) {
                bestErr = e;
                bestN = n;
                bestP = p;
            }
        }
        pred[c] = bestP;
        int ni = idx[c] + kAdpcmAdj[bestN];
        idx[c] = ni < 0 ? 0 : (ni > 0x2c0 ? 0x2c0 : ni);
        if (!(i & 1)) out.push_back(uint8_t(bestN));
        else out.back() |= uint8_t(bestN << 4);
    }
}

double SnrDb(const int16_t* ref, const int16_t* test, size_t n) {
    double s = 0, e = 0;
    for (size_t i = 0; i < n; ++i) {
        double r = ref[i], d = r - test[i];
        s += r * r;
        e += d * d;
    }
    if (e == 0) return 999.0;
    if (s == 0) return -999.0;
    return 10.0 * std::log10(s / e);
}
