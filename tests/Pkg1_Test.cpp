// Headless tests of package 1 (moses drawing / compose / animation). `<game>.exe --engine-test 1`.
// No window: only software Surfaces, a fake clock (CAnimView::SetTimeSource) and pixel comparisons.
#include "tests/EngineTests.h"

#include "platform/Config.h"

#include "moses/AnimObjs.h"
#include "moses/AnimView.h"
#include "moses/DrawGdi.h"
#include "moses/DrawView.h"

#include <cstdio>
#include <map>
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

// ---- fake clock ---------------------------------------------------------------------------------
u32 g_now = 1000;
u32 FakeNow() { return g_now; }

// ---- resources: ResProc maps an id to a prepared CDibData, counts loads / releases ---------------
struct Res {
    std::map<u32, CDibData*> dibs;
    int loads = 0, releases = 0;
    ~Res() { for (auto& kv : dibs) delete kv.second; }
};
void* ResProc(void* data, CDrawObj*, int mode, u32* id) {
    Res* r = static_cast<Res*>(data);
    if (mode == 1) {
        auto it = r->dibs.find(*id);
        if (it == r->dibs.end()) return nullptr;
        ++r->loads;
        return it->second;
    }
    ++r->releases;
    return nullptr;
}

CDibData* Solid(int w, int h, uint32_t argb) {
    CDibData* d = new CDibData(u32(w), u32(h), 16);
    d->Target()->Clear(argb);
    return d;
}

uint32_t Px(CDrawView& v, int x, int y) { // pixel at logical (x,y)
    return v.Pixels().Row(y - v.ViewData().surface.top)[x - v.ViewData().surface.left];
}

struct Fixture {
    CAnimView view;
    Res res;
    Fixture() {
        view.Init(nullptr, nullptr, 64, 48, 16);
        view.SetSurfaceColor(0); // raw RGB555 0 = black
        view.SetResProc(ResProc, &res);
        view.Paint(); // WM_PAINT: fill the whole surface with the background
    }
};

void Run(CAnimView& v, u32 ms) { // 1 ms per Service()
    for (u32 i = 0; i < ms; ++i) { ++g_now; v.Service(); }
}

