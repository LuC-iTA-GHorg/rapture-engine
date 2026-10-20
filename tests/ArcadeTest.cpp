// G1 - headless tests of the Arcade framework: <game>.exe --game-test arcade
// No window, no audio.  The registry is redirected to a temporary .ini (NEVER the real settings file of the game).
#include "tests/ArcadeTests.h"

#include "aaron/RegKey.h"
#include "aaron/Scene.h"
#include "aaron/Sprites.h"
#include "arcade/ArcadeGame.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace rapture;

namespace {

int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

// ---------------------------------------------------------------------------------------------
// observer that records what a property broadcasts
struct Observer : aaron::Subscriber {
    std::vector<std::pair<int, intptr_t>> got;
    void OnAdvise(int code, intptr_t value, aaron::Broadcaster*) override { got.emplace_back(code, value); }
};

int g_propsAlive = 0;
struct CountedProp : CPropNumeric {
    CountedProp(int mn, int mx, int def, int v) : CPropNumeric(mn, mx, def, v) { ++g_propsAlive; }
    ~CountedProp() override { --g_propsAlive; }
};

void TestProperties() {
    std::printf("[properties]\n");
    CPropNumeric p(0, 10, 5, 5);
    Observer o;
    p.AddListener(&o);
    CHECK(p.Get() == 5 && p.GetMin() == 0 && p.GetMax() == 10 && p.GetDefault() == 5);
    CHECK(p.Set(7) == 7 && p.Get() == 7);
    CHECK(o.got.size() == 1 && o.got[0].first == -1 && o.got[0].second == 7);
    CHECK(p.Set(99) == 10);              // clamped to max
    CHECK(p.Set(-4) == 0);               // clamped to min
    CHECK(p.Add(3) == 3 && p.Add(100) == 10);
    CHECK(o.got.size() == 5);
    p.Set(10);
    size_t n = o.got.size();
    p.Set(10);                           // QUIRK: always notifies, even when the value is unchanged
    CHECK(o.got.size() == n + 1);
    // snapshot / reset
    p.Set(8);
    CHECK(p.Snapshot() == 8 && p.GetSnapshot() == 8);
    p.Set(2);
    CHECK(p.GetSnapshot() == 8);
    CHECK(p.Reset() == 5 && p.Get() == 5 && p.GetSnapshot() == 5); // Reset = Set(default) + Snapshot
    // Configure sets min/max/default then Set(value) (notifies)
    n = o.got.size();
    p.Configure(1, 3, 2, 50);
    CHECK(p.GetMin() == 1 && p.GetMax() == 3 && p.GetDefault() == 2 && p.Get() == 3 && o.got.size() == n + 1);
    // QUIRK: max < min (the lives of the reference game: min 0, max -1, init 11): every value above min becomes `max`
    CPropNumeric lives(0, -1, 11, 11);
    CHECK(lives.Set(11) == -1 && lives.Set(0) == 0 && lives.Set(-5) == 0 && lives.Reset() == -1);
    // score: 0..INT_MAX
    CPropScore s;
    CHECK(s.Get() == 0 && s.GetMax() == 0x7fffffff && s.Set(123456) == 123456 && s.Set(-1) == 0);
    // removing an observer
    p.RemoveListener(&o);
    n = o.got.size();
    p.Set(1);
    CHECK(o.got.size() == n);
    // an observer destroyed while subscribed unlinks itself
    {
        Observer tmp;
        p.AddListener(&tmp);
        p.Set(2);
        CHECK(tmp.got.size() == 1);
    }
    p.Set(3); // must not touch the destroyed observer
    CHECK(true);
    // a property destroyed while observed does not leave the observer with a dangling back pointer
    {
        Observer o2;
        {
            CPropNumeric q(0, 5, 0, 0);
            q.AddListener(&o2);
            q.Set(4);
        }
        CHECK(o2.got.size() == 1);
    }

    // ---- CPropertyList ------------------------------------------------------------------------------
    {
        CPropertyList l(4);
        l.Add(new CountedProp(0, 100, 1, 1));
        l.Add(new CountedProp(0, 100, 2, 2));
        l.Add(new CountedProp(0, 100, 3, 3));
        CHECK(g_propsAlive == 3 && l.Size() == 3);
        CHECK(l.Get(1) == 2 && l.GetDefault(2) == 3 && l.GetMax(0) == 100 && l.GetMin(0) == 0);
        CHECK(l.Get(7) == 0 && l.GetAt(7) == nullptr);                         // out of range -> 0 / null
        CHECK(l.SetValue(1, 50) == 50 && l.Get(1) == 50);                     // prop->Set
        CHECK(l.AddValue(1, 5) == 55 && l.Get(1) == 55);                      // prop->Add
        l.AddValue(-1, 1);                                                    // every property
        CHECK(l.Get(0) == 2 && l.Get(1) == 56 && l.Get(2) == 4);
        l.Snapshot(-1);
        CHECK(l.GetSnapshot(1) == 56 && l.GetSnapshot(2) == 4);
        l.SetValue(-1, 9);
        CHECK(l.Get(0) == 9 && l.Get(2) == 9 && l.GetSnapshot(0) == 2);       // the snapshot is kept
        l.Reset(1);                                                            // Set(default) + snapshot, only property 1
        CHECK(l.Get(1) == 2 && l.GetSnapshot(1) == 2 && l.Get(0) == 9);
        l.Reset(-1);
        CHECK(l.Get(0) == 1 && l.Get(2) == 3);
        l.Configure(0, 0, 5, 3, 4);
        CHECK(l.GetMax(0) == 5 && l.GetDefault(0) == 3 && l.Get(0) == 4);
        CProperty* p1 = l.GetAt(1);
        CHECK(l.IndexOf(p1) == 1 && l.IndexOf(nullptr) == -1);
        CHECK(l.Remove(p1) && l.Size() == 2 && g_propsAlive == 3);             // Remove does NOT delete
        delete p1;
        CHECK(g_propsAlive == 2);
        l.RemoveAll();
        CHECK(l.Size() == 0 && g_propsAlive == 2);                             // RemoveAll does not delete either (leak in the original too)
        l.Add(new CountedProp(0, 1, 0, 0));
        // destructor deletes what is left (3 alive: 2 leaked on purpose + 1)
        CHECK(g_propsAlive == 3);
    }
    CHECK(g_propsAlive == 2); // the two removed-but-not-deleted ones are still alive (this is what the original does)
}

// ---------------------------------------------------------------------------------------------
// event tests with a fake clock
u32 g_clock = 0;
std::vector<std::string> g_log;
int g_events = 0;

// completes `ms` after Start (checked by Tick); ms == 0: completes synchronously inside Start
struct TimedEvent : IEvent {
    std::string name;
    u32 ms, deadline = 0;
    bool running = false;
    static std::vector<TimedEvent*>& all() { static std::vector<TimedEvent*> v; return v; }
    TimedEvent(const char* n, u32 d) : name(n), ms(d) { ++g_events; all().push_back(this); }
    ~TimedEvent() override {
        --g_events;
        auto& a = all();
        for (size_t i = 0; i < a.size(); ++i) if (a[i] == this) { a.erase(a.begin() + long(i)); break; }
    }
    bool Start() override {
        g_log.push_back(name + "+");
        running = true;
        deadline = g_clock + ms;
        if (ms == 0) { running = false; if (owner) owner->OnChildDone(); }
        return true;
    }
    bool End() override { g_log.push_back(name + "-"); running = false; return true; }
    static void Tick() {
        auto v = all(); // copy: completion may delete events
        for (TimedEvent* e : v) {
            // the event may have been deleted by a previous completion: check it is still registered
            bool alive = false;
            for (TimedEvent* x : all()) if (x == e) alive = true;
            if (alive && e->running && g_clock >= e->deadline) {
                e->running = false;
                if (e->owner) e->owner->OnChildDone();
            }
        }
    }
};

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (auto& x : v) { if (!s.empty()) s += ' '; s += x; }
    return s;
}
void RunTo(u32 until) { while (g_clock < until) { g_clock += 10; TimedEvent::Tick(); } }

struct SeqProbe {
    std::vector<std::string> log;
    static void Cb(CEventSequencer*, int code, uintptr_t ud1, uintptr_t ud2) {
        SeqProbe* p = reinterpret_cast<SeqProbe*>(ud1);
        p->log.push_back(std::string(code == 1 ? "start" : "end") + ":" + std::to_string(ud2));
    }
};

void TestSequencer() {
    std::printf("[event sequencer]\n");
    g_clock = 0;
    g_log.clear();
    {
        CEventSequencer seq(1, 0, nullptr);
        CHECK(seq.GetRoot() == &seq);
        seq.Add(new TimedEvent("A", 100));
        seq.Add(new TimedEvent("B", 50));
        seq.Add(new TimedEvent("C", 0));   // completes synchronously inside Start (busy / pendingAdvance path)
        seq.Add(new TimedEvent("D", 30));
        CHECK(seq.Size() == 4 && g_events == 4);
        SeqProbe probe;
        seq.StartSequence(0, &SeqProbe::Cb, reinterpret_cast<uintptr_t>(&probe), 7);
        CHECK(seq.IsPlaying() && Join(g_log) == "A+");
        CHECK(seq.Add(new TimedEvent("X", 1)) == -1); // refused while playing (the event is leaked by the caller: delete it)
        delete TimedEvent::all().back();
        seq.StartSequence(0, nullptr, 0, 0);          // "called while playing": ignored
        CHECK(Join(g_log) == "A+");
        RunTo(90);
        CHECK(Join(g_log) == "A+");
        RunTo(100);
        CHECK(Join(g_log) == "A+ A- B+");
        RunTo(150);
        CHECK(Join(g_log) == "A+ A- B+ B- C+ C- D+");
        RunTo(180);
        CHECK(Join(g_log) == "A+ A- B+ B- C+ C- D+ D-");
        CHECK(!seq.IsPlaying());
        CHECK(probe.log.size() == 2 && probe.log[0] == "start:7" && probe.log[1] == "end:7");
        CHECK(seq.Size() == 4);                        // mode 0: the list is kept
        // run it again from index 2
        g_log.clear();
        seq.StartSequence(2, nullptr, 0, 0);
        CHECK(Join(g_log) == "C+ C- D+");
        RunTo(g_clock + 30);
        CHECK(Join(g_log) == "C+ C- D+ D-" && !seq.IsPlaying());
    }
    CHECK(g_events == 0); // the sequencer deleted its events (refcount 1 -> 0)

    // Stop(): ends the running event, notifies, nothing else runs
    g_log.clear();
    {
        CEventSequencer seq(1, 0, nullptr);
        seq.Add(new TimedEvent("A", 100));
        seq.Add(new TimedEvent("B", 100));
        SeqProbe probe;
        seq.StartSequence(0, &SeqProbe::Cb, reinterpret_cast<uintptr_t>(&probe), 1);
        CHECK(seq.Stop(1));
        CHECK(Join(g_log) == "A+ A-" && !seq.IsPlaying());
        CHECK(probe.log.size() == 2 && probe.log[1] == "end:1");
        CHECK(!seq.Stop(1));                             // not playing
        RunTo(g_clock + 500);
        CHECK(Join(g_log) == "A+ A-");
    }

    // mode 1: the list is cleared at the end; mode 2: top level sequencer deletes itself
    g_log.clear();
    {
        CEventSequencer seq(1, 1, nullptr);
        seq.Add(new TimedEvent("A", 10));
        seq.StartSequence(0, nullptr, 0, 0);
        RunTo(g_clock + 20);
        CHECK(seq.Size() == 0 && g_events == 0);
        CEventSequencer* self = new CEventSequencer(1, 2, nullptr);
        self->Add(new TimedEvent("S", 10));
        self->StartSequence(0, nullptr, 0, 0);
        RunTo(g_clock + 20);                              // `self` is gone now
        CHECK(g_events == 0);
    }

    // empty sequence / failing first event
    {
        CEventSequencer seq(1, 0, nullptr);
        SeqProbe probe;
        seq.StartSequence(0, &SeqProbe::Cb, reinterpret_cast<uintptr_t>(&probe), 3);
        CHECK(!seq.IsPlaying() && probe.log.size() == 2); // nothing to start: ends at once
    }

    // nested: group (parallel) inside a sequencer inside a sequencer
    g_log.clear();
    {
        CEventSequencer outer(1, 0, nullptr);
        CEventSequencer* inner = new CEventSequencer(1, 0, &outer);
        CEventGroupList* grp = new CEventGroupList(1, &outer);
        grp->Add(new TimedEvent("X", 100));
        grp->Add(new TimedEvent("Y", 30));
        grp->Add(new TimedEvent("Z", 60));
        inner->Add(new TimedEvent("I1", 20));
        inner->Add(new TimedEvent("I2", 20));
        outer.Add(new TimedEvent("P", 10));
        outer.Add(grp);
        outer.Add(inner);
        outer.Add(new TimedEvent("Q", 10));
        CHECK(grp->GetRoot() == &outer && inner->GetRoot() == &outer);
        u32 t0 = g_clock;
        outer.StartSequence(0, nullptr, 0, 0);
        RunTo(t0 + 10);
        CHECK(Join(g_log) == "P+ P- X+ Y+ Z+");
        RunTo(t0 + 100);                                  // X (the slowest, started at t0+10) is not done yet
        CHECK(Join(g_log) == "P+ P- X+ Y+ Z+");
        RunTo(t0 + 110);                                  // group done when the slowest child is done; children Ended in list order
        CHECK(Join(g_log) == "P+ P- X+ Y+ Z+ X- Y- Z- I1+");
        RunTo(t0 + 170);
        CHECK(!outer.IsPlaying());
        CHECK(g_log.back() == "Q-");
        size_t pI1 = 0, pI2 = 0, pQ = 0;
        for (size_t i = 0; i < g_log.size(); ++i) { if (g_log[i] == "I1+") pI1 = i; if (g_log[i] == "I2+") pI2 = i; if (g_log[i] == "Q+") pQ = i; }
        CHECK(pI1 < pI2 && pI2 < pQ);
    }
    CHECK(g_events == 0);

    // an empty group completes at once (notifies its owner)
    g_log.clear();
    {
        CEventSequencer seq(1, 0, nullptr);
        seq.Add(new CEventGroupList(1, &seq));
        seq.Add(new TimedEvent("E", 10));
        seq.StartSequence(0, nullptr, 0, 0);
        CHECK(Join(g_log) == "E+");
        RunTo(g_clock + 10);
        CHECK(!seq.IsPlaying());
    }

    // CEventList bookkeeping
    {
        CEventList l(1, nullptr);
        auto* a = new TimedEvent("a", 1);
        auto* b = new TimedEvent("b", 1);
        auto* c = new TimedEvent("c", 1);
        l.Add(a);
        l.Add(b);
        l.Insert(c, 1);
        CHECK(l.GetAt(0) == a && l.GetAt(1) == c && l.GetAt(2) == b && l.IndexOf(c) == 1 && l.IndexOf(nullptr) == -1);
        CHECK(a->refcount == 1 && a->owner == &l);
        CHECK(l.NextIndex(0) == 1 && l.NextIndex(0) == 2 && l.NextIndex(0) == -1 && l.NextIndex(1) == 0); // wrap
        CHECK(l.Prev(0) == -1 && l.Prev(1) == 2 && l.Prev(0) == 1);
        CHECK(l.SetIndex(2) == 1 && l.SetIndex(9) == -1 && l.CurrentIndex() == 2);
        CHECK(l.GetNextEvent(1) == a);
        CHECK(l.RemoveAt(1) && l.Size() == 2 && g_events == 2);         // c deleted (refcount 0)
        a->refcount += 1;                                                // shared by a second owner
        l.RemoveAll();
        CHECK(g_events == 1);                                            // a survives: refcount 2 -> 1
        delete a;
        CHECK(g_events == 0);
    }
}

// ---------------------------------------------------------------------------------------------
std::vector<std::string> g_ufLog;
CEventUserFunc* g_ufEv = nullptr;
void* UserFn(CEventUserFunc* ev, uintptr_t p1, uintptr_t p2, int phase, void* data) {
    g_ufLog.push_back("fn" + std::to_string(phase) + ":" + std::to_string(p1) + ":" + std::to_string(p2));
    if (phase == 1) {
        g_ufEv = ev; // the function completes the event later (like the HUD counters do when counting ends)
        return reinterpret_cast<void*>(0x1234);
    }
    g_ufLog.push_back(data == reinterpret_cast<void*>(0x1234) ? "data-ok" : "data-bad");
    return nullptr;
}

void TestUserFunc() {
    std::printf("[event user func / random / queue arrays]\n");
    g_clock = 0;
    g_log.clear();
    g_ufLog.clear();
    {
        CEventSequencer seq(1, 0, nullptr);
        seq.Add(new CEventUserFunc(0xF000101E, 5, &UserFn, nullptr, 0));
        seq.Add(new TimedEvent("T", 20));
        CHECK(seq.GetAt(0)->Init());
        seq.StartSequence(0, nullptr, 0, 0);
        CHECK(g_ufLog.size() == 1 && g_ufLog[0] == "fn1:" + std::to_string(0xF000101Eu) + ":5" && g_log.empty());
        CEventUserFunc::OnDone(nullptr, reinterpret_cast<uintptr_t>(static_cast<IEvent*>(g_ufEv)), 0);
        CHECK(g_ufLog.size() == 3 && g_ufLog[1].substr(0, 3) == "fn2" && g_ufLog[2] == "data-ok");
        CHECK(Join(g_log) == "T+");
        RunTo(g_clock + 20);
        CHECK(!seq.IsPlaying());
    }
    {
        // an event whose function never completes: Stop() calls fn with phase 3
        struct Hang { static void* Fn(CEventUserFunc*, uintptr_t, uintptr_t, int ph, void*) { g_ufLog.push_back("ph" + std::to_string(ph)); return nullptr; } };
        CEventSequencer seq(1, 0, nullptr);
        seq.Add(new CEventUserFunc(1, 2, &Hang::Fn, nullptr, 0));
        g_ufLog.clear();
        seq.StartSequence(0, nullptr, 0, 0);
        CHECK(seq.IsPlaying() && g_ufLog.size() == 1 && g_ufLog[0] == "ph1");
        seq.Stop(1);
        CHECK(g_ufLog.size() == 2 && g_ufLog[1] == "ph3");
    }
    CHECK(g_events == 0);

    // CQueueMsgArray
    {
        moses::QUEUEMSG a = moses::qm::Anim(50, 0x8b0001, 0, 3), b = moses::qm::Delay(5), c = moses::qm::Anim(80, 0x8b0002, 0, 1);
        moses::QUEUEMSG* src[2] = {&a, &b};
        CQueueMsgArray arr(3, src, 2);
        CHECK(arr.Count() == 2 && arr.Insert(&c) == 2 && arr.Count() == 3 && arr.Data()[2] == &c);
        CHECK(arr.Insert(&b, 0) == 0 && arr.Data()[0] == &b && arr.Count() == 4);
        arr.SetParam100(33);
        CHECK(a.a == 33 && c.a == 33 && b.a == 5);              // only op 100 messages
        CHECK(arr.Remove(0) == 1 && arr.Count() == 3 && arr.Remove(1, 2) == 1 && arr.Count() == 1 && arr.Remove(5) == 0);
        moses::QUEUEMSG* two[2] = {&b, &c};
        CHECK(arr.InsertN(two, 2, 0) == 0 && arr.Count() == 3 && arr.Data()[0] == &b && arr.Data()[1] == &c);
    }

    // random select: a permutation before any repetition, never the same twice in a row
    {
        aaron::SeedRandom(12345);
        CEventSequencer seq(1, 0, nullptr);
        CEventRandomSelect* rs = new CEventRandomSelect(1, &seq);
        for (int i = 0; i < 3; ++i) rs->Add(new TimedEvent("R", 10));
        CHECK(rs->Init());
        int p[7];
        for (int i = 0; i < 7; ++i) p[i] = rs->PickRandomFree(0);
        CHECK(p[0] != p[1] && p[1] != p[2] && p[0] != p[2]);
        CHECK(p[3] != p[2]);
        CHECK(p[3] != p[4] && p[4] != p[5] && p[3] != p[5]);
        CHECK(p[6] != p[5]);
        for (int i = 0; i < 20; ++i) { int r = rs->PickRandomFree(1); CHECK(r >= 0 && r < 3); }
        g_log.clear();
        rs->ResetIndex();
        seq.Add(rs);
        seq.StartSequence(0, nullptr, 0, 0);
        CHECK(g_log.size() == 1 && g_log[0] == "R+");
        RunTo(g_clock + 10);
        CHECK(!seq.IsPlaying() && g_log.size() == 2 && g_log[1] == "R-");
    }
    CHECK(g_events == 0);
}

void TestRandomLists() {
    std::printf("[random lists]\n");
    int item[4];
    {
        CEventRandomList l(1);
        for (int i = 0; i < 3; ++i) l.Add(&item[i]);
        CHECK(l.Total() == 3 && l.AvailCount() == 3 && l.UsedCount() == 0);
        CHECK(l.GetNext() == &item[0] && l.GetNext() == &item[1] && l.GetNext() == &item[2] && l.GetNext() == nullptr);
        CHECK(l.UsedCount() == 3 && l.GetUsedAt(1) == &item[1] && l.GetAvailAt(0) == nullptr);
        l.Return(&item[1]);
        CHECK(l.AvailCount() == 1 && l.GetNext() == &item[1]);
        l.ResetAll();
        CHECK(l.AvailCount() == 3 && l.UsedCount() == 0 && l.GetAvailAt(0) == &item[0]);
        aaron::SeedRandom(7);
        l.Shuffle();
        CHECK(l.AvailCount() == 3);
        int seen = 0;
        for (int i = 0; i < 3; ++i) { void* v = l.GetAvailAt(u32(i)); for (int k = 0; k < 3; ++k) if (v == &item[k]) ++seen; }
        CHECK(seen == 3);
    }
    {
        CHunRandomList h(1);
        for (int i = 0; i < 3; ++i) h.Add(&item[i]);
        CHECK(h.GetNext() == &item[0] && h.GetNext() == &item[1] && h.GetNext() == &item[2]);
        CHECK(h.GetNext() == &item[0]);      // recycles the OLDEST used one
        CHECK(h.GetNext() == &item[1]);
        CHunRandomList empty(1);
        CHECK(empty.GetNext() == nullptr);
    }
}

// ---------------------------------------------------------------------------------------------
// A game scene without window / resources: every HUD resource is "none", so no sprite is needed except the iris overlay.
struct FakePopups : IPopupFactory {
    struct Call { void* parent; int type; uintptr_t arg; void* ret; };
    std::vector<Call> calls;
    void* CreateObject(void* parent, int type, uintptr_t arg) override {
        void* r = reinterpret_cast<void*>(uintptr_t(calls.size() + 1) * 0x100);
        calls.push_back({parent, type, arg, r});
        return r;
    }
};

const RECT kRect = {-320, -155, 320, 170};
ArcadeGameDef MakeDef(int transitions = 0) {
    ArcadeGameDef d = {};
    d.propCapacity = 4;
    d.game = {2, 2, 4, &kRect};
    d.pauseRes = 0;
    d.hsDigitsRes = d.p1DigitsRes = d.p2DigitsRes = d.big1Res = d.big2Res = kNoRes;
    d.lettersRes = d.hsScoreDigitsRes = d.hsLevelDigitsRes = d.hsIconRes = kNoRes;
    d.hsTable = nullptr; // -> default table
    d.sfxBankRes = d.midiRes = d.pauseAnimRes = kNoRes;
    d.transitions = transitions;
    return d;
}

struct TGame : CArcadeGame {
    Observer hsObs, pObs[2];
    int extraLifeCalls = 0;
    struct TPlayer : CPlayer {
        TGame* tg;
        TPlayer(int i, TGame* g) : CPlayer(i, g, 3, 3, 5, 100), tg(g) {}
        void OnExtraLife() override { ++tg->extraLifeCalls; }
    };
    explicit TGame(const ArcadeGameDef* d) : CArcadeGame(nullptr, "arcade-test", d) {
        rootWidget_ = new aaron::Node(aaron::WidgetParent(static_cast<aaron::Scene*>(this)), 0, 0);
    }
    ~TGame() override {
        DestroyMembers(); // as Scene::Destroy -> OnClose does
        delete rootWidget_;
        rootWidget_ = nullptr;
    }
    CPlayerBase* CreatePlayer(int i) override { return new TPlayer(i, this); }
    aaron::Subscriber* GetHSDisplaySubscriber() override { return &hsObs; }
    aaron::Subscriber* GetPlayerDisplaySubscriber(int i) override { return &pObs[i & 1]; }
};

std::string TempIni() {
    std::filesystem::path p = std::filesystem::temp_directory_path() / ("arcade_test_" + std::to_string(unsigned(std::rand())) + ".ini");
    return p.string();
}

void TestPlayersAndGame() {
    std::printf("[players / game]\n");
    ArcadeGameDef def = MakeDef();
    TGame g(&def);
    CHECK(CGameBase::Get() == &g);
    CHECK(g.Init());
    CHECK(g.players.Count() == 2 && g.ev0 && g.ev1 && g.transition && g.settings0 == 1);
    CHECK(g.numPlayers == 1);                         // no saved settings: forced to >= 1
    CPlayer* p0 = g.Player(0);
    CPlayer* p1 = g.Player(1);
    CHECK(p0->index == 0 && p1->index == 1 && p0->controller && p1->controller);
    CHECK(p0->GetLives() == 3 && p0->GetLevel() == 1 && p0->GetDifficulty() == 0 && p0->GetScore() == 0);
    CHECK(p0->deviceType == 2);                       // device 0 in the (empty) blob -> default device of the definition
    // observers of the score: high-score display + the player's display (+ the player itself for extra lives)
    p0->SetScore(40);
    CHECK(g.hsObs.got.size() == 1 && g.hsObs.got[0].second == 40 && g.pObs[0].got.size() == 1 && g.pObs[1].got.empty());
    p1->SetScore(70);
    CHECK(g.hsObs.got.size() == 2 && g.pObs[1].got.size() == 1 && g.pObs[1].got[0].second == 70);
    // extra life every 100 points; the threshold starts at 0 (disabled) until ResetGame
    p0->SetScore(250);
    CHECK(p0->GetLives() == 3 && g.extraLifeCalls == 0);
    p0->ResetGame();
    CHECK(p0->GetScore() == 0 && p0->nextExtraLife == 100 && p0->GetLives() == 3);
    p0->SetScore(99);
    CHECK(p0->GetLives() == 3);
    p0->SetScore(100);
    CHECK(p0->GetLives() == 4 && g.extraLifeCalls == 1 && p0->nextExtraLife == 200);
    p0->SetScore(950);                                 // QUIRK: one life only, even if several thresholds were crossed
    CHECK(p0->GetLives() == 5 && p0->nextExtraLife == 300);
    p0->AddScore(1);                                   // already above the threshold: another life (lives max 5 -> clamped)
    CHECK(p0->GetLives() == 5 && g.extraLifeCalls == 3);
    p0->SetExtraLifeThreshold(1234);
    CHECK(p0->nextExtraLife == 1300);
    // properties of the player list
    p0->PropAdd(new CPropNumeric(0, 7, 7, 7));
    CHECK(p0->PropCount() == 1 && p0->PropGet(0) == 7 && p0->PropGetMax(0) == 7);
    p0->PropAddValue(0, -3);
    CHECK(p0->PropGet(0) == 4);
    p0->PropSnapshot(-1);
    p0->PropSetValue(0, 1);
    CHECK(p0->PropGet(0) == 1 && p0->PropGetSnapshot(0) == 4);
    // resume / pause drive the controllers' suspend counter
    g.curPlayer = p0;
    g.Resume();
    CHECK(g.running == 1);
    g.Pause();
    CHECK(g.running == 0);
    // level property (global)
    CHECK(g.GetGlobalLevel() == 1);
    g.numPlayers = 2;
    CHECK(g.NextLevel(2) == 3 && p0->GetLevel() == 3 && p1->GetLevel() == 3);
    CHECK(g.SetLevelAll(5) == 5 && p1->GetLevel() == 5);
    g.AdvanceLevel();                                  // single mode: current player's level + 1
    CHECK(p0->GetLevel() == 6 && p1->GetLevel() == 5);
    g.simultaneous = 1;
    g.AdvanceLevel();                                  // simultaneous: global level + 1 for the alive players
    CHECK(g.GetGlobalLevel() == 6 && p0->GetLevel() == 6 && p1->GetLevel() == 6);
    g.simultaneous = 0;
}

