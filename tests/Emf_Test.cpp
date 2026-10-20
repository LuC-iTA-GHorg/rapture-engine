// EMF metafile renderer tests for the records added on the `limits` branch (task 3): `<game>.exe --engine-test 10`.
// Every test builds a small EMF in memory (100x100 reference device, 1 pixel per logical unit in MM_TEXT), plays it into a 100x100
// software surface and checks pixels.  No files, no window.
#include "tests/EngineTests.h"

#include "moses/DrawMeta.h"
#include "moses/DrawGdi.h"
#include "moses/GdiRaster.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace moses;

namespace {

int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                          \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

const uint32_t RED = 0xFFFF0000, GREEN = 0xFF00FF00, BLUE = 0xFF0000FF, WHITE = 0xFFFFFFFF, BLACK = 0xFF000000, CYAN = 0xFF00FFFF;
// COLORREF values (0x00BBGGRR)
const int C_RED = 0x0000FF, C_GREEN = 0x00FF00, C_BLUE = 0xFF0000, C_WHITE = 0xFFFFFF, C_BLACK = 0;

using Bytes = std::vector<u8>;

void P32(Bytes& v, u32 x) { for (int i = 0; i < 4; ++i) v.push_back(u8(x >> (8 * i))); }
void P16(Bytes& v, u32 x) { v.push_back(u8(x)); v.push_back(u8(x >> 8)); }
void PF(Bytes& v, float f) { u32 u; std::memcpy(&u, &f, 4); P32(v, u); }
void Pad(Bytes& v) { while (v.size() % 4) v.push_back(0); }
Bytes I(std::initializer_list<int> l) { Bytes v; for (int x : l) P32(v, u32(x)); return v; }
void Cat(Bytes& a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); }

Bytes Rec(u32 type, Bytes payload) {
    Pad(payload);
    Bytes f;
    P32(f, type);
    P32(f, u32(payload.size() + 8));
    Cat(f, payload);
    return f;
}

struct Body {
    Bytes b;
    int n = 0;
    Body& add(const Bytes& r) { Cat(b, r); ++n; return *this; }
    Body& rec(u32 type, std::initializer_list<int> ints) { return add(Rec(type, I(ints))); }
};

Bytes MakeEmf(const Body& body, int handles = 16) {
    Bytes h, f;
    for (int x : {0, 0, 99, 99, 0, 0, 9999, 9999}) P32(h, u32(x));
    P32(h, 0x464d4520); P32(h, 0x10000);
    P32(h, 0); P32(h, u32(body.n + 2)); P16(h, u32(handles)); P16(h, 0);
    P32(h, 0); P32(h, 0); P32(h, 0);
    for (int x : {100, 100, 100, 100}) P32(h, u32(x)); // 100x100 px, 100x100 mm
    Cat(f, Rec(1, h));
    Cat(f, body.b);
    Cat(f, Rec(14, I({0, 0x10, 0x10})));
    u32 n = u32(f.size());
    std::memcpy(&f[8 + 40], &n, 4);
    return f;
}

struct Canvas {
    ::Surface surf{100, 100};
    GdiCtx dc;
    Canvas() {
        dc.surf = &surf;
        dc.viewportOrg = POINT{0, 0};
        dc.clip = RECT{0, 0, 100, 100};
    }
    uint32_t at(int x, int y) const { return surf.Row(y)[x]; }
    int count(uint32_t c) const {
        int n = 0;
        for (int y = 0; y < 100; ++y) for (int x = 0; x < 100; ++x) n += surf.Row(y)[x] == c;
        return n;
    }
    int nonzero() const {
        int n = 0;
        for (int y = 0; y < 100; ++y) for (int x = 0; x < 100; ++x) n += surf.Row(y)[x] != 0;
        return n;
    }
};

struct Played {
    emf::Metafile m;
    Canvas cv;
    u32 executed = 0;
    bool ok = false;
};
// plays into a fresh canvas, dest = the whole canvas
void Play(Played& p, const Body& body, Canvas* into = nullptr) {
    Bytes e = MakeEmf(body);
    p.ok = p.m.Load(e.data(), e.size());
    Canvas& c = into ? *into : p.cv;
    if (p.ok) p.executed = p.m.Play(c.dc, RECT{0, 0, 100, 100});
}

// ---- record builders ----------------------------------------------------------------------------------------------------
Bytes Brush(int ih, int style, int color, int hatch = 0) { return Rec(39, I({ih, style, color, hatch})); }
Bytes Pen(int ih, int style, int w, int color) { return Rec(38, I({ih, style, w, 0, color})); }
Bytes Sel(int ih) { return Rec(37, I({ih})); }
Bytes StockSel(u32 n) { return Rec(37, I({int(0x80000000u | n)})); }
Bytes RectRec(int l, int t, int r, int b) { return Rec(43, I({0, 0, 99, 99, l, t, r, b})); }
Bytes Move(int x, int y) { return Rec(27, I({x, y})); }
Bytes Line(int x, int y) { return Rec(54, I({x, y})); }
Bytes RgnData(std::initializer_list<std::initializer_list<int>> rects) {
    Bytes d;
    P32(d, 32); P32(d, 1); P32(d, u32(rects.size())); P32(d, u32(rects.size() * 16));
    for (int x : {0, 0, 99, 99}) P32(d, u32(x));
    for (auto& r : rects) for (int x : r) P32(d, u32(x));
    return d;
}
Bytes ExtSelClip(u32 mode, const Bytes& rgn) {
    Bytes p;
    P32(p, u32(rgn.size())); P32(p, mode); Cat(p, rgn);
    return Rec(75, p);
}
Bytes RgnRec(u32 type, const Bytes& rgn, int ihBrush, int sw = 0, int sh = 0) {
    Bytes p = I({0, 0, 99, 99});
    P32(p, u32(rgn.size()));
    if (type == 71 || type == 72) P32(p, u32(ihBrush));
    if (type == 72) { P32(p, u32(sw)); P32(p, u32(sh)); }
    Cat(p, rgn);
    return Rec(type, p);
}
Bytes Arc(u32 type, int l, int t, int r, int b, int sx, int sy, int ex, int ey) { return Rec(type, I({0, 0, 99, 99, l, t, r, b, sx, sy, ex, ey})); }

