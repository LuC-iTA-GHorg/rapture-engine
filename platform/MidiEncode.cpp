#include "platform/MidiEncode.h"

#include <algorithm>
#include <cmath>

namespace {

struct Ev {
    uint64_t tick;
    uint32_t order; // stable merge
    uint32_t dw;    // packed event, 0xffffffff = tempo change (value in `tempo`), 0xfffffffe = end of track
    uint32_t tempo;
};

bool ReadVlq(const uint8_t* d, size_t size, size_t& p, uint32_t& v) {
    v = 0;
    for (int i = 0; i < 4; ++i) {
        if (p >= size) return false;
        uint8_t b = d[p++];
        v = (v << 7) | (b & 0x7f);
        if (!(b & 0x80)) return true;
    }
    return false;
}

void PutDw(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i)));
}

} // namespace

bool SmfToCompactMidi(const uint8_t* d, size_t size, std::vector<uint8_t>& compact, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    compact.clear();
    if (size < 14 || d[0] != 'M' || d[1] != 'T' || d[2] != 'h' || d[3] != 'd') return fail("not a MIDI file");
    uint32_t hl = uint32_t(d[4]) << 24 | uint32_t(d[5]) << 16 | uint32_t(d[6]) << 8 | d[7];
    int ntrk = d[10] << 8 | d[11];
    int div = d[12] << 8 | d[13];
    if (div & 0x8000) return fail("SMPTE time division not supported");
    if (div == 0) return fail("bad division");
    size_t p = 8 + hl;
    std::vector<Ev> evs;
    uint32_t order = 0;
    uint64_t endTick = 0;
    for (int t = 0; t < ntrk; ++t) {
        if (p + 8 > size || d[p] != 'M' || d[p + 1] != 'T' || d[p + 2] != 'r' || d[p + 3] != 'k') return fail("bad track chunk");
        uint32_t len = uint32_t(d[p + 4]) << 24 | uint32_t(d[p + 5]) << 16 | uint32_t(d[p + 6]) << 8 | d[p + 7];
        p += 8;
        size_t end = std::min<size_t>(size, p + len);
        uint64_t tick = 0;
        uint8_t running = 0;
        while (p < end) {
            uint32_t dt;
            if (!ReadVlq(d, end, p, dt)) return fail("truncated delta");
            tick += dt;
            if (p >= end) return fail("truncated event");
            uint8_t st = d[p];
            if (st == 0xff) { // meta
                if (p + 2 > end) return fail("truncated meta");
                uint8_t type = d[p + 1];
                p += 2;
                uint32_t ml;
                if (!ReadVlq(d, end, p, ml) || p + ml > end) return fail("bad meta length");
                if (type == 0x51 && ml == 3) {
                    evs.push_back({tick, order++, 0xffffffffu, uint32_t(d[p]) << 16 | uint32_t(d[p + 1]) << 8 | d[p + 2]});
                } else if (type == 0x2f) {
                    evs.push_back({tick, order++, 0xfffffffeu, 0});
                    endTick = std::max(endTick, tick);
                }
                p += ml;
                running = 0;
            } else if (st == 0xf0 || st == 0xf7) { // sysex: dropped
                ++p;
                uint32_t sl;
                if (!ReadVlq(d, end, p, sl) || p + sl > end) return fail("bad sysex length");
                p += sl;
                running = 0;
            } else {
                if (st & 0x80) {
                    running = st;
                    ++p;
                } else if (!running) {
                    return fail("data byte without running status");
                }
                int n = ((running & 0xf0) == 0xc0 || (running & 0xf0) == 0xd0) ? 1 : 2;
                if (p + n > end) return fail("truncated channel event");
                uint32_t dw = running | uint32_t(d[p]) << 8;
                if (n == 2) dw |= uint32_t(d[p + 1]) << 16;
                p += n;
                evs.push_back({tick, order++, dw, 0});
            }
        }
        p = end;
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.tick < b.tick; });

    // tick -> microseconds through the tempo map, then to whole milliseconds
    double us = 0;
    uint64_t lastTick = 0;
    uint32_t tempo = 500000;
    uint64_t curMs = 0;
    auto emitDelta = [&](uint64_t ms) {
        while (ms > 0) {
            uint32_t c = uint32_t(std::min<uint64_t>(ms, 0x7fffffffu));
            PutDw(compact, 0x80000000u | c);
            ms -= c;
        }
    };
    for (const Ev& e : evs) {
        us += double(e.tick - lastTick) * tempo / div;
        lastTick = e.tick;
        if (e.dw == 0xffffffffu) {
            tempo = e.tempo ? e.tempo : 500000;
            continue;
        }
        uint64_t ms = uint64_t(std::llround(us / 1000.0));
        if (ms > curMs) {
            emitDelta(ms - curMs);
            curMs = ms;
        }
        if (e.dw != 0xfffffffeu) PutDw(compact, e.dw);
    }
    (void)endTick;
    return true;
}