void TestPersistence() {
    std::printf("[settings / GB / AG persistence]\n");
    std::string ini = TempIni();
    aaron::RegKey::SetIniPath(ini);
    ArcadeGameDef def = MakeDef();
    {
        TGame g(&def);
        CHECK(g.Init());
        // Settings blob
        g.numPlayers = 2;
        g.Player(0)->SetDevice(1);
        g.Player(1)->SetDevice(3);
        g.Player(1)->SetDifficulty(2);
        g.SetMute(1, 0);                               // saves "Settings"
        CHECK(g.sfxMute == 1 && g.musicMute == 0);
        // GB / AG values
        g.curIndex = 1;
        g.Player(0)->PropAdd(new CPropNumeric(0, 100, 10, 10));
        g.Player(1)->PropAdd(new CPropNumeric(0, 100, 20, 20));
        g.Player(0)->PropSnapshot(-1);
        g.Player(1)->PropSetValue(0, 33);
        g.Player(1)->PropSnapshot(0);
        g.Player(1)->PropSetValue(0, 44);              // current 44, snapshot 33: the snapshot is what gets saved
        g.Player(0)->SetScore(1500);
        g.Player(0)->GetScoreProp()->Snapshot();
        g.Player(0)->SetScore(1700);
        g.Player(0)->SetLevel(4);
        g.Player(0)->GetLevelProp()->Snapshot();
        g.Player(0)->SetLives(2);
        g.Player(0)->GetLivesProp()->Snapshot();
        g.Player(1)->SetDifficulty(1);
        g.Player(1)->GetDifficultyProp()->Snapshot();
        g.levelProp->Set(9);
        aaron::RegKey slot(aaron::RegKey::HKCU, "ArcadeTest\\Save\\0");
        g.SerializeGlobals(&slot, 1);
        CHECK(slot.GetDword("GBNumPlayers", 99) == 2 && slot.GetDword("GBCurPlayer", 99) == 1);
        CHECK(slot.GetDword("GBP1Prop0", 99) == 33 && slot.GetDword("GBP0Prop0", 99) == 10);
        CHECK(slot.GetDword("AGP0Score", 99) == 1500 && slot.GetDword("AGP0Level", 99) == 4);
        CHECK(slot.GetDword("AGP0Lives", 99) == 2 && slot.GetDword("AGP1Difficulty", 99) == 1);
        CHECK(slot.GetDword("AGLevel", 99) == 9);      // AGLevel is the CURRENT value, the per-player ones the snapshots
    }
    aaron::RegKey::FlushAll();
    CHECK(std::filesystem::exists(ini));
    aaron::RegKey::SetIniPath(ini); // force a reload from disk
    {
        TGame g(&def);
        CHECK(g.Init());
        CHECK(g.numPlayers == 2 && g.sfxMute == 1 && g.musicMute == 0);
        CHECK(g.settings0 == 2);                       // Init increments it every run; the saved value was 1 -> 2
        CHECK(g.Player(0)->deviceType == 1 && g.Player(1)->deviceType == 3 && g.Player(1)->GetDifficulty() == 2);
        g.Player(0)->PropAdd(new CPropNumeric(0, 100, 10, 10));
        g.Player(1)->PropAdd(new CPropNumeric(0, 100, 20, 20));
        aaron::RegKey slot(aaron::RegKey::HKCU, "ArcadeTest\\Save\\0");
        g.SerializeGlobals(&slot, 0);
        CHECK(g.numPlayers == 2 && g.curIndex == 1);
        CHECK(g.Player(1)->PropGet(0) == 33 && g.Player(0)->PropGet(0) == 10);
        CHECK(g.Player(0)->GetScore() == 1500 && g.Player(0)->GetLevel() == 4 && g.Player(0)->GetLives() == 2);
        CHECK(g.Player(1)->GetDifficulty() == 1 && g.levelProp->Get() == 9);
        // the loaded score also arms the extra-life threshold: (1500 / 100 + 1) * 100
        CHECK(g.Player(0)->nextExtraLife == 1600);
        // an unknown slot loads zeros: SetNumPlayers(0)
        aaron::RegKey empty(aaron::RegKey::HKCU, "ArcadeTest\\Save\\5");
        g.SerializeGlobals(&empty, 0);
        CHECK(g.numPlayers == 0);
    }
    aaron::RegKey::SetIniPath(ini); // drop in-memory state
    std::error_code ec;
    std::filesystem::remove(ini, ec);
}

