#include "platform/Sound.h"

#include "platform/Adpcm.h"

#include <cstdio>
#include <cstring>

static uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
static uint32_t Le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool LoadSound(const uint8_t* data, size_t size, Sound& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    out = Sound();
    if (size < 8) return fail("sound too short");
    uint32_t kind = Le32(data);
    out.param = Le32(data + 4);
    if (kind == 1) { // wave: moses.dll 0x10010a10 / CAudioWave::Init 0x10013e23
        if (size < 0x1c) return fail("wave header too short");
        int ch = Le16(data + 10);
        int rate = int(Le32(data + 12));
        int bits = Le16(data + 22);
        out.streamFlag = data[0x18] != 0;
        bool compressed = data[0x19] != 0;
        if (ch < 1 || ch > 2 || rate <= 0) return fail("bad wave format");
        const uint8_t* body = data + 0x1c;
        size_t n = size - 0x1c;
        auto pcm = std::make_shared<PcmBuffer>();
        pcm->channels = ch;
        pcm->rate = rate;
        if (compressed) {
            AdpcmDecode(body, n, ch == 2, pcm->samples);
        } else if (bits == 16) {
            pcm->samples.resize(n / 2);
            for (size_t i = 0; i < pcm->samples.size(); ++i) pcm->samples[i] = int16_t(Le16(body + 2 * i));
        } else if (bits == 8) {
            pcm->samples.resize(n);
            for (size_t i = 0; i < n; ++i) pcm->samples[i] = int16_t((int(body[i]) - 128) << 8);
        } else {
            return fail("unsupported wave bits");
        }
        out.kind = Sound::kWave;
        out.pcm = std::move(pcm);
        return true;
    }
    if (kind == 2) { // MIDI: moses.dll 0x10010a74
        out.kind = Sound::kMidi;
        out.midi.assign(data + 8, data + size);
        return true;
    }
    if (kind == 3) { // AVI reference: moses.dll 0x10010ab9 (0x104 byte NUL-terminated name)
        size_t n = 0;
        while (8 + n < size && n < 0x104 && data[8 + n]) ++n;
        out.kind = Sound::kAvi;
        out.aviName.assign(reinterpret_cast<const char*>(data + 8), n);
        return true;
    }
    return fail("unknown sound kind");
}

// ---------------------------------------------------------------------------------------------
// WP-5: WAV files
bool LoadWav(const uint8_t* data, size_t size, PcmBuffer& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) return fail("not a RIFF/WAVE file");
    int fmtTag = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* body = nullptr;
    size_t bodyLen = 0;
    size_t pos = 12;
    while (pos + 8 <= size) {
        uint32_t len = Le32(data + pos + 4);
        const uint8_t* p = data + pos + 8;
        size_t avail = size - (pos + 8);
        if (std::memcmp(data + pos, "fmt ", 4) == 0 && len >= 16 && avail >= 16) {
            fmtTag = Le16(p);
            ch = Le16(p + 2);
            rate = Le32(p + 4);
            bits = Le16(p + 14);
            if (fmtTag == 0xFFFE && len >= 26 && avail >= 26) fmtTag = Le16(p + 24); // WAVE_FORMAT_EXTENSIBLE: sub format tag
        } else if (std::memcmp(data + pos, "data", 4) == 0) {
            body = p;
            bodyLen = len > avail ? avail : len; // tolerate a truncated / streamed data chunk
            break;
        }
        pos += 8 + size_t(len) + (len & 1);
    }
    if (!body || ch < 1 || rate == 0) return fail("missing fmt/data chunk");
    bool isFloat = fmtTag == 3;
    if (!(fmtTag == 1 || isFloat)) return fail("unsupported wave format (PCM only)");
    int bytes = bits / 8;
    if (!((fmtTag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (isFloat && bits == 32))) return fail("unsupported sample size");
    size_t frames = bodyLen / (size_t(bytes) * size_t(ch));
    int outCh = ch >= 2 ? 2 : 1;
    out.channels = outCh;
    out.rate = int(rate);
    out.samples.assign(frames * size_t(outCh), 0);
    for (size_t f = 0; f < frames; ++f)
        for (int c = 0; c < outCh; ++c) {
            const uint8_t* s = body + (f * size_t(ch) + size_t(c)) * size_t(bytes);
            int v;
            if (isFloat) {
                float x;
                std::memcpy(&x, s, 4);
                v = int(x * 32767.0f);
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
            } else if (bits == 8) v = (int(s[0]) - 128) << 8;
            else if (bits == 16) v = int16_t(Le16(s));
            else if (bits == 24) v = int16_t(Le16(s + 1));
            else v = int16_t(Le16(s + 2));
            out.samples[f * size_t(outCh) + size_t(c)] = int16_t(v);
        }
    return true;
}

bool LoadWavFile(const std::string& path, PcmBuffer& out, std::string* err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "cannot open file";
        return false;
    }
    std::vector<uint8_t> buf;
    uint8_t tmp[65536];
    size_t n;
    while ((n = std::fread(tmp, 1, sizeof tmp, f)) > 0) buf.insert(buf.end(), tmp, tmp + n);
    std::fclose(f);
    return LoadWav(buf.data(), buf.size(), out, err);
}
