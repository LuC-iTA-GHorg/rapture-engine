// Package 2 tests: CMem, path helpers, CResfile/CResMgr over the real .rrs (if present), CAudioLib headless.
// Never opens an audio device or a window.
#include "host/HostConfig.h"
#include "moses/AudioLib.h"
#include "moses/Mem.h"
#include "moses/ResMgr.h"
#include "tests/EngineTests.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace moses;

namespace {
int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

struct Ev { u32 id; int n; u32 u0; };
std::vector<Ev> g_ev;
void Proc(void*, u32 id, SNDNOTIFY n, PLAYSOUNDINFO* info) { g_ev.push_back({id, int(n), info ? info->user0 : 0}); }

void TestMem() {
    std::printf("[mem/paths]\n");
    MEMINFO a, b;
    CMem::GetMemInfo(&a);
    CMem m(0, 0);
    u8* p = (u8*)m.Alloc(100, 1);
    CHECK(p && p[0] == 0 && p[99] == 0);
    std::memset(p, 7, 100);
    u8* q = (u8*)m.Copy(p);
    CHECK(q && q[50] == 7);
    q = (u8*)m.Realloc(q, 200, 1);
    CHECK(q[99] == 7 && q[150] == 0);
    CHECK(m.Free(p) == nullptr);
    m.Free(q);
    CMem::GetMemInfo(&b);
    CHECK(b.allocs - a.allocs == 2 && b.frees - a.frees == 2 && b.bytes == a.bytes && b.peak >= 300);
    char path[260], file[260], full[260];
    GetPathAndFile("C:\\games\\mus\\game.rrs", path, file);
    CHECK(!std::strcmp(path, "C:\\games\\mus\\") && !std::strcmp(file, "game.rrs"));
    MakeFullPath(full, "C:\\games\\Host.exe", "x.ini");
    CHECK(!std::strcmp(full, "C:\\games\\x.ini"));
    std::strcpy(full, "sub\\a.rrs");
    MakeFilePath(full, "C:\\games\\Host.exe");
    CHECK(!std::strcmp(full, "C:\\games\\sub\\a.rrs"));
    CHECK(!FileExists("definitely_missing.file"));
}

void TestSynthAudio() {
    std::printf("[audio headless]\n");
    CAudioLib lib;
    lib.EnableDevice(false);
    lib.SetSoundProc(Proc, nullptr);
    ::Sound s;
    s.kind = ::Sound::kWave;
    s.pcm = std::make_shared<PcmBuffer>();
    s.pcm->samples.assign(2205, 1000);
    CAudioSnd* w = lib.SoundCreate(std::move(s));
    CHECK(w);
    lib.SoundSetId(w, 0x12345);
    PLAYSOUNDINFO bad; bad.size = 5;
    CHECK(lib.SoundPlay(w, &bad) == 0);
    PLAYSOUNDINFO inf; inf.user0 = 77;
    CHECK(lib.SoundPlay(w, &inf) == 1);
    lib.Service();
    CHECK(g_ev.size() == 2 && g_ev[0].n == SND_STARTED && g_ev[1].n == SND_ENDED && g_ev[0].id == 0x12345 && g_ev[0].u0 == 77);
    g_ev.clear();
    lib.SoundPlay(w, nullptr);
    lib.SoundStop(nullptr);
    lib.Service();
    CHECK(g_ev.size() == 2 && g_ev[1].n == SND_ENDED);
    // mixer state
    CHECK(lib.volume(MIX_WAVE, 1, 150) == 100);
    CHECK(lib.volume(MIX_MIDI, 1, 40) == 40 && lib.volume(MIX_MIDI, 0, 0) == 40);
    CHECK(lib.mute(MIX_WAVE, 1, 1) == 1 && lib.mute(MIX_WAVE, 0, 0) == 1);
    lib.Scroll(5); lib.SetPanInfo(10, -20);
    // midi priority
    g_ev.clear();
    ::Sound m1; m1.kind = ::Sound::kMidi; m1.param = 5; m1.midi = {0x00, 0x00, 0x00, 0x80};
    ::Sound m2 = m1; m2.param = 3;
    CAudioSnd* a = lib.SoundCreate(std::move(m1));
    CAudioSnd* b = lib.SoundCreate(std::move(m2));
    lib.SoundSetId(a, 1); lib.SoundSetId(b, 2);
    PLAYSOUNDINFO loop; loop.flags = PSF_LOOP;
    CHECK(lib.SoundPlay(a, &loop) == 1);
    CHECK(lib.SoundPlay(b, &loop) == 0); // lower priority rejected
    CHECK(lib.SoundPlay(a, &loop) == 1); // same priority replaces
    lib.SoundDelete(a); lib.SoundDelete(b);
    lib.Service();
    CHECK(lib.LiveSoundCount() == 1);
}

// ---- CDibTile / RRS type 9 (moses.dll 0x1000c970) ---------------------------------------------
void Put16(std::vector<u8>& v, u32 x) { v.push_back(u8(x)); v.push_back(u8(x >> 8)); }
void Put32(std::vector<u8>& v, u32 x) { Put16(v, x & 0xffff); Put16(v, x >> 16); }
// DIBREC: w h ox oy (u16 each), bpp 16, flags 2 (RLE, u16 line table); every pixel = `px` (RGB555, != 0)
std::vector<u8> SynthDib(u32 w, u32 h, u32 px) {
    std::vector<u8> v;
    Put16(v, w); Put16(v, h); Put16(v, 0); Put16(v, 0); v.push_back(16); v.push_back(2);
    u32 rowLen = 4; // [n][lo][hi][0]
    for (u32 y = 0; y < h; ++y) Put16(v, h * 2 + y * rowLen);
    for (u32 y = 0; y < h; ++y) { v.push_back(u8(w)); Put16(v, px); v.push_back(0); }
    return v;
}

void TestDibTileSynthetic() {
    std::printf("[dibtile synthetic]\n");
    // records: 0 string (dummy), 1 DIB red 4x4, 2 DIB green 4x4, 3 DIBTILE 2x1 cells of 4x4 -> ids {1,2}, 4 DIBTILE bad (cell is a string)
    std::vector<u8> d1 = SynthDib(4, 4, 0x7c00), d2 = SynthDib(4, 4, 0x03e0);
    std::vector<u8> pool = {'x', 0};                              // id 0 string at pool+0
    u32 tileOff = u32(pool.size());
    Put16(pool, 2); Put16(pool, 0);                               // count, pad
    Put16(pool, 2); Put16(pool, 1); Put16(pool, 4); Put16(pool, 4); Put16(pool, 0); Put16(pool, 0); // DIBTILEHDR
    Put32(pool, 1); Put32(pool, 2);                               // ids
    u32 tileSize = u32(pool.size()) - tileOff;
    u32 badOff = u32(pool.size());
    Put16(pool, 1); Put16(pool, 0);
    Put16(pool, 1); Put16(pool, 1); Put16(pool, 4); Put16(pool, 4); Put16(pool, 0); Put16(pool, 0);
    Put32(pool, 0x10000 | 0);                                     // cell id 0 (a string, not a DIB) -> load fails
    u32 badSize = u32(pool.size()) - badOff;
    // layout: header 0x38, index (5 * 12) at 0x38, pool, data
    u32 idxOff = 0x38, poolOff = idxOff + 5 * 12, dataOff = poolOff + u32(pool.size());
    std::vector<u8> f;
    Put32(f, 0x54504152); Put32(f, 0x10000); Put32(f, 0); Put32(f, 0);
    Put32(f, idxOff); Put32(f, 5 * 12); Put32(f, poolOff); Put32(f, u32(pool.size()));
    while (f.size() < 0x38) f.push_back(0);
    auto rec = [&](u32 off, u32 size, u32 type) { Put32(f, off); Put32(f, size); Put16(f, type); Put16(f, 0); };
    rec(0, 2, 12);
    rec(dataOff, u32(d1.size()), 2);
    rec(dataOff + u32(d1.size()), u32(d2.size()), 2);
    rec(tileOff, tileSize, 9);
    rec(badOff, badSize, 9);
    f.insert(f.end(), pool.begin(), pool.end());
    f.insert(f.end(), d1.begin(), d1.end());
    f.insert(f.end(), d2.begin(), d2.end());
    const char* path = "dibtile_synth.rrs";
    FILE* fp = std::fopen(path, "wb");
    if (!fp) { std::printf("  SKIP (cannot write)\n"); return; }
    std::fwrite(f.data(), 1, f.size(), fp);
    std::fclose(fp);

    CResMgr mgr(nullptr);
    CHECK(mgr.Open(path));
    CHECK(mgr.GetResType(3) == RT_DIBTILE);
    rrs::DibTileInfo info;
    CHECK(mgr.File().Archive().GetDibTile(3, info) && info.cols == 2 && info.rows == 1 && info.tileW == 4 && info.tileH == 4);
    CDibObj* t = static_cast<CDibObj*>(mgr.Load(3, nullptr));
    CHECK(t != nullptr);
    if (t) {
        CHECK(t->Bounds().left == -4 && t->Bounds().right == 4 && t->Bounds().top == -2 && t->Bounds().bottom == 2);
        CHECK(t->GetPixel(-4, -2, 0) == 1 && t->GetPixel(3, 1, 0) == 1 && t->GetPixel(4, 1, 0) == 0);
        CDibData dst(8, 4, 16);
        RECT sr = t->Bounds();
        t->DibBlt(&dst, -4, -2, sr, 0, DROP_OPAQUE);
        CHECK(dst.Target()->Row(0)[0] == 0xFFFF0000 && dst.Target()->Row(3)[7] == 0xFF00FF00);
        CHECK(mgr.IsLoaded(1) && mgr.IsLoaded(2)); // cells stay referenced by the tile
    }
    CHECK(mgr.Load(4, nullptr) == nullptr);        // a non-DIB cell makes the load fail
    CHECK(!mgr.IsLoaded(4));
    mgr.Free(3, nullptr);
    CHECK(!mgr.IsLoaded(3) && !mgr.IsLoaded(1) && !mgr.IsLoaded(2));
    mgr.Close();
    std::remove(path);
}

// Every type 9 record of the Flounder Quest archives (skipped unless RAPTURE_FLOUNDER_DIR points to your own copy).
void TestDibTileFlounder() {
    std::printf("[dibtile flounder]\n");
    const char* env = std::getenv("RAPTURE_FLOUNDER_DIR");
    std::string dir = env && *env ? env : ".";  // set RAPTURE_FLOUNDER_DIR to your own copy; skipped when the files are absent
    const char* names[] = {"frenzy.rrs", "frenzylz.rrs", "seb.rrs", "sebfx.rrs", "seblin.rrs"};
    int tiles = 0, archives = 0;
    for (const char* n : names) {
        std::string p = dir + "/" + n;
        if (!FileExists(p.c_str())) continue;
        ++archives;
        CAudioLib lib;
        lib.EnableDevice(false);
        CResMgr mgr(&lib);
        if (!mgr.Open(p.c_str())) { CHECK(false); continue; }
        for (u32 i = 0; i < mgr.File().GetNumResources(); ++i) {
            if (mgr.GetResType(i) != RT_DIBTILE) continue;
            ++tiles;
            CDibTile* t = static_cast<CDibTile*>(mgr.Load(i, nullptr));
            CHECK(t != nullptr);
            if (t) CHECK(t->Count() == t->Cols() * t->Rows());
        }
        mgr.FreeAll(nullptr);
        mgr.Close();
    }
    if (!archives) std::printf("  SKIP (Flounder folder not found)\n");
    else std::printf("  %d archives, %d type 9 records decoded\n", archives, tiles);
}
int TestArchive(const std::string& path, bool strings) {
    std::printf("[%s]\n", path.c_str());
    CAudioLib lib;
    lib.EnableDevice(false);
    CResMgr mgr(&lib);
    if (!mgr.Open(path.c_str())) { std::printf("  cannot open\n"); return 1; }
    u32 n = mgr.File().GetNumResources();
    u32 ok = 0, fail = 0, counts[16] = {};
    for (u32 i = 0; i < n; ++i) {
        RESTYPE t = mgr.GetResType(i);
        void* o = mgr.Load(i, nullptr);
        if (o) { ++ok; if (t < 16) counts[t]++; } else if (t != RT_PALETTE) { ++fail; if (fail < 5) std::printf("  load failed id %u type %d\n", i, int(t)); }
    }
    std::printf("  %u records, loaded %u, failed %u (dib %u raw %u snd %u grp %u ref %u list %u str %u alias %u)\n",
                n, ok, fail, counts[2], counts[3], counts[5], counts[6] + counts[11], counts[7], counts[8], counts[12], counts[1]);
    CHECK(fail == 0);
    // refcount on a fresh manager: Load twice, Free once keeps it, Free twice releases it
    {
        CResMgr m2(&lib);
        m2.Open(path.c_str());
        for (u32 i = 0; i < n; ++i) {
            if (m2.GetResType(i) == RT_DIB) {
                void* o1 = m2.Load(i, nullptr);
                void* o2 = m2.Load(i, nullptr);
                CHECK(o1 && o1 == o2 && m2.GetObj(i, nullptr) == o1);
                m2.Free(i, nullptr);
                CHECK(m2.IsLoaded(i));
                m2.Free(i, nullptr);
                CHECK(!m2.IsLoaded(i));
                break;
            }
        }
    }
    // dibref: hot-spot replaced
    for (u32 i = 0; i < n; ++i) {
        if (mgr.GetResType(i) == RT_DIBREF) {
            rrs::DibRefInfo info;
            CHECK(mgr.File().load_dibref_info(i, info));
            CDibData* d = (CDibData*)mgr.GetObj(i, nullptr);
            CHECK(d && d->Origin().x == info.x && d->Origin().y == info.y);
            break;
        }
    }
    // diblist count / group indexed ids
    for (u32 i = 0; i < n; ++i) {
        RESTYPE t = mgr.GetResType(i);
        if (t == RT_DIBLIST) {
            CDibList* l = (CDibList*)mgr.GetObj(i, nullptr);
            std::vector<u32> ids;
            mgr.File().GetGroup(i, ids);
            CHECK(l && l->Count() == ids.size());
            u32 id0 = 0;
            CHECK(mgr.GetIndexedResId(&id0, i, 0) && ids.size() && id0 == ids[0]);
            CHECK(!mgr.GetIndexedResId(&id0, i, u32(ids.size())));
            break;
        }
    }
    if (strings) {
        for (u32 i = 0, shown = 0; i < n && shown < 3; ++i) {
            if (mgr.GetResType(i) == RT_STRING) {
                char buf[256];
                u32 len = mgr.LoadStringA(i, buf, sizeof buf);
                const char* o = (const char*)mgr.Load(i, nullptr);
                std::printf("  string %u (%u): \"%s\"\n", i, len, buf);
                CHECK(o && !std::strcmp(o, buf));
                ++shown;
            }
        }
    }
    mgr.FreeAll(nullptr);
    u32 left = 0;
    for (u32 i = 0; i < n; ++i) left += mgr.IsLoaded(i) ? 1 : 0;
    CHECK(left == 0);
    CHECK(lib.LiveSoundCount() == 0);
    mgr.Close();
    return 0;
}
} // namespace

int RunPkg2Tests(int argc, char** argv) {
    g_fail = 0;
    // argv[0] = "2" (test number), argv[1] = optional folder with the .rrs files
    std::string dir = (argc > 1 && argv && argv[1] && argv[1][0]) ? argv[1] : ".";
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    TestMem();
    TestSynthAudio();
    TestDibTileSynthetic();
    TestDibTileFlounder();
    int found = 0;
    for (int i = 0; i < rapture::Config().dataFileCount; ++i) {
        const rapture::DataFile& df = rapture::Config().dataFiles[i];
        std::string p = dir + df.file;
        if (!FileExists(p.c_str())) { std::printf("[%s] SKIP (not found)\n", p.c_str()); continue; }
        ++found;
        g_fail += TestArchive(p, df.hasStrings);
    }
    std::printf("Pkg2: %s (%d failures, %d archives)\n", g_fail ? "FAILED" : "OK", g_fail, found);
    return g_fail ? 1 : 0;
}