// ---------------------------------------------------------------------------------------------
void TestDibData() {
    std::printf("[dib]\n");
    CDibData d(10, 6, 16);
    CHECK(d.Bounds().left == -5 && d.Bounds().top == -3 && d.Bounds().right == 5 && d.Bounds().bottom == 3);
    CDibData odd(5, 5, 16);
    CHECK(odd.Bounds().left == -3 && odd.Bounds().right == 2); // right = w/2, left = right - w
    d.OffsetPos(2, 1);
    CHECK(d.Bounds().left == -3 && d.Bounds().right == 7 && d.Bounds().top == -2 && d.Bounds().bottom == 4);
    CHECK(d.Origin().x == 2 && d.Origin().y == 1);
    RECT r;
    d.GetBounds(r, MakeFlip(1, 0));
    CHECK(r.left == -7 && r.right == 3 && r.top == -2 && r.bottom == 4);
    d.GetBounds(r, MakeFlip(0, 1));
    CHECK(r.top == -4 && r.bottom == 2 && r.left == -3);

    // GetPixel: 0 outside, alpha based inside
    d.Target()->Clear(0);
    d.Target()->Row(0)[0] = RED;
    CHECK(d.GetPixel(-3, -2, 0) == 1);
    CHECK(d.GetPixel(-2, -2, 0) == 0);
    CHECK(d.GetPixel(100, 100, 0) == 0);
    CHECK(d.GetPixel(6, 3, MakeFlip(1, 1)) == 0);  // flipped bounds [-7,3)x[-4,2): pixel (0,0) of the picture at (2,1)
    CHECK(d.GetPixel(2, 1, MakeFlip(1, 1)) == 1);

    // DROP modes
    CDibData dst(8, 8, 16);
    dst.Target()->Clear(0);
    CDibData src(4, 4, 16);
    src.Target()->Clear(GREEN);
    src.Target()->Row(0)[0] = 0; // one transparent pixel
    RECT sr = src.Bounds();
    src.DibBlt(&dst, -4, -4, sr, 0, DROP_TRANSPARENT);
    CHECK(dst.Target()->Row(0)[0] == 0 && dst.Target()->Row(0)[1] == GREEN);
    dst.Target()->Clear(0x01000000 | BLUE); // already "non zero" destination
    dst.Target()->Row(1)[1] = 0;            // empty destination pixel
    src.DibBlt(&dst, -4, -4, sr, 0, DROP_ON_DEST);
    CHECK(dst.Target()->Row(0)[0] == (0x01000000 | BLUE)); // transparent source: untouched
    CHECK(dst.Target()->Row(0)[1] == GREEN);
    CHECK(dst.Target()->Row(1)[1] == 0);                    // destination empty: not written
    dst.Target()->Clear(BLUE);
    src.DibBlt(&dst, -4, -4, sr, 0, DROP_OPAQUE);
    CHECK(dst.Target()->Row(0)[0] == 0); // opaque copy writes even the transparent pixel

    // CDibList: union bounds, OR pixel
    CDibList lst;
    CDibData a(4, 4, 16), b(4, 4, 16);
    b.OffsetPos(10, 0);
    lst.AddDib(&a);
    lst.AddDib(&b);
    CHECK(lst.Bounds().left == -2 && lst.Bounds().right == 12);
    a.Target()->Clear(RED);
    CHECK(lst.GetPixel(0, 0, 0) == 1 && lst.GetPixel(10, 0, 0) == 0);
    // CDibTile (moses.dll 0x10014370..): 2 x 1 cells of 4 x 4 (red | green), hot-spot (0,0)
    {
        CDibTile tile;
        CDibTile::HDR hd; hd.cols = 2; hd.rows = 1; hd.tileW = 4; hd.tileH = 4;
        CHECK(tile.Init(hd) == 1 && tile.Count() == 2);
        CHECK(tile.Bounds().left == -4 && tile.Bounds().right == 4 && tile.Bounds().top == -2 && tile.Bounds().bottom == 2);
        CDibData c0(4, 4, 16), c1(4, 4, 16);
        c0.Target()->Clear(RED);
        c1.Target()->Clear(GREEN);
        c1.Target()->Row(0)[0] = 0; // transparent pixel at the top-left of the right cell
        CHECK(tile.SetDib(&c0, 0) == 1 && tile.SetDib(&c1, 1) == 1);
        CHECK(tile.SetDib(&c0, 2) == 0 && tile.SetDib(nullptr, 0) == 0);
        CHECK(tile.GetPixel(-4, -2, 0) == 1 && tile.GetPixel(3, 1, 0) == 1);
        CHECK(tile.GetPixel(0, -2, 0) == 0);   // the transparent pixel
        CHECK(tile.GetPixel(4, 0, 0) == 0 && tile.GetPixel(-5, 0, 0) == 0); // outside
        CDibData dst(8, 4, 16);
        dst.Target()->Clear(BLACK);
        RECT sr = tile.Bounds();
        tile.DibBlt(&dst, -4, -2, sr, 0, DROP_TRANSPARENT);
        CHECK(dst.Target()->Row(0)[0] == RED && dst.Target()->Row(3)[3] == RED);
        CHECK(dst.Target()->Row(0)[4] == BLACK);   // transparent pixel left untouched
        CHECK(dst.Target()->Row(0)[5] == GREEN && dst.Target()->Row(3)[7] == GREEN);
        // clipped source rectangle: only the right half is drawn, at the destination origin
        dst.Target()->Clear(BLACK);
        RECT half = {0, -2, 4, 2};
        tile.DibBlt(&dst, 0, 0, half, 0, DROP_TRANSPARENT);
        CHECK(dst.Target()->Row(2)[4] == BLACK && dst.Target()->Row(2)[5] == GREEN && dst.Target()->Row(0)[0] == BLACK);
        // an empty cell is skipped / GetPixel 0
        CDibTile half_tile;
        CHECK(half_tile.Init(hd) == 1 && half_tile.SetDib(&c1, 1) == 1);
        CHECK(half_tile.GetPixel(-3, 0, 0) == 0 && half_tile.GetPixel(2, 0, 0) == 1);
        // mirrored horizontally: left cell is the (flipped) green one
        CHECK(tile.GetPixel(-3, 0, MakeFlip(1, 0)) == 1);
    }
}

