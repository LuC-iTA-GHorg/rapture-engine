// Package 3 headless tests: aaron application / scene / loop / timers / input / registry. No window is ever created.
// Usage: <game>.exe --engine-test 3 [DIR_WITH_RRS]
#include "host/HostConfig.h"
#include "aaron/Aaron.h"
#include "tests/EngineTests.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace aaron;

namespace {

int g_fail = 0, g_checks = 0;

#define CHECK(c)                                                                         \
    do {                                                                                 \
        ++g_checks;                                                                      \
        if (!(c)) {                                                                      \
            ++g_fail;                                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                   \
        }                                                                                \
    } while (0)

// ---- fake time -----------------------------------------------------------------------------------------------------
u32 g_now = 0;
u32 fakeNow() { return g_now; }

// ---- test scene ----------------------------------------------------------------------------------------------------
std::vector<std::string> g_log;
struct Cmd { u32 id, param; };
std::vector<Cmd> g_cmds;
struct In { u32 code; int x, y; u32 user; };
std::vector<In> g_inputs;
int g_scenesAlive = 0;
int g_sceneNo = 0;

class TestScene : public Scene {
public:
    explicit TestScene(aaron::Window* host, const char* nm = "TestScene") : Scene(host, nm) { ++g_scenesAlive; }
    ~TestScene() override { --g_scenesAlive; g_log.push_back("dtor"); }
    bool OpenResources() override { g_log.push_back("OpenResources"); return true; }
    bool OnPreCreate() override { g_log.push_back("OnPreCreate"); return true; }
    bool OnCreate() override { g_log.push_back("OnCreate"); return true; }
    bool OnStart() override { g_log.push_back("OnStart"); return true; }
    void OnReady() override { g_log.push_back("OnReady"); }
    void OnClose() override { g_log.push_back("OnClose"); }
    int OnCommand(u32 id, uintptr_t param) override {
        g_cmds.push_back({id, u32(param)});
        if (id == 0x500) PostMessage(WMSG_COMMAND, 0x501, 0); // posted during the drain: next iteration
        return 1;
    }
    void OnInput(u32 code, int x, int y, u32 user) override {
        Scene::OnInput(code, x, y, user);
        g_inputs.push_back({code, x, y, user});
    }
};

Scene* CreateTestScene(aaron::Window* host) {
    ++g_sceneNo;
    return new TestScene(host, g_sceneNo == 1 ? "TestScene" : "TestScene2");
}

// ---- helpers for the object tests -------------------------------------------------------------------------------------
int g_nodeDtors = 0;
struct TNode : TreeNode {
    ~TNode() override { ++g_nodeDtors; DeleteChildren(); }
};

u32 g_notifyId = 0;
Widget* g_notifySrc = nullptr;
struct TW : Node {
    explicit TW(WidgetParent p) : Node(p, 0, 0) {}
    void OnNotify(u32 id, Widget* src) override { g_notifyId = id; g_notifySrc = src; }
};

struct Sub : Subscriber {
    int n = 0;
    int lastCode = 0;
    intptr_t lastVal = 0;
    void OnAdvise(int c, intptr_t v, Broadcaster*) override { ++n; lastCode = c; lastVal = v; }
};

// ---------------------------------------------------------------------------------------------------------------------
void test_random() {
    std::puts("[random]");
    SeedRandom(1); // MSVC CRT default seed: rand() = 41, 18467, 6334, 26500, 19169
    CHECK(Random(0, 32767) == 41);
    CHECK(Random(0, 32767) == 18467);
    CHECK(Random(0, 32767) == 6334);
    CHECK(Random(0, 32767) == 26500);
    CHECK(Random(0, 32767) == 19169);
    SeedRandom(7);
    for (int i = 0; i < 100; ++i) {
        int r = Random(5, 9);
        CHECK(r >= 5 && r <= 9);
    }
    CHECK(Random(3, 3) == 3);
}

void test_ptrarray() {
    std::puts("[ptrarray]");
    PtrArray a(2);
    int v[5] = {0, 1, 2, 3, 4};
    for (int i = 0; i < 4; ++i) CHECK(a.Insert(&v[i], -1) == i);
    CHECK(a.Count() == 4);
    CHECK(a.Insert(&v[4], 1) == 1);
    CHECK(a.Count() == 5 && a.At(1) == &v[4] && a.At(2) == &v[1] && a.At(4) == &v[3]);
    CHECK(a.Remove(&v[1]) == 2);
    CHECK(a.Remove(&v[1]) == -1);
    CHECK(a.RemoveAt(0) == &v[0]);
    CHECK(a.Count() == 3);
    a.Truncate(1);
    CHECK(a.Count() == 1 && a.At(0) == &v[4]);
    a.Clear();
    CHECK(a.Count() == 0);
    CHECK(a.Resize(32) == 1);
    a.Free();
    CHECK(a.Count() == 0);
}

struct PlainNode : TreeNode { static int dtors; ~PlainNode() override { ++dtors; } };
int PlainNode::dtors = 0;

void test_tree() {
    std::puts("[treenode]");
    { // ~TreeNode deletes the children (verified in the original, 0x10002944)
        PlainNode::dtors = 0;
        PlainNode* r = new PlainNode;
        PlainNode* a = new PlainNode;
        r->AddChild(a);
        a->AddChild(new PlainNode);
        r->AddChild(new PlainNode);
        delete r;
        CHECK(PlainNode::dtors == 4);
    }
    g_nodeDtors = 0;
    {
        TNode* root = new TNode;
        TNode* c1 = new TNode;
        TNode* c2 = new TNode;
        TNode* g = new TNode;
        root->AddChild(c1);
        root->AddChild(c2);
        c1->AddChild(g);
        CHECK(root->FirstChild() == c1 && root->LastChild() == c2);
        CHECK(c1->NextSibling() == c2 && c2->PrevSibling() == c1);
        CHECK(c1->Parent() == root && g->Parent() == c1 && g->Root() == root);
        delete c1; // unlinks itself from root; deletes its own child (TNode dtor)
        CHECK(g_nodeDtors == 2);
        CHECK(root->FirstChild() == c2 && root->LastChild() == c2 && c2->PrevSibling() == nullptr);
        root->DeleteChildren();
        CHECK(g_nodeDtors == 3 && root->FirstChild() == nullptr);
        delete root;
        CHECK(g_nodeDtors == 4);
    }
}

void test_broadcaster() {
    std::puts("[broadcaster]");
    Broadcaster b;
    Sub s1, s2;
    b.AddListener(&s1);
    b.AddListener(&s2);
    CHECK(b.ListenerCount() == 2);
    b.Broadcast(-1, 42, &b);
    CHECK(s1.n == 1 && s2.n == 1 && s1.lastCode == -1 && s1.lastVal == 42);
    b.RemoveListener(&s1);
    b.Broadcast(5, 1, &b);
    CHECK(s1.n == 1 && s2.n == 2);
    {
        Sub tmp;
        b.AddListener(&tmp);
        CHECK(b.ListenerCount() == 2);
    } // ~Subscriber unlinks
    CHECK(b.ListenerCount() == 1);
    {
        Broadcaster* b2 = new Broadcaster;
        Sub s3;
        b2->AddListener(&s3);
        delete b2; // ~Broadcaster clears the back reference: s3's destructor must not touch the dead broadcaster
        CHECK(s3.n == 0);
    }
    // Broadcast2: the second dword is plain data; a Subscriber that overrides OnAdvise2 gets both, the others get it as the source pointer
    struct Sub2 : Subscriber {
        int code = 0;
        intptr_t a = 0, b = 0;
        void OnAdvise2(int c, intptr_t x, intptr_t y) override { code = c; a = x; b = y; }
    };
    Broadcaster b2;
    Sub2 q;
    Sub plain;
    b2.AddListener(&q);
    b2.AddListener(&plain);
    b2.Broadcast2(0x1401, 7, 0x12345);
    CHECK(q.code == 0x1401 && q.a == 7 && q.b == 0x12345);
    CHECK(plain.n == 1 && plain.lastCode == 0x1401 && plain.lastVal == 7);
    b2.Broadcast(3, 9, &b2);                      // the plain Broadcast still reaches OnAdvise2's default (-> OnAdvise) / the override with the pointer
    CHECK(q.code == 3 && q.a == 9 && q.b == intptr_t(&b2) && plain.n == 2);
}

void test_regkey() {
    std::puts("[regkey]");
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "pkg3_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::path ini = dir / "sub" / "test.ini";
    RegKey::SetIniPath(ini.string());
    {
        RegKey root(RegKey::HKCU, "Software\\Disney Interactive\\Test Game\\US");
        RegKey hs("HighScore", &root);
        RegKey e0("0", &hs);
        CHECK(e0.GetDword("Score", 77) == 77); // missing -> default
        e0.SetDword("Score", 12345);
        const unsigned char blob[6] = {0x00, 0xff, 0x10, 0xab, 0x7f, 0x01};
        e0.SetBinary("Data", blob, sizeof blob);
        e0.SetString("Name", "AAA");
        RegKey e1("1", &hs);
        e1.SetDword("Score", 999);
        RegKey m(RegKey::HKLM, "Software\\Disney Interactive\\Test Game\\US");
        m.SetDword("WaveVol", 70);
        CHECK(e0.GetDword("Score") == 12345);
        RegKey::FlushAll();
    }
    CHECK(fs::exists(ini)); // written under the temp dir, never the user's real file
    { // hand-edited file: FKeys=0x1 (hex) is accepted, junk falls back to the default
        fs::path hini = dir / "hex.ini";
        std::FILE* hf = std::fopen(hini.string().c_str(), "wb");
        std::fputs("[HKCU\\HexTest]\nFKeys=0x1\nBig=0xFFFFFFFF\nJunk=abc\n", hf);
        std::fclose(hf);
        RegKey::SetIniPath(hini.string());
        RegKey h(RegKey::HKCU, "HexTest");
        CHECK(h.GetDword("FKeys", 0) == 1 && h.GetDword("Big", 0) == 0xFFFFFFFFu && h.GetDword("Junk", 9) == 9);
    }
    RegKey::SetIniPath(ini.string()); // forces a re-read from disk
    {
        RegKey root(RegKey::HKCU, "Software\\Disney Interactive\\Test Game\\US");
        RegKey hs("HighScore", &root);
        RegKey e0("0", &hs), e1("1", &hs);
        CHECK(e0.GetDword("Score") == 12345);
        unsigned char got[6] = {0};
        const unsigned char def[6] = {9, 9, 9, 9, 9, 9};
        CHECK(e0.GetBinary("Data", got, 6, def));
        CHECK(got[0] == 0 && got[1] == 0xff && got[2] == 0x10 && got[3] == 0xab && got[4] == 0x7f && got[5] == 1);
        unsigned char got2[6] = {0};
        CHECK(!e0.GetBinary("Nope", got2, 6, def) && got2[0] == 9 && got2[5] == 9);
        char s[16];
        CHECK(e0.GetString("Name", s, sizeof s, "x") && std::strcmp(s, "AAA") == 0);
        CHECK(e1.GetDword("Score") == 999);
        RegKey m(RegKey::HKLM, "Software\\Disney Interactive\\Test Game\\US");
        CHECK(m.GetDword("WaveVol", 100) == 70);
        e0.DeleteValue("Score");
        CHECK(e0.GetDword("Score", 5) == 5);
        hs.DeleteKey(); // recursive
        CHECK(e1.GetDword("Score", 5) == 5);
        CHECK(e0.GetDword("Data", 5) == 5);
        CHECK(m.GetDword("WaveVol", 100) == 70); // HKLM untouched
        RegKey::FlushAll();
    }
    RegKey::SetIniPath(ini.string());
    {
        RegKey root(RegKey::HKCU, "Software\\Disney Interactive\\Test Game\\US");
        RegKey hs("HighScore", &root);
        RegKey e1("1", &hs);
        CHECK(e1.GetDword("Score", 5) == 5);
    }
    // never leave the redirected path pointing at a deleted dir with pending writes
    RegKey::SetIniPath(ini.string());
    fs::remove_all(dir, ec);
}

void test_input_maps() {
    std::puts("[keys]");
    CHECK(SdlKeyToVk('a') == 'A' && SdlKeyToVk('z') == 'Z' && SdlKeyToVk('5') == '5');
    CHECK(SdlKeyToVk(1073741904) == AVK_LEFT && SdlKeyToVk(1073741903) == AVK_RIGHT);
    CHECK(SdlKeyToVk(1073741906) == AVK_UP && SdlKeyToVk(1073741905) == AVK_DOWN);
    CHECK(SdlKeyToVk(1073741882) == AVK_F1 && SdlKeyToVk(1073741886) == AVK_F5);
    CHECK(SdlKeyToVk(1073742048) == AVK_CONTROL && SdlKeyToVk(1073742049) == AVK_SHIFT && SdlKeyToVk(1073742050) == AVK_MENU);
    CHECK(SdlKeyToVk(32) == AVK_SPACE && SdlKeyToVk(27) == AVK_ESCAPE && SdlKeyToVk(13) == AVK_RETURN);
    CHECK(SdlKeyToVk(1073741909) == AVK_MULTIPLY && SdlKeyToVk(1073741911) == AVK_ADD && SdlKeyToVk(1073741910) == AVK_SUBTRACT);
    CHECK(SdlModToAmod(0x0040 | 0x0001) == (AMOD_CTRL | AMOD_SHIFT));
    CHECK(SdlModToAmod(0x0200) == AMOD_ALT && SdlModToAmod(0) == 0);
}

void test_managers() {
    std::puts("[managers]");
    TimerMgr tm;
    Sub s;
    tm.Tick(100); // no listeners: ignored
    CHECK(tm.TotalMs() == 0);
    tm.AddListener(&s);
    tm.Tick(1000); // first tick with a listener: reference time only
    CHECK(s.n == 0);
    tm.Tick(1250);
    CHECK(s.n == 1 && s.lastCode == int(AMSG_TICK) && s.lastVal == 250);
    tm.Tick(1250); // same time: nothing
    CHECK(s.n == 1);
    tm.Tick(1300);
    CHECK(s.n == 2 && s.lastVal == 300);
    // pause counter + broadcast
    TimerMgr pm;
    Sub ps;
    pm.AddListener(&ps);
    CHECK(pm.PauseCounter(true) == 1 && ps.n == 1 && ps.lastCode == int(AMSG_PAUSE) && ps.lastVal == 1);
    CHECK(pm.PauseCounter(true) == 2 && ps.n == 1);
    CHECK(pm.Paused());
    CHECK(pm.PauseCounter(false) == 1 && ps.n == 1);
    CHECK(pm.PauseCounter(false) == 0 && ps.n == 2 && ps.lastVal == 0);
    CHECK(!pm.Paused());
    CHECK(pm.PauseCounter(false) == 0 && ps.n == 2); // never negative
}

// ---------------------------------------------------------------------------------------------------------------------
void test_app_scene(const char* rrsDir) {
    std::puts("[application / scene / loop]");
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "pkg3_test_app";
    std::error_code ec;
    fs::remove_all(dir, ec);
    RegKey::SetIniPath((dir / "test.ini").string());

    moses::CAnimView::SetTimeSource(&fakeNow);
    g_now = 1000;
    g_log.clear(); g_cmds.clear(); g_inputs.clear();
    g_sceneNo = 0;

    RegisterScene("Test.DLL", "CreateScene", &CreateTestScene);
    CHECK(FindScene("test.dll", nullptr) == &CreateTestScene); // case-insensitive, default function name
    CHECK(FindScene("Nope.DLL", "CreateScene") == nullptr);

    APPINIT ai;
    std::snprintf(ai.name, sizeof ai.name, "Test App");
    std::snprintf(ai.regKey, sizeof ai.regKey, "Software\\Test\\Pkg3");
    {
        Application app(&ai);
        CHECK(Application::GetApp() == &app);
        app.SetSleepMs(0);

        Module* m = app.GetModule("Test.DLL", "CreateScene", nullptr);
        CHECK(m != nullptr);
        CHECK(app.GetModule("TEST.dll", "createscene", nullptr) == m); // cached
        CHECK(app.GetModule("Missing.DLL", "CreateScene", nullptr) == nullptr);

        // --- scene creation order ---------------------------------------------------------------------------------
        CHECK(app.LoadSceneNow(m, 0));
        Scene* sc = app.GetScene();
        CHECK(sc != nullptr && sc->Initialized());
        const char* want[] = {"OpenResources", "OnPreCreate", "OnCreate", "OnStart", "OnReady"};
        CHECK(g_log.size() == 5);
        for (size_t i = 0; i < 5 && i < g_log.size(); ++i) CHECK(g_log[i] == want[i]);
        CHECK(sc->Origin().x == 320 && sc->Origin().y == 240);
        CHECK(sc->Name() == "TestScene");
        CHECK(sc->GetModule() == m);
        CHECK(sc->Arg() == 0);
        CHECK(sc->RootWidget() != nullptr && sc->TreeRootNode() != nullptr);
        CHECK(g_scenesAlive == 1);

        // --- messages: FIFO, no re-entrancy -------------------------------------------------------------------------
        sc->PostMessage(WMSG_COMMAND, 0x500, 11);
        sc->PostMessage(WMSG_COMMAND, 0x502, 12);
        CHECK(g_cmds.empty());
        CHECK(app.RunFrame());
        CHECK(g_cmds.size() == 2 && g_cmds[0].id == 0x500 && g_cmds[0].param == 11 && g_cmds[1].id == 0x502);
        CHECK(app.RunFrame());
        CHECK(g_cmds.size() == 3 && g_cmds[2].id == 0x501); // posted from inside OnCommand: processed one iteration later
        // queue capacity: 100 entries, extra are dropped
        for (int i = 0; i < 105; ++i) sc->PostMessage(WMSG_COMMAND, 0x600 + u32(i), 0);
        g_cmds.clear();
        app.RunFrame();
        CHECK(g_cmds.size() == 100);

        // --- AMSG_NOTIFY goes to the widget (or its scene command) -------------------------------------------------------
        TW* w = static_cast<TW*>(Widget::InitOrDelete(new TW(sc)));
        CHECK(w != nullptr);
        sc->PostMessage(AMSG_NOTIFY, 42, reinterpret_cast<intptr_t>(w));
        CHECK(g_notifyId == 0);
        app.RunFrame();
        CHECK(g_notifyId == 42 && g_notifySrc == w);

        // --- accelerators ---------------------------------------------------------------------------------------------
        sc->AddAccelerator('A', 0x1234, AMOD_CTRL, 99);
        g_cmds.clear();
        sc->HandleKey(WMSG_KEYDOWN, 'A', 0); // modifier mismatch
        app.RunFrame();
        CHECK(g_cmds.empty());
        sc->HandleKey(WMSG_KEYDOWN, 'A', AMOD_CTRL);
        app.RunFrame();
        CHECK(g_cmds.size() == 1 && g_cmds[0].id == 0x1234 && g_cmds[0].param == 99);
        sc->RemoveAccelerator('A', AMOD_CTRL);
        g_cmds.clear();
        sc->HandleKey(WMSG_KEYDOWN, 'A', AMOD_CTRL);
        app.RunFrame();
        CHECK(g_cmds.empty());

        // accelerator push/pop (modal dialogs hide the scene's accelerators)
        sc->AddAccelerator('B', 0x4321, 0, 1);
        sc->Accelerators().Push();
        g_cmds.clear();
        sc->HandleKey(WMSG_KEYDOWN, 'B', 0);
        app.RunFrame();
        CHECK(g_cmds.empty());
        sc->Accelerators().Pop();
        sc->HandleKey(WMSG_KEYDOWN, 'B', 0);
        app.RunFrame();
        CHECK(g_cmds.size() == 1 && g_cmds[0].id == 0x4321);
        sc->RemoveAccelerator('B', 0);
        // cursor: SetCursor returns the previous one
        CHECK(sc->Cursor().SetCursor(3) == 1 && sc->Cursor().SetCursor(1) == 3);
        // MsgQueue::PurgeWidget
        {
            MsgQueue q;
            Widget* fake = reinterpret_cast<Widget*>(uintptr_t(0x1000));
            q.Post(AMSG_NOTIFY, 1, reinterpret_cast<intptr_t>(fake));
            q.Post(AMSG_NOTIFY, 2, 0); // null widget: ignored by Scene::OnMessage
            q.Post(WMSG_COMMAND, 3, reinterpret_cast<intptr_t>(fake)); // not a notify: kept
            q.PurgeWidget(fake);
            g_cmds.clear();
            g_notifyId = 0;
            q.Dispatch(sc);
            CHECK(g_cmds.size() == 1 && g_cmds[0].id == 3 && q.Empty());
        }

        // --- audio keys: '+' '-' and '*' ----------------------------------------------------------------------------------
        CHECK(app.Audio().WaveVolume() == 100);
        // AudioMgr::OnMessage 0x10011c33: acts on KEY UP only, Shift excludes the wave mixer, Ctrl excludes the MIDI mixer
        sc->HandleKey(WMSG_KEYDOWN, AVK_SUBTRACT, 0);
        CHECK(app.Audio().WaveVolume() == 100 && app.Audio().MidiVolume() == 100); // key down: nothing
        sc->HandleKey(WMSG_KEYUP, AVK_SUBTRACT, 0);
        CHECK(app.Audio().WaveVolume() == 90 && app.Audio().MidiVolume() == 90);
        sc->HandleKey(WMSG_KEYUP, AVK_SUBTRACT, AMOD_CTRL); // Ctrl excludes MIDI: wave only
        CHECK(app.Audio().WaveVolume() == 80 && app.Audio().MidiVolume() == 90);
        sc->HandleKey(WMSG_KEYUP, AVK_SUBTRACT, AMOD_SHIFT); // Shift excludes wave: MIDI only
        CHECK(app.Audio().WaveVolume() == 80 && app.Audio().MidiVolume() == 80);
        sc->HandleKey(WMSG_KEYUP, AVK_ADD, 0);
        sc->HandleKey(WMSG_KEYUP, AVK_ADD, 0);
        sc->HandleKey(WMSG_KEYUP, AVK_ADD, 0);
        CHECK(app.Audio().WaveVolume() == 100 && app.Audio().MidiVolume() == 100); // clamped at 100
        sc->HandleKey(WMSG_KEYUP, AVK_MULTIPLY, AMOD_CTRL);
        CHECK(app.Audio().Lib().mute(moses::MIX_WAVE, 0, 0) == 1 && app.Audio().Lib().mute(moses::MIX_MIDI, 0, 0) == 0);
        sc->HandleKey(WMSG_KEYUP, AVK_MULTIPLY, AMOD_CTRL);
        CHECK(app.Audio().Lib().mute(moses::MIX_WAVE, 0, 0) == 0);
        sc->HandleKey(WMSG_KEYUP, AVK_MULTIPLY, AMOD_CTRL | AMOD_SHIFT); // both excluded: nothing changes
        CHECK(app.Audio().Lib().mute(moses::MIX_WAVE, 0, 0) == 0 && app.Audio().Lib().mute(moses::MIX_MIDI, 0, 0) == 0);
        CHECK(!sc->HandleKey(WMSG_KEYUP, AVK_MULTIPLY, 0)); // the key is not consumed
        sc->HandleKey(WMSG_KEYUP, AVK_MULTIPLY, 0);        // both toggled twice: unmuted again
        CHECK(app.Audio().Lib().mute(moses::MIX_WAVE, 0, 0) == 0 && app.Audio().Lib().mute(moses::MIX_MIDI, 0, 0) == 0);

        // --- volumes in the registry (machine key) -------------------------------------------------------------------------
        app.Audio().SaveVolumes(app.GetMachineKey());
        {
            RegKey* mk = app.GetMachineKey();
            CHECK(mk->GetDword("WaveVol", 1) == 100 && mk->GetDword("MidiVol", 1) == 100);
            mk->SetDword("WaveVol", 55);
            app.Audio().LoadVolumes(mk);
            CHECK(app.Audio().WaveVolume() == 55);
            mk->SetDword("WaveVol", 100);
            app.Audio().LoadVolumes(mk);
        }
        {
            RegKey* uk = sc->GetUserKey();
            CHECK(uk->Path().find("Software\\Test\\Pkg3\\TestScene") != std::string::npos);
            uk->SetDword("AGLevel", 3);
            CHECK(sc->GetUserKey()->GetDword("AGLevel") == 3);
        }

        // --- scene timers + idle timer (fake clock) --------------------------------------------------------------------------
        Sub ts;
        sc->Timers().AddListener(&ts);
        g_now = 2000;
        app.RunFrame(); // first tick with a listener: reference
        g_now = 2100;
        app.RunFrame();
        CHECK(ts.n == 1 && ts.lastCode == int(AMSG_TICK) && ts.lastVal == 100);
        sc->Timers().RemoveListener(&ts);
        g_cmds.clear();
        sc->Idle().Start(1000, 0x77);
        g_now = 2600;
        app.RunFrame();
        app.RunFrame();
        CHECK(g_cmds.empty());
        g_now = 3200;
        app.RunFrame(); // posts the command (dispatched on the same iteration's next Dispatch)
        app.RunFrame();
        CHECK(g_cmds.size() == 1 && g_cmds[0].id == 0x77);
        sc->Idle().Stop();

        // --- controllers ---------------------------------------------------------------------------------------------------------
        g_inputs.clear();
        Controller* ctl = sc->CreateController(2 /*4 directions*/, 7);
        new KeyboardSource(ctl, 1);
        new MouseSource(ctl, 0);
        // sources are inert until EnableSource (original behaviour)
        sc->HandleKey(WMSG_KEYDOWN, AVK_LEFT, 0);
        CHECK(g_inputs.empty());
        sc->HandleKey(WMSG_KEYUP, AVK_LEFT, 0);
        ctl->EnableSource(SRC_KEYBOARD, 3);
        sc->HandleKey(WMSG_KEYDOWN, AVK_LEFT, 0);
        CHECK(g_inputs.size() == 1 && g_inputs[0].code == 8 && g_inputs[0].user == 7);
        sc->HandleKey(WMSG_KEYDOWN, AVK_UP, 0); // left+up = 9: not a 4-direction value, ignored in mode 2
        CHECK(g_inputs.size() == 1);
        sc->HandleKey(WMSG_KEYUP, AVK_LEFT, 0); // up only -> 1
        CHECK(g_inputs.size() == 2 && g_inputs[1].code == 1);
        sc->HandleKey(WMSG_KEYUP, AVK_UP, 0);
        CHECK(g_inputs.size() == 3 && g_inputs[2].code == 0);
        sc->HandleKey(WMSG_KEYDOWN, AVK_CONTROL, 0);
        CHECK(g_inputs.size() == 4 && g_inputs[3].code == IN_BTN1);
        sc->HandleKey(WMSG_KEYDOWN, AVK_SPACE, 0);
        CHECK(g_inputs.back().code == IN_BTN3);
        sc->HandleKey(WMSG_KEYUP, AVK_CONTROL, 0);
        CHECK(g_inputs.back().code == (IN_BTN1 | IN_RELEASE));
        // Suspend: releases the pressed buttons then emits IN_ONOFF(0); resume emits IN_ONOFF(1)
        g_inputs.clear();
        CHECK(ctl->Suspend(0) == 1);
        CHECK(g_inputs.size() == 2 && g_inputs[0].code == (IN_BTN3 | IN_RELEASE) && g_inputs[1].code == IN_ONOFF && g_inputs[1].x == 0);
        sc->HandleKey(WMSG_KEYDOWN, AVK_LEFT, 0); // suspended: nothing
        CHECK(g_inputs.size() == 2);
        CHECK(ctl->Suspend(1) == 0);
        CHECK(g_inputs.back().code == IN_ONOFF && g_inputs.back().x == 1);
        sc->HandleKey(WMSG_KEYUP, AVK_LEFT, 0);
        // application pause suspends the controllers (broadcast 0xc04) and stops the animation service
        g_inputs.clear();
        Sub tick;
        sc->Timers().AddListener(&tick);
        g_now = 4000;
        app.RunFrame(); // reference time
        g_now = 4010;
        app.RunFrame();
        CHECK(tick.n == 1);
        app.PauseApp(true);
        CHECK(!g_inputs.empty() && g_inputs.back().code == IN_ONOFF && g_inputs.back().x == 0);
        g_now = 4020;
        app.RunFrame();
        CHECK(tick.n == 1); // ServiceAnimViews skipped while the application is paused
        app.PauseApp(false);
        CHECK(g_inputs.back().code == IN_ONOFF && g_inputs.back().x == 1);
        g_now = 4030;
        app.RunFrame();
        CHECK(tick.n == 2);
        sc->Timers().RemoveListener(&tick);

        // Scene::SuspendControllers (dialog modality)
        g_inputs.clear();
        sc->SuspendControllers(true);
        CHECK(!g_inputs.empty() && g_inputs.back().code == IN_ONOFF && g_inputs.back().x == 0);
        size_t n0 = g_inputs.size();
        sc->HandleKey(WMSG_KEYDOWN, AVK_DOWN, 0);
        CHECK(g_inputs.size() == n0);
        sc->SuspendControllers(false);
        CHECK(g_inputs.back().code == IN_ONOFF && g_inputs.back().x == 1);
        sc->HandleKey(WMSG_KEYUP, AVK_DOWN, 0);

        // absolute mouse controller (mode 4)
        g_inputs.clear();
        Controller* mc = sc->CreateController(4, 9);
        new MouseSource(mc, 0);
        RECT box{-100, -100, 100, 100};
        mc->SetBounds(&box);
        mc->EnableSource(SRC_MOUSE, 3);
        sc->HandleMouse(WMSG_MOUSEMOVE, 50, -20, 0);
        bool abs = false;
        for (auto& i : g_inputs) if (i.code == IN_ABSOLUTE && i.x == 50 && i.y == -20 && i.user == 9) abs = true;
        CHECK(abs);
        g_inputs.clear();
        sc->HandleMouse(WMSG_LBUTTONDOWN, 50, -20, 1);
        CHECK(!g_inputs.empty() && g_inputs.back().code == IN_BTN1);
        sc->HandleMouse(WMSG_LBUTTONUP, 50, -20, 0);
        CHECK(g_inputs.back().code == (IN_BTN1 | IN_RELEASE));
        g_inputs.clear();
        sc->HandleMouse(WMSG_LBUTTONDOWN, 0, 0, 8 /*MK_CONTROL*/); // Ctrl held: ignored
        CHECK(g_inputs.empty());
        sc->HandleMouse(WMSG_LBUTTONUP, 0, 0, 0);

        // --- scene change: posted, executed at the start of the next iteration -----------------------------------------------------
        Module* m2 = m;
        g_log.clear();
        app.LoadScene(m2);
        CHECK(app.GetScene() == sc); // not yet
        CHECK(app.RunFrame());
        Scene* sc2 = app.GetScene();
        CHECK(sc2 != nullptr); // (address may be reused by the allocator)
        CHECK(sc2->Name() == "TestScene2");
        CHECK(g_scenesAlive == 1);
        CHECK(g_log.size() >= 7 && g_log[0] == "OnClose" && g_log[1] == "dtor" && g_log[2] == "OpenResources");
        CHECK(sc2->Arg() == 0); // same module reloaded: the previous scene's arg is kept (Window::LoadScene quirk)
        // different module: arg = previous module
        RegisterScene("Other.DLL", "CreateScene", &CreateTestScene);
        Module* mo = app.GetModule("Other.DLL", "CreateScene", nullptr);
        CHECK(mo != nullptr && mo != m);
        app.LoadScene(mo);
        app.RunFrame();
        CHECK(app.GetScene() != nullptr);
        CHECK(app.GetScene()->Arg() == reinterpret_cast<uintptr_t>(m));
        CHECK(app.GetScene()->GetModule() == mo);

        // --- Quit -----------------------------------------------------------------------------------------------------------------
        CHECK(!app.IsQuitting());
        intptr_t res = 0;
        CHECK(app.WndProc(WMSG_CLOSE, 0, 0, &res)); // OnQueryClose() default true -> Quit
        CHECK(app.IsQuitting());
        CHECK(!app.RunFrame());
        CHECK(app.OnSysCommand(0xF100) && !app.OnSysCommand(0xF060));

        // --- archive sharing (needs the real .rrs files; skipped when absent) --------------------------------------------------------
        std::string base = rrsDir ? rrsDir : "";
        const std::string mainName = rapture::Config().mainArchive;   // the game main archive; no game -> test skipped
        fs::path mus = fs::path(base) / mainName;
        if (!fs::exists(mus)) mus = fs::path(mainName);
        if (!mainName.empty() && fs::exists(mus)) {
            Scene* s = app.GetScene();
            std::string dirs = mus.parent_path().string();
            CHECK(s->OpenArchive(0, mainName.c_str(), dirs.c_str()) == 1);
            CHECK(s->OpenArchive(1, mainName.c_str(), dirs.c_str()) == 1); // second slot, same shared archive
            CHECK(s->Resources().Archive(MakeResId(0, 0x82, 0)) == s->Resources().Archive(MakeResId(1, 0x82, 0)));
            CHECK(s->Resources().Archive(MakeResId(0, 0x82, 0))->RefCount() >= 3); // cache + 2 slots
            void* a1 = s->LoadResource(MakeResId(0, 0x82, 1));
            void* a2 = s->LoadResource(MakeResId(0, 0x82, 1));
            if (a1) {
                CHECK(a1 == a2);
                CHECK(s->Resources().At(0).entries[1].refcount == 2);
                s->FreeResource(MakeResId(0, 0x82, 1));
                CHECK(s->Resources().At(0).entries[1].refcount == 1);
                s->FreeResource(MakeResId(0, 0x82, 1));
                CHECK(s->Resources().At(0).entries[1].refcount == 0);
            }
            CHECK(s->OpenArchive(1, nullptr, nullptr) == 0); // closes slot 1
            CHECK(s->Resources().Archive(MakeResId(1, 0x82, 0)) == nullptr);
        } else {
            std::puts("  (main archive not found: archive sharing test SKIPPED)");
        }
    } // ~Application destroys the scene
    CHECK(g_scenesAlive == 0);
    CHECK(Application::GetApp() == nullptr);
    moses::CAnimView::SetTimeSource(nullptr);
    RegKey::SetIniPath((dir / "test.ini").string());
    fs::remove_all(dir, ec);
}

} // namespace

int RunAaronCoreTests(int* checks); // tests/AaronCore_Test.cpp (WP-4)
int RunAaronAppTests(int* checks);  // tests/AaronApp_Test.cpp (WP-4)

int RunPkg3Tests(int argc, char** argv) {
    const char* dir = nullptr;
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] != '-' && std::strcmp(argv[i], "3") != 0) dir = argv[i];
    test_random();
    test_ptrarray();
    test_tree();
    test_broadcaster();
    test_regkey();
    test_input_maps();
    test_managers();
    test_app_scene(dir);
    g_fail += RunAaronCoreTests(&g_checks);
    g_fail += RunAaronAppTests(&g_checks);
    std::printf("pkg3: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