// 32 bit DIB, pixels top-down ARGB; returns BITMAPINFOHEADER + bits (bottom-up)
void Dib32(int w, int h, const std::vector<uint32_t>& px, Bytes& bmi, Bytes& bits) {
    bmi.clear(); bits.clear();
    P32(bmi, 40); P32(bmi, u32(w)); P32(bmi, u32(h)); P16(bmi, 1); P16(bmi, 32); P32(bmi, 0); P32(bmi, u32(w * h * 4)); P32(bmi, 0); P32(bmi, 0); P32(bmi, 0); P32(bmi, 0);
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x) P32(bits, px[size_t(y) * size_t(w) + size_t(x)]);
}
// 1 bit DIB (black/white palette); bit[x,y] top-down
void Dib1(int w, int h, const std::vector<int>& bit, Bytes& bmi, Bytes& bits) {
    bmi.clear(); bits.clear();
    P32(bmi, 40); P32(bmi, u32(w)); P32(bmi, u32(h)); P16(bmi, 1); P16(bmi, 1); P32(bmi, 0); P32(bmi, 0); P32(bmi, 0); P32(bmi, 0); P32(bmi, 2); P32(bmi, 0);
    P32(bmi, 0x00000000); P32(bmi, 0x00ffffff); // palette: 0 black, 1 white
    size_t stride = ((size_t(w) + 31) / 32) * 4;
    for (int y = h - 1; y >= 0; --y) {
        Bytes row(stride, 0);
        for (int x = 0; x < w; ++x) if (bit[size_t(y) * size_t(w) + size_t(x)]) row[size_t(x >> 3)] |= u8(0x80 >> (x & 7));
        Cat(bits, row);
    }
}

// ---- tests --------------------------------------------------------------------------------------------------------------------
void TestSkippedAndNoops() {
    std::printf("[emf: unknown / no-op records]\n");
    Body b;
    b.rec(200, {1, 2});                  // not an EMF record type: counted
    b.rec(107, {});                      // EMR_STARTDOC-like reserved number: counted
    for (u32 t : {16u, 23u, 58u, 98u, 99u, 100u, 101u, 102u, 103u, 104u, 105u, 106u, 109u, 110u, 111u, 112u, 113u, 115u, 119u, 120u, 121u, 122u, 48u, 50u, 51u, 52u, 65u, 13u, 21u})
        b.add(Rec(t, I({0, 0, 0, 0})));
    Played p;
    Play(p, b);
    CHECK(p.ok);
    CHECK(p.m.skipped().size() == 2 && p.m.skipped().count(200) == 1 && p.m.skipped().count(107) == 1);
    CHECK(p.executed == u32(b.n) + 2); // header + records + EOF
    // EMF+ comment
    Body c;
    Bytes com;
    P32(com, 8); com.insert(com.end(), {'E', 'M', 'F', '+', 0, 0, 0, 0});
    c.add(Rec(70, com));
    Played q;
    Play(q, c);
    CHECK(q.m.emfPlusRecords() == 1 && q.m.skipped().empty());
}

void TestRop2() {
    std::printf("[emf: SETROP2]\n");
    Body b;
    b.add(Brush(1, 0, C_RED)).add(Sel(1)).add(StockSel(8)).add(RectRec(10, 10, 50, 50));
    b.add(Brush(2, 0, C_WHITE)).add(Sel(2)).rec(20, {7});    // R2_XORPEN
    b.add(RectRec(30, 30, 70, 70));
    Played p;
    Play(p, b);
    CHECK(p.cv.at(20, 20) == RED);
    CHECK(p.cv.at(40, 40) == CYAN);                           // red ^ white
    CHECK(p.cv.at(60, 60) == WHITE);                          // black surface ^ white
    Body n;                                                    // R2_NOT inverts the destination, whatever the brush
    n.add(Brush(1, 0, C_RED)).add(Sel(1)).add(StockSel(8)).add(RectRec(10, 10, 50, 50)).rec(20, {6}).add(RectRec(20, 20, 30, 30));
    Played q;
    Play(q, n);
    CHECK(q.cv.at(25, 25) == CYAN && q.cv.at(15, 15) == RED);
    Body k; // R2_BLACK / R2_WHITE
    k.add(Brush(1, 0, C_RED)).add(Sel(1)).add(StockSel(8)).rec(20, {16}).add(RectRec(5, 5, 10, 10)).rec(20, {1}).add(RectRec(20, 5, 25, 10));
    Played r;
    Play(r, k);
    CHECK(r.cv.at(7, 7) == WHITE && r.cv.at(22, 7) == BLACK);
}

void TestDashAndHatch() {
    std::printf("[emf: dashed pens, user-style pens, hatch brushes]\n");
    {
        Body b; // PS_DASH, transparent background mode
        b.rec(18, {1}).add(Pen(1, 1, 0, C_RED)).add(Sel(1)).add(Move(0, 5)).add(Line(60, 5));
        Played p;
        Play(p, b);
        bool on = true;
        for (int x = 0; x < 18; ++x) on = on && p.cv.at(x, 5) == RED;
        CHECK(on);                                                       // 18 pixels on
        CHECK(p.cv.at(18, 5) == 0 && p.cv.at(23, 5) == 0);               // 6 off, background untouched (TRANSPARENT)
        CHECK(p.cv.at(24, 5) == RED && p.cv.at(41, 5) == RED);           // next dash
        CHECK(p.cv.at(42, 5) == 0);
    }
    {
        Body b; // OPAQUE (default): the gaps take the background colour
        b.add(Pen(1, 2, 0, C_RED)).add(Sel(1)).add(Move(0, 5)).add(Line(30, 5)); // PS_DOT: 3 on 3 off
        Played p;
        Play(p, b);
        CHECK(p.cv.at(1, 5) == RED && p.cv.at(4, 5) == WHITE && p.cv.at(7, 5) == RED);
    }
    {
        Body b; // PS_USERSTYLE with entries 4,2 on a 1 pixel pen
        Bytes pen = I({2, 0, 0, 0, 0});
        P32(pen, 7); P32(pen, 1); P32(pen, 0); P32(pen, u32(C_RED)); P32(pen, 0); P32(pen, 2); P32(pen, 4); P32(pen, 2);
        b.add(Rec(95, pen)).add(Sel(2)).rec(18, {1}).add(Move(0, 8)).add(Line(30, 8));
        Played p;
        Play(p, b);
        CHECK(p.cv.at(3, 8) == RED && p.cv.at(4, 8) == 0 && p.cv.at(5, 8) == 0 && p.cv.at(6, 8) == RED);
    }
    {
        Body b; // horizontal hatch: rows with y % 8 == 4
        b.rec(18, {1}).add(Brush(1, 2, C_RED, 0)).add(Sel(1)).add(StockSel(8)).add(RectRec(0, 0, 40, 40));
        Played p;
        Play(p, b);
        CHECK(p.cv.at(10, 4) == RED && p.cv.at(10, 12) == RED && p.cv.at(10, 5) == 0 && p.cv.at(10, 3) == 0);
        Body d; // diagonal cross, opaque background: the gaps are white
        d.add(Brush(1, 2, C_BLUE, 5)).add(Sel(1)).add(StockSel(8)).add(RectRec(0, 0, 40, 40));
        Played q;
        Play(q, d);
        CHECK(q.cv.at(8, 8) == BLUE && q.cv.at(9, 8) == WHITE);
    }
}

