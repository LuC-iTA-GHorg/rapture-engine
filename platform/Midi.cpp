#include "platform/Midi.h"

double MidiTempoScale = 1.0;

namespace {
void Vlq(std::vector<uint8_t>& o, uint32_t n) {
    uint8_t tmp[5];
    int k = 0;
    tmp[k++] = n & 0x7f;
    n >>= 7;
    while (n) {
        tmp[k++] = uint8_t(0x80 | (n & 0x7f));
        n >>= 7;
    }
    while (k) o.push_back(tmp[--k]);
}
void Be32(std::vector<uint8_t>& o, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back(uint8_t(v >> s));
}
} // namespace

// Same output as midi_to_smf() in engine/tools/rrs_extract.py (when MidiTempoScale == 1).
std::vector<uint8_t> CompactMidiToSmf(const uint8_t* data, size_t size) {
    std::vector<uint8_t> trk;
    // tempo meta: 1,000,000 us per quarter note with division 1000 -> 1 tick = 1 ms
    const uint8_t tempo[] = {0x00, 0xff, 0x51, 0x03, 0x0f, 0x42, 0x40};
    trk.insert(trk.end(), tempo, tempo + sizeof(tempo));
    uint64_t delta = 0;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t dw = uint32_t(data[i]) | uint32_t(data[i + 1]) << 8 | uint32_t(data[i + 2]) << 16 |
                      uint32_t(data[i + 3]) << 24;
        if (dw & 0x80000000u) {
            delta += dw & 0x7fffffffu;
            continue;
        }
        uint8_t st = dw & 0xff;
        if (st < 0x80) continue;
        int len = ((st & 0xf0) == 0xc0 || (st & 0xf0) == 0xd0) ? 2 : 3;
        Vlq(trk, uint32_t(double(delta) * MidiTempoScale));
        trk.push_back(st);
        trk.push_back(uint8_t(dw >> 8));
        if (len == 3) trk.push_back(uint8_t(dw >> 16));
        delta = 0;
    }
    Vlq(trk, uint32_t(double(delta) * MidiTempoScale));
    trk.push_back(0xff);
    trk.push_back(0x2f);
    trk.push_back(0x00);

    std::vector<uint8_t> smf = {'M', 'T', 'h', 'd'};
    Be32(smf, 6);
    const uint8_t hdr[] = {0, 0, 0, 1, 0x03, 0xe8}; // format 0, 1 track, division 1000
    smf.insert(smf.end(), hdr, hdr + sizeof(hdr));
    smf.push_back('M');
    smf.push_back('T');
    smf.push_back('r');
    smf.push_back('k');
    Be32(smf, uint32_t(trk.size()));
    smf.insert(smf.end(), trk.begin(), trk.end());
    return smf;
}

std::vector<uint8_t> MidiPrepareCompact(const std::vector<uint8_t>& in, bool remapDrums, uint32_t muteMask, uint32_t num, uint32_t den) {
    if (!remapDrums && muteMask == 0 && num == den) return in;
    std::vector<uint8_t> out;
    out.reserve(in.size());
    for (size_t i = 0; i + 4 <= in.size(); i += 4) {
        uint32_t v = uint32_t(in[i]) | uint32_t(in[i + 1]) << 8 | uint32_t(in[i + 2]) << 16 | uint32_t(in[i + 3]) << 24;
        if (v & 0x80000000u) {
            if (num != den && den != 0) v = 0x80000000u | uint32_t((uint64_t(v & 0x7fffffffu) * num / den) & 0x7fffffffu);
        } else {
            if (remapDrums) v = MidiRemapDrumsMsg(v);
            if ((v & 0xf0) == 0x90 && (muteMask >> (v & 0xf) & 1)) continue;
        }
        for (int k = 0; k < 4; ++k) out.push_back(uint8_t(v >> (8 * k)));
    }
    return out;
}