// Package 4 tests: aaron widgets, buttons, dialogs, sprites, timer. Headless (no window).
// They run against whatever Scene / moses implementation is linked; with the temporary stubs
// (engine/stubs/Pkg4_Stubs.cpp) the Scene is a minimal fake that records posted messages.
#include "host/HostConfig.h"
#include "aaron/Scene.h"
#include "aaron/Sprites.h"
#include "tests/EngineTests.h"

#include <cstdio>
#include <vector>

using namespace aaron;

namespace {

int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int g_alive = 0;
struct Probe : Node {
    explicit Probe(WidgetParent p) : Node(p, 0, 0) { ++g_alive; }
    ~Probe() override { --g_alive; }
};

struct Posted { u32 msg; uintptr_t wp; intptr_t lp; };

struct TScene : Scene {
    std::vector<u32> cmds;
    std::vector<Posted> log; // messages seen by OnMessage (filled by Drain)
    int OnMessage(u32 msg, uintptr_t wp, intptr_t lp) override { log.push_back({msg, wp, lp}); return 1; }
    // Dispatches the deferred queue (Scene::PostMessage -> MsgQueue) and returns what arrived.
    std::vector<Posted> Drain() { log.clear(); Messages().Dispatch(this); return log; }
    TScene() : Scene(nullptr, "pkg4") {
        Init(nullptr, nullptr, 640, 480, 16);      // CDrawView::Init, then the real Scene::Initialize (creates the root widgets)
        Initialize();
        if (rapture::Config().mainArchive[0] && OpenArchive(0, rapture::Config().mainArchive, ".")) Discover();
    }
    // Real resource ids discovered in the game main archive (HostConfig::mainArchive) (0 when the archive is not in the current folder -> resource tests skipped).
    u32 group = kNoRes, dib[3] = {kNoRes, kNoRes, kNoRes};
    bool HasRes() const { return group != kNoRes; }
    void Discover() {
        int nd = 0;
        for (u32 i = 0; i < 0x400 && (group == kNoRes || nd < 3); ++i) {
            u32 g = MakeResId(0, 0x8b, i);
            if (group == kNoRes && GetIndexedResId(g, 2) != kNoRes && GetIndexedResId(g, 3) == kNoRes) group = g;
            u32 d = MakeResId(0, 0x82, i);
            if (nd < 3) {
                if (void* o = LoadResource(d)) { FreeResource(d); (void)o; dib[nd++] = d; }
            }
        }
        if (nd < 3) group = kNoRes;
    }
    int OnCommand(u32 id, uintptr_t) override { cmds.push_back(id); return 1; }
};

struct FailWidget : DibSprite {
    using DibSprite::DibSprite;
    bool Create() override { return false; }
};

struct TDialog : Dialog {
    int closed = 0, inits = 0, lastResult = -1;
    using Dialog::Dialog;
    int OnInit() override { ++inits; return 1; }
    void OnClose(int r) override { ++closed; lastResult = r; }
};


void TestTree() {
    std::printf("[tree]\n");
    TScene sc;
    Widget* root = sc.RootWidget();
    auto* a = new Probe(root);
    auto* b = new Probe(a);
    auto* c = new Probe(a);
    CHECK(g_alive == 3);
    CHECK(root->FirstChild() == a && a->FirstChild() == b && a->LastChild() == c);
    CHECK(b->NextSibling() == c && c->PrevSibling() == b && b->Parent() == a);
    CHECK(b->GetScene() == &sc);
    delete b; // a deleted child unlinks itself
    CHECK(a->FirstChild() == c && g_alive == 2);
    a->DeleteChildren();
    CHECK(g_alive == 1 && a->FirstChild() == nullptr);
    new Probe(a);
    new Probe(root);
    CHECK(g_alive == 3);
    delete a; // deleting a parent deletes its children (0x10002944)
    CHECK(g_alive == 1);
    root->DeleteChildren();
    CHECK(g_alive == 0);

    // EnableCount / HideCount counters (recursive, transition based)
    auto* p = new Probe(root);
    auto* q = new Probe(p);
    p->EnableCount(0);
    p->EnableCount(0);
    CHECK(p->DisableCount() == 2 && q->DisableCount() == 2 && (p->GetStyle() & 2));
    p->EnableCount(1);
    CHECK(p->DisableCount() == 1 && (p->GetStyle() & 2));
    p->EnableCount(1);
    CHECK(p->DisableCount() == 0 && !(p->GetStyle() & 2) && q->DisableCount() == 0);
    p->EnableCount(1); // extra enable is ignored
    CHECK(p->DisableCount() == 0);
    p->HideCount(1);
    CHECK(p->IsHidden() && q->IsHidden() && (p->GetStyle() & 4));
    p->HideCount(0);
    CHECK(!p->IsHidden() && !(p->GetStyle() & 4));
    p->SetStyle(2 | 4); // SetStyle drives the counters from the changed bits
    CHECK(p->DisableCount() == 1 && p->IsHidden());
    p->SetStyle(0);
    CHECK(p->DisableCount() == 0 && !p->IsHidden());
    root->DeleteChildren();
}

void TestInitOrDelete() {
    std::printf("[InitOrDelete]\n");
    TScene sc;
    DibSprite* ok = Widget::Attach(new DibSprite(&sc, kNoRes, 0, 0, 0x26fc, 0, 0));
    CHECK(ok != nullptr && ok->Parent() == sc.RootWidget());
    CHECK(Widget::GetFromObj(static_cast<moses::CDrawDib*>(ok)) == ok);
    FailWidget* bad = Widget::Attach(new FailWidget(&sc, kNoRes, 0, 0, 0x26fc, 0, 0));
    CHECK(bad == nullptr);
    CHECK(sc.RootWidget()->FirstChild() == ok && sc.RootWidget()->LastChild() == ok); // failed widget deleted and unlinked
    CHECK(Widget::InitOrDelete(nullptr) == nullptr);
    sc.RootWidget()->DeleteChildren();
}

void TestDibSprite() {
    std::printf("[DibSprite]\n");
    TScene sc;
    if (!sc.HasRes()) { std::printf("  SKIP (the main archive is not in the current folder)\n"); return; }
    auto* s = new DibSprite(&sc, sc.dib[0], 5, 7, 0x26fc, 3, 0x10);
    CHECK(s->GetCmdId() == 3 && s->GetLevel() == 0x26fc);
    CHECK(s->GetPosition()->x == 5 && s->GetPosition()->y == 7);
    CHECK(s->GetResId() == sc.dib[0]);
    CHECK(s->GetPickMode() == 0); // style 0x10 -> pixel pick
    auto* t = new DibSprite(&sc, sc.dib[1], 0, 0, 1, 0, 0);
    CHECK(t->GetPickMode() == 1); // no pick bits -> never pickable
    auto* u = new DibSprite(&sc, sc.dib[1], 0, 0, 1, 0, 0x60);
    CHECK(u->GetPickMode() == 6);
    CHECK(Widget::InitOrDelete(s) == s);
    CHECK(s->IsVisible());          // Create -> Show
    auto* h = new DibSprite(&sc, sc.dib[2], 0, 0, 1, 0, 1);
    CHECK(Widget::InitOrDelete(h) == h);
    CHECK(!h->IsVisible());         // style 1 = created hidden
    h->Show(true);
    CHECK(h->IsVisible());
    h->Show(false);
    CHECK(!h->IsVisible());
    sc.RootWidget()->DeleteChildren();
}

void TestButton() {
    std::printf("[Button]\n");
    TScene sc;
    if (!sc.HasRes()) { std::printf("  SKIP (the main archive is not in the current folder)\n"); return; }
    sc.Drain();
    auto* b = new Button(&sc, sc.group, 10, 20, 0x26fc, 0x1234, 8, 0);
    // frames: 0..2 resolved by GetIndexedResId, 3..7 copied through the fallback table {0,0,0,0,1,3,1,4}
    u32 g0 = b->Frame(0), g1 = b->Frame(1), g2 = b->Frame(2);
    CHECK(g0 != g1 && g1 != g2);
    CHECK(b->Frame(3) == g0 && b->Frame(4) == g1 && b->Frame(5) == g0 && b->Frame(6) == g1 && b->Frame(7) == g1);
    CHECK((b->GetStyle() & 0x18) == 0x18 || (b->GetStyle() & 0x10)); // style gets |0x10
    CHECK(b->GetPickFlags() == (4 | 0x40));
    CHECK(Widget::InitOrDelete(b) == b);
    CHECK(b->StateIndex() == 0);

    // simulated click: press inside, release inside -> deferred AMSG_NOTIFY(cmd, button)
    b->OnMouse(WMSG_LBUTTONDOWN, 0, 0);
    CHECK(b->StateIndex() == 1);
    CHECK(sc.Cursor().Captured() == b);
    b->OnMouse(WMSG_LBUTTONUP, 0, 0);
    CHECK(sc.Cursor().Captured() == nullptr);
    CHECK(b->StateIndex() == 0);
    auto log = sc.Drain();
    CHECK(log.size() == 1 && log[0].msg == AMSG_NOTIFY && log[0].wp == 0x1234 && log[0].lp == reinterpret_cast<intptr_t>(b));
    // the scene dispatch of 0xc06 calls widget->OnNotify(id, widget): default routes to scene->OnCommand
    b->OnNotify(0x1234, b);
    CHECK(sc.cmds.size() == 1 && sc.cmds[0] == 0x1234);

    // HideCount's slot 7 (NopVirtual2) is the Button's Draw (Aaron 0x100032cb): a hide/show transition redraws the button
    b->moses::CDrawObj::Hide();
    CHECK(!b->IsVisible());
    b->HideCount(1);
    CHECK(b->IsVisible());
    b->HideCount(0);

    // disabled button: no capture, state index 2
    b->EnableCount(0);
    CHECK(b->StateIndex() == 2);
    b->EnableCount(1);

    // checkbox (mode 3)
    auto* cb = Widget::Attach(new Button(&sc, sc.group, 0, 0, 1, 7, 8, 3));
    CHECK(cb->GetPickFlags() == 0x40 && !cb->IsToggled());
    cb->PostNotify(1);
    CHECK(cb->IsToggled() && cb->StateIndex() == 3);
    cb->PostNotify(1);
    CHECK(!cb->IsToggled());
    cb->SetToggled(true);
    CHECK(cb->IsToggled());
    cb->SetToggled(true);
    CHECK(cb->IsToggled());

    // radio group (mode 9): first button starts a group (style 0x200)
    auto* r0 = Widget::Attach(new Button(&sc, sc.group, 0, 0, 1, 20, 8 | 0x200, 9));
    auto* r1 = Widget::Attach(new Button(&sc, sc.group, 0, 0, 1, 21, 8, 9));
    auto* r2 = Widget::Attach(new Button(&sc, sc.group, 0, 0, 1, 22, 8, 9));
    CHECK(r0->GetPickFlags() == (1 | 0x40));
    r1->PostNotify(1);
    CHECK(!r0->IsToggled() && r1->IsToggled() && !r2->IsToggled());
    r2->PostNotify(1);
    CHECK(!r0->IsToggled() && !r1->IsToggled() && r2->IsToggled());
    r0->PostNotify(1);
    CHECK(r0->IsToggled() && !r1->IsToggled() && !r2->IsToggled());
    CHECK(r0->TabSibling(1) == r1);
    CHECK(r2->TabSibling(1) == sc.RootWidget()->FirstChild()); // no group start ahead: wraps to the first sibling
    Widget::Attach(new Button(&sc, sc.group, 0, 0, 1, 23, 8 | 0x200, 9)); // next group starts here
    CHECK(r2->TabSibling(1) == r0);                              // ring of a group wraps to its 0x200 start
    sc.RootWidget()->DeleteChildren();
}

void TestDialog() {
    std::printf("[Dialog]\n");
    TScene sc;
    if (!sc.HasRes()) { std::printf("  SKIP (the main archive is not in the current folder)\n"); return; }
    sc.Drain();
    auto* under = Widget::Attach(new Button(&sc, sc.group, 0, 0, 1, 1, 8, 0));
    sc.Accelerators().Add(AVK_ESCAPE, 0x77, 0, 0);
    auto* dlg = new TDialog(&sc, sc.dib[0], 0, 0, 0x26fc, 99, 0x20000, 42, 3);
    CHECK(dlg->GetPickFlags() == 0x80 && dlg->GetClassId() == CID_DIALOG);
    auto* ok = new Button(dlg, sc.group, 0, 0, 0x26fd, 5, 0, 1); // mode 1 -> default button
    CHECK(ok->GetStyle() & 8);  // child of a dialog notifies its parent
    CHECK(Widget::InitOrDelete(dlg) == dlg);
    CHECK(Widget::InitOrDelete(ok) == ok);
    CHECK(dlg->inits == 1 && dlg->UserArg() == 42);
    CHECK(under->DisableCount() == 1);      // the scene below is disabled while the dialog is open
    CHECK(dlg->DisableCount() == 0);
    CHECK(sc.Focus().GetFocus() == dlg);
    CHECK(!sc.Accelerators().Translate(&sc, AVK_ESCAPE, 0)); // scene accelerators are masked while a dialog is open (FUN_1000de5a)

    // Enter activates the default button -> deferred notify
    CHECK(dlg->OnKey(WMSG_KEYDOWN, AVK_RETURN, 0) == 1);
    auto log = sc.Drain();
    CHECK(log.size() == 1 && log[0].wp == 5);
    CHECK(dlg->OnKey(WMSG_KEYDOWN, 'A', 0) == 0);
    CHECK(dlg->OnKey(WMSG_KEYUP, AVK_ESCAPE, 0) == 0);

    // Esc -> Close(0, 0): OnClose(0), dialog deleted, scene restored
    for (int py = -40; py <= 40 && !sc.Cursor().Hovered(); py += 4)
        for (int px = -60; px <= 60 && !sc.Cursor().Hovered(); px += 4) sc.HandleMouse(WMSG_MOUSEMOVE, px, py, 0);
    CHECK(sc.Cursor().Hovered() != nullptr); // pointer over the dialog's widgets
    sc.Cursor().Capture(ok); // regression: manager references to a widget deleted with its dialog must be dropped
    CHECK(dlg->OnKey(WMSG_KEYDOWN, AVK_ESCAPE, 0) == 1);
    CHECK(sc.Cursor().Captured() == nullptr && (sc.Cursor().Hovered() == nullptr || sc.Cursor().Hovered() == under));   // the scene below may legitimately be hovered (depends on the pictures)
    CHECK(sc.Focus().GetFocus() == nullptr);   // focus restored on close (checked before the click: a click may legitimately focus `under`, which depends on the game's pictures)
    sc.HandleMouse(WMSG_LBUTTONDOWN, 0, 0, 1); // first click after the dialog closed (used to crash on a stale hover_)
    sc.HandleMouse(WMSG_LBUTTONUP, 0, 0, 0);
    CHECK(under->DisableCount() == 0);
    CHECK(sc.Accelerators().Translate(&sc, AVK_ESCAPE, 0)); // ... and restored on close
    CHECK(sc.RootWidget()->FirstChild() == under && sc.RootWidget()->LastChild() == under);

    // nested dialogs: Close(1, 1) closes the whole stack
    auto* d1 = Widget::Attach(new TDialog(&sc, sc.dib[0], 0, 0, 0x26fc, 1, 0x20000, 0, 3));
    auto* d2 = Widget::Attach(new TDialog(d1, sc.dib[1], 0, 0, 0x26fc, 2, 0x20000, 0, 3));
    CHECK(d1 && d2 && d2->FindDialogAncestor(false) == d1);
    d2->Close(1, 1);
    CHECK(sc.RootWidget()->FirstChild() == under && sc.RootWidget()->LastChild() == under);
    CHECK(under->DisableCount() == 0);
    sc.RootWidget()->DeleteChildren();
}

void TestAnimSprite() {
    std::printf("[AnimSprite / Rect / Text]\n");
    TScene sc;
    if (!sc.HasRes()) { std::printf("  SKIP (the main archive is not in the current folder)\n"); return; }
    auto* a = new AnimSprite(&sc, sc.dib[0], 0, 0, 5, 77, 1);
    CHECK(a->GetCmdId() == 77 && a->GetAnimObj() == static_cast<moses::CAnimObj*>(a));
    CHECK(Widget::InitOrDelete(a) == a);
    CHECK(!a->IsVisible()); // style 1 = hidden
    moses::QUEUEMSG* none[1] = {nullptr};
    (void)none;
    a->Play(sc.dib[1], nullptr, 0, 1); // different resource -> LoadRes path, then QStart
    CHECK(a->WidgetResId() == sc.dib[1]);
    CHECK(a->GetResId() == sc.dib[1]);
    a->Show(false);
    CHECK(!a->InQueue());

    RECT rc{-10, -10, 10, 10};
    auto* r = Widget::Attach(new RectSprite(&sc, 0, 0, 0x2706, 0, 0, &rc, 0));
    CHECK(r != nullptr && r->IsVisible());
    auto* ar = Widget::Attach(new AnimRectSprite(&sc, 0, 0, 10000, 0, 0x2000, &rc, 0));
    CHECK(ar != nullptr);
    auto* at = Widget::Attach(new AnimTextSprite(&sc, 0, 0, 0x7fffffff, 0, 1, nullptr, nullptr, 0, 0));
    CHECK(at != nullptr);
    sc.RootWidget()->DeleteChildren();
}

void TestTimer() {
    std::printf("[Timer]\n");
    TScene sc;
    // Real TimerMgr: the reference time is taken at the first Tick with a listener, and the total is reset to 0 when the
    // last listener leaves. So every Start() is followed by a baseline Tick(T0), then Tick(T0 + elapsed).
    auto* t = new Timer(&sc, 0x1402, 0, 0, 0);
    CHECK(!t->Running());                      // period 0 -> not started
    t->Start(100, 0xffffffffu);
    CHECK(t->Running() && sc.Timers().ListenerCount() >= 1);
    sc.Timers().Tick(1000);                    // baseline
    sc.Timers().Tick(1050);
    CHECK(sc.Drain().empty() && t->Count() == 0);
    sc.Timers().Tick(1100);
    auto log = sc.Drain();
    CHECK(t->Count() == 1 && log.size() == 1);
    CHECK(log.size() == 1 && log[0].msg == AMSG_NOTIFY && log[0].wp == 0x1402);
    sc.Timers().Tick(1150);
    CHECK(t->Count() == 1);                    // next deadline = 200 (drifting: 100 + 100)
    sc.Timers().Tick(1210);
    CHECK(t->Count() == 2);
    t->Stop();
    CHECK(!t->Running() && sc.Timers().ListenerCount() == 0);
    sc.Timers().Tick(2000);
    CHECK(t->Count() == 2);

    // restarting a running timer keeps ONE listener entry (the destructor removes only one)
    t->Start(100, 0xffffffffu);
    size_t lc = sc.Timers().ListenerCount();
    t->Start(100, 0xffffffffu);
    CHECK(sc.Timers().ListenerCount() == lc);
    t->Stop();
    CHECK(sc.Timers().ListenerCount() == 0);
    // fixed repetitions: 2 ticks then stop
    sc.Drain();
    t->Start(10, 2);
    sc.Timers().Tick(5000);                    // baseline
    sc.Timers().Tick(5010);
    sc.Timers().Tick(5020);
    sc.Timers().Tick(5040);
    CHECK(t->Count() == 2 && !t->Running() && sc.Drain().size() == 2);

    // disabled timers count but do not notify
    t->Start(10, 0xffffffffu);
    t->EnableCount(0);
    sc.Timers().Tick(6000);                    // baseline
    sc.Timers().Tick(6010);
    CHECK(t->Count() == 1 && sc.Drain().empty());
    sc.RootWidget()->DeleteChildren();
    CHECK(sc.Timers().ListenerCount() == 0);   // ~Timer unsubscribed

    // hiding a Timer (Aaron ord 515, vtable slot 7) freezes the time left; showing it resumes from there
    {
        sc.Timers().Tick(6500);                // no listener: the manager forgets its reference time
        auto* h = new Timer(&sc, 0x1403, 0, 0, 0);
        sc.Drain();
        h->Start(100, 0xffffffffu);
        sc.Timers().Tick(7000);                // baseline
        sc.Timers().Tick(7040);                // 40 ms elapsed, 60 left
        h->HideCount(1);
        CHECK(h->IsHidden() && h->Remaining() == 60);
        sc.Timers().Tick(7500);                // time passes while hidden: nothing happens
        CHECK(h->Count() == 0 && sc.Drain().empty() && h->Remaining() == 60);
        h->HideCount(0);
        CHECK(!h->IsHidden() && h->Remaining() == 60);
        sc.Timers().Tick(7550);
        CHECK(h->Count() == 0);
        sc.Timers().Tick(7560);                // 60 ms after it was shown
        CHECK(h->Count() == 1 && sc.Drain().size() == 1);
        sc.RootWidget()->DeleteChildren();
    }
}

void TestWp2Ordinals() {
    std::printf("[WP-2 ordinals]\n");
    TScene sc;
    // ord 138 Scene::AddResource: first free entry, id = idx | 0xff800000
    int a = 1, b = 2;
    u32 ia = sc.AddResource(&a), ib = sc.AddResource(&b);
    CHECK(ia == 0xff800000u && ib == 0xff800001u);
    CHECK(sc.DynamicResource(ia) == &a && sc.DynamicResource(ib) == &b && sc.DynamicResource(0x00000000u) == nullptr);
    // dynamic resources are reference counted: AddResource gives the creator's reference; every Load adds one, every Free removes one,
    // the last Free runs the deleter (the original's ResTable::Release for the slot 0xff)
    {
        static int deleted = 0;
        deleted = 0;
        int* obj = new int(5);
        u32 id = sc.AddResource(obj, [](void* p) { ++deleted; delete static_cast<int*>(p); });
        CHECK(sc.LoadResource(id) == obj && sc.LoadResource(id) == obj);     // 3 references
        sc.FreeResource(id);
        sc.FreeResource(id);
        CHECK(deleted == 0 && sc.DynamicResource(id) == obj);
        sc.FreeResource(id);                                                 // the creator's: gone
        CHECK(deleted == 1 && sc.DynamicResource(id) == nullptr && sc.LoadResource(id) == nullptr);
        u32 id2 = sc.AddResource(new int(6), [](void* p) { ++deleted; delete static_cast<int*>(p); });
        CHECK(id2 == id);                                                    // the free entry is reused
        sc.FreeResource(id2);
        CHECK(deleted == 2);
    }
    // ord 226 / 234 / 237: empty functions
    DibSprite::Nop226(); Timer::Nop234(); Widget::Nop237();
    // ord 488 Widget::LoadAndActivate: no resource -> LoadRes succeeds, Activate runs
    auto* d = new DibSprite(&sc, kNoRes, 0, 0, 0x26fc, 0, 0);
    CHECK(d->LoadAndActivate());
    // ord 77 Widget::ReleaseBase is idempotent without a loaded resource
    Widget::ReleaseBase(d);
    // ord 144 DibSprite::AnimateList: count 0 is a no-op; ids without a loaded archive must not crash
    d->AnimateList(0u);
    sc.RootWidget()->DeleteChildren();
}
} // namespace

int RunAaronWidgetTests(int* checks); // tests/AaronWidgets_Test.cpp (WP-4)

int RunPkg4Tests(int, char**) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_fail = g_checks = 0;
    TestTree();
    TestInitOrDelete();
    TestDibSprite();
    TestButton();
    TestDialog();
    TestAnimSprite();
    TestTimer();
    TestWp2Ordinals();
    g_fail += RunAaronWidgetTests(&g_checks);
    std::printf("Pkg4: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