void TestWinding() {
    std::printf("[emf: WINDING fill]\n");
    // pentagram: the centre is covered twice (winding) / twice = hole (alternate)
    int cx = 50, cy = 50, R = 40;
    std::vector<int> pts;
    int order[5] = {0, 2, 4, 1, 3};
    for (int i = 0; i < 5; ++i) {
        double a = -1.5707963267948966 + 2 * 3.14159265358979 * order[i] / 5;
        pts.push_back(cx + int(R * std::cos(a)));
        pts.push_back(cy + int(R * std::sin(a)));
    }
    for (int mode : {1, 2}) {
        Body b;
        b.add(Brush(1, 0, C_RED)).add(Sel(1)).add(StockSel(8)).rec(19, {mode});
        Bytes p = I({0, 0, 99, 99, 5});
        for (int v : pts) P16(p, u32(v));
        b.add(Rec(86, p));
        Played pl;
        Play(pl, b);
        if (mode == 1) CHECK(pl.cv.at(50, 50) == 0);      // ALTERNATE: hole in the middle
        else CHECK(pl.cv.at(50, 50) == RED);               // WINDING: filled
        CHECK(pl.cv.at(50, 20) == RED);                    // a tip area is filled in both modes
    }
}

void TestRegionsAndClip() {
    std::printf("[emf: regions, clip regions, FILLRGN / FRAMERGN / PAINTRGN / INVERTRGN, SETMETARGN, OFFSETCLIPRGN]\n");
    Bytes A = RgnData({{10, 10, 40, 40}}), B2 = RgnData({{30, 30, 60, 60}});
    auto fillAll = [&](Body& b) { b.add(Brush(9, 0, C_RED)).add(Sel(9)).add(StockSel(8)).add(RectRec(0, 0, 101, 101)); }; // NULL pen: the right / bottom edge is not painted
    {
        Body b; // RGN_COPY
        b.add(ExtSelClip(5, A));
        fillAll(b);
        Played p;
        Play(p, b);
        CHECK(p.cv.at(20, 20) == RED && p.cv.at(5, 5) == 0 && p.cv.at(45, 45) == 0);
        CHECK(p.cv.count(RED) == 30 * 30);
    }
    {
        Body b; // AND of two overlapping rectangles
        b.add(ExtSelClip(5, A)).add(ExtSelClip(1, B2));
        fillAll(b);
        Played p;
        Play(p, b);
        CHECK(p.cv.count(RED) == 10 * 10 && p.cv.at(35, 35) == RED && p.cv.at(20, 20) == 0);
    }
    {
        Body b; // OR
        b.add(ExtSelClip(5, A)).add(ExtSelClip(2, B2));
        fillAll(b);
        Played p;
        Play(p, b);
        CHECK(p.cv.count(RED) == 900 + 900 - 100 && p.cv.at(20, 20) == RED && p.cv.at(55, 55) == RED && p.cv.at(55, 15) == 0);
    }
    {
        Body b; // XOR
        b.add(ExtSelClip(5, A)).add(ExtSelClip(3, B2));
        fillAll(b);
        Played p;
        Play(p, b);
        CHECK(p.cv.count(RED) == 900 + 900 - 200 && p.cv.at(35, 35) == 0 && p.cv.at(20, 20) == RED);
    }
    {
        Body b; // DIFF: A minus B
        b.add(ExtSelClip(5, A)).add(ExtSelClip(4, B2));
        fillAll(b);
        Played p;
        Play(p, b);
        CHECK(p.cv.count(RED) == 900 - 100 && p.cv.at(35, 35) == 0 && p.cv.at(15, 15) == RED);
    }
    {
        Body b; // an empty region with RGN_COPY restores the unclipped state
        b.add(ExtSelClip(5, A)).add(Rec(75, I({0, 5})));
        fillAll(b);
        Played p;
        Play(p, b);
        CHECK(p.cv.count(RED) == 100 * 100);
    }
    {
        Body b; // IntersectClipRect still combines with a clip region
        b.add(ExtSelClip(5, A)).rec(30, {0, 0, 25, 100});
        fillAll(b);
        Played p;
        Play(p, b);
        CHECK(p.cv.count(RED) == 15 * 30 && p.cv.at(24, 20) == RED && p.cv.at(25, 20) == 0);
    }
    {
        Body b; // SETMETARGN: the clip becomes the meta region, an empty COPY goes back to it, not to the whole surface
        b.add(ExtSelClip(5, A)).rec(28, {}).add(ExtSelClip(5, B2)).add(Rec(75, I({0, 5})));
        fillAll(b);
        Played p;
        Play(p, b);
        CHECK(p.cv.count(RED) == 900 && p.cv.at(20, 20) == RED && p.cv.at(50, 50) == 0);
    }
    {
        Body b; // OFFSETCLIPRGN moves the clip region
        b.add(ExtSelClip(5, A)).rec(26, {20, 5});
        fillAll(b);
        Played p;
        Play(p, b);
        CHECK(p.cv.at(35, 20) == RED && p.cv.at(15, 15) == 0 && p.cv.at(55, 20) == RED && p.cv.at(65, 20) == 0 && p.cv.at(35, 10) == 0 && p.cv.count(RED) == 900);
    }
    {
        Body b; // two excluded rectangles
        b.rec(29, {10, 10, 30, 30}).rec(29, {50, 50, 70, 70});
        fillAll(b);
        Played p;
        Play(p, b);
        CHECK(p.cv.count(RED) == 10000 - 800 && p.cv.at(20, 20) == 0 && p.cv.at(60, 60) == 0 && p.cv.at(40, 40) == RED);
    }
    {
        Body b; // FILLRGN / PAINTRGN / FRAMERGN
        b.add(Brush(1, 0, C_BLUE)).add(Brush(2, 0, C_GREEN)).add(Sel(2));
        b.add(RgnRec(71, A, 1));                         // FILLRGN with brush 1 (blue)
        b.add(RgnRec(74, B2, 0));                        // PAINTRGN with the selected brush (green)
        Played p;
        Play(p, b);
        CHECK(p.cv.at(15, 15) == BLUE && p.cv.at(50, 50) == GREEN && p.cv.at(35, 35) == GREEN && p.cv.at(80, 80) == 0);
        Body f;
        f.add(Brush(1, 0, C_BLUE)).add(RgnRec(72, A, 1, 2, 2));
        Played q;
        Play(q, f);
        CHECK(q.cv.at(10, 25) == BLUE && q.cv.at(11, 25) == BLUE && q.cv.at(12, 25) == 0 && q.cv.at(25, 25) == 0 && q.cv.at(39, 25) == BLUE);
        Body inv; // INVERTRGN
        inv.add(Brush(1, 0, C_RED)).add(Sel(1)).add(StockSel(8)).add(RectRec(0, 0, 50, 50)).add(RgnRec(73, A, 0));
        Played r;
        Play(r, inv);
        CHECK(r.cv.at(20, 20) == CYAN && r.cv.at(5, 5) == RED && r.cv.at(45, 45) == RED);
    }
}

