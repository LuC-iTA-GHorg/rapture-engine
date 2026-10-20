// Headless test harness for the platform layer (no window, no audio device).
//
//   <game>.exe --rrs-test [GAME_DIR] [--dump OUT_DIR]
//
// Loads the archives of HostConfig::dataFiles, decodes every DIB and sound, converts MIDI, decodes every frame of
// the HostConfig::movies and prints counts + checksums. With --dump it also writes OUT_DIR/rrs_hashes.txt
// (per-resource CRC32 of the decoded data, compared against engine/tools/rrs_extract.py output
// by the scratch verifier) and a few AVI frames as PNG.
#include "tests/RrsTest.h"
#include "tests/RrsWrite_Test.h"

#include "host/HostConfig.h"

#include "platform/Adpcm.h"
#include "platform/Dib.h"
#include "platform/Midi.h"
#include "platform/Movie.h"
#include "platform/Rrs.h"
#include "platform/Sound.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

uint32_t Crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

uint64_t Fnv(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

// RGBA bytes of a top-down ARGB surface: transparent pixel -> 0,0,0,0 (same as rrs_extract.py PNGs).
std::vector<uint8_t> ToRgba(const uint32_t* px, size_t count) {
    std::vector<uint8_t> o(count * 4);
    for (size_t i = 0; i < count; ++i) {
        uint32_t v = px[i];
        o[i * 4] = uint8_t(v >> 16);
        o[i * 4 + 1] = uint8_t(v >> 8);
        o[i * 4 + 2] = uint8_t(v);
        o[i * 4 + 3] = uint8_t(v >> 24);
    }
    return o;
}

void WritePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba.begin() + size_t(y) * w * 4, rgba.begin() + size_t(y + 1) * w * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(n & 0xff); z.push_back(n >> 8);
        z.push_back(~n & 0xff); z.push_back((~n >> 8) & 0xff);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    uint32_t ad = b << 16 | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back(uint8_t(ad >> s));
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    auto chunk = [&](const char* t, const std::vector<uint8_t>& d) {
        for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(d.size() >> s));
        std::vector<uint8_t> td(t, t + 4);
        td.insert(td.end(), d.begin(), d.end());
        out.insert(out.end(), td.begin(), td.end());
        uint32_t c = Crc32(td.data(), td.size());
        for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(c >> s));
    };
    std::vector<uint8_t> ihdr;
    for (int v : {w, h})
        for (int s = 24; s >= 0; s -= 8) ihdr.push_back(uint8_t(v >> s));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
    }
}

bool FileExists(const std::string& p) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

int g_errors = 0;
void Error(const char* fmt, const std::string& a, uint32_t b = 0) {
    ++g_errors;
    std::printf("  ERROR: ");
    std::printf(fmt, a.c_str(), b);
    std::printf("\n");
}

} // namespace

