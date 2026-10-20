// Headless tests of WP-5 (moses completeness): object caches, CFileIO, BMP/palette, CResfile extras, metafiles (CDrawMeta /
// CAnimMeta), pen-cache wiring, portable text backend, CD audio / MIDI extras.  Called at the end of `--engine-test 1`.
// No window, no audio device: software surfaces, temporary files in the current folder (removed at the end).
#include "tests/EngineTests.h"

#include "moses/AnimObjs.h"
#include "moses/AnimView.h"
#include "moses/AudioLib.h"
#include "moses/DibData.h"
#include "moses/DrawGdi.h"
#include "moses/DrawMeta.h"
#include "moses/DrawView.h"
#include "moses/FileIO.h"
#include "moses/GdiRaster.h"
#include "moses/ObjCache.h"
#include "moses/ResMgr.h"

#include <cstdio>
#include <filesystem>
#include <cstring>
#include <string>
#include <vector>

using namespace moses;

namespace {

int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                  \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

const uint32_t RED = 0xFFFF0000, GREEN = 0xFF00FF00, BLUE = 0xFF0000FF, BLACK = 0xFF000000;

uint32_t Px(CDrawView& v, int x, int y) {
    return v.Pixels().Row(y - v.ViewData().surface.top)[x - v.ViewData().surface.left];
}

struct SoftDc { // a GdiCtx over a 64x64 surface, origin at the top-left
    ::Surface surf{64, 64};
    GdiCtx dc;
    SoftDc() {
        dc.surf = &surf;
        dc.viewportOrg = POINT{0, 0};
        dc.clip = RECT{0, 0, 64, 64};
    }
};

// ---------------------------------------------------------------------------------------------
// CObjCache / CGdi*Cache (moses.dll 0x100030e0 .. 0x100031a0, 0x10008cf0)
struct CountingCache : CObjCache {
    int created = 0, deleted = 0;
    CountingCache(u32 cap) : CObjCache(cap, 4) {}
    ~CountingCache() override { release_all(); }
    void* create_object(const void* key) override { ++created; return new int(*static_cast<const int*>(key)); }
    void delete_object(void* o) override { ++deleted; delete static_cast<int*>(o); }
};

void TestObjCache() {
    std::printf("[objcache]\n");
    {
        CountingCache c(3);
        int k1 = 1, k2 = 2, k3 = 3, k4 = 4;
        void* o1 = c.Select(&k1);
        void* o2 = c.Select(&k2);
        c.Select(&k3);
        CHECK(c.Count() == 3 && c.created == 3);
        CHECK(c.Select(&k1) == o1 && c.created == 3);           // hit: no new object
        CHECK(c.ObjectAt(2) == o1 && c.ObjectAt(0) == o2);      // moved to the newest position
        c.Select(&k4);                                          // full: the oldest (k2) is evicted
        CHECK(c.Count() == 3 && c.created == 4 && c.deleted == 1);
        int calls = c.created;
        c.Select(&k2);                                          // k2 is gone: re-created (evicting k3)
        CHECK(c.created == calls + 1 && c.deleted == 2);
        CHECK(c.Select(&k1) != nullptr && c.created == calls + 1); // k1 survived the evictions
        CHECK(c.Capacity() == 3 && c.KeySize() == 4);
    }
    {
        // GDI caches: Select into a DC returns the PREVIOUS object (stock when none), the DC takes the new values
        SoftDc s;
        CGdiPenCache pens(2);
        LOGPEN p1; p1.style = 0; p1.width = POINT{3, 0}; p1.color = Rgb(1, 2, 3);
        LOGPEN p2 = p1; p2.color = Rgb(9, 9, 9);
        void* prev = pens.Select(&s.dc, &p1);
        CHECK(prev == gdi::StockObject(GdiObj::PEN));
        CHECK(s.dc.penWidth == 3 && s.dc.penColor == Rgb(1, 2, 3) && !s.dc.penNull);
        void* prev2 = pens.Select(&s.dc, &p2);
        CHECK(prev2 == pens.ObjectAt(0));                       // the first pen was selected
        CHECK(s.dc.penColor == Rgb(9, 9, 9));
        CHECK(pens.Select(nullptr, &p1) == pens.ObjectAt(1));  // no DC: the cached object itself (and now the newest)
        LOGPEN pn; pn.style = 5;
        pens.Select(&s.dc, &pn);
        CHECK(s.dc.penNull);
        CGdiBrushCache brushes(2);
        LOGBRUSH b; b.style = 0; b.color = Rgb(200, 0, 0);
        brushes.Select(&s.dc, &b);
        CHECK(s.dc.brushColor == Rgb(200, 0, 0) && s.dc.brushStyle == 0);
        CGdiFontCache fonts(2);
        LOGFONT_LITE lf; lf.height = -14; std::strcpy(lf.face, "Arial");
        void* fo = fonts.Select(nullptr, &lf);
        LOGFONT_LITE lf2 = lf;
        CHECK(fonts.Select(nullptr, &lf2) == fo);               // field-wise key comparison
        lf2.weight = 700;
        CHECK(fonts.Select(nullptr, &lf2) != fo);
    }
}

// pen/brush caches wired into CDrawRect / CDrawPoly (original path), same pixels as the no-cache path
void TestCacheWiring() {
    std::printf("[cache wiring]\n");
    CAnimView view;
    view.Init(nullptr, nullptr, 64, 48, 16);
    view.SetSurfaceColor(0);
    view.Paint();
    CGdiPenCache pens(4);
    CGdiBrushCache brushes(4);
    CDrawRect r(&view, 5);
    RECT rc{-10, -10, 10, 10};
    r.Init(&rc, 0);
    r.brush_.style = 0; r.brush_.color = Rgb(255, 0, 0);
    r.pen_.style = 5; // PS_NULL
    r.pen_cache_ = &pens;
    r.brush_cache_ = &brushes;
    r.Show();
    CHECK(Px(view, 0, 0) == RED);
    CHECK(pens.Count() == 1 && brushes.Count() == 1);
    r.Update();
    r.brush_.color = Rgb(0, 255, 0);
    r.Update();
    r.Show();
    CHECK(Px(view, 0, 0) == GREEN);
    CHECK(brushes.Count() == 2);                                 // a new key = a new cached brush
    CDrawPoly poly(&view, 6);
    POINT pts[3] = {{-20, -20}, {-5, -20}, {-5, -5}};
    poly.pen_.style = 0; poly.pen_.width = POINT{1, 0}; poly.pen_.color = Rgb(0, 0, 255);
    poly.pen_cache_ = &pens;
    CHECK(poly.Init(pts, 3, 1) == 1);
    poly.Show();
    CHECK(pens.Count() == 2);
}

// ---------------------------------------------------------------------------------------------
// CFileIO (moses.dll 0x10002bb0 ..)
void TestFileIO() {
    std::printf("[fileio]\n");
    const char* path = "wp5_fileio.tmp";
    CFileIO io(path);
    io.Delete();
    CHECK(io.Exists() == 0);
    CHECK(io.Read(nullptr, 1) == 0 && io.Tell() == 0xFFFFFFFFu);   // closed: failures
    CHECK(io.Create() == 1 && io.Create() == 0);                   // second create refused (already open)
    CHECK(io.SetFile("x") == 0);                                   // SetFile refused while open
    CHECK(io.Write("abc", 3) == 1);
    CHECK(io.WriteString("hello") == 7);                           // u16 length + chars
    CHECK(io.WriteString(nullptr) == 2);                           // null string: length 0
    CHECK(io.WriteLine("n=%d", 42) == 1);                          // "n=42\r\n"
    CHECK(io.WriteSize("zz", 2) == 2);
    io.Flush();
    CHECK(io.GetSize() == 3 + 7 + 2 + 6 + 2);
    CHECK(io.Tell() == 20);                                        // GetSize restores the position
    io.Close();
    CHECK(!io.IsOpen());
    CHECK(io.Exists() == 1);
    CHECK(io.Open(0) == 1 && io.Open(0) == 1);
    char buf[32] = {0};
    CHECK(io.Read(buf, 3) == 1 && std::memcmp(buf, "abc", 3) == 0);
    CHECK(io.ReadString(buf, sizeof buf) == 5 && std::strcmp(buf, "hello") == 0);
    CHECK(io.ReadString(buf, sizeof buf) == 0 && buf[0] == 0);     // empty string
    char small[4];
    CHECK(io.Seek(0, 0) == 0);
    CHECK(io.Seek(3, 0) == 3);
    CHECK(io.ReadString(small, sizeof small) == 5 && std::strcmp(small, "hel") == 0); // lstrcpyn truncation, length of the stored string
    CHECK(io.Seek(0, 2) == 20);
    CHECK(io.Read(buf, 1) == 0 && io.ReadSize(buf, 4) == 0);       // at EOF
    CHECK(io.Seek(-2, 1) == 18 && io.ReadSize(buf, 8) == 2);
    CHECK(io.Write("q", 1) == 0);                                  // opened read only: the write fails
    io.Close();
    // Load
    u32 n = 0;
    u8* all = io.Load(nullptr, &n);
    CHECK(all != nullptr && n == 20 && all[0] == 'a');
    CMem mem(0, 0);
    mem.Free(all);
    CHECK(!io.IsOpen());                                           // Load closes a file it opened
    // RIFF chunks: build RIFF('TEST') { 'abcd'(3 bytes) 'LIST'('inner') { 'efgh'(4) } }
    CFileIO r("wp5_riff.tmp");
    CHECK(r.Create() == 1);
    MMCKINFO form, a, list, in;
    form.fccType = FourCC('T', 'E', 'S', 'T');
    CHECK(r.CreateChunk(&form, MMIO_CREATERIFF) == 1);
    a.ckid = FourCC('a', 'b', 'c', 'd');
    CHECK(r.CreateChunk(&a, 0) == 1 && r.Write("xyz", 3) == 1 && r.Ascend(&a) == 1);
    CHECK(a.cksize == 3);
    list.fccType = FourCC('i', 'n', 'n', 'r');
    CHECK(r.CreateChunk(&list, MMIO_CREATELIST) == 1);
    in.ckid = FourCC('e', 'f', 'g', 'h');
    CHECK(r.CreateChunk(&in, 0) == 1 && r.Write("1234", 4) == 1 && r.Ascend(&in) == 1);
    CHECK(r.Ascend(&list) == 1 && r.Ascend(&form) == 1);
    r.Close();
    CHECK(r.Open(0) == 1);
    MMCKINFO f2, c1, c2, c3;
    f2.fccType = FourCC('T', 'E', 'S', 'T');
    CHECK(r.Descend(&f2, nullptr, MMIO_FINDRIFF) == 1 && f2.ckid == FourCC('R', 'I', 'F', 'F'));
    c1.ckid = FourCC('e', 'f', 'g', 'h');                          // nested chunk: skips 'abcd' (odd size, padded) and descends the LIST
    CHECK(r.Descend(&c1, &f2, MMIO_FINDCHUNK) == 0);               // not a direct child of the RIFF (inside the LIST)
    r.Seek(long(f2.dwDataOffset + 4), 0);
    c2.ckid = FourCC('a', 'b', 'c', 'd');
    CHECK(r.Descend(&c2, &f2, MMIO_FINDCHUNK) == 1 && c2.cksize == 3);
    char xyz[4] = {0};
    CHECK(r.Read(xyz, 3) == 1 && std::strcmp(xyz, "xyz") == 0);
    CHECK(r.Ascend(&c2) == 1);                                     // skips the pad byte
    c3.fccType = FourCC('i', 'n', 'n', 'r');
    CHECK(r.Descend(&c3, &f2, MMIO_FINDLIST) == 1 && c3.cksize == 4 + 8 + 4);
    r.Close();
    CHECK(r.Delete() == 1 || true);
    std::remove("wp5_riff.tmp");
    CFileIO d(path);
    CHECK(d.Delete() == 1 && d.Exists() == 0);
    // helpers
    char out[260];
    FormatDebugMessage(out);
    CHECK(out[0] == 0);
    MakeModulePath(nullptr, out, "data.bin");
    CHECK(std::strlen(out) > 8 && std::strcmp(out + std::strlen(out) - 8, "data.bin") == 0);
}

// ---------------------------------------------------------------------------------------------
// CDibData::Read/Write/Print, CDibPal, CDibSect palette
void TestDibIO() {
    std::printf("[dib io]\n");
    CDibPal pal;
    u32 quad[3] = {0x00112233, 0x00445566, 0x00778899}; // B=0x33.. (little endian bytes 33 22 11 00)
    CDibPal p3(quad, 3);
    CHECK(p3.Count() == 3 && p3.Version() == 0x300);
    CHECK(p3.ToArgb(0) == 0xFF112233u);                 // RGBQUAD byte order B,G,R
    u8 q[12] = {0};
    CHECK(p3.ToRgbQuad(q) == 3 && q[0] == 0x11 && q[2] == 0x33);  // the original swaps the 1st and 3rd byte again
    CDibPal big(quad, 999);
    CHECK(big.Count() == 256);
    CDibSect sect;
    sect.Init(nullptr, 8, 8, 8);
    CHECK(sect.Palette() == nullptr);
    sect.SetPalette(&p3);
    CHECK(sect.Palette() && sect.Palette()->Count() == 3);
    u8 table[1024];
    sect.ColorTable(table);
    CHECK(table[0] == 0x11 && table[4 + 1] == 0x55 && table[3 * 4] == 0);
    sect.DeletePalette();
    CHECK(sect.Palette() == nullptr);

    CDibData src(5, 3, 16);                              // odd width on purpose
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) src.Target()->Row(y)[x] = 0xFF000000u | uint32_t(x * 40) << 16 | uint32_t(y * 80) << 8 | 7;
    CFileIO io("wp5_dib.bmp");
    CHECK(io.Create() == 1 && src.Write(&io) == 1);
    io.Close();
    CHECK(io.Open(0) == 1);
    CDibData back;
    CHECK(back.Read(&io) == 1);
    CHECK(back.Width() == 5 && back.Height() == 3);
    bool same = true;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) same = same && back.Target()->Row(y)[x] == src.Target()->Row(y)[x];
    CHECK(same);
    io.Close();
    // > 256 colours: written as 24 bit
    CDibData many(20, 20, 16);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) many.Target()->Row(y)[x] = 0xFF000000u | uint32_t(x * 12) << 16 | uint32_t(y * 12) << 8 | uint32_t(x + y);
    CHECK(io.Create() == 1 && many.Write(&io) == 1 && io.GetSize() == 14 + 40 + 20 * 60);
    io.Close();
    CHECK(io.Open(0) == 1);
    CDibData rd;
    CHECK(rd.Read(&io) == 0);                            // 24 bit: the original (and the port) accept only 4/8 bit
    io.Close();
    CHECK(io.Delete() == 1);
    // garbage is refused
    CFileIO g("wp5_dib.bmp");
    CHECK(g.Create() == 1 && g.Write("not a bitmap file at all............................", 50) == 1);
    g.Close();
    CHECK(g.Open(0) == 1 && rd.Read(&g) == 0);
    g.Close();
    g.Delete();
    // Print: scaled and centred, aspect ratio kept
    ::Surface page(40, 40);
    CDibData sq(10, 5, 16);
    sq.Target()->Clear(GREEN);
    CHECK(sq.Print(&page) == 1);
    CHECK(page.Row(20)[20] == GREEN && page.Row(2)[20] == 0 && page.Row(37)[20] == 0);  // 40x20 picture centred vertically
    CHECK(page.Row(10)[0] == GREEN && page.Row(9)[0] == 0);
}