void TestPaths() {
    std::printf("[emf: paths (clip path with a real mask, WIDENPATH, text in a path), ARCTO, SETARCDIRECTION, POLYDRAW]\n");
    {
        Body b; // SELECTCLIPPATH of a triangle: fill everything, only the triangle is painted (not its bounding box)
        b.rec(59, {}).add(Move(10, 10)).add(Line(90, 10)).add(Line(10, 90)).rec(61, {}).rec(60, {}).rec(67, {5});
        b.add(Brush(1, 0, C_RED)).add(Sel(1)).add(StockSel(8)).add(RectRec(0, 0, 100, 100));
        Played p;
        Play(p, b);
        CHECK(p.cv.at(20, 20) == RED && p.cv.at(80, 80) == 0 && p.cv.at(60, 15) == RED && p.cv.at(15, 60) == RED);
        int n = p.cv.count(RED);
        CHECK(n > 2800 && n < 3400); // area of the triangle = 3200
    }
    {
        Body b; // FILLPATH of a donut (two figures, even-odd) keeps the hole
        b.add(Brush(1, 0, C_RED)).add(Sel(1)).add(StockSel(8)).rec(59, {});
        b.add(Move(10, 10)).add(Line(90, 10)).add(Line(90, 90)).add(Line(10, 90)).rec(61, {});
        b.add(Move(30, 30)).add(Line(70, 30)).add(Line(70, 70)).add(Line(30, 70)).rec(61, {});
        b.rec(60, {}).rec(62, {});
        Played p;
        Play(p, b);
        CHECK(p.cv.at(20, 20) == RED && p.cv.at(50, 50) == 0 && p.cv.at(80, 50) == RED);
    }
    {
        Body b; // WIDENPATH: a 1 pixel line becomes a 10 pixel wide band when filled
        b.add(Pen(1, 0, 10, C_RED)).add(Sel(1)).add(Brush(2, 0, C_GREEN)).add(Sel(2));
        b.rec(59, {}).add(Move(10, 50)).add(Line(90, 50)).rec(60, {}).rec(66, {}).rec(62, {});
        Played p;
        Play(p, b);
        CHECK(p.cv.at(50, 47) == GREEN && p.cv.at(50, 53) == GREEN && p.cv.at(50, 40) == 0 && p.cv.at(50, 60) == 0);
    }
    {
        Body b; // text in a path used as a clip: only glyph pixels get painted
        Bytes font = I({3});
        P32(font, u32(-24)); P32(font, 0); P32(font, 0); P32(font, 0); P32(font, 700);
        font.push_back(0); font.push_back(0); font.push_back(0); font.push_back(0); font.push_back(0); font.push_back(0); font.push_back(0); font.push_back(0);
        for (const char* s = "Arial"; *s; ++s) P16(font, u32(*s));
        font.resize(4 + 28 + 64, 0);
        b.add(Rec(82, font)).add(Sel(3));
        Bytes txt;
        for (int x : {0, 0, 99, 99}) P32(txt, u32(x));
        P32(txt, 1); PF(txt, 1.0f); PF(txt, 1.0f);       // iGraphicsMode, exScale, eyScale
        P32(txt, 5); P32(txt, 20);                        // reference (5, 20)
        P32(txt, 3); P32(txt, 76); P32(txt, 0);          // nChars, offString, options
        for (int x : {0, 0, 0, 0}) P32(txt, u32(x));     // rcl
        P32(txt, 0);                                      // offDx
        txt.push_back('H'); txt.push_back('I'); txt.push_back('I');
        b.rec(59, {}).add(Rec(83, txt)).rec(60, {}).rec(67, {5});
        b.add(Brush(1, 0, C_RED)).add(Sel(1)).add(StockSel(8)).add(RectRec(0, 0, 100, 100));
        Played p;
        Play(p, b);
        int n = p.cv.count(RED);
        CHECK(n > 20 && n < 1500);                         // text-shaped, not the whole surface
    }
    {
        Body b; // ARCTO: a line from the current position to the start of the arc, then the arc, current position = end point
        b.add(Pen(1, 0, 0, C_RED)).add(Sel(1)).add(Move(10, 50));
        b.add(Arc(55, 30, 30, 70, 70, 30, 50, 70, 50));    // counterclockwise (default): through the bottom
        b.add(Line(90, 50));
        Played p;
        Play(p, b);
        CHECK(p.cv.at(20, 50) == RED);                     // the connecting line
        CHECK(p.cv.at(50, 69) == RED || p.cv.at(50, 70) == RED);   // bottom of the arc
        CHECK(p.cv.at(50, 30) == 0 && p.cv.at(50, 29) == 0);
        CHECK(p.cv.at(80, 50) == RED);                     // LineTo from the end point of the arc
        Body c; // AD_CLOCKWISE: through the top
        c.add(Pen(1, 0, 0, C_RED)).add(Sel(1)).rec(57, {2}).add(Move(10, 50)).add(Arc(55, 30, 30, 70, 70, 30, 50, 70, 50));
        Played q;
        Play(q, c);
        CHECK(q.cv.at(50, 30) == RED || q.cv.at(50, 29) == RED);
        CHECK(q.cv.at(50, 69) == 0 && q.cv.at(50, 70) == 0);
        Body d; // plain ARC honours the direction too
        d.add(Pen(1, 0, 0, C_RED)).add(Sel(1)).rec(57, {2}).add(Arc(45, 30, 30, 70, 70, 30, 50, 70, 50));
        Played r;
        Play(r, d);
        CHECK((r.cv.at(50, 30) == RED || r.cv.at(50, 29) == RED) && r.cv.at(50, 69) == 0);
    }
    {
        Body b; // POLYDRAW: moveto, lineto, bezierto (3 points), lineto with closefigure
        Bytes p = I({0, 0, 99, 99, 6});
        int xy[6][2] = {{10, 10}, {60, 10}, {80, 20}, {90, 40}, {90, 60}, {10, 60}};
        for (auto& q : xy) { P32(p, u32(q[0])); P32(p, u32(q[1])); }
        u8 types[6] = {6, 2, 4, 4, 4, u8(2 | 1)};
        for (u8 t : types) p.push_back(t);
        b.add(Pen(1, 0, 0, C_RED)).add(Sel(1)).add(Rec(56, p));
        Played pl;
        Play(pl, b);
        CHECK(pl.cv.at(30, 10) == RED);                    // the line
        CHECK(pl.cv.at(90, 60) == RED);                    // end of the Bezier curve
        CHECK(pl.cv.at(50, 60) == RED);                    // lineto
        CHECK(pl.cv.at(10, 35) == RED);                    // closefigure: back to the start
        CHECK(pl.cv.at(50, 35) == 0);
        Body b16;
        Bytes q16 = I({0, 0, 99, 99, 3});
        for (int v : {10, 80, 50, 80, 50, 90}) P16(q16, u32(v));
        q16.insert(q16.end(), {6, 2, 2});
        b16.add(Pen(1, 0, 0, C_RED)).add(Sel(1)).add(Rec(92, q16));
        Played p16;
        Play(p16, b16);
        CHECK(p16.cv.at(30, 80) == RED && p16.cv.at(50, 85) == RED);
    }
}