int RunRrsTest(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--roundtrip")) return RunRrsRoundtrip(argc, argv); // WP-7: writer round-trip (tests/RrsWrite_Test.cpp)
    std::string dir;
    std::string dumpDir;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--rrs-test")) continue;
        if (!std::strcmp(argv[i], "--dump") && i + 1 < argc) dumpDir = argv[++i];
        else dir = argv[i];
    }
    if (dir.empty()) {
        for (const char* c : {".", "..", "../.."})
            if (rapture::Config().dataFileCount > 0 && FileExists(std::string(c) + "/" + rapture::Config().dataFiles[0].file)) {
                dir = c;
                break;
            }
    }
    if (dir.empty()) {
        std::printf("rrs-test: game data not found (pass the game folder as argument)\n");
        return 2;
    }
    std::printf("rrs-test: game dir = %s\n", dir.c_str());

    FILE* hashes = nullptr;
    if (!dumpDir.empty()) hashes = std::fopen((dumpDir + "/rrs_hashes.txt").c_str(), "w");

    for (int fi = 0; fi < rapture::Config().dataFileCount; ++fi) {
        std::string nameStr = rapture::Config().dataFiles[fi].file;   // "mus.rrs" -> "mus" (the names of rrs_hashes.txt)
        if (nameStr.size() > 4 && nameStr.compare(nameStr.size() - 4, 4, ".rrs") == 0) nameStr.resize(nameStr.size() - 4);
        const char* name = nameStr.c_str();
        rrs::Archive ar;
        std::string err;
        if (!ar.Open(dir + "/" + rapture::Config().dataFiles[fi].file, &err)) {
            Error("%s", err);
            continue;
        }
        std::map<int, int> typeCount;
        int dibs = 0, waves = 0, midis = 0, avis = 0, strings = 0, groups = 0, refs = 0, badIds = 0;
        uint64_t dibHash = 1469598103934665603ull, sndHash = 1469598103934665603ull;
        uint64_t dibPixels = 0, pcmFrames = 0;
        for (uint32_t i = 0; i < ar.Count(); ++i) {
            const rrs::Record& r = ar.Rec(i);
            typeCount[r.type]++;
            uint16_t t = ar.TypeOf(i);
            if (t == 0) {
                Error("alias chain broken at %s #%u", name, i);
                continue;
            }
            if (r.type == rrs::kAlias) continue;
            const uint8_t* p;
            uint32_t sz;
            if (t == rrs::kDib) {
                Dib d;
                if (!ar.GetData(i, p, sz) || !DecodeDib(p, sz, d, &err)) {
                    Error("DIB decode failed %s #%u", name, i);
                    continue;
                }
                ++dibs;
                dibPixels += uint64_t(d.Width()) * d.Height();
                int hdr[4] = {d.Width(), d.Height(), d.OriginX(), d.OriginY()};
                dibHash = Fnv(dibHash, hdr, sizeof(hdr));
                dibHash = Fnv(dibHash, d.Pixels(), size_t(d.Width()) * d.Height() * 4);
                if (hashes) {
                    auto rgba = ToRgba(d.Pixels(), size_t(d.Width()) * d.Height());
                    std::fprintf(hashes, "D %s %u %d %d %d %d %08x\n", name, i, d.Width(), d.Height(), d.OriginX(),
                                 d.OriginY(), Crc32(rgba.data(), rgba.size()));
                }
            } else if (t == rrs::kSound) {
                Sound s;
                if (!ar.GetData(i, p, sz) || !LoadSound(p, sz, s, &err)) {
                    Error("sound decode failed %s #%u", name, i);
                    continue;
                }
                if (s.kind == Sound::kWave) {
                    ++waves;
                    pcmFrames += s.pcm->Frames();
                    sndHash = Fnv(sndHash, s.pcm->samples.data(), s.pcm->samples.size() * 2);
                    if (hashes)
                        std::fprintf(hashes, "S %s %u %d %d %zu %08x\n", name, i, s.pcm->channels, s.pcm->rate,
                                     s.pcm->samples.size() * 2,
                                     Crc32(reinterpret_cast<const uint8_t*>(s.pcm->samples.data()),
                                           s.pcm->samples.size() * 2));
                } else if (s.kind == Sound::kMidi) {
                    ++midis;
                    auto smf = CompactMidiToSmf(s.midi.data(), s.midi.size());
                    if (hashes) std::fprintf(hashes, "M %s %u %zu %08x\n", name, i, smf.size(), Crc32(smf.data(), smf.size()));
                } else if (s.kind == Sound::kAvi) {
                    ++avis;
                    std::printf("  %s #%u: AVI ref '%s'\n", name, i, s.aviName.c_str());
                }
            } else if (t == rrs::kGroup || t == rrs::kGroup2 || t == rrs::kDibList || t == rrs::kDibTile) {
                std::vector<uint32_t> ids;
                if (!ar.GetGroup(i, ids)) {
                    Error("group parse failed %s #%u", name, i);
                    continue;
                }
                ++groups;
                for (uint32_t id : ids)
                    if (!ar.Valid(rrs::IdIndex(id))) ++badIds;
            } else if (t == rrs::kDibRef) {
                rrs::DibRefInfo ri;
                if (!ar.GetDibRef(i, ri) || ar.TypeOf(rrs::IdIndex(ri.targetId)) != rrs::kDib) {
                    Error("dibref bad %s #%u", name, i);
                    continue;
                }
                ++refs;
            } else if (t == rrs::kString) {
                std::string s;
                if (!ar.GetString(i, s)) Error("string bad %s #%u", name, i);
                ++strings;
            }
        }
        std::printf("%-9s records=%u dib=%d (px=%llu hash=%016llx) wave=%d (frames=%llu hash=%016llx) midi=%d avi=%d "
                    "group=%d dibref=%d string=%d badGroupIds=%d\n",
                    name, ar.Count(), dibs, (unsigned long long)dibPixels, (unsigned long long)dibHash, waves,
                    (unsigned long long)pcmFrames, (unsigned long long)sndHash, midis, avis, groups, refs, strings,
                    badIds);
        std::printf("          types:");
        for (auto& kv : typeCount) std::printf(" %d=%d", kv.first, kv.second);
        std::printf("\n");
        if (badIds) ++g_errors;
    }
    if (hashes) std::fclose(hashes);

    for (int mi = 0; mi < rapture::Config().movieCount; ++mi) {
        const char* avi = rapture::Config().movies[mi];
        Movie m;
        std::string err;
        if (!m.Open(dir + "/" + avi, &err)) {
            Error("%s", err);
            continue;
        }
        uint64_t h = 1469598103934665603ull;
        int decoded = 0;
        while (m.NextFrameIndex() < m.FrameCount()) {
            if (!m.DecodeNext()) {
                Error("frame decode failed in %s", avi, m.NextFrameIndex());
                break;
            }
            ++decoded;
            h = Fnv(h, m.Picture().Pixels(), size_t(m.Width()) * m.Height() * 4);
            if (!dumpDir.empty() && (decoded == 1 || decoded == 40 || decoded == 90 || decoded == m.FrameCount())) {
                auto rgba = ToRgba(m.Picture().Pixels(), size_t(m.Width()) * m.Height());
                WritePng(dumpDir + "/" + avi + "_" + std::to_string(decoded) + ".png", m.Width(), m.Height(), rgba);
            }
        }
        std::printf("%-10s %dx%d %.2f fps frames=%d decoded=%d audio=%s frames=%zu hash=%016llx\n", avi, m.Width(),
                    m.Height(), m.Fps(), m.FrameCount(), decoded,
                    m.AudioTrack() ? (m.AudioTrack()->channels == 2 ? "stereo" : "mono") : "none",
                    m.AudioTrack() ? m.AudioTrack()->Frames() : 0, (unsigned long long)h);
    }

    std::printf("rrs-test: %s (%d error%s)\n", g_errors ? "FAILED" : "OK", g_errors, g_errors == 1 ? "" : "s");
    return g_errors ? 1 : 0;
}