// ---------------------------------------------------------------------------------------------
void TestHighScores() {
    std::printf("[high scores]\n");
    std::string ini = TempIni();
    aaron::RegKey::SetIniPath(ini);
    aaron::RegKey key(aaron::RegKey::HKCU, "ArcadeTest\\HS");
    {
        CHighScoreTable t(&key, nullptr, kDefaultHSTable, 1, 2, 3, 4);
        CHECK(t.Rows() == 10 && t.Columns() == 0xf);
        CHECK(t.GetTopScore() == 0 && t.GetLowestScore() == 0);
        CHECK(t.InsertScore(5000, 3, 1) == 1);
        CHECK(t.InsertScore(3000, 2, 0) == 2);
        CHECK(t.InsertScore(4000, 5, 2) == 2);          // shifts 3000 down
        CHECK(t.Score(0) == 5000 && t.Score(1) == 4000 && t.Score(2) == 3000 && t.Level(1) == 5 && t.Icon(1) == 2);
        CHECK(t.InsertScore(4000, 7, 0) == 2);          // QUIRK: a tie goes above the older equal score
        CHECK(t.Level(1) == 7 && t.Level(2) == 5);
        CHECK(t.Initials(1)[0] == -1);                  // new rows have empty initials
        int ini3[3] = {4, 5, 6};
        t.SetInitials(ini3, 2);
        CHECK(t.Initials(1)[0] == 4 && t.Initials(1)[2] == 6);
        for (int i = 0; i < 10; ++i) t.InsertScore(100 + i, 1, 0);   // fill the table
        CHECK(t.Score(9) >= 100 && t.GetLowestScore() == t.Score(9));
        int lowest = t.GetLowestScore();
        CHECK(t.InsertScore(lowest, 1, 0) == 0);        // not strictly greater than the last row: rejected
        CHECK(t.InsertScore(lowest - 1, 1, 0) == 0);
        int r = t.InsertScore(lowest + 1, 9, 2);
        CHECK(r >= 1 && t.GetLowestScore() >= lowest);
    }
    // raw record format: 6 x i32 = {initials[3], score, level, icon}
    {
        int rec[6] = {};
        aaron::RegKey sub("HighScore", &key);
        CHECK(sub.GetBinary("0", rec, 0x18, nullptr));
        CHECK(rec[0] == -1 && rec[3] == 5000 && rec[4] == 3 && rec[5] == 1);
    }
    // a second table instance sees the persisted rows
    aaron::RegKey::FlushAll();
    aaron::RegKey::SetIniPath(ini);
    {
        CHighScoreTable t(&key, nullptr, kDefaultHSTable, 1, 2, 3, 4);
        CHECK(t.GetTopScore() == 5000 && t.Score(1) == 4000 && t.Level(1) == 7 && t.Initials(1)[0] == 4 && t.Initials(1)[2] == 6);
        t.Clear();
        CHECK(t.GetTopScore() == 0 && t.Initials(0)[1] == -1);
    }
    {
        CHighScoreTable t(&key, nullptr, kDefaultHSTable, 1, 2, 3, 4);
        CHECK(t.GetTopScore() == 0 && t.Score(5) == 0);
        // QUIRK: a row missing from the registry inherits the previous row (the read buffer is reused)
        t.InsertScore(900, 1, 0);
        t.InsertScore(800, 2, 1);
        aaron::RegKey sub("HighScore", &key);
        sub.DeleteValue("1");
        CHighScoreTable u(&key, nullptr, kDefaultHSTable, 1, 2, 3, 4);
        u.Load();
        CHECK(u.Score(0) == 900 && u.Score(1) == 900 && u.Level(1) == 1);
    }
    // a table with only the score column keeps the other fields untouched
    {
        HSTableDef d = kDefaultHSTable;
        d.rows = 3;
        d.columns = 1;
        aaron::RegKey k2(aaron::RegKey::HKCU, "ArcadeTest\\HS2");
        CHighScoreTable t(&k2, nullptr, d, 1, 2, 3, 4);
        CHECK(t.Rows() == 3 && t.InsertScore(10, 5, 5) == 1 && t.InsertScore(20, 6, 6) == 1 && t.Score(1) == 10);
        d.rows = 50; // capped at 20
        CHighScoreTable big(&k2, nullptr, d, 1, 2, 3, 4);
        CHECK(big.Rows() == 20);
    }
    aaron::RegKey::SetIniPath(ini);
    std::error_code ec;
    std::filesystem::remove(ini, ec);
}

// ---------------------------------------------------------------------------------------------
u32 FakeNow() { return g_clock; }