void TestMapModes() {
    std::printf("[emf: MM_ISOTROPIC]\n");
    for (int mode : {7, 8}) {
        Body b;
        b.rec(17, {mode}).rec(9, {100, 100}).rec(11, {200, 100}); // window ext 100x100, viewport ext 200x100
        b.add(Brush(1, 0, C_RED)).add(Sel(1)).add(StockSel(8)).add(RectRec(40, 40, 60, 60));
        Played p;
        Play(p, b);
        if (mode == 7) { // isotropic: the x extent shrinks to 100 so 1 logical unit = 1 pixel on both axes
            CHECK(p.cv.at(50, 50) == RED && p.cv.at(70, 50) == 0 && p.cv.at(39, 50) == 0 && p.cv.at(41, 50) == RED);
        } else {         // anisotropic: x is doubled
            CHECK(p.cv.at(90, 50) == RED && p.cv.at(70, 50) == 0 && p.cv.at(50, 70) == 0);
        }
    }
}

void TestText() {
    std::printf("[emf: text (ETO_OPAQUE rectangle, polytextout, smalltextout, escapement, dx)]\n");
    auto fontRec = [](int ih, int height, int escapement) {
        Bytes font = I({ih});
        P32(font, u32(height)); P32(font, 0); P32(font, u32(escapement)); P32(font, u32(escapement)); P32(font, 400);
        for (int i = 0; i < 8; ++i) font.push_back(0);
        for (const char* s = "Arial"; *s; ++s) P16(font, u32(*s));
        font.resize(4 + 28 + 64, 0);
        return Rec(82, font);
    };
    auto textRec = [](u32 type, int x, int y, const char* s, u32 opts, RECT rcl, const std::vector<int>* dx) {
        Bytes t;
        for (int v : {0, 0, 99, 99}) P32(t, u32(v));
        P32(t, 1); PF(t, 1.0f); PF(t, 1.0f);
        P32(t, u32(x)); P32(t, u32(y));
        u32 n = u32(std::strlen(s));
        u32 strOff = 76 + 0;
        u32 dxOff = 0;
        P32(t, n); P32(t, strOff); P32(t, opts);
        for (int v : {rcl.left, rcl.top, rcl.right, rcl.bottom}) P32(t, u32(v));
        size_t dxField = t.size();
        P32(t, 0);
        for (u32 i = 0; i < n; ++i) t.push_back(u8(s[i]));
        Pad(t);
        if (dx) {
            dxOff = u32(t.size() + 8);
            std::memcpy(&t[dxField], &dxOff, 4);
            for (int v : *dx) P32(t, u32(v));
        }
        return Rec(type, t);
    };
    {
        Body b; // ETO_OPAQUE fills the given rectangle with the background colour
        b.add(fontRec(1, -16, 0)).add(Sel(1)).rec(25, {C_GREEN}).rec(24, {C_RED});
        b.add(textRec(83, 20, 20, "Hi", 2, RECT{10, 10, 60, 40}, nullptr));
        Played p;
        Play(p, b);
        CHECK(p.cv.at(12, 12) == GREEN && p.cv.at(58, 38) == GREEN && p.cv.at(70, 70) == 0 && p.cv.count(RED) > 5);
    }
    {
        Body b; // TRANSPARENT mode and no rectangle: only glyph pixels
        b.add(fontRec(1, -16, 0)).add(Sel(1)).rec(18, {1}).rec(24, {C_RED});
        b.add(textRec(83, 10, 10, "WWWW", 0, RECT{0, 0, 0, 0}, nullptr));
        Played p;
        Play(p, b);
        int minx = 100, maxx = -1, miny = 100, maxy = -1;
        for (int y = 0; y < 100; ++y) for (int x = 0; x < 100; ++x) if (p.cv.at(x, y) == RED) { minx = std::min(minx, x); maxx = std::max(maxx, x); miny = std::min(miny, y); maxy = std::max(maxy, y); }
        CHECK(maxx > minx && (maxx - minx) > (maxy - miny)); // wider than tall
    }
    {
        Body b; // escapement 900 (90 degrees counterclockwise): taller than wide
        b.add(fontRec(1, -16, 900)).add(Sel(1)).rec(18, {1}).rec(24, {C_RED}).rec(22, {24});  // TA_BASELINE
        b.add(textRec(83, 30, 60, "WWWW", 0, RECT{0, 0, 0, 0}, nullptr));
        Played p;
        Play(p, b);
        int minx = 100, maxx = -1, miny = 100, maxy = -1;
        for (int y = 0; y < 100; ++y) for (int x = 0; x < 100; ++x) if (p.cv.at(x, y) == RED) { minx = std::min(minx, x); maxx = std::max(maxx, x); miny = std::min(miny, y); maxy = std::max(maxy, y); }
        CHECK(maxx >= minx && (maxy - miny) > (maxx - minx));
        CHECK(miny < 60 && maxy <= 61); // the text climbs upwards from the reference point
    }
    {
        Body b; // explicit advances spread the characters
        std::vector<int> dx = {30, 30, 30};
        b.add(fontRec(1, -12, 0)).add(Sel(1)).rec(18, {1}).rec(24, {C_RED});
        b.add(textRec(83, 5, 20, "III", 0, RECT{0, 0, 0, 0}, &dx));
        Played p;
        Play(p, b);
        int cols[3] = {0, 0, 0};
        for (int y = 0; y < 100; ++y) for (int x = 0; x < 100; ++x) if (p.cv.at(x, y) == RED) cols[std::min(2, (x - 5) / 30)] += (x >= 5);
        CHECK(cols[0] > 0 && cols[1] > 0 && cols[2] > 0);
    }
    {
        Body b; // POLYTEXTOUTA with two strings, SMALLTEXTOUT
        b.add(fontRec(1, -14, 0)).add(Sel(1)).rec(18, {1}).rec(24, {C_RED});
        Bytes pt = I({0, 0, 99, 99, 1});
        PF(pt, 1.0f); PF(pt, 1.0f);   // exScale, eyScale (iGraphicsMode above)
        // aemrtext: 2 strings; build by hand
        Bytes q;
        for (int v : {0, 0, 99, 99}) P32(q, u32(v));
        P32(q, 1); PF(q, 1.0f); PF(q, 1.0f);
        P32(q, 2);                                   // cStrings
        size_t emr = q.size();
        q.resize(emr + 80, 0);
        const char* s1 = "AB";
        const char* s2 = "CD";
        u32 off1 = u32(q.size() + 8);
        q.push_back(s1[0]); q.push_back(s1[1]); q.push_back(0); q.push_back(0);
        u32 off2 = u32(q.size() + 8);
        q.push_back(s2[0]); q.push_back(s2[1]); q.push_back(0); q.push_back(0);
        auto set = [&](size_t o, u32 v) { std::memcpy(&q[o], &v, 4); };
        set(emr + 0, 5); set(emr + 4, 10); set(emr + 8, 2); set(emr + 12, off1); set(emr + 16, 0);
        set(emr + 40, 5); set(emr + 44, 60); set(emr + 48, 2); set(emr + 52, off2); set(emr + 56, 0);
        b.add(Rec(96, q));
        Played p;
        Play(p, b);
        int top = 0, bottom = 0;
        for (int y = 0; y < 100; ++y) for (int x = 0; x < 100; ++x) if (p.cv.at(x, y) == RED) { if (y < 40) ++top; else ++bottom; }
        CHECK(top > 5 && bottom > 5);
        Body s; // SMALLTEXTOUT, 8 bit chars, no rect
        Bytes st = I({10, 30, 3, 0x100 | 0x200, 1});
        PF(st, 1.0f); PF(st, 1.0f);
        st.push_back('I'); st.push_back('I'); st.push_back('I');
        s.add(fontRec(1, -14, 0)).add(Sel(1)).rec(18, {1}).rec(24, {C_RED}).add(Rec(108, st));
        Played q2;
        Play(q2, s);
        CHECK(q2.cv.count(RED) > 5);
    }
}