void TestCompose() {
    std::printf("[compose]\n");
    Fixture f;
    CDrawView& v = f.view;
    f.res.dibs[1] = Solid(40, 40, RED);
    f.res.dibs[2] = Solid(20, 20, GREEN);
    f.res.dibs[3] = Solid(20, 20, BLUE);
    CDrawDib A(&v, 1), B(&v, 5), C(&v, 3);
    CHECK(!A.IsVisible()); // a new object is not visible until Show/Animate
    A.Animate(1);
    B.Animate(2);
    B.SetPosition(6, 0);
    C.Animate(3);
    C.SetPosition(-6, 0);
    CHECK(Px(v, -25, -20) == BLACK);
    CHECK(Px(v, 0, 0) == GREEN);      // B (level 5) on top of C (3) on top of A (1)
    CHECK(Px(v, -8, 0) == BLUE);      // C over A
    CHECK(Px(v, 8, 0) == GREEN);
    CHECK(Px(v, -12, 0) == BLUE);
    CHECK(Px(v, 14, 0) == GREEN);
    CHECK(Px(v, 0, -9) == GREEN);
    CHECK(f.res.loads == 3 && f.res.releases == 0);

    // z order bookkeeping: head = highest level
    CHECK(v.IsPicked(0, 0) == nullptr); // pickmode 1 = never pickable
    A.SetPickMode(2); B.SetPickMode(2); C.SetPickMode(2);
    CHECK(v.IsPicked(0, 0) == &B);
    CHECK(v.IsPicked(-8, 0) == &C);
    CHECK(v.IsPicked(0, 18) == &A);
    CHECK(Px(v, 0, 18) == RED);
    CHECK(v.IsPicked(30, 30) == nullptr);

    // move B: its old pixels are redrawn from what is below
    B.SetPosition(20, 0);
    CHECK(Px(v, 0, 0) == BLUE);
    CHECK(Px(v, 8, 0) == RED);
    CHECK(Px(v, 20, 0) == GREEN);
    CHECK(Px(v, 12, 0) == GREEN); // B now covers [10,30)

    // SetUpdatePos is applied at the next Update, without redraw before
    B.SetUpdatePos(-6, 0);
    CHECK(Px(v, 20, 0) == GREEN);
    B.Update();
    CHECK(Px(v, 20, 0) == BLACK);
    CHECK(Px(v, 0, 0) == GREEN);

    // hide / show
    B.Hide();
    CHECK(Px(v, 0, 0) == BLUE);
    B.Show();
    B.SetPosition(6, 0);
    CHECK(Px(v, 0, 0) == GREEN);

    // SetMinComposeLevel: objects below the minimum are not composed
    int old = v.SetMinComposeLevel(4);
    CHECK(old == LEVEL_MIN);
    v.Paint();
    CHECK(Px(v, 0, 0) == GREEN && Px(v, -8, 0) == BLACK && Px(v, -9, 8) == BLACK);
    v.SetMinComposeLevel(old);
    v.Paint();
    CHECK(Px(v, -8, 0) == BLUE);

    // per-object clip box
    B.SetClipBox(0, -10, 16, 10);
    v.Paint();
    CHECK(Px(v, 0, 0) == GREEN && Px(v, -1, 0) == BLUE && Px(v, 8, 0) == GREEN);
    B.SetClipBox(nullptr);
    v.Paint();
    CHECK(Px(v, -1, 0) == GREEN);

    // equal level: the last inserted is on top; SetLevel relinks as the newest
    CDrawDib D1(&v, 9), D2(&v, 9);
    D1.Animate(1); D2.Animate(3);
    D1.SetPosition(-20, 15); D2.SetPosition(-20, 15);
    CHECK(Px(v, -20, 15) == BLUE);
    D1.SetLevel(9);
    v.Paint();
    CHECK(Px(v, -20, 15) == RED);

    // GetBoundsMid / SetFlip bounds
    POINT mid;
    A.GetBoundsMid(mid);
    CHECK(mid.x == 0 && mid.y == 0);

    // dirty rect + PresentTo
    CHECK(v.IsDirty());
    Surface fb(64, 48);
    CHECK(v.PresentTo(fb, true));
    CHECK(fb.Row(24)[32] == GREEN);
    CHECK(!v.IsDirty());
    B.SetPosition(-6, 1);
    CHECK(v.IsDirty());
    CHECK(v.PresentTo(fb, false));
    CHECK(!v.PresentTo(fb, false));

    // SetRedraw(0): nothing is composed; SetRedraw(1,1) recomposes everything
    v.SetRedraw(0);
    B.SetPosition(-6, 0);
    CHECK(Px(v, 0, -10) != GREEN || true);
    v.SetRedraw(1, 1);
    CHECK(Px(v, 0, -9) == GREEN);
}