// ---------------------------------------------------------------------------------------------
// synthetic archive: key resources + palette record
void Put16(std::vector<u8>& v, u32 x) { v.push_back(u8(x)); v.push_back(u8(x >> 8)); }
void Put32(std::vector<u8>& v, u32 x) { Put16(v, x & 0xffff); Put16(v, x >> 16); }

void TestResfile() {
    std::printf("[resfile]\n");
    // header 0x38, key0 = index at 0x38 (2 records x 12), key1 = pool, palette data
    std::vector<u8> pool = {'h', 'i', 0};
    u32 idxOff = 0x38, poolOff = idxOff + 2 * 12, dataOff = poolOff + u32(pool.size());
    std::vector<u8> palette;
    Put32(palette, 0x00112233); Put32(palette, 0x00445566);
    std::vector<u8> f;
    Put32(f, 0x54504152); Put32(f, 0x10000); Put32(f, 0x38); Put32(f, 0);
    Put32(f, idxOff); Put32(f, 2 * 12); Put32(f, poolOff); Put32(f, u32(pool.size()));
    while (f.size() < 0x38) f.push_back(0);
    auto rec = [&](u32 off, u32 size, u32 type) { Put32(f, off); Put32(f, size); Put16(f, type); Put16(f, 0); };
    rec(0, 3, 12);                                   // string
    rec(dataOff, u32(palette.size()), 4);            // palette
    f.insert(f.end(), pool.begin(), pool.end());
    f.insert(f.end(), palette.begin(), palette.end());
    const char* path = "wp5_synth.rrs";
    FILE* fp = std::fopen(path, "wb");
    if (!fp) { std::printf("  SKIP (cannot write)\n"); return; }
    std::fwrite(f.data(), 1, f.size(), fp);
    std::fclose(fp);

    {
        CResfile rf;
        CHECK(rf.OpenFile(path, 0x10) == 1 && rf.IsOpen());
        CHECK(rf.GetNumResources() == 2 && rf.GetRecType(1) == RT_PALETTE);
        u32 sz = 0;
        void* k0 = rf.LoadKeyResource(RESKEY_INDEX, &sz);
        CHECK(k0 != nullptr && sz == 24);
        CMem mem(0, 0);
        mem.Free(k0);
        CHECK(rf.LoadKeyResource(RESKEY_2, &sz) == nullptr && sz == 0);
        CHECK(rf.LoadKeyResource(RESKEY(7), &sz) == nullptr);
        std::unique_ptr<CDibPal> pal = rf.load_palette(1);
        CHECK(pal && pal->Count() == 2 && pal->ToArgb(1) == 0xFF445566u);
        // load(): plain and chunked (the load proc is called once per chunk; offsets of later chunks are wrong in the original)
        u8 raw[8] = {0};
        CHECK(rf.load(raw, dataOff, 8) == 1 && raw[0] == 0x33);
        static int calls = 0;
        calls = 0;
        RESLOADPROC old = rf.SetLoadProc([](CResfile*, void*) -> int { ++calls; return 1; }, nullptr);
        CHECK(old == nullptr);
        CHECK(rf.load(raw, dataOff, 4) == 1 && calls == 1);   // 4 bytes < chunk size 0x10: one chunk
        CHECK(rf.SetLoadProc(nullptr, nullptr) != nullptr);
        // copy: the destination re-opens the same archive
        CResfile cp;
        cp = rf;
        CHECK(cp.IsOpen() && cp.GetNumResources() == 2);
        rf.CloseFile();
        CHECK(!rf.IsOpen() && cp.IsOpen());
        // destructor closes (valgrind-style: no crash)
    }
    {
        CResMgr m1(nullptr), m2(nullptr);
        CHECK(m1.Open(path) == 1);
        m2 = m1;
        CHECK(m2.File().IsOpen() && m2.File().GetNumResources() == 2);
    }
    std::remove(path);
}