void TestBlits() {
    std::printf("[emf: ALPHABLEND, TRANSPARENTBLT, MASKBLT, PLGBLT, ROP3 BitBlt, STRETCHDIBITS ROP]\n");
    // a 2x2 premultiplied-alpha bitmap: red 50% | green 100% / blue 0% | white 100%
    std::vector<uint32_t> px = {0x80800000u, 0xFF00FF00u, 0x000000FFu, 0xFFFFFFFFu};
    Bytes bmi, bits;
    Dib32(2, 2, px, bmi, bits);
    auto alpha = [&](u32 type, u32 bf, u32 key) {
        // fixed part: bounds(4) xD yD cxD cyD rop xS yS xform(6) crBk usage offBmi cbBmi offBits cbBits cxS cyS = 25 dwords + header 2 = 108 bytes
        Bytes p = I({0, 0, 99, 99, 20, 20, 20, 20, int(bf), 0, 0});
        for (float f : {1.f, 0.f, 0.f, 1.f, 0.f, 0.f}) PF(p, f);
        P32(p, key); P32(p, 0); P32(p, 0); P32(p, 0); P32(p, 0); P32(p, 0); P32(p, 2); P32(p, 2);
        u32 offBmi = u32(p.size() + 8), offBits = offBmi + u32(bmi.size());
        std::memcpy(&p[84 - 8], &offBmi, 4);
        u32 cb = u32(bmi.size()), cbb = u32(bits.size());
        std::memcpy(&p[88 - 8], &cb, 4);
        std::memcpy(&p[92 - 8], &offBits, 4);
        std::memcpy(&p[96 - 8], &cbb, 4);
        Cat(p, bmi); Cat(p, bits);
        return Rec(type, p);
    };
    {
        Body b; // per-pixel alpha (premultiplied) over a blue background; constant alpha 255
        b.add(Brush(1, 0, C_BLUE)).add(Sel(1)).add(StockSel(8)).add(RectRec(0, 0, 100, 100));
        b.add(alpha(114, 0x01FF0000u, 0));
        Played p;
        Play(p, b);
        uint32_t c = p.cv.at(25, 25);   // upper-left source pixel: 50% red premultiplied (0x80) over blue
        CHECK(((c >> 16) & 0xff) >= 0x7f && ((c >> 16) & 0xff) <= 0x81 && (c & 0xff) >= 0x7e && (c & 0xff) <= 0x80);
        CHECK(p.cv.at(35, 25) == GREEN);                  // opaque green
        CHECK(p.cv.at(25, 35) == BLUE);                   // alpha 0: destination untouched
        CHECK(p.cv.at(35, 35) == WHITE);
        Body q; // constant alpha 128, no per-pixel alpha: 50% mix of the source colour with the destination
        q.add(Brush(1, 0, C_BLUE)).add(Sel(1)).add(StockSel(8)).add(RectRec(0, 0, 100, 100));
        q.add(alpha(114, 0x00800000u, 0));
        Played r;
        Play(r, q);
        uint32_t g = r.cv.at(35, 25);   // green 50% over blue
        CHECK(((g >> 8) & 0xff) >= 0x7e && ((g >> 8) & 0xff) <= 0x80 && (g & 0xff) >= 0x7e && (g & 0xff) <= 0x80);
    }
    {
        Body b; // TRANSPARENTBLT: pure white is the colour key
        b.add(Brush(1, 0, C_BLUE)).add(Sel(1)).add(StockSel(8)).add(RectRec(0, 0, 100, 100));
        b.add(alpha(116, 0, C_WHITE));
        Played p;
        Play(p, b);
        CHECK(p.cv.at(35, 35) == BLUE && p.cv.at(35, 25) == GREEN);
    }
    {
        // MASKBLT: 4x4 red source, mask = left half set; foreground SRCCOPY, background DSTCOPY-like (0xAA = D)
        std::vector<uint32_t> red(16, 0xFFFF0000u);
        Bytes sb, sbits, mb, mbits;
        Dib32(4, 4, red, sb, sbits);
        std::vector<int> bit(16, 0);
        for (int y = 0; y < 4; ++y) { bit[size_t(y) * 4] = 1; bit[size_t(y) * 4 + 1] = 1; }
        Dib1(4, 4, bit, mb, mbits);
        Bytes p = I({0, 0, 99, 99, 10, 10, 8, 8, int(0xAACC0000u), 0, 0});
        for (float f : {1.f, 0.f, 0.f, 1.f, 0.f, 0.f}) PF(p, f);
        P32(p, 0); P32(p, 0); P32(p, 0); P32(p, 0); P32(p, 0); P32(p, 0);       // crBk, usage, offBmi, cbBmi, offBits, cbBits
        P32(p, 0); P32(p, 0); P32(p, 0); P32(p, 0); P32(p, 0); P32(p, 0); P32(p, 0); // xMask, yMask, usageMask, offBmiM, cbBmiM, offBitsM, cbBitsM
        u32 base = u32(p.size() + 8);
        u32 offs[4] = {base, base + u32(sb.size()), base + u32(sb.size() + sbits.size()), base + u32(sb.size() + sbits.size() + mb.size())};
        auto set = [&](size_t field, u32 v) { std::memcpy(&p[field - 8], &v, 4); };
        set(84, offs[0]); set(88, u32(sb.size())); set(92, offs[1]); set(96, u32(sbits.size()));
        set(112, offs[2]); set(116, u32(mb.size())); set(120, offs[3]); set(124, u32(mbits.size()));
        Cat(p, sb); Cat(p, sbits); Cat(p, mb); Cat(p, mbits);
        Body b;
        b.add(Brush(1, 0, C_BLUE)).add(Sel(1)).add(StockSel(8)).add(RectRec(0, 0, 100, 100)).add(Rec(78, p));
        Played pl;
        Play(pl, b);
        CHECK(pl.cv.at(11, 11) == RED && pl.cv.at(13, 11) == RED);       // left half (mask set): source
        CHECK(pl.cv.at(15, 11) == BLUE && pl.cv.at(17, 11) == BLUE);     // right half: destination kept
    }
    {
        // PLGBLT: a 2x2 bitmap (red, green / blue, white) mapped on the parallelogram (20,20) (60,20) (20,60) = a plain 40x40 box
        std::vector<uint32_t> q = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFFFFu};
        Bytes sb, sbits;
        Dib32(2, 2, q, sb, sbits);
        Bytes p = I({0, 0, 99, 99, 20, 20, 60, 20, 20, 60, 0, 0, 2, 2});
        for (float f : {1.f, 0.f, 0.f, 1.f, 0.f, 0.f}) PF(p, f);
        for (int i = 0; i < 12; ++i) P32(p, 0);   // crBk, usage, offBmi, cbBmi, offBits, cbBits, xMask, yMask, usageMask, offBmiM, cbBmiM, offBitsM, cbBitsM -> 13
        P32(p, 0);
        u32 base = u32(p.size() + 8);
        auto set = [&](size_t field, u32 v) { std::memcpy(&p[field - 8], &v, 4); };
        set(96, base); set(100, u32(sb.size())); set(104, base + u32(sb.size())); set(108, u32(sbits.size()));
        Cat(p, sb); Cat(p, sbits);
        Body b;
        b.add(Rec(79, p));
        Played pl;
        Play(pl, b);
        CHECK(pl.cv.at(30, 30) == RED && pl.cv.at(50, 30) == GREEN && pl.cv.at(30, 50) == BLUE && pl.cv.at(50, 50) == WHITE && pl.cv.at(65, 65) == 0);
        // a sheared target: (20,20) (60,20) (40,60): the lower-left source corner lands at (40,60)
        Bytes s = I({0, 0, 99, 99, 20, 20, 60, 20, 40, 60, 0, 0, 2, 2});
        for (float f : {1.f, 0.f, 0.f, 1.f, 0.f, 0.f}) PF(s, f);
        for (int i = 0; i < 13; ++i) P32(s, 0);
        u32 base2 = u32(s.size() + 8);
        std::memcpy(&s[96 - 8], &base2, 4);
        u32 cb = u32(sb.size()), cbits = u32(sbits.size()), ob = base2 + cb;
        std::memcpy(&s[100 - 8], &cb, 4); std::memcpy(&s[104 - 8], &ob, 4); std::memcpy(&s[108 - 8], &cbits, 4);
        Cat(s, sb); Cat(s, sbits);
        Body b2;
        b2.add(Rec(79, s));
        Played p2;
        Play(p2, b2);
        CHECK(p2.cv.at(25, 22) == RED && p2.cv.at(55, 22) == GREEN && p2.cv.at(40, 58) != 0 && p2.cv.at(22, 58) == 0);
    }
    {
        // BitBlt without a bitmap: DSTINVERT (0x550009), PATINVERT (0x5A0049), BLACKNESS, and a ROP that needs a source is counted
        Body b;
        b.add(Brush(1, 0, C_RED)).add(Sel(1)).add(StockSel(8)).add(RectRec(0, 0, 50, 50));
        Bytes inv = I({0, 0, 99, 99, 10, 10, 20, 20, 0x00550009});
        inv.resize(100 - 8, 0);
        b.add(Rec(76, inv));
        Bytes patinv = I({0, 0, 99, 99, 30, 30, 10, 10, 0x005A0049});
        patinv.resize(100 - 8, 0);
        b.add(Rec(76, patinv));
        Bytes src = I({0, 0, 99, 99, 0, 0, 5, 5, 0x00CC0020});
        src.resize(100 - 8, 0);
        b.add(Rec(76, src));
        Played p;
        Play(p, b);
        CHECK(p.cv.at(15, 15) == CYAN && p.cv.at(5, 5) == RED);
        CHECK(p.cv.at(35, 35) == BLACK);                 // PATINVERT: pattern (red) ^ destination (red) = black
        CHECK(p.m.skipped().count(76) == 1);
    }
    {
        // 16 bit BI_BITFIELDS (565) bitmap through STRETCHDIBITS
        Bytes bmi;
        P32(bmi, 40); P32(bmi, 1); P32(bmi, 1); P16(bmi, 1); P16(bmi, 16); P32(bmi, 3); P32(bmi, 4); P32(bmi, 0); P32(bmi, 0); P32(bmi, 0); P32(bmi, 0);
        P32(bmi, 0xF800); P32(bmi, 0x07E0); P32(bmi, 0x001F);
        Bytes bits;
        P16(bits, 0xF800); P16(bits, 0);   // one red pixel + padding
        Bytes p = I({0, 0, 99, 99, 10, 10, 0, 0, 1, 1});
        u32 off = u32(80), offBits = u32(80 + bmi.size());
        P32(p, off); P32(p, u32(bmi.size())); P32(p, offBits); P32(p, u32(bits.size())); P32(p, 0); P32(p, 0x00CC0020); P32(p, 10); P32(p, 10);
        p.resize(80 - 8, 0);
        Cat(p, bmi); Cat(p, bits);
        // offsets inside p: fixed part is 80 bytes
        Body b;
        b.add(Rec(81, p));
        Played pl;
        Play(pl, b);
        CHECK(pl.cv.at(15, 15) == RED);
    }
}