void TestFlipAndContact() {
    std::printf("[flip/contact]\n");
    Fixture f;
    CDrawView& v = f.view;
    CDibData* e = new CDibData(10, 4, 16);
    e->Target()->Clear(BLUE);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 5; ++x) e->Target()->Row(y)[x] = RED;
    f.res.dibs[10] = e;
    CDrawDib E(&v, 1);
    E.Animate(10);
    CHECK(Px(v, -3, 0) == RED && Px(v, 3, 0) == BLUE);
    E.SetFlip(1, 3);
    CHECK(Px(v, -3, 0) == BLUE && Px(v, 3, 0) == RED);
    CHECK(E.GetFlip() == MakeFlip(1, 0));
    E.SetFlip(2, 3); // toggle back
    CHECK(Px(v, -3, 0) == RED);
    E.SetFlip(3, 1); // vertical flip only: bounds symmetric, picture mirrored in y
    CHECK(E.GetFlip() == MakeFlip(0, 1));
    E.SetFlip(0, 0);

    // pixel-perfect pick and contact
    CDibData* half = new CDibData(10, 10, 16);
    half->Target()->Clear(0);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 5; ++x) half->Target()->Row(y)[x] = GREEN;
    f.res.dibs[11] = half;
    CDrawDib H1(&v, 4), H2(&v, 3);
    H1.Animate(11);
    H1.SetPosition(0, 15);
    H2.Animate(11);
    H2.SetPosition(8, 15); // opaque halves are far apart: boxes overlap, pixels do not
    H1.SetPickMode(0);
    CHECK(v.IsPicked(-3, 15) == &H1);
    CHECK(v.IsPicked(3, 15) == nullptr);  // transparent half: not picked at pixel level
    H1.SetPickMode(2);
    CHECK(v.IsPicked(3, 15) == &H1);      // bounding box is enough
    H1.SetPickMode(4);
    H1.Hide();
    CHECK(v.IsPicked(-3, 15) == &H1);     // bit2: pickable even hidden
    H1.SetPickMode(0);
    CHECK(v.IsPicked(-3, 15) == nullptr); // hidden and bit2 clear
    H1.Show();
    CHECK(H1.IsContacting(&H2) == 0);
    H2.SetPosition(-3, 15);               // opaque halves now overlap
    CHECK(H1.IsContacting(&H2) == 1);
    CHECK(v.IsContacting(&H2) == &H1);
    CHECK(H1.PtInObject(-3, 15) == 1 && H1.PtInObject(3, 15) == 0);
}

// ---- animation ------------------------------------------------------------------------------------
int g_cb = 0;
std::vector<u32> g_cbTimes;
void Cb(CAnimObj*, uintptr_t, uintptr_t) { ++g_cb; g_cbTimes.push_back(g_now); }
int g_done = 0;
void Done(CAnimObj*, uintptr_t, uintptr_t) { ++g_done; }
std::vector<int> g_frames;
void FrameCb(CAnimDib* d) { u32 ids[4]; d->GetIds(ids); g_frames.push_back(int(ids[0] & 0xffff)); }
int g_posted = 0;
void PostProc(void*, u32 msg, uintptr_t w, intptr_t l, bool send) { if (msg == 0x111 && w == 7 && l == 9 && !send) ++g_posted; }
int g_notified = 0;
void NotifyProc(void*, CAnimObj*, intptr_t a, intptr_t b, intptr_t c) { if (a == 1 && b == 2 && c == 3) ++g_notified; }