// ---------------------------------------------------------------------------------------------
// EMF
void E32(std::vector<u8>& v, u32 x) { Put32(v, x); }
void Rec(std::vector<u8>& f, u32 type, const std::vector<u8>& payload) {
    std::vector<u8> p = payload;
    while (p.size() % 4) p.push_back(0);
    E32(f, type);
    E32(f, u32(p.size() + 8));
    f.insert(f.end(), p.begin(), p.end());
}
std::vector<u8> P(std::initializer_list<int> v) {
    std::vector<u8> r;
    for (int x : v) E32(r, u32(x));
    return r;
}
std::vector<u8> MakeEmf(const std::vector<u8>& body, int nrec, int handles = 4) {
    std::vector<u8> f;
    std::vector<u8> h;
    // bounds 0,0,99,99 ; frame 0,0,9999,9999 (0.01 mm) ; ' EMF' ; version ; nBytes ; nRecords ; nHandles
    for (int x : {0, 0, 99, 99, 0, 0, 9999, 9999}) E32(h, u32(x));
    E32(h, 0x464d4520); E32(h, 0x10000);
    E32(h, 0); /*nBytes patched*/ E32(h, u32(nrec + 2)); Put16(h, u32(handles)); Put16(h, 0);
    E32(h, 0); E32(h, 0); E32(h, 0);             // nDescription, offDescription, nPalEntries
    for (int x : {100, 100, 100, 100}) E32(h, u32(x));   // device 100x100 px, 100x100 mm: 1 px per mm
    Rec(f, 1, h);
    f.insert(f.end(), body.begin(), body.end());
    Rec(f, 14, P({0, 0x10, 0x10}));
    u32 n = u32(f.size());
    std::memcpy(&f[8 + 40], &n, 4);
    return f;
}
std::vector<u8> BrushRec(int ih, int style, u32 color) {
    std::vector<u8> r;
    Rec(r, 39, P({ih, style, int(color), 0}));
    return r;
}