void TestGradientFlood() {
    std::printf("[emf: GRADIENTFILL, EXTFLOODFILL, pattern brushes]\n");
    auto vtx = [](Bytes& v, int x, int y, u32 r, u32 g, u32 b) {
        P32(v, u32(x)); P32(v, u32(y)); P16(v, r); P16(v, g); P16(v, b); P16(v, 0);
    };
    {
        Body b; // horizontal ramp black -> white across 10..90
        Bytes p = I({0, 0, 99, 99, 2, 1, 0});
        vtx(p, 10, 10, 0, 0, 0);
        vtx(p, 90, 40, 0xff00, 0xff00, 0xff00);
        P32(p, 0); P32(p, 1);
        b.add(Rec(118, p));
        Played pl;
        Play(pl, b);
        uint32_t l = pl.cv.at(11, 20) & 0xff, m = pl.cv.at(50, 20) & 0xff, r = pl.cv.at(88, 20) & 0xff;
        CHECK(l < 12 && m > 100 && m < 160 && r > 240);
        CHECK(pl.cv.at(50, 5) == 0 && pl.cv.at(50, 45) == 0);
        CHECK((pl.cv.at(50, 11) & 0xff) == (pl.cv.at(50, 39) & 0xff));   // constant along y
        Body v; // vertical ramp red -> blue
        Bytes q = I({0, 0, 99, 99, 2, 1, 1});
        vtx(q, 10, 10, 0xff00, 0, 0);
        vtx(q, 40, 90, 0, 0, 0xff00);
        P32(q, 0); P32(q, 1);
        v.add(Rec(118, q));
        Played pv;
        Play(pv, v);
        CHECK(((pv.cv.at(20, 11) >> 16) & 0xff) > 240 && (pv.cv.at(20, 89) & 0xff) > 240 && (pv.cv.at(20, 50) & 0xff) > 100 && (pv.cv.at(20, 50) & 0xff) < 160);
        Body t; // triangle: red, green, blue corners; the centroid is a mix
        Bytes tr = I({0, 0, 99, 99, 3, 1, 2});
        vtx(tr, 10, 90, 0xff00, 0, 0);
        vtx(tr, 90, 90, 0, 0xff00, 0);
        vtx(tr, 50, 10, 0, 0, 0xff00);
        P32(tr, 0); P32(tr, 1); P32(tr, 2);
        t.add(Rec(118, tr));
        Played pt;
        Play(pt, t);
        uint32_t c = pt.cv.at(50, 63);
        CHECK(((c >> 16) & 0xff) > 60 && ((c >> 8) & 0xff) > 60 && (c & 0xff) > 60 && pt.cv.at(5, 5) == 0 && pt.cv.at(50, 95) == 0);
        CHECK(((pt.cv.at(14, 88) >> 16) & 0xff) > 200);       // near the red corner
    }
    {
        Body b; // flood fill inside a red outlined box until the border colour
        b.add(Pen(1, 0, 0, C_RED)).add(Sel(1)).add(Brush(2, 0, C_GREEN)).add(Sel(2));
        b.add(Move(20, 20)).add(Line(60, 20)).add(Line(60, 60)).add(Line(20, 60)).add(Line(20, 20));
        b.rec(53, {40, 40, C_RED, 0});                 // FLOODFILLBORDER
        Played p;
        Play(p, b);
        CHECK(p.cv.at(40, 40) == GREEN && p.cv.at(25, 25) == GREEN && p.cv.at(20, 40) == RED && p.cv.at(70, 70) == 0);
        Body s; // FLOODFILLSURFACE: replaces the area of one colour
        s.add(Brush(1, 0, C_BLUE)).add(Sel(1)).add(StockSel(8)).add(RectRec(10, 10, 50, 50)).add(Brush(2, 0, C_GREEN)).add(Sel(2)).rec(53, {30, 30, C_BLUE, 1});
        Played q;
        Play(q, s);
        CHECK(q.cv.at(30, 30) == GREEN && q.cv.at(48, 48) == GREEN && q.cv.at(60, 60) == 0);
    }
    {
        // CREATEDIBPATTERNBRUSHPT: 8x8 tile, top-left 4x4 blue, rest red; aligned to the surface grid
        std::vector<uint32_t> tile(64, 0xFFFF0000u);
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) tile[size_t(y) * 8 + size_t(x)] = 0xFF0000FFu;
        Bytes bmi, bits;
        Dib32(8, 8, tile, bmi, bits);
        Bytes p = I({1, 0});
        u32 offBmi = 32, offBits = 32 + u32(bmi.size());
        P32(p, offBmi); P32(p, u32(bmi.size())); P32(p, offBits); P32(p, u32(bits.size()));
        Cat(p, bmi); Cat(p, bits);
        Body b;
        b.add(Rec(94, p)).add(Sel(1)).add(StockSel(8)).add(RectRec(0, 0, 40, 40));
        Played pl;
        Play(pl, b);
        CHECK(pl.cv.at(1, 1) == BLUE && pl.cv.at(5, 1) == RED && pl.cv.at(1, 5) == RED && pl.cv.at(9, 9) == BLUE && pl.cv.at(13, 13) == RED);
        // CREATEMONOBRUSH: bit 0 = text colour, bit 1 = background colour
        std::vector<int> mono(64, 1);
        for (int x = 0; x < 8; ++x) mono[size_t(x)] = 0;   // first row 0
        Bytes mb, mbits;
        Dib1(8, 8, mono, mb, mbits);
        Bytes m = I({2, 0});
        P32(m, 32); P32(m, u32(mb.size())); P32(m, 32 + u32(mb.size())); P32(m, u32(mbits.size()));
        Cat(m, mb); Cat(m, mbits);
        Body c;
        c.rec(24, {C_RED}).rec(25, {C_GREEN}).add(Rec(93, m)).add(Sel(2)).add(StockSel(8)).add(RectRec(0, 0, 40, 40));
        Played pm;
        Play(pm, c);
        CHECK(pm.cv.at(3, 0) == RED && pm.cv.at(3, 1) == GREEN && pm.cv.at(3, 8) == RED);
    }
}

} // namespace

int RunEmfTests() {
    std::printf("[emf records]\n");
    TestSkippedAndNoops();
    TestRop2();
    TestDashAndHatch();
    TestWinding();
    TestRegionsAndClip();
    TestPaths();
    TestMapModes();
    TestText();
    TestBlits();
    TestGradientFlood();
    std::printf("emf records: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail;
}