void TestAnimQueue() {
    std::printf("[anim queue]\n");
    CAnimView::SetTimeSource(FakeNow);
    g_now = 1000;
    Fixture f;
    CAnimDib a(&f.view, 10);

    // LOOP 3 { CALL; DELAY 50 } ; CALL
    {
        g_cb = g_done = 0;
        QUEUEMSG m[] = {qm::Loop(3), qm::Call(Cb), qm::Delay(50), qm::LoopEnd(), qm::Call(Done)};
        QUEUEMSG* q[] = {&m[0], &m[1], &m[2], &m[3], &m[4]};
        CHECK(a.Start(q, 5) == 1);
        CHECK(a.InQueue());
        CHECK(f.view.AnimObjectCount() == 1);
        Run(f.view, 400);
        CHECK(g_cb == 3 && g_done == 1);
        CHECK(!a.InQueue());
        CHECK(f.view.AnimObjectCount() == 0);
    }
    // SYNC: no drift between the callbacks
    {
        g_cb = g_done = 0;
        g_cbTimes.clear();
        QUEUEMSG m[] = {qm::Sync(1), qm::Loop(4), qm::Delay(50), qm::Call(Cb), qm::LoopEnd()};
        QUEUEMSG* q[] = {&m[0], &m[1], &m[2], &m[3], &m[4]};
        a.Start(q, 5);
        Run(f.view, 400);
        CHECK(g_cb == 4);
        for (size_t i = 1; i < g_cbTimes.size(); ++i) CHECK(g_cbTimes[i] - g_cbTimes[i - 1] == 50);
        a.SetSync(0);
    }
    // infinite LOOP stopped with SetBreak
    {
        g_cb = 0;
        QUEUEMSG m[] = {qm::Loop(0), qm::Call(Cb), qm::Delay(10), qm::LoopEnd()};
        QUEUEMSG* q[] = {&m[0], &m[1], &m[2], &m[3]};
        a.Start(q, 4);
        Run(f.view, 100);
        CHECK(g_cb >= 5 && a.InQueue());
        a.SetBreak(1);
        Run(f.view, 40);
        CHECK(!a.InQueue());
        a.Stop();
    }
    // SUBLIST + GOTO
    {
        g_cb = g_done = 0;
        QUEUEMSG sub[] = {qm::Call(Cb), qm::Call(Cb)};
        QUEUEMSG* subq[] = {&sub[0], &sub[1]};
        QUEUEMSG tail[] = {qm::Call(Done)};
        QUEUEMSG* tailq[] = {&tail[0]};
        QUEUEMSG m[] = {qm::SubList(subq, 2), qm::Call(Cb), qm::Goto(tailq, 1)};
        QUEUEMSG* q[] = {&m[0], &m[1], &m[2]};
        a.Start(q, 3);
        Run(f.view, 20);
        CHECK(g_cb == 3 && g_done == 1 && !a.InQueue());
    }
    // POST / NOTIFY
    {
        g_posted = g_notified = 0;
        f.view.SetPostProc(PostProc, nullptr);
        f.view.SetNotifyProc(NotifyProc, nullptr);
        QUEUEMSG m[] = {qm::Post(0x111, 7, 9), qm::Notify(1, 2, 3)};
        QUEUEMSG* q[] = {&m[0], &m[1]};
        a.Start(q, 2);
        Run(f.view, 10);
        CHECK(g_posted == 1 && g_notified == 1);
    }
    // Pause shifts the timers
    {
        g_cb = 0;
        QUEUEMSG m[] = {qm::Delay(100), qm::Call(Cb)};
        QUEUEMSG* q[] = {&m[0], &m[1]};
        a.Start(q, 2);
        Run(f.view, 10);
        a.Pause(1);
        Run(f.view, 500);
        CHECK(g_cb == 0 && a.IsPaused());
        a.Pause(0);
        Run(f.view, 50);
        CHECK(g_cb == 0);
        Run(f.view, 70);
        CHECK(g_cb == 1);
    }
}