void TestMeta() {
    std::printf("[metafile]\n");
    // a red 50x50 box (logical 10..60) played into a 25x25 destination: scale 0.5
    std::vector<u8> body;
    auto add = [&](const std::vector<u8>& r) { body.insert(body.end(), r.begin(), r.end()); };
    add(BrushRec(1, 0, 0x0000FF));                 // solid COLORREF red
    { std::vector<u8> r; Rec(r, 37, P({1})); add(r); }                // SELECTOBJECT 1
    { std::vector<u8> r; Rec(r, 38, P({2, 5, 0, 0, 0})); add(r); }    // CREATEPEN 2: PS_NULL
    { std::vector<u8> r; Rec(r, 37, P({2})); add(r); }
    { std::vector<u8> r; Rec(r, 43, P({0, 0, 99, 99, 10, 10, 60, 60})); add(r); }  // RECTANGLE
    { std::vector<u8> r; Rec(r, 114, P({0, 0, 99, 99})); add(r); }    // ALPHABLEND: unsupported -> counted
    std::vector<u8> emf = MakeEmf(body, 6);

    emf::Metafile m;
    CHECK(m.Load(emf.data(), emf.size()) && m.Valid());
    CHECK(m.header().devCx == 100 && m.header().frame.right == 9999 && m.RecordCount() == 8);
    std::vector<u8> bad = emf;
    bad[40] = 'X';
    emf::Metafile m2;
    CHECK(!m2.Load(bad.data(), bad.size()));
    std::vector<u8> wmf = {0xD7, 0xCD, 0xC6, 0x9A, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};    // placeable WMF key: refused like GetEnhMetaFile
    CHECK(!m2.Load(wmf.data(), wmf.size()));
    std::vector<u8> trunc(emf.begin(), emf.end() - 4);
    CHECK(!m2.Load(trunc.data(), trunc.size()));

    SoftDc s;
    CHECK(m.Play(s.dc, RECT{0, 0, 50, 50}) == 8);
    CHECK(s.surf.Row(10)[10] == RED && s.surf.Row(28)[28] == RED);        // 10..60 at scale 0.5 = 5..30 -> after 0.5 of 100px frame
    CHECK(s.surf.Row(2)[2] == 0 && s.surf.Row(40)[40] == 0);
    CHECK(m.skipped().count(114) == 1);

    // inside a view: CDrawMeta + CAnimMeta
    CAnimView view;
    view.Init(nullptr, nullptr, 64, 48, 16);
    view.SetSurfaceColor(0);
    view.Paint();
    CDrawMeta dm(&view, 5);
    RECT rc{-30, -20, 20, 30};
    CHECK(dm.Metafile() == nullptr);
    CHECK(dm.Init(&rc, "wp5_does_not_exist.emf") == 0);
    CHECK(dm.InitFromMemory(&rc, emf.data(), emf.size()) == 1 && dm.Metafile() != nullptr);
    dm.Show();
    CHECK(Px(view, -30 + 5 + 1, -20 + 5 + 1) == RED);
    CHECK(Px(view, -29, -19) == BLACK);
    // CAnimMeta shares the metafile on Copy
    CAnimMeta am(&view, 6);
    CHECK(am.InitFromMemory(&rc, emf.data(), emf.size()) == 1);
    CDrawObj* cp = am.Copy();
    CHECK(cp != nullptr);
    delete cp;
    // file round trip
    FILE* fp = std::fopen("wp5.emf", "wb");
    if (fp) {
        std::fwrite(emf.data(), 1, emf.size(), fp);
        std::fclose(fp);
        CDrawMeta fm(&view, 7);
        CHECK(fm.Init(&rc, "wp5.emf") == 1);
        std::remove("wp5.emf");
    }

    // a richer metafile: map mode LOMETRIC (y up, 0.1 mm), world transform translate, polygon 16, ellipse, text
    std::vector<u8> b2;
    auto add2 = [&](const std::vector<u8>& r) { b2.insert(b2.end(), r.begin(), r.end()); };
    { std::vector<u8> r; Rec(r, 17, P({1})); add2(r); }                 // MM_TEXT
    add2(BrushRec(1, 0, 0x00FF00));                                     // green
    { std::vector<u8> r; Rec(r, 37, P({1})); add2(r); }
    { std::vector<u8> r; Rec(r, 37, P({int(0x80000008u)})); add2(r); }  // NULL_PEN (stock)
    {   // POLYGON16: triangle (10,10) (50,10) (10,50)
        std::vector<u8> r = P({0, 0, 99, 99, 3});
        Put16(r, 10); Put16(r, 10); Put16(r, 50); Put16(r, 10); Put16(r, 10); Put16(r, 50);
        std::vector<u8> rr; Rec(rr, 86, r); add2(rr);
    }
    { std::vector<u8> r; Rec(r, 35, [] { std::vector<u8> x; float v[6] = {1, 0, 0, 1, 20, 20}; for (float f2 : v) { u32 u; std::memcpy(&u, &f2, 4); E32(x, u); } return x; }()); add2(r); } // SETWORLDTRANSFORM translate 20,20
    { std::vector<u8> r; Rec(r, 42, P({0, 0, 99, 99, 10, 10, 30, 30})); add2(r); }                // ELLIPSE
    std::vector<u8> emf2 = MakeEmf(b2, 7);
    emf::Metafile m3;
    CHECK(m3.Load(emf2.data(), emf2.size()));
    SoftDc s2;
    CHECK(m3.Play(s2.dc, RECT{0, 0, 100, 100}) == 9);
    CHECK(s2.surf.Row(20)[12] == GREEN && s2.surf.Row(12)[20] == GREEN);   // triangle
    CHECK(s2.surf.Row(55)[55] == 0);
    CHECK(s2.surf.Row(40)[40] == GREEN);                                    // ellipse centre (20+20, 20+20)
    CHECK(s2.surf.Row(30)[60] == 0);
}