void TestGameFlow() {
    std::printf("[game flow: popups, transitions, pause]\n");
    std::string ini = TempIni();
    aaron::RegKey::SetIniPath(ini);
    FakePopups popups;
    SetPopupFactory(&popups);
    ArcadeGameDef def = MakeDef(0);
    {
        TGame g(&def);
        CHECK(g.Init());
        g.numPlayers = 2;
        // ---- game over, single mode: rank 1 -> popup 6; player 1 -> 7; no rank -> table (8)
        g.curPlayer = g.Player(0);
        g.Player(0)->SetScore(7000);
        g.ShowGameOver();
        CHECK(popups.calls.size() == 1 && popups.calls[0].type == POPUP_NAME_P1 && popups.calls[0].arg == 1);
        g.curPlayer = g.Player(1);
        g.Player(1)->SetScore(8000);
        g.ShowGameOver();
        CHECK(popups.calls.size() == 2 && popups.calls[1].type == POPUP_NAME_P2 && popups.calls[1].arg == 1);
        g.curPlayer = g.Player(1);
        g.Player(1)->SetScore(0);
        g.ShowGameOver();
        CHECK(popups.calls.size() == 3 && popups.calls[2].type == POPUP_HIGHSCORES && popups.calls[2].arg == 0);
        // ---- simultaneous: both enter, chain parent = previous popup, the LAST player's popup first
        popups.calls.clear();
        g.simultaneous = 1;
        g.Player(0)->SetScore(9000);
        g.Player(1)->SetScore(9500);
        g.ShowGameOver();
        CHECK(popups.calls.size() == 2);
        CHECK(popups.calls[0].type == POPUP_NAME_P2 && popups.calls[0].parent == nullptr);
        CHECK(popups.calls[1].type == POPUP_NAME_P1 && popups.calls[1].parent == popups.calls[0].ret);
        CHECK(popups.calls[0].arg == 1 && popups.calls[1].arg == 2);  // player 2 has the better score: rank 1; ties/ordering handled by InsertScore
        g.simultaneous = 0;
        // ---- commands
        popups.calls.clear();
        g.OnCommand(CMD_SHOW_HISCORES, 0);
        CHECK(popups.calls.size() == 1 && popups.calls[0].type == POPUP_HIGHSCORES);
        g.OnCommand(CMD_GAMEOVER_NAMES, 0);              // alive players with scores -> name popups 0x10/0x11
        CHECK(popups.calls.size() >= 2 && popups.calls[1].type >= POPUP_NAME2_P1);
        // ---- pause command: stops the running menu sequence and remembers the state
        popups.calls.clear();
        g_clock = 0;
        g_log.clear();
        CEventSequencer* menu = new CEventSequencer(1, 0, nullptr);
        menu->Add(new TimedEvent("M", 1000));
        g.SetMenuSequence(menu);
        g.ShowMenu();                                    // state = CMD_STATE_8, ev0 plays the sequence
        CHECK(g.state == CMD_STATE_8 && g.ev0->IsPlaying() && Join(g_log) == "M+");
        g.OnCommand(CMD_PAUSE, 0);
        CHECK(g.pausedByMenu == 1 && g.savedState == CMD_STATE_8 && !g.ev0->IsPlaying());
        CHECK(popups.calls.size() == 1 && popups.calls[0].type == POPUP_OPTIONS);
        g.state = 0;
        g.OnCommand(CMD_RESUME_MENU, 0);                 // restores the state and dispatches it (hook B4: opens the iris)
        CHECK(g.pausedByMenu == 0 && g.state == CMD_STATE_8);
        CHECK(g.irisClosed == 0);                        // StateB4 -> StartTransition(1, ...) opened the iris
        // ---- CMD_SKIP stops both managers
        CEventSequencer* menu2 = new CEventSequencer(1, 0, nullptr);
        menu2->Add(new TimedEvent("N", 1000));
        g.ev0->Add(menu2);
        g.ev0->StartSequence(0, nullptr, 0, 0);
        CHECK(g.ev0->IsPlaying());
        g.OnCommand(CMD_SKIP, 0);
        CHECK(!g.ev0->IsPlaying());
    }
    CHECK(g_events == 0);

    // ---- transitions driven by the engine's animation service and a fake clock
    {
        popups.calls.clear();
        ArcadeGameDef d2 = MakeDef(1);
        TGame g(&d2);
        CHECK(g.Init());
        g_clock = 1000;
        moses::CAnimView::SetTimeSource(&FakeNow);
        CHECK(g.irisClosed == 1);
        CHECK(g.StartTransition(0, 3, 0, 10000) == 0);   // already closed: nothing to do
        CHECK(!g.Focus().Locked());
        CHECK(g.StartTransition(1, 3, 1, 10000) == 1);   // open the iris, then StartPlayers(1)
        CHECK(g.irisClosed == 0 && g.Focus().Locked());
        CHECK(g.StartTransition(1, 3, 1, 10000) == 0);   // already open
        int guard = 0;
        while (g.Focus().Locked() && guard++ < 400) {
            g_clock += 10;
            g.Service();
        }
        CHECK(!g.Focus().Locked());                       // the QM_NOTIFY arrived and OnSoundMessage unlocked the input
        CHECK(g.curIndex == 1);                           // action 3 -> StartPlayers(1) -> SetCurrentPlayer(1)
        CHECK(g.curPlayer == g.Player(1));
        // closing and re-opening again
        CHECK(g.StartTransition(0, 5, 0, 10000) == 1 && g.irisClosed == 1);
        guard = 0;
        while (g.Focus().Locked() && guard++ < 400) { g_clock += 10; g.Service(); }
        CHECK(!g.Focus().Locked() && g.running == 1);     // action 5 -> Resume()
        moses::CAnimView::SetTimeSource(nullptr);
    }
    SetPopupFactory(nullptr);
    aaron::RegKey::SetIniPath(ini);
    std::error_code ec;
    std::filesystem::remove(ini, ec);
}

void TestSpriteSequencer() {
    std::printf("[sprite sequencer]\n");
    std::string ini = TempIni();
    aaron::RegKey::SetIniPath(ini);
    ArcadeGameDef def = MakeDef();
    TGame g(&def);
    CHECK(g.Init());
    CEventSpriteSequencer* ev = g.ev1;
    CHECK(ev->FindSprite(5) == nullptr);
    CEventSpriteSequencer::SpriteRec* r = ev->AddSprite(5, 0xffffffffu, nullptr, 0, 10, 20, 100, nullptr, 0, 1);
    CHECK(r && r->widget && ev->FindSprite(5) == r && r->level == 100 && ev->SpriteCount() == 1);
    CHECK(ev->AddSprite(5, 0xffffffffu, nullptr, 0, 0, 0, 7, nullptr, 0, 0) == r);   // existing record, forceNew == 0: kept
    CHECK(r->level == 100);
    // a sprite without a rest animation is destroyed at the end of a sequence
    ev->OnEnd();
    CHECK(r->widget == nullptr && ev->FindSprite(5) == r);
    // CEventUserSprite without a sprite and without an animation cannot start: the sequence ends at once
    ev->Add(new CEventUserSprite(9, 0xffffffffu, nullptr, 0, 50, CEventUserSprite::WAIT_END, 0xffffffffu, 0));
    CHECK(ev->GetAt(0)->Init());
    ev->StartSequence(0, nullptr, 0, 0);
    CHECK(!ev->IsPlaying() && ev->Size() == 0);
    aaron::RegKey::SetIniPath(ini);
    std::error_code ec;
    std::filesystem::remove(ini, ec);
}

} // namespace

int RunArcadeTests(int, char**) {
    std::printf("== arcade framework tests (G1) ==\n");
    // never touch the real registry file
    std::string ini0 = TempIni();
    aaron::RegKey::SetIniPath(ini0);
    TestProperties();
    TestSequencer();
    TestUserFunc();
    TestRandomLists();
    TestPlayersAndGame();
    TestPersistence();
    TestHighScores();
    TestGameFlow();
    TestSpriteSequencer();
    std::printf("== %d checks, %d failed ==\n", g_checks, g_fail);
    aaron::RegKey::SetIniPath(ini0);
    std::error_code ec;
    std::filesystem::remove(ini0, ec);
    return g_fail == 0 ? 0 : 1;
}