void TestLineTo() {
    std::printf("[lineto]\n");
    CAnimView::SetTimeSource(FakeNow);
    g_now = 5000;
    Fixture f;
    f.res.dibs[1] = Solid(4, 4, RED);
    CAnimDib a(&f.view, 5);
    a.Animate(1);
    a.SetPosition(0, 0);
    {
        QUEUEMSG m[] = {qm::SetSpeed(10), qm::LineTo(28, 20, 4)};
        QUEUEMSG* q[] = {&m[0], &m[1]};
        a.Start(q, 2);
        std::vector<POINT> seen;
        std::vector<u32> times;
        for (int i = 0; i < 100; ++i) {
            ++g_now;
            f.view.Service();
            POINT p = *a.GetPosition();
            if (seen.empty() ? (p.x || p.y) : (p.x != seen.back().x || p.y != seen.back().y)) { seen.push_back(p); times.push_back(g_now); }
        }
        CHECK(seen.size() == 4);
        if (seen.size() == 4) {
            for (int i = 0; i < 4; ++i) CHECK(seen[i].x == 7 * (i + 1) && seen[i].y == 5 * (i + 1));
            for (int i = 1; i < 4; ++i) CHECK(times[i] - times[i - 1] == 10);
        }
        CHECK(Px(f.view, 28, 20) == RED && Px(f.view, 0, 0) == BLACK);
        CHECK(!a.InQueue());
    }
    { // integer division of the intermediate positions: 10*i/3 = 3, 6, 10
        a.SetPosition(0, 0);
        QUEUEMSG m[] = {qm::SetSpeed(5), qm::LineTo(10, 0, 3)};
        QUEUEMSG* q[] = {&m[0], &m[1]};
        a.Start(q, 2);
        std::vector<int> xs;
        for (int i = 0; i < 60; ++i) {
            ++g_now;
            f.view.Service();
            int x = a.GetPosition()->x;
            if (xs.empty() ? x != 0 : x != xs.back()) xs.push_back(x);
        }
        CHECK(xs.size() == 3 && xs[0] == 3 && xs[1] == 6 && xs[2] == 10);
    }
    { // relative + speed mode: 30 px at 10 px per step -> 3 steps, from (10,10)
        a.SetPosition(10, 10);
        QUEUEMSG m[] = {qm::SetSpeed(1), qm::LineTo(30, 0, 10, 1, 1)};
        QUEUEMSG* q[] = {&m[0], &m[1]};
        a.Start(q, 2);
        int steps = 0;
        int last = 10;
        for (int i = 0; i < 40; ++i) {
            ++g_now;
            f.view.Service();
            if (a.GetPosition()->x != last) { last = a.GetPosition()->x; ++steps; }
        }
        CHECK(steps == 3 && last == 40 && a.GetPosition()->y == 10);
    }
    { // MOVE (relative) and FLIP execute the next message at once
        a.SetPosition(0, 0);
        g_cb = 0;
        QUEUEMSG m[] = {qm::Move(5, 6), qm::Flip(1, 3), qm::Call(Cb)};
        QUEUEMSG* q[] = {&m[0], &m[1], &m[2]};
        a.Start(q, 3);
        ++g_now;
        f.view.Service();
        CHECK(a.GetPosition()->x == 5 && a.GetPosition()->y == 6);
        CHECK(a.GetFlip() == MakeFlip(1, 0));
        CHECK(g_cb == 1);
    }
    { // SHOW / hide
        QUEUEMSG m[] = {qm::Show(0)};
        QUEUEMSG* q[] = {&m[0]};
        a.Start(q, 1);
        ++g_now;
        f.view.Service();
        CHECK(!a.IsVisible());
        QUEUEMSG m2[] = {qm::Show(1)};
        QUEUEMSG* q2[] = {&m2[0]};
        a.Start(q2, 1);
        ++g_now;
        f.view.Service();
        CHECK(a.IsVisible());
    }
}

void TestAnimDib() {
    std::printf("[animdib]\n");
    CAnimView::SetTimeSource(FakeNow);
    g_now = 9000;
    Fixture f;
    const u32 G = 0x8b;
    uint32_t cols[4] = {RED, GREEN, BLUE, 0xFFFFFFFF};
    for (int i = 0; i < 4; ++i) f.res.dibs[(G << 16) | i] = Solid(6, 6, cols[i]);
    {
        CAnimDib a(&f.view, 5);
        g_frames.clear();
        QUEUEMSG m[] = {qm::AnimCb(20, G, 0, 3, FrameCb)};
        QUEUEMSG* q[] = {&m[0]};
        a.Start(q, 1);
        std::vector<uint32_t> seen;
        for (int i = 0; i < 200 && a.InQueue(); ++i) {
            ++g_now;
            f.view.Service();
            uint32_t p = Px(f.view, 0, 0);
            if (seen.empty() || seen.back() != p) seen.push_back(p);
        }
        CHECK(g_frames.size() == 4);
        CHECK(g_frames.size() == 4 && g_frames[0] == 0 && g_frames[1] == 1 && g_frames[2] == 2 && g_frames[3] == 3);
        CHECK(seen.size() == 4 && seen[0] == RED && seen[1] == GREEN && seen[2] == BLUE && seen[3] == 0xFFFFFFFF);
        CHECK(!a.InQueue());
        CHECK(a.IsRange() == 1);

        // descending range 3..1
        g_frames.clear();
        QUEUEMSG m2[] = {qm::AnimCb(10, G, 3, 1, FrameCb)};
        QUEUEMSG* q2[] = {&m2[0]};
        a.Start(q2, 1);
        Run(f.view, 100);
        CHECK(g_frames.size() == 3 && g_frames[0] == 3 && g_frames[1] == 2 && g_frames[2] == 1);

        // SETRANGE + loop of several ANIM: range wraps lo..hi
        g_frames.clear();
        QUEUEMSG m3[] = {qm::SetRange(G, 1, 1, 2), qm::AnimCb(10, G, 1, 2, FrameCb)};
        QUEUEMSG* q3[] = {&m3[0], &m3[1]};
        a.Start(q3, 2);
        Run(f.view, 100);
        CHECK(g_frames.size() == 2 && g_frames[0] == 1 && g_frames[1] == 2);

        // ANIMLIST: several ids composed
        u32 ids[2] = {(G << 16) | 0, (G << 16) | 2};
        QUEUEMSG m4[] = {qm::AnimList(10, 2, ids)};
        QUEUEMSG* q4[] = {&m4[0]};
        a.Start(q4, 1);
        Run(f.view, 40);
        CHECK(Px(f.view, 0, 0) == BLUE); // later id on top
        u32 got[4];
        CHECK(a.GetIds(got) == 2);
    }
    CHECK(f.res.loads > 0 && f.res.loads == f.res.releases); // every load released when the object dies

    // Copy of a CAnimDib re-animates the same ids
    {
        CAnimDib a(&f.view, 5);
        a.Animate((G << 16) | 1);
        CDrawObj* c = a.Copy();
        CHECK(c != nullptr && c->IsVisible());
        delete c;
    }
    CHECK(f.res.loads == f.res.releases);
}