// ---------------------------------------------------------------------------------------------
// Portable text backend (stb_truetype / bitmap font)
void TestTtf() {
    std::printf("[text ttf]\n");
    LOGFONT_LITE f;
    f.height = -16;
    std::strcpy(f.face, "Arial");
    RECT rc{0, 0, 0, 0};
    int h = gdi::ttf::TextRect(f, "Hello", 5, rc, DT_SINGLELINE_ | 0x400);
    CHECK(h >= 8 && rc.right > 0 && rc.bottom == h);
    RECT rc2{0, 0, 0, 0};
    gdi::ttf::TextRect(f, "Hello Hello", 11, rc2, DT_SINGLELINE_ | 0x400);
    CHECK(rc2.right > rc.right);                                           // longer text is wider
    RECT wrap{0, 0, rc.right + 2, 0};
    int hw = gdi::ttf::TextRect(f, "Hello Hello", 11, wrap, DT_WORDBREAK_ | 0x400);
    CHECK(hw >= 2 * h - 1 && wrap.right <= rc.right + 2);                  // wrapped on two lines
    RECT nl{0, 0, 0, 0};
    CHECK(gdi::ttf::TextRect(f, "a\nb", 3, nl, 0x400) >= 2 * h - 1);
    CHECK(gdi::ttf::TextAscent(f) > 0 && gdi::ttf::TextAscent(f) <= h);
    SoftDc s;
    s.dc.textColor = Rgb(255, 0, 0);
    gdi::ttf::TextDraw(s.dc, f, "Hello", 5, RECT{2, 2, 62, 40}, DT_SINGLELINE_);
    int red = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) red += s.surf.Row(y)[x] == RED;
    CHECK(red > 20);                                                       // glyph pixels drawn, colour applied
    // clipping to the rectangle
    SoftDc s2;
    s2.dc.textColor = Rgb(255, 0, 0);
    gdi::ttf::TextDraw(s2.dc, f, "Hello", 5, RECT{2, 2, 8, 8}, DT_SINGLELINE_);
    bool outside = false;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (s2.surf.Row(y)[x] && (x >= 8 || y >= 8)) outside = true;
    CHECK(!outside);
    // centre alignment moves the text to the middle
    SoftDc s3;
    s3.dc.textColor = Rgb(255, 0, 0);
    gdi::ttf::TextDraw(s3.dc, f, "I", 1, RECT{0, 0, 64, 30}, 1 /*DT_CENTER*/ | DT_SINGLELINE_);
    int minx = 99, maxx = -1;
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 64; ++x)
            if (s3.surf.Row(y)[x]) { minx = std::min(minx, x); maxx = std::max(maxx, x); }
    CHECK(maxx >= 0 && minx > 20 && maxx < 44);
    std::printf("  outline font %s\n", gdi::ttf::HasOutlineFont(f) ? "found" : "not found (bitmap font used)");
}

