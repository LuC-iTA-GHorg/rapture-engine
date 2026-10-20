// WP-7 tests: .rrs writer + DIB / ADPCM / MIDI encoders.  See tests/RrsWrite_Test.h and docs/WP7.md.
#include "tests/RrsWrite_Test.h"

#include "platform/AdpcmEncode.h"
#include "platform/Adpcm.h"
#include "platform/DibEncode.h"
#include "platform/Midi.h"
#include "platform/MidiEncode.h"
#include "platform/Rrs.h"
#include "platform/RrsWriter.h"
#include "platform/Sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int g_errors = 0;
void Fail(const char* fmt, const std::string& a = std::string(), const std::string& b = std::string()) {
    ++g_errors;
    std::printf("  FAIL: ");
    std::printf(fmt, a.c_str(), b.c_str());
    std::printf("\n");
}
#define CHECK(c, msg)                                   \
    do {                                                \
        if (!(c)) Fail("%s%s", std::string(msg), std::string()); \
    } while (0)

uint32_t Rng(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

// ---------------------------------------------------------------------------------------------------------------
void SelfTest() {
    std::printf("rrs-write: synthetic self-test\n");
    uint32_t seed = 12345;
    // DIB: random sprites with transparent areas, runs, long runs (> 127), big rows
    int dibOk = 0, dibTot = 0;
    for (int t = 0; t < 40; ++t) {
        DibRaw d;
        d.w = 1 + int(Rng(seed) % 300);
        d.h = 1 + int(Rng(seed) % 40);
        d.ox = int(Rng(seed) % 50) - 25;
        d.oy = int(Rng(seed) % 50) - 25;
        d.px.resize(size_t(d.w) * d.h);
        for (int y = 0; y < d.h; ++y)
            for (int x = 0; x < d.w; ++x) {
                uint32_t r = Rng(seed) % 10;
                uint16_t v = r < 4 ? 0 : (r < 7 ? 0x8000 : uint16_t(0x8000 | (Rng(seed) & 0x7fff)));
                if (x && r >= 7 && (Rng(seed) & 1)) v = d.px[size_t(y) * d.w + x - 1];
                d.px[size_t(y) * d.w + x] = v;
            }
        std::vector<uint8_t> enc = EncodeDib(d);
        DibRaw back;
        std::string err;
        ++dibTot;
        if (!DecodeDibRaw(enc.data(), enc.size(), back, &err)) {
            Fail("DIB decode failed: %s%s", err);
            continue;
        }
        if (back.px != d.px || back.w != d.w || back.h != d.h || back.ox != d.ox || back.oy != d.oy) Fail("DIB round trip mismatch%s%s");
        else if (EncodeDib(back) != enc) Fail("DIB re-encode not stable%s%s");
        else ++dibOk;
    }
    // big DIB (dword line table)
    {
        DibRaw d;
        d.w = 640;
        d.h = 480;
        d.px.resize(size_t(d.w) * d.h);
        for (auto& v : d.px) v = uint16_t(0x8000 | (Rng(seed) & 0x7fff));
        std::vector<uint8_t> enc = EncodeDib(d);
        DibRaw back;
        CHECK(enc[9] == 7, "large opaque DIB must have flags 7");
        CHECK(DecodeDibRaw(enc.data(), enc.size(), back) && back.px == d.px, "large DIB round trip");
    }
    std::printf("  dib synthetic: %d/%d lossless\n", dibOk, dibTot);

    // ADPCM: sine + noise
    {
        std::vector<int16_t> pcm(22050);
        for (size_t i = 0; i < pcm.size(); ++i)
            pcm[i] = int16_t(12000 * std::sin(i * 0.05) + 3000 * std::sin(i * 0.31) + int(Rng(seed) % 200) - 100);
        std::vector<uint8_t> enc;
        std::vector<int16_t> dec;
        AdpcmEncode(pcm.data(), pcm.size(), false, enc);
        AdpcmDecode(enc.data(), enc.size(), false, dec);
        double snr = SnrDb(pcm.data(), dec.data(), pcm.size());
        std::printf("  adpcm synthetic mono SNR = %.1f dB (%zu samples -> %zu bytes)\n", snr, pcm.size(), enc.size());
        CHECK(enc.size() == pcm.size() / 2 && snr > 25.0, "ADPCM mono SNR too low");
        std::vector<int16_t> st(pcm.size());
        for (size_t i = 0; i + 1 < st.size(); i += 2) {
            st[i] = pcm[i];
            st[i + 1] = int16_t(-pcm[i] / 2);
        }
        AdpcmEncode(st.data(), st.size(), true, enc);
        AdpcmDecode(enc.data(), enc.size(), true, dec);
        snr = SnrDb(st.data(), dec.data(), st.size());
        std::printf("  adpcm synthetic stereo SNR = %.1f dB\n", snr);
        CHECK(snr > 25.0, "ADPCM stereo SNR too low");
    }

    // MIDI: compact -> SMF -> compact
    {
        std::vector<uint8_t> c;
        auto dw = [&](uint32_t v) {
            for (int i = 0; i < 4; ++i) c.push_back(uint8_t(v >> (8 * i)));
        };
        dw(0x6bc0);
        dw(0x400ab0);
        dw(0x3f2d9f);
        dw(0x80000049);
        dw(0x2d9f);
        dw(0x80000117);
        dw(0x4890);
        dw(0x800001da);
        std::vector<uint8_t> smf = CompactMidiToSmf(c.data(), c.size());
        std::vector<uint8_t> back;
        std::string err;
        CHECK(SmfToCompactMidi(smf.data(), smf.size(), back, &err), "SMF parse failed");
        CHECK(back == c, "compact MIDI -> SMF -> compact not identical");
    }

    // Image: build from scratch, save, reopen, compare
    {
        rrs::Image im;
        im.layout = rrs::Image::kDirFirst;
        DibRaw d;
        d.w = 5;
        d.h = 3;
        d.ox = 1;
        d.oy = 2;
        d.px.assign(15, 0x8000);
        d.px[2] = 0;
        uint32_t a = im.Add(rrs::kDib, 7, EncodeDib(d));
        uint32_t al = im.AddAlias(a, 9);
        uint32_t g = im.Add(rrs::kGroup, 0, rrs::Image::MakeGroup({0x820000 | a, 0x820000 | al}));
        im.Add(rrs::kDibRef, 0, rrs::Image::MakeDibRef(0x820000 | a, 3, -4));
        im.Add(rrs::kString, 0, rrs::Image::MakeString("hello"));
        PcmBuffer p;
        p.samples.assign(100, 1000);
        im.Add(rrs::kSound, 0, rrs::Image::MakeWaveSound(p, false));
        im.Add(rrs::kSound, 0, rrs::Image::MakeAviSound("x.avi"));
        (void)g;
        for (int lay = 0; lay < 2; ++lay) {
            im.layout = lay ? rrs::Image::kDataFirst : rrs::Image::kDirFirst;
            std::string path = (std::filesystem::temp_directory_path() / "rrs_wp7_selftest.rrs").string();
            std::string err;
            CHECK(im.Save(path, &err), "Save failed");
            rrs::Archive ar;
            CHECK(ar.Open(path, &err), "reopen failed");
            CHECK(ar.Count() == im.Count(), "count mismatch");
            std::string s;
            CHECK(ar.GetString(4, s) && s == "hello", "string");
            std::vector<uint32_t> ids;
            CHECK(ar.GetGroup(2, ids) && ids.size() == 2 && (ids[1] & 0xffff) == 1, "group");
            rrs::DibRefInfo ri;
            CHECK(ar.GetDibRef(3, ri) && ri.x == 3 && ri.y == -4, "dibref");
            CHECK(ar.Rec(1).size == ar.Rec(0).size, "alias size");
            rrs::Image im2;
            CHECK(im2.Load(ar, &err) && im2.Serialize() == im.Serialize(), "reload/serialize not stable");
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
    std::printf("rrs-write: self-test %s\n", g_errors ? "FAILED" : "ok");
}

// ---------------------------------------------------------------------------------------------------------------
struct Totals {
    int archives = 0, archivesIdentical = 0, rebuildIdentical = 0;
    size_t dibs = 0, dibsIdentical = 0, waves = 0, wavesIdentical = 0, midis = 0, midisIdentical = 0, pools = 0, poolsIdentical = 0;
    size_t aliasSizeMismatch = 0;
    double snrSum = 0, snrMin = 1e9;
    size_t snrCount = 0;
};

size_t FirstDiff(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    size_t n = std::min(a.size(), b.size()), i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}

void TestArchive(const std::string& path, Totals& T) {
    rrs::Archive ar;
    std::string err;
    if (!ar.Open(path, &err)) {
        Fail("open: %s%s", err);
        return;
    }
    ++T.archives;
    rrs::Image im;
    if (!im.Load(ar, &err)) {
        Fail("image load: %s%s", err);
        return;
    }
    // (a) byte-identical re-serialization of the loaded archive
    std::vector<uint8_t> out = im.Serialize();
    bool same = out == ar.File();
    if (same) ++T.archivesIdentical;
    else Fail("round trip differs at offset %s%s", std::to_string(FirstDiff(out, ar.File())));

    // (b) rebuild from decoded content with the encoders
    rrs::Image nw;
    nw.layout = im.layout;
    nw.version = im.version;
    nw.reserved = im.reserved;
    size_t dibs = 0, dibsOk = 0, waves = 0, wavesOk = 0, midis = 0, midisOk = 0, pools = 0, poolsOk = 0;
    double snrSum = 0, snrMin = 1e9;
    size_t snrCount = 0;
    for (uint32_t i = 0; i < ar.Count(); ++i) {
        const rrs::Record& r = ar.Rec(i);
        const std::vector<uint8_t>& blob = im.At(i).blob;
        std::vector<uint8_t> nb;
        switch (r.type) {
        case rrs::kAlias:
            nw.AddAlias(r.offset, r.tag);
            nw.At(i).rec.offset = r.offset; // keep the high word (alias_sub)
            continue;
        case rrs::kDib: {
            DibRaw d;
            ++dibs;
            if (!DecodeDibRaw(blob.data(), blob.size(), d, &err)) {
                Fail("dib decode: %s%s", err);
                nb = blob;
                break;
            }
            nb = EncodeDib(d);
            dibsOk += nb == blob;
            break;
        }
        case rrs::kSound: {
            Sound s;
            if (!LoadSound(blob.data(), blob.size(), s, &err)) {
                Fail("sound: %s%s", err);
                nb = blob;
                break;
            }
            if (s.kind == Sound::kWave) {
                ++waves;
                bool compressed = blob[0x19] != 0;
                nb = rrs::Image::MakeWaveSound(*s.pcm, s.streamFlag, s.param, compressed);
                wavesOk += nb == blob;
                if (compressed) { // SNR of the codec on a signal that is NOT already a decoded ADPCM stream: 0.73 gain
                    std::vector<int16_t> src(s.pcm->samples.size()), dec;
                    for (size_t k = 0; k < src.size(); ++k) src[k] = int16_t(std::lround(s.pcm->samples[k] * 0.73));
                    std::vector<uint8_t> e;
                    AdpcmEncode(src.data(), src.size(), s.pcm->channels == 2, e);
                    AdpcmDecode(e.data(), e.size(), s.pcm->channels == 2, dec);
                    double snr = SnrDb(src.data(), dec.data(), std::min(src.size(), dec.size()));
                    if (snr < 998) {
                        snrSum += snr;
                        snrMin = std::min(snrMin, snr);
                        ++snrCount;
                    }
                }
            } else if (s.kind == Sound::kMidi) {
                ++midis;
                std::vector<uint8_t> smf = CompactMidiToSmf(s.midi.data(), s.midi.size()), back;
                if (!SmfToCompactMidi(smf.data(), smf.size(), back, &err)) Fail("smf: %s%s", err);
                midisOk += back == s.midi;
                nb = rrs::Image::MakeMidiSound(back, s.param);
            } else {
                nb = rrs::Image::MakeAviSound(s.aviName, s.param);
            }
            break;
        }
        case rrs::kGroup:
        case rrs::kGroup2:
        case rrs::kDibList:
        case rrs::kDibTile:
        case rrs::kDibRef:
        case rrs::kString: {
            ++pools;
            std::vector<uint32_t> ids;
            if (r.type == rrs::kString) {
                std::string s;
                ar.GetString(i, s);
                nb = rrs::Image::MakeString(s);
            } else if (r.type == rrs::kDibRef) {
                rrs::DibRefInfo ri;
                ar.GetDibRef(i, ri);
                nb = rrs::Image::MakeDibRef(ri.targetId, ri.x, ri.y);
            } else if (r.type == rrs::kDibTile) {
                rrs::DibTileInfo ti;
                ar.GetDibTile(i, ti);
                ar.GetGroup(i, ids);
                nb = rrs::Image::MakeDibTile(ti, ids);
            } else {
                ar.GetGroup(i, ids);
                nb = r.type == rrs::kGroup2 ? rrs::Image::MakeGroup(ids, true)
                     : r.type == rrs::kDibList ? rrs::Image::MakeDibList(ids)
                                               : rrs::Image::MakeGroup(ids, false);
            }
            poolsOk += nb == blob;
            break;
        }
        default:
            nb = blob; // raw / palette: opaque
            break;
        }
        nw.Add(r.type, r.tag, nb);
    }
    nw.SetOrder(im.FileOrder(), im.PoolOrder());
    // aliases: size must come out of RefreshAliasSizes exactly like in the original
    size_t before = 0;
    for (uint32_t i = 0; i < nw.Count(); ++i)
        if (nw.At(i).rec.type == rrs::kAlias) nw.At(i).rec.size = 0;
    nw.RefreshAliasSizes();
    for (uint32_t i = 0; i < nw.Count(); ++i)
        if (nw.At(i).rec.type == rrs::kAlias && nw.At(i).rec.size != ar.Rec(i).size) ++before;
    T.aliasSizeMismatch += before;
    std::vector<uint8_t> out2 = nw.Serialize();
    bool same2 = out2 == ar.File();
    if (same2) ++T.rebuildIdentical;
    std::printf("  %-14s %9zu bytes  roundtrip:%s  rebuild:%s  dib %zu/%zu  wave %zu/%zu  midi %zu/%zu  pool %zu/%zu",
                std::filesystem::path(path).filename().string().c_str(), ar.File().size(), same ? "IDENTICAL" : "DIFFERENT",
                same2 ? "IDENTICAL" : "DIFFERENT", dibsOk, dibs, wavesOk, waves, midisOk, midis, poolsOk, pools);
    if (snrCount) std::printf("  snr avg %.1f dB", snrSum / snrCount);
    std::printf("\n");
    if (!same2) {
        Fail("rebuild differs at offset %s%s", std::to_string(FirstDiff(out2, ar.File())));
    }
    T.dibs += dibs;
    T.dibsIdentical += dibsOk;
    T.waves += waves;
    T.wavesIdentical += wavesOk;
    T.midis += midis;
    T.midisIdentical += midisOk;
    T.pools += pools;
    T.poolsIdentical += poolsOk;
    T.snrSum += snrSum;
    T.snrCount += snrCount;
    T.snrMin = std::min(T.snrMin, snrMin);
}

} // namespace

int RunRrsRoundtrip(int argc, char** argv) {
    std::string dir = ".";
    bool selfOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--rrs-test") || !std::strcmp(argv[i], "--roundtrip")) continue;
        if (!std::strcmp(argv[i], "--selftest-only")) selfOnly = true;
        else dir = argv[i];
    }
    g_errors = 0;
    SelfTest();
    if (selfOnly) return g_errors ? 1 : 0;

    std::vector<std::string> files;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext == ".rrs") files.push_back(it->path().string());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::printf("rrs-write: no .rrs files under '%s' (pass the game folder)\n", dir.c_str());
        return g_errors ? 1 : 0;
    }
    std::printf("rrs-write: round trip of %zu archive(s) under %s\n", files.size(), dir.c_str());
    Totals T;
    for (const std::string& f : files) TestArchive(f, T);
    std::printf("rrs-write: archives byte-identical (load->serialize) %d/%d, rebuilt from content %d/%d\n", T.archivesIdentical, T.archives,
                T.rebuildIdentical, T.archives);
    std::printf("rrs-write: DIB re-encode identical %zu/%zu, wave re-encode identical %zu/%zu, MIDI via SMF identical %zu/%zu, pool blobs %zu/%zu, alias size mismatches %zu\n",
                T.dibsIdentical, T.dibs, T.wavesIdentical, T.waves, T.midisIdentical, T.midis, T.poolsIdentical, T.pools, T.aliasSizeMismatch);
    if (T.snrCount) std::printf("rrs-write: ADPCM SNR on 0.73-gain real audio: avg %.1f dB, min %.1f dB (%zu waves)\n", T.snrSum / T.snrCount, T.snrMin, T.snrCount);
    if (T.archivesIdentical != T.archives) ++g_errors;
    if (T.aliasSizeMismatch) ++g_errors;
    std::printf("rrs-write: %s (%d error%s)\n", g_errors ? "FAILED" : "OK", g_errors, g_errors == 1 ? "" : "s");
    return g_errors ? 1 : 0;
}