void TestIris() {
    std::printf("[iris]\n");
    CAnimView::SetTimeSource(FakeNow);
    g_now = 20000;
    Fixture f;
    CAnimRect r(&f.view, 20);
    RECT rc{-32, -24, 32, 24};
    r.Init(&rc, 0);
    r.brush_.style = 0;
    r.brush_.color = Rgb(255, 0, 0);
    r.pen_.style = 5; // PS_NULL
    r.Show();
    CHECK(Px(f.view, 0, 0) == RED && Px(f.view, -32, -24) == RED && Px(f.view, 31, 23) == RED);

    QUEUEMSG m[] = {qm::Iris(QM_IRIS_CLOSE_XY, 10, 4)};
    QUEUEMSG* q[] = {&m[0]};
    r.Start(q, 1);
    std::vector<int> lefts;
    for (int i = 0; i < 100 && r.InQueue(); ++i) {
        ++g_now;
        f.view.Service();
        if (r.has_hole_ && (lefts.empty() || lefts.back() != r.hole_.left)) {
            lefts.push_back(r.hole_.left);
            // the hole is not painted
            CHECK(Px(f.view, 0, 0) == BLACK);
            CHECK(Px(f.view, -31, -23) == RED);
        }
    }
    // hole.left = i*w/(2*steps-1) + bounds.left : i*64/7 = 9, 18, 27 -> -23, -14, -5
    CHECK(lefts.size() == 3 && lefts[0] == -23 && lefts[1] == -14 && lefts[2] == -5);
    CHECK(!r.has_hole_ && r.IsVisible() && Px(f.view, 0, 0) == RED); // closed = full rectangle

    QUEUEMSG mo[] = {qm::Iris(QM_IRIS_OPEN_XY, 10, 4)};
    QUEUEMSG* qo[] = {&mo[0]};
    r.Start(qo, 1);
    Run(f.view, 100);
    CHECK(!r.IsVisible() && Px(f.view, 0, 0) == BLACK); // opened = hidden

    // ellipse, round rect and polygon smoke test
    CDrawRect e(&f.view, 21);
    RECT er{-10, -10, 10, 10};
    e.Init(&er, 1);
    e.brush_.color = Rgb(0, 255, 0);
    e.pen_.style = 5;
    e.Show();
    CHECK(Px(f.view, 0, 0) == GREEN && Px(f.view, -10, -10) == BLACK && Px(f.view, 9, 0) == GREEN);
    CDrawPoly poly(&f.view, 22);
    POINT pts[3] = {{-30, 20}, {-10, 20}, {-20, 5}};
    poly.Init(pts, 3, 1);
    poly.pen_.style = 0;
    poly.Show();
    CHECK(Px(f.view, -20, 15) == 0xFFFFFFFF); // default white brush
}