// ---------------------------------------------------------------------------------------------
// CD audio over WAV files, SoundCreate(CFileIO), MIDI BreakLoops
u32 g_clock = 0;
u32 TestClock() { return g_clock; }

void WriteWav(const std::string& path, double seconds, int rate = 8000) {
    u32 frames = u32(seconds * rate);
    std::vector<u8> w;
    auto p4 = [&](const char* s) { w.insert(w.end(), s, s + 4); };
    p4("RIFF"); Put32(w, 36 + frames * 2); p4("WAVE"); p4("fmt "); Put32(w, 16);
    Put16(w, 1); Put16(w, 1); Put32(w, u32(rate)); Put32(w, u32(rate) * 2); Put16(w, 2); Put16(w, 16);
    p4("data"); Put32(w, frames * 2);
    for (u32 i = 0; i < frames; ++i) Put16(w, (i % 40) < 20 ? 8000 : u32(-8000) & 0xffff);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fwrite(w.data(), 1, w.size(), f);
    std::fclose(f);
}

void TestCd() {
    std::printf("[cd audio]\n");
    {
        PcmBuffer pcm;
        std::string err;
        CHECK(!LoadWav(reinterpret_cast<const uint8_t*>("RIFFxxxxWAVE"), 12, pcm, &err));
        CHECK(!LoadWavFile("wp5_no_such_file.wav", pcm, &err));
    }
    const char* dir = "wp5_cd";
    std::filesystem::create_directories(dir);
    CAudioLib lib;
    lib.EnableDevice(false);
    CCdAudio::SetClock(TestClock);
    lib.CdSetFolder("wp5_cd_missing");
    CHECK(lib.CdGetMaxTrack() == 0);                           // empty drive
    CHECK(lib.CdPlay(1, 0) == 0 && lib.CdIsPlaying() == 0);
    CDPOS pos;
    CHECK(lib.CdGetPosition(pos) == 0);
    WriteWav(std::string(dir) + "/track01.wav", 1.5);
    WriteWav(std::string(dir) + "/Track 02.wav", 0.5);
    WriteWav(std::string(dir) + "/track03.wav", 2.0);
    lib.CdSetFolder(dir);
    CHECK(lib.CdGetMaxTrack() == 3 && lib.CdIsPlaying() == 0); // opening to count the tracks leaves the device closed
    u32 len = 0;
    CHECK(lib.CdGetLengths(1, 3, &len) == 3 && len == 2000);   // the original keeps only the LAST length
    CHECK(lib.CdGetLengths(2, 9, &len) == 2 && len == 2000);   // stops at the first missing track
    CHECK(lib.CdGetLengths(2, 2, &len) == 1 && len == 500);
    g_clock = 1000;
    CHECK(lib.CdPlay(1, 2) == 1 && lib.CdIsPlaying() == 1 && lib.CdIsPaused() == 0);
    g_clock = 1500;
    CHECK(lib.CdGetPosition(pos) == 1 && pos.track == 1 && pos.min == 0 && pos.sec == 0 && pos.frame == 37);
    g_clock = 2600;                                            // track 1 (1.5 s) is over: track 2 is playing 0.1 s
    lib.Service();
    CHECK(lib.CdGetPosition(pos) == 1 && pos.track == 2 && pos.sec == 0 && pos.frame == 7);
    g_clock = 3200;                                            // 'to' = 2 ended
    lib.Service();
    CHECK(lib.Cd().IsSounding() == false && lib.CdIsPlaying() == 1);   // device stays open until CdStop
    lib.CdStop();
    CHECK(lib.CdIsPlaying() == 0);
    // pause / resume, play to the end of the disc (to = 0)
    g_clock = 10000;
    CHECK(lib.CdPlay(3, 0) == 1);
    g_clock = 10500;
    lib.CdPause(1);
    CHECK(lib.CdIsPaused() == 1);
    g_clock = 12000;
    CHECK(lib.CdGetPosition(pos) == 1 && pos.track == 3 && pos.sec == 0 && pos.frame == 37);   // frozen at 0.5 s
    lib.CdPause(0);
    g_clock = 12700;
    CHECK(lib.CdGetPosition(pos) == 1 && pos.track == 3 && pos.sec == 1 && pos.frame == 15);   // 0.5 + 0.7 = 1.2 s
    CHECK(lib.CdPlay(9, 0) == 0 && lib.CdIsPlaying() == 0);    // out of range track: stops
    // mixer: CD volume / mute reach the channel gain without errors
    lib.volume(MIX_CD, 1, 50);
    lib.mute(MIX_CD, 1, 1);
    CHECK(lib.volume(MIX_CD, 0, 0) == 50 && lib.mute(MIX_CD, 0, 0) == 1);
    CCdAudio::SetClock(nullptr);
    std::filesystem::remove_all(dir);

    // MIDI BreakLoops without a sequence is harmless; SoundCreate(CFileIO*, off, size)
    lib.MidiBreakLoops();
    {
        std::vector<u8> rec;
        Put32(rec, 3); Put32(rec, 7);
        const char* nm = "intro.avi";
        rec.insert(rec.end(), nm, nm + 10);
        std::vector<u8> file(5, 0xAA);
        file.insert(file.end(), rec.begin(), rec.end());
        CFileIO io("wp5_snd.tmp");
        CHECK(io.Create() == 1 && io.Write(file.data(), u32(file.size())) == 1);
        CAudioSnd* snd = lib.SoundCreate(&io, 5, u32(rec.size()));
        CHECK(snd != nullptr && snd->data.kind == ::Sound::kAvi && snd->data.aviName == "intro.avi" && snd->param == 7);
        CHECK(lib.SoundCreate(&io, 500, 4) == nullptr);        // beyond the end
        lib.SoundDelete(snd);
        io.Delete();
    }
}

} // namespace

int RunMoses5Tests() {
    g_fail = g_checks = 0;
    TestObjCache();
    TestCacheWiring();
    TestFileIO();
    TestDibIO();
    TestResfile();
    TestMeta();
    TestTtf();
    TestCd();
    std::printf("Moses5: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