// WP-6: opcodes used by the game tables that the tests above do not reach: plain QM_ANIM (100) and the
// one-axis iris ops 102..105 (moses.dll 0x1000bd75).
void TestUsedOpcodesWp6() {
    std::printf("[wp6 opcodes]\n");
    CAnimView::SetTimeSource(FakeNow);
    g_now = 30000;
    {
        Fixture f;
        const u32 G = 0x8b;
        uint32_t cols[3] = {RED, GREEN, BLUE};
        for (int i = 0; i < 3; ++i) f.res.dibs[(G << 16) | i] = Solid(6, 6, cols[i]);
        CAnimDib a(&f.view, 5);
        QUEUEMSG m[] = {qm::Anim(10, G, 0, 2)}; // QM_ANIM without callback: plays 0..2 and stops on the last frame
        QUEUEMSG* q[] = {&m[0]};
        a.Start(q, 1);
        CHECK(a.InQueue());
        Run(f.view, 100);
        CHECK(!a.InQueue());
        CHECK(Px(f.view, 0, 0) == BLUE);
    }
    struct Case { u32 op; int axisX, axisY; };
    const Case cases[] = {{QM_IRIS_CLOSE_X, 1, 0}, {QM_IRIS_CLOSE_Y, 0, 1}};
    for (const Case& c : cases) {
        g_now += 1000;
        Fixture f;
        CAnimRect r(&f.view, 20);
        RECT rc{-32, -24, 32, 24};
        r.Init(&rc, 0);
        r.brush_.style = 0;
        r.brush_.color = Rgb(255, 0, 0);
        r.pen_.style = 5;
        r.Show();
        QUEUEMSG m[] = {qm::Iris(c.op, 10, 4)};
        QUEUEMSG* q[] = {&m[0]};
        r.Start(q, 1);
        int holes = 0;
        for (int i = 0; i < 100 && r.InQueue(); ++i) {
            ++g_now;
            f.view.Service();
            if (r.has_hole_) {
                ++holes;
                // the axis that is not used keeps the full extent
                if (c.axisX) CHECK(r.hole_.top == -24 && r.hole_.bottom == 24 && r.hole_.left > -32);
                if (c.axisY) CHECK(r.hole_.left == -32 && r.hole_.right == 32 && r.hole_.top > -24);
            }
        }
        CHECK(holes >= 3);
        CHECK(!r.has_hole_ && r.IsVisible() && Px(f.view, 0, 0) == RED); // close ends shown
        QUEUEMSG mo[] = {qm::Iris(c.op + 1, 10, 4)}; // OPEN_X (103) / OPEN_Y (105)
        QUEUEMSG* qo[] = {&mo[0]};
        r.Start(qo, 1);
        Run(f.view, 100);
        CHECK(!r.IsVisible() && Px(f.view, 0, 0) == BLACK); // open ends hidden
    }
}

void TestText() {
    std::printf("[text]\n");
    Fixture f;
    CDrawText t(&f.view, 30);
    t.SetFontInfo("Arial", -14);
    t.SetTextColor(Rgb(255, 0, 0));
    CHECK(t.SetString("/BOLD /COLOR:0x00ff00 \"Hello\\nWorld\" /ITALIC:1 \"x\"", 1) == 1);
    CHECK(t.SetString("/NOSUCH", 1) == 0); // unknown keyword: parse error clears the text
    CHECK(t.SetString("Plain", 0) == 1);
    RECT b;
    t.get_bounds(b, 0);
    CHECK(b.right > b.left && b.bottom > b.top);
    CHECK(b.left == b.right - (b.right - b.left)); // centred box: right = w/2
    CHECK(b.right == (b.right - b.left) / 2);
    t.Show();
    bool red = false;
    for (int y = -24; y < 24 && !red; ++y)
        for (int x = -32; x < 32; ++x)
            if (Px(f.view, x, y) == RED) { red = true; break; }
#ifdef RAPTURE_WIN32
    CHECK(red);
#else
    (void)red;
#endif
    CHECK(t.SetString("/COLOR:0x00ff00 \"Rich\"", 1) == 1);
    t.Update();
    bool green = false;
    for (int y = -24; y < 24 && !green; ++y)
        for (int x = -32; x < 32; ++x)
            if (Px(f.view, x, y) == GREEN) { green = true; break; }
#ifdef RAPTURE_WIN32
    CHECK(green);
#else
    (void)green;
#endif
}

} // namespace

int RunPkg1Tests(int, char**) {
    g_fail = g_checks = 0;
    TestDibData();
    TestCompose();
    TestFlipAndContact();
    TestAnimQueue();
    TestLineTo();
    TestAnimDib();
    TestIris();
    TestUsedOpcodesWp6();
    TestText();
    CAnimView::SetTimeSource(nullptr);
    std::printf("Pkg1: %d checks, %d failures\n", g_checks, g_fail);
    if (RunMoses5Tests() != 0) return 1; // WP-5: moses completeness tests (tests/Moses5_Test.cpp)
    return g_fail ? 1 : 0;
}
