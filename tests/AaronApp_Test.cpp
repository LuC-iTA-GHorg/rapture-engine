// WP-4 headless tests, group "app": Path, Module, Application / Window plumbing, RegKey, input sources + joystick (fake devices),
// managers (Focus / Cursor / Pick / Idle / Accel / Audio), cursors, Scene extras. No window is ever created.
// Called from RunPkg3Tests.
#include "aaron/Aaron.h"
#include "aaron/Application.h"
#include "aaron/Cursors.h"
#include "aaron/Joystick.h"
#include "aaron/Path.h"
#include "aaron/Scene.h"
#include "aaron/Sprites.h"
#include "platform/Joystick.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace aaron;

namespace {

int g_checks = 0, g_fail = 0;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        ++g_checks;                                                                      \
        if (!(c)) {                                                                      \
            ++g_fail;                                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                   \
        }                                                                                \
    } while (0)

u32 g_now = 5000;
u32 fakeNow() { return g_now; }

struct In { u32 code; int x, y; u32 user; };
std::vector<In> g_in;
std::vector<std::string> g_events;
int g_queries = 0;
int g_queryAnswer = 1;
std::vector<u32> g_cmds;

class AppScene : public Scene {
public:
    explicit AppScene(aaron::Window* host) : Scene(host, "AppScene") {}
    bool OpenResources() override { return true; }
    void OnInput(u32 code, int x, int y, u32 user) override {
        Scene::OnInput(code, x, y, user);
        g_in.push_back({code, x, y, user});
    }
    int OnQuery(u32 what) override { ++g_queries; (void)what; return g_queryAnswer; }
    int OnCommand(u32 id, uintptr_t) override { g_cmds.push_back(id); return 1; }
    int OnMessage(u32 msg, uintptr_t wp, intptr_t lp) override {
        if (msg >= 0x1400) { g_events.push_back("msg" + std::to_string(msg)); return 77; }
        return Scene::OnMessage(msg, wp, lp);
    }
    void Resize(int w, int h) override { g_events.push_back("resize" + std::to_string(w) + "x" + std::to_string(h)); Scene::Resize(w, h); }
};

Scene* CreateAppScene(aaron::Window* host) { return new AppScene(host); }
Scene* CreateOther(aaron::Window* host) { return new AppScene(host); }

// ---- path / module ----------------------------------------------------------------------------------------------------
void test_path_module() {
    std::puts("[wp4 path / module]");
#ifdef _WIN32
    CHECK(CombinePath("C:\\games\\x\\", "mus.rrs") == "C:\\games\\x\\mus.rrs");
    CHECK(CombinePath("C:\\games\\x\\Shoot.exe", "mus.rrs") == "C:\\games\\x\\mus.rrs"); // the base's file part is dropped
    CHECK(CombinePath("C:\\a\\b\\", "..\\c\\x.rrs") == "C:\\a\\c\\x.rrs");
    CHECK(CombinePath("C:\\a\\b\\", "sub/y.rrs") == "C:\\a\\b\\sub\\y.rrs");
    CHECK(CombinePath("C:\\a\\b\\", "\\root\\z.rrs") == "C:\\root\\z.rrs");   // rooted: replaces the directory, keeps the drive
    CHECK(CombinePath("C:\\a\\b\\", "D:\\other\\w.rrs") == "D:\\other\\w.rrs"); // a drive in the name wins
#endif
    CHECK(!ExecutablePath().empty());

    Module m1("Foo.DLL", "CreateScene", &CreateAppScene);   // the registry constructor
    CHECK(m1.GetClassId() == CID_MODULE);
    CHECK(m1.Matches("foo.dll", "createscene"));              // ord 417
    CHECK(m1.HasCreateFn(&CreateAppScene) && !m1.HasCreateFn(&CreateOther)); // ord 416
    Module m2(&CreateOther);                                  // ord 41
    CHECK(m2.HasCreateFn(&CreateOther) && m2.GetCreateFn() == &CreateOther);
    CHECK(!m2.IsLoaded());
    RegisterScene("Reg.DLL", "CreateScene", &CreateAppScene);
    Module m3("Reg.DLL", "CreateScene", "C:\\base\\dir\\"); // ord 42: resolved lazily from the registry
    CHECK(m3.GetCreateFn() == &CreateAppScene && m3.IsLoaded());
#ifdef _WIN32
    CHECK(m3.FilePath() == "C:\\base\\dir\\Reg.DLL");
    CHECK(m3.BuildPath("data.rrs") == "C:\\base\\dir\\data.rrs"); // ord 439
#endif
    Module m4; // ord 43: the executable's module
    CHECK(m4.FilePath() == ExecutablePath() && m4.GetCreateFn() == nullptr);
    // Unload (ord 702): the cache's reference is the only one left -> the function pointer is dropped and re-resolved on demand
    Module* cached = new Module("Reg.DLL", "CreateScene", &CreateAppScene);
    cached->AddRef(); // cache
    cached->AddRef(); // a user
    CHECK(cached->IsLoaded());
    cached->Unload(); // Release() -> 1
    CHECK(cached->RefCount() == 1 && !cached->IsLoaded());
    CHECK(cached->GetCreateFn() == &CreateAppScene && cached->IsLoaded()); // lazily re-resolved
    cached->Release();
}

// ---- application / window ---------------------------------------------------------------------------------------------------
void test_application_window() {
    std::puts("[wp4 application / window]");
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "wp4_test_app";
    std::error_code ec;
    fs::remove_all(dir, ec);
    RegKey::SetIniPath((dir / "test.ini").string());
    moses::CAnimView::SetTimeSource(&fakeNow);
    g_in.clear(); g_events.clear();
    RegisterScene("App.DLL", "CreateScene", &CreateAppScene);

    Application app("Four", "App.DLL", 0x1234); // ord 53
    CHECK(Application::GetApp() == &app);
    CHECK(app.Init().size == 0x39c && std::strcmp(app.Init().name, "Four") == 0 && std::strcmp(app.Init().sceneDll, "App.DLL") == 0);
    CHECK(app.Init().extra38c == 0x1234 && app.ReqBpp() == 16);
    CHECK(app.GetClassId() == CID_APPLICATION && app.GetObjClassId() == CID_ROBJECT);

    // ParseCommandLine (ord 528): '-x'/'/x' options carry the REST OF THE TOKEN; bare / quoted tokens are options ' '
    struct P : Application {
        std::vector<std::pair<char, std::string>> got;
        P() : Application("p", static_cast<const char*>(nullptr), 0) {}
        bool ParseArg(char o, const char* v) override { got.emplace_back(o, v); return true; }
    };
    {
        P p; // (a second Application: the global pointer moves to it)
        char cl[] = "  -fFoo /x \"quoted arg\" bare -";
        p.ParseCommandLine(cl);
        CHECK(p.got.size() == 5);
        if (p.got.size() == 5) {
            CHECK(p.got[0].first == 'f' && p.got[0].second == "Foo");
            CHECK(p.got[1].first == 'x' && p.got[1].second.empty());
            CHECK(p.got[2].first == ' ' && p.got[2].second == "quoted arg");
            CHECK(p.got[3].first == ' ' && p.got[3].second == "bare");
            CHECK(p.got[4].first == 0 && p.got[4].second.empty()); // "-" alone: option char = the terminator
        }
    }
    // (the P object took over g_app and cleared it when destroyed: re-create the main app)
    Application app2("Four", "App.DLL", 0x1234);
    CHECK(Application::GetApp() == &app2);
    CHECK(app2.ParseArg(' ', "Other.DLL") && std::strcmp(app2.Init().sceneDll, "Other.DLL") == 0); // ' ' = scene dll (ord 510)
    app2.SetRegistryPath("Software\\X");
    CHECK(std::strcmp(app2.Init().regKey, "Software\\X") == 0);
    // MessageBox (ord 447) without / with a hook
    CHECK(app2.MessageBox("hello", 0) == 1 && app2.MessageBox("q", 4) == 6 && app2.MessageBox("r", 5) == 4);
    std::string seen;
    Application::SetMessageBoxHook([&](const char* t, const char* x, u32 f) { seen = std::string(t) + "|" + x + "|" + std::to_string(f); return 7; });
    CHECK(app2.MessageBox("txt", 36) == 7 && seen == "Four|txt|36");
    Application::SetMessageBoxHook(nullptr);

    // modules
    CreateSceneFn fn = &CreateOther;
    Module* mf = app2.GetModuleForFn(fn); // ord 168
    CHECK(mf && mf->HasCreateFn(fn) && app2.GetModuleForFn(fn) == mf);
    Module* mm = app2.GetModule("App.DLL", "CreateScene", nullptr);
    CHECK(mm != nullptr);
    CHECK(app2.LoadSceneNow(mm, 0));
    Scene* sc = app2.GetScene();
    CHECK(sc && sc->GetObjClassId() == CID_SCENE); // ord 386

    // window handle plumbing (a fake handle: no platform window is created)
    CHECK(app2.NativeHandle() == nullptr && app2.GetClientWidth() == 640 && app2.GetClientHeight() == 480);
    app2.Attach(reinterpret_cast<void*>(0x1234), 0);
    CHECK(app2.NativeHandle() == reinterpret_cast<void*>(0x1234));
    app2.SetTitle("New Title"); // ord 670 -> SetTitleText (ord 667)
    CHECK(app2.TitleText() == "New Title" && std::strcmp(app2.Init().name, "New Title") == 0);
    app2.SetStatusTitle("Level 2"); // ord 148
    CHECK(app2.TitleText() == "New Title - Level 2");
    app2.SetPos(10, 20, 800, 600); // ord 445
    WINPLACEMENT wp = app2.GetPlacement();
    CHECK(wp.left == 10 && wp.top == 20 && wp.right == 810 && wp.bottom == 620 && wp.length == 0x2c);
    app2.Resize(300, 200); // ord 691
    wp = app2.GetPlacement();
    CHECK(wp.right == 310 && wp.bottom == 220);
    app2.Show(3); // ord 687
    CHECK(app2.ShowState() == 3 && !app2.IsIconic());
    app2.Show(2);
    CHECK(app2.IsIconic() && app2.GetClientWidth() == 300 && app2.GetClientHeight() == 200); // iconic: restored size (ord 395 / 285)
    app2.Show(1);
    // placement round trip through RegKey (ord 585 / 583)
    RegKey key(RegKey::HKCU, "Software\\Test\\Placement");
    app2.SetPos(1, 2, 640, 480);
    app2.Show(3);
    app2.SavePlacement(&key);
    app2.Show(1);
    app2.SetPos(0, 0, 10, 10);
    app2.ShowPlacement(&key, 5);
    CHECK(app2.ShowState() == 3 && app2.GetPlacement().left == 1 && app2.GetPlacement().right == 641);
    app2.ShowPlacement(&key, 2); // minimised / maximised requests override the saved state
    CHECK(app2.ShowState() == 2);
    RegKey empty(RegKey::HKCU, "Software\\Test\\NoPlacement");
    app2.ShowPlacement(&empty, 5); // no saved placement: plain show
    CHECK(app2.ShowState() == 5);
    app2.ShowPlacement(nullptr, 1);
    CHECK(app2.ShowState() == 1);
    CHECK(!app2.SetIcon(0) && !app2.SetIcon(5)); // no icon resources in the port
    app2.SetClassName("AaronTest");
    CHECK(aaron::Window::ActivateExisting("AaronTest") && !aaron::Window::ActivateExisting("Nope")); // ord 638

    // window list: another Window attaches itself to the application
    {
        aaron::Window w2;
        w2.Attach(reinterpret_cast<void*>(7), 1);
        CHECK(app2.WindowCount() == 1 && app2.DialogWindowCount() == 1);
        w2.SetClassName("W2");
        CHECK(app2.FindWindowByClass("W2") == &w2);
        int n = 0;
        auto cb = [](void* it, void* a, void*) { ++*static_cast<int*>(a); return it ? 1 : 0; };
        CHECK(app2.ForEachWindow(cb, &n, nullptr) == 1 && n == 1);   // ord 194
        n = 0;
        CHECK(app2.ForEachScene(cb, &n, nullptr) == 1 && n == 1);    // ord 195 (the application's own scene; w2 has none)
        CHECK(app2.IsDialogMessage(0x100, 0, 0) == 0);               // ord 543
        app2.InvalidateWindows(true);                                 // ord 573
    }
    CHECK(app2.WindowCount() == 0 && app2.DialogWindowCount() == 0); // ~Window left the list

    // WindowProc (ord 400) / WndProc (ord 401) routing
    g_events.clear();
    intptr_t res = 0;
    CHECK(app2.WindowProc(0xf, 0, 0, &res) && res == 1);              // WM_PAINT
    CHECK(app2.WindowProc(0x14, 0, 0, &res));                         // WM_ERASEBKGND swallowed
    CHECK(!app2.WindowProc(0x82, 0, 0, &res) && app2.IsDestroyed());  // WM_NCDESTROY marks it destroyed
    app2.Attach(reinterpret_cast<void*>(0x1234), 0);
    CHECK(!app2.IsDestroyed());
    CHECK(app2.WindowProc(0x1400, 3, 4, &res) && res == 77);          // >= 0x1400 -> the scene
    CHECK(!app2.WindowProc(0x500, 0, 0, &res));                       // other messages are not handled
    g_events.clear();
    CHECK(!app2.WindowProc(0x47, 0, 0, &res));                        // WM_WINDOWPOSCHANGED, flags 0: OnSize + scene->Resize
    CHECK(!g_events.empty() && g_events.back() == "resize640x480");
    g_events.clear();
    app2.WindowProc(0x47, 0, 1, &res); // SWP_NOSIZE
    CHECK(g_events.empty());
    g_cmds.clear();
    CHECK(app2.WindowProc(0x111, 0x42, 0, &res));                     // WM_COMMAND -> scene->OnMessage(0x111) -> OnCommand (returns 1 = handled)
    CHECK(g_cmds.size() == 1 && g_cmds[0] == 0x42);
    // Application::WndProc falls back to Window::WindowProc and adds the application messages:
    CHECK(app2.OnWindowMessage(0x1500, 0, 0, &res) && res == 77);
    CHECK(app2.OnWindowMessage(0x11, 0, 0, &res) && res == 1);        // WM_QUERYENDSESSION
    CHECK(app2.OnWindowMessage(0x30f, 0, 0, &res) && res == 1);       // WM_QUERYNEWPALETTE
    // PostMessage (ord 542) is deferred, SendMessage (ord 593) immediate
    g_events.clear();
    app2.PostMessage(0x1600, 0, 0);
    CHECK(g_events.empty());
    CHECK(app2.SendMessage(0x1601, 0, 0) == 77 && g_events.size() == 1);
    app2.PumpPlatformEvents();
    CHECK(g_events.size() == 2 && g_events[1] == "msg5632");
    // activation: WM_ACTIVATEAPP is cached by the application broadcaster (ord 496)
    struct Sub2 : Subscriber {
        int n = 0;
        void OnAdvise(int c, intptr_t, Broadcaster*) override { if (c == int(WMSG_ACTIVATEAPP)) ++n; }
    } sub;
    app2.AppBroadcaster().AddListener(&sub);
    CHECK(!app2.AppActive());
    app2.NotifyApp(int(WMSG_ACTIVATEAPP), 1);
    app2.NotifyApp(int(WMSG_ACTIVATEAPP), 1); // unchanged: dropped
    CHECK(app2.AppActive() && sub.n == 1);
    app2.NotifyApp(int(WMSG_ACTIVATEAPP), 0);
    CHECK(!app2.AppActive() && sub.n == 2);
    app2.AppBroadcaster().RemoveListener(&sub);
    app2.ResetDisplayMode(); // ord 579 (no-op in the port)
    app2.SetBaseDir("data"); // ord 626
    app2.DestroyNative();
    CHECK(app2.NativeHandle() == nullptr && app2.IsDestroyed());

    // Audio manager pieces (ord 533/574/654/692/695): no crash without a loaded sound
    app2.Audio().Reset();
    app2.Audio().SetPan(10, 0);
    app2.Audio().SetPan(10, 100);
    app2.Audio().StopAll(1, 1, 1);
    app2.Audio().SoundStop(nullptr);
}

// ---- input sources + joystick ---------------------------------------------------------------------------------------------
void test_input_joystick() {
    std::puts("[wp4 input / joystick]");
    g_in.clear();
    Application app("Joy", static_cast<const char*>(nullptr), 0);
    RegisterScene("Joy.DLL", "CreateScene", &CreateAppScene);
    Module* m = app.GetModule("Joy.DLL", "CreateScene", nullptr);
    CHECK(m && app.LoadSceneNow(m, 0));
    Scene* sc = app.GetScene();

    pjoy::ClearFakes();
    CHECK(JoystickMgr::CountDevices() >= 0);
    pjoy::State s0;
    pjoy::SetFake(0, &s0); // centred, no buttons
    CHECK(JoystickMgr::CountDevices() == 1);     // ord 314
    CHECK(JoystickDev::Probe(0) && !JoystickDev::Probe(1)); // ord 408
    CHECK(JoystickSource::PickDevice(0, 0) == 0);
    CHECK(JoystickSource::PickDevice(1, 0) == 0); // device 1 absent: the first present one
    CHECK(JoystickSource::PickDevice(1, 1) == 1); // ... searching from index 1 finds nothing: the preferred id again
    unsigned range[4];
    app.Joystick(0)->GetRange(range); // ord 295
    CHECK(range[0] == 0 && range[1] == 0 && range[2] == 65535 && range[3] == 65535);

    Controller* ctl = sc->CreateController(3, 9);
    JoystickSource* js = new JoystickSource(ctl, 0x8000); // "pick the best device, preferring 0"
    KeyboardSource* ks = new KeyboardSource(ctl, 1);
    MouseSource* ms = new MouseSource(ctl, 0);
    CHECK(js->Type() == 3 && ks->Type() == 1 && ms->Type() == 2);
    CHECK(ctl->SourceCount() == 3);
    CHECK(ctl->FindSource(SRC_MOUSE) == ms && ctl->FindSource(SRC_KEYBOARD) == ks);
    CHECK(ctl->FindSource(7) == ms);              // QUIRK (ord 291): no match -> the LAST source
    CHECK(ks->FindKey(0x10) == 0x11 && ks->FindKey(0x20) == 0x12 && ks->FindKey(0x400) == 0); // ord 297

    ctl->SetSourceFlags(SRC_KEYBOARD, 3);         // ord 185: only the keyboard changes
    CHECK(ks->Flags() == 3 && js->Flags() == 0 && ks->IsActive());
    ctl->EnableSource(SRC_JOYSTICK, 3);           // ord 587 -> InputSource::Enable (ord 588) -> JoystickSource::Activate (ord 449)
    CHECK(js->Flags() == 3 && ks->Flags() == 0 && !ks->IsActive());
    CHECK(js->DeviceId() == 0 && js->CenterX() == 32767 && js->DeadZone() == (65535 - 32767) / 3);
    CHECK(app.Joystick(0)->ListenerCount() == 1);
    CHECK(!app.Joystick(0)->Capturing());         // the application is not active yet
    app.Activate(true);                           // WM_ACTIVATEAPP -> JoystickDev::OnAdvise (0x100129cf)
    CHECK(app.Joystick(0)->Capturing());

    app.Joystick(0)->Poll(1000, true);            // centred: nothing to report
    CHECK(g_in.empty());
    pjoy::State right;
    right.x = 65535;
    pjoy::SetFake(0, &right);
    app.Joystick(0)->Poll(1010);                   // MM_JOY1MOVE: far right of the centre -> direction RIGHT (4)
    CHECK(g_in.size() == 1 && g_in[0].code == 4 && g_in[0].user == 9);
    app.Joystick(0)->Poll(1011);                   // inside the 10 ms period: skipped
    CHECK(g_in.size() == 1);
    pjoy::State fire = right;
    fire.buttons = 1;
    pjoy::SetFake(0, &fire);
    app.Joystick(0)->Poll(1030);                   // button 1 changed -> MM_JOY1BUTTONDOWN -> IN_BTN1 (0x10)
    CHECK(g_in.size() == 2 && g_in[1].code == 0x10);
    fire.buttons = 0;
    pjoy::SetFake(0, &fire);
    app.Joystick(0)->Poll(1050);
    CHECK(g_in.size() == 3 && g_in[2].code == (0x10u | IN_RELEASE));
    // the same messages routed through the application window procedure (MM_JOY1BUTTONDOWN with JOY_BUTTON2CHG)
    intptr_t res = 0;
    CHECK(app.WndProc(MMSG_JOY1BUTTONDOWN, 0x200 | 2, 0, &res));
    CHECK(g_in.size() == 4 && g_in[3].code == 0x20);
    CHECK(app.WndProc(MMSG_JOY1BUTTONUP, 0x200, 0, &res));
    CHECK(g_in.size() == 5 && g_in[4].code == (0x20u | IN_RELEASE));
    CHECK(app.WndProc(MMSG_JOY2MOVE, 0, 0, &res)); // device 1 has no listener: nothing happens
    // pause / deactivate: the capture stops (AMSG_PAUSE / WM_ACTIVATEAPP)
    app.PauseApp(true);
    CHECK(!app.Joystick(0)->Capturing());
    app.PauseApp(false);
    CHECK(app.Joystick(0)->Capturing());
    // Suspend/resume: the controller re-activates its enabled sources (Reactivate, ord 469)
    ctl->Suspend(0);
    CHECK(!js->IsActive() && app.Joystick(0)->ListenerCount() == 0);
    ctl->Suspend(1);
    CHECK(js->IsActive() && app.Joystick(0)->ListenerCount() == 1);
    // disabling unregisters
    ctl->EnableSource(SRC_MOUSE, 1);
    CHECK(app.Joystick(0)->ListenerCount() == 0 && ms->IsActive());
    // SetBounds with ints (ord 608 / 650)
    ctl->SetBounds(-100, -50, 100, 50);
    CHECK(ctl->Bounds().left == -100 && ctl->Bounds().bottom == 50);
    ms->SetBounds(0, 0, 20, 10);
    pjoy::ClearFakes();
    // direct dispatch into a dead device
    app.Joystick(1)->Dispatch(MMSG_JOY2MOVE, 0, 0);
}

// ---- managers -----------------------------------------------------------------------------------------------------------------
struct FW : Node {
    int focus = 0, keys = 0, pressed = 0;
    explicit FW(WidgetParent p) : Node(p, 0, 0) {}
    void OnFocus(bool on) override { focus += on ? 1 : -1; }
    int OnKey(u32, u32, u32) override { ++keys; return keys_return; }
    void SetPressedFlag(bool on) override { pressed += on ? 1 : -1; Node::SetPressedFlag(on); }
    int keys_return = 0;
};

struct Rec : SceneListener {
    int keyUps = 0;
    void OnInput(u32 msg, uintptr_t) override { if (msg == WMSG_KEYUP) ++keyUps; }
};

struct TestCursor : Cursor {
    int act = 0, lastNum = -1;
    TestCursor(Scene* s, u32 id) : Cursor(s, id) {}
    int Activate(bool on) override { act += on ? 1 : -1; return int(id_); }
    uintptr_t Handle(int, int, int n) override { lastNum = n; return 0x77; }
};

void test_managers() {
    std::puts("[wp4 managers / cursors / scene]");
    Application app("Mgr", static_cast<const char*>(nullptr), 0);
    RegisterScene("Mgr.DLL", "CreateScene", &CreateAppScene);
    Module* m = app.GetModule("Mgr.DLL", "CreateScene", nullptr);
    CHECK(m && app.LoadSceneNow(m, 0));
    Scene* sc = app.GetScene();

    // ---- FocusMgr
    FW* a = static_cast<FW*>(Widget::InitOrDelete(new FW(sc)));
    FW* b = static_cast<FW*>(Widget::InitOrDelete(new FW(sc)));
    FocusMgr& f = sc->Focus();
    f.SetFocusState(a, true);                          // ord 200
    CHECK(f.GetFocus() == a && f.Preferred() == a && a->focus == 1);
    f.SetFocusWidget(b);                               // ord 633
    CHECK(f.GetFocus() == b && a->focus == 0 && b->focus == 1);
    f.SetFocusWidget(nullptr);
    CHECK(f.GetFocus() == nullptr && f.Preferred() == nullptr && b->focus == 0);
    f.SetFocusWidget(a);
    CHECK(f.OnMessage(sc, 8, 0, 0) == false && f.GetFocus() == nullptr);   // WM_KILLFOCUS
    CHECK(f.OnMessage(sc, 7, 0, 0) == false && f.GetFocus() == a);        // WM_SETFOCUS restores the preferred widget
    // keys: modal first, then the focused widget, then the listeners
    Rec rec;
    f.AddKeyListener(&rec);
    CHECK(!f.OnMessage(sc, WMSG_KEYUP, 'A', 0) && a->keys == 1 && rec.keyUps == 1);
    a->keys_return = 1; // the focused widget consumes the key: the listeners do not see it
    CHECK(f.OnMessage(sc, WMSG_KEYUP, 'A', 0) && rec.keyUps == 1);
    CHECK(f.SetModal(b) == nullptr && f.Modal() == b);   // ord 610
    b->keys_return = 1;
    a->keys = 0;
    CHECK(f.OnMessage(sc, WMSG_KEYDOWN, 'B', 0) && b->keys == 1 && a->keys == 0);
    f.ClearModal();                                       // ord 563
    CHECK(f.Modal() == nullptr);
    f.SetPaused(true);                                    // paused: keys are ignored, except the Pause key (0x13)
    a->keys = 0;
    CHECK(!f.OnMessage(sc, WMSG_KEYDOWN, 'B', 0) && a->keys == 0);
    CHECK(f.OnMessage(sc, WMSG_KEYDOWN, 0x13, 0) && a->keys == 1);
    f.SetPaused(false);
    f.Lock(0);
    a->keys = 0;
    CHECK(!f.OnMessage(sc, WMSG_KEYDOWN, 'B', 0) && a->keys == 0); // locked
    f.Lock(1);
    f.RemoveKeyListener(&rec);
    f.OnWidgetDestroyed(a, sc);                           // ord 153
    CHECK(f.Preferred() == nullptr && f.GetFocus() == nullptr);
    f.SetFocusToWindow(sc);                               // ord 636
    CHECK(f.Preferred() == nullptr);

    // ---- CursorMgr / cursors
    CursorMgr& c = sc->Cursor();
    Cursor* arrow = CursorMgr::GlobalSet(1);
    CHECK(arrow && arrow->GetObjClassId() == CID_CURSOR && arrow->Id() == 1);  // ord 367
    CHECK(c.CurrentCursor() == arrow);                                         // Scene::Initialize selects set 1
    CHECK(c.SetCursor(2) == 1 && c.CurrentCursor() == CursorMgr::GlobalSet(2));
    CHECK(static_cast<ResCursor*>(CursorMgr::GlobalSet(2))->NativeHandle() == 0x7f02);
    CHECK(static_cast<ResCursor*>(CursorMgr::GlobalSet(0))->NativeHandle() == 0);
    CHECK(CursorMgr::GlobalSet(5) == nullptr);
    int before = c.CursorCount();
    TestCursor* t1 = new TestCursor(sc, 5);
    TestCursor* t2 = new TestCursor(sc, 0);
    CHECK(c.CursorCount() == before + 2);             // cursors register in their constructor (FUN_10013332)
    CursorPair pair(sc, t1, t2);                      // ord 9
    CHECK(c.CursorCount() == before + 3 && pair.Id() == 0);
    CHECK(pair.Handle(1, 2, 0) == 0x77 && t1->lastNum == 0 && pair.Selected() == t1 && t1->act == 0);
    CHECK(pair.Handle(1, 2, 3) == 0x77 && t2->lastNum == 3 && pair.Selected() == t2); // switches: t1 off, t2 on
    CHECK(t1->act == -1 && t2->act == 1);
    CHECK(pair.Activate(true) == 0 && t2->act == 2);  // ord 134: the selected cursor is activated
    c.SetCursorObject(t1);
    CHECK(c.CurrentCursor() == t1 && t1->act == 0);
    c.SetNumber(4);                                   // ord 616
    CHECK(c.Number() == 4);
    delete t2;                                        // leaves the CursorMgr
    CHECK(c.CursorCount() == before + 2);
    c.SetCursor(1);
    CursorMgr::SetUserFlag(0xabc);                    // ord 639 / 286
    CHECK(CursorMgr::GetUserFlag() == 0xabc);
    CursorMgr::RefreshPointer();                      // ord 208
    // capture
    CHECK(c.SetCapture(b) == nullptr && c.Captured() == b);   // ord 611
    CHECK(c.SetCapture(b) == b);
    c.ReleaseCapture();                                      // ord 564 -> WM_CAPTURECHANGED
    CHECK(c.Captured() == nullptr);
    c.SetCapture(b);
    c.OnWidgetDestroyed(b);                                  // ord 154
    CHECK(c.Captured() == nullptr);
    CHECK(!c.OnMessage(sc, 0x20, 0, 0));
    c.OnMessage(sc, WMSG_MOUSEMOVE, 0, intptr_t(uint32_t(uint16_t(30)) | (uint32_t(uint16_t(-20)) << 16)));
    CHECK(c.LastX() == 30 && c.LastY() == -20);              // ord 500 stores the position

    // ---- PickMgr
    PickMgr& p = sc->Picker();
    p.Hold();                                         // ord 136
    CHECK(p.HoldCount() == 1);
    p.Hold();
    p.Release();                                      // ord 570
    CHECK(p.HoldCount() == 1);
    p.SetHover(b);                                    // ord 609
    CHECK(p.Hover() == b && b->pressed == 1);
    p.SetHover(a);
    CHECK(b->pressed == 0 && a->pressed == 1 && p.Hover() == a);
    p.SetEnabled(false);                              // ord 181: clears the hover widget
    CHECK(!p.Enabled() && p.Hover() == nullptr && a->pressed == 0);
    p.SetEnabled(true);
    p.Release();
    PickMgr::RefreshPointer();                        // ord 560
    POINT far;
    far.x = far.y = 5000;
    CHECK(sc->PickWidget(&far) == nullptr); // ord 179 / 330: nothing there

    // ---- IdleTimer / Accel
    IdleTimer& idle = sc->Idle();
    g_queries = 0;
    g_now = 10000;
    idle.SetTimeout(500);                             // ord 642
    CHECK(idle.Enabled() && idle.Timeout() == 500 && idle.Count() == 0);
    idle.Tick(sc, 10400);
    CHECK(g_queries == 0);
    idle.Tick(sc, 10500);
    CHECK(g_queries == 1 && idle.Count() == 1);
    idle.Reset();                                     // input resets the count
    CHECK(idle.Count() == 0);
    g_queryAnswer = 0;                                // OnQuery() == 0 stops the timer
    idle.Tick(sc, 20000);
    CHECK(g_queries == 2 && !idle.Enabled());
    g_queryAnswer = 1;
    idle.SetTimeout(0);                               // 0 disables
    CHECK(!idle.Enabled());

    g_cmds.clear();
    sc->AddAccelerator('A', 0x700, 0, 1);
    CHECK(sc->Accelerators().Translate(sc, 'A', 0));
    sc->PushAccelerators(false);                      // ord 556: hides the table
    CHECK(sc->Accelerators().Depth() == 1 && !sc->Accelerators().Translate(sc, 'A', 0));
    sc->PopAccelerators();                            // ord 537
    CHECK(sc->Accelerators().Translate(sc, 'A', 0));
    sc->PushAccelerators(true);                       // pass-through: unknown keys fall to the table below
    sc->AddAccelerator('B', 0x701, 0, 0);
    CHECK(sc->Accelerators().Translate(sc, 'A', 0) && sc->Accelerators().Translate(sc, 'B', 0) && !sc->Accelerators().Translate(sc, 'C', 0));
    sc->PopAccelerators();
    CHECK(!sc->Accelerators().Translate(sc, 'B', 0));
    sc->PopAccelerators(); // nothing below: no-op
    int pending = 0;
    sc->Messages().Dispatch(sc);
    (void)pending;

    // ---- Scene extras
    CHECK(sc->GetResType(kNoRes) == 0 && sc->GetResObject(kNoRes) == nullptr && sc->GetIndexedResObject(kNoRes, 0) == nullptr);
    CHECK(sc->GetResType(MakeResId(100, 0x82, 3)) == 0);           // unbound slot
    POINT pts[2] = {{1, 2}, {-320, -240}};
    sc->ToScreen(pts, 2);                                          // ord 440
    CHECK(pts[0].x == 321 && pts[0].y == 242 && pts[1].x == 0 && pts[1].y == 0);
    sc->FromScreen(pts, 2);                                        // ord 441
    CHECK(pts[0].x == 1 && pts[1].y == -240);
    CHECK(sc->ContainsWidget(a) && sc->ContainsWidget(b));         // ord 413
    Node orphan(WidgetParent(static_cast<Widget*>(nullptr)), 0, 0);
    CHECK(!sc->ContainsWidget(&orphan));
    sc->EnableCount(0);                                            // ord 184
    CHECK(a->DisableCount() >= 1 || sc->RootWidget()->DisableCount() == 1);
    sc->EnableCount(1);
    CHECK(sc->HideCount(1) == 1);                                  // ord 532
    CHECK(sc->HideCount(0) == 0);
    CHECK(sc->MessageBox(0x1234, 0) == 0);                         // no string -> 0 (ord 446)
    CHECK(sc->SetPaletteRes(kNoRes) == 0);                         // ord 653: not a palette
    sc->SetRegistryPath("Software\\Other");                        // ord 622
    // dynamic resources with a deleter (ord 202)
    static int deleted = 0;
    deleted = 0;
    int* obj = new int(5);
    u32 id = sc->AddResource(obj, [](void* p) { ++deleted; delete static_cast<int*>(p); });
    CHECK((id >> 16) == 0xff80 && sc->GetResObject(id) == obj);    // ord 199 for dynamic ids
    sc->FreeDynamicResource(id);
    CHECK(deleted == 1 && sc->GetResObject(id) == nullptr);
    sc->FreeDynamicResource(id);                                   // second time: nothing
    CHECK(deleted == 1);
    // PlaySoundInfo (ord 536) with a missing resource fails cleanly
    moses::PLAYSOUNDINFO info;
    CHECK(sc->PlaySoundInfo(MakeResId(100, 0x85, 1), &info) == 0);
    // Cursor objects die with the scene (CursorMgr::DeleteCursors)
}

} // namespace

int RunAaronAppTests(int* checks) {
    g_checks = g_fail = 0;
    test_path_module();
    test_application_window();
    test_input_joystick();
    test_managers();
    moses::CAnimView::SetTimeSource(nullptr);
    std::printf("aaron-wp4 app: %d checks, %d failures\n", g_checks, g_fail);
    if (checks) *checks += g_checks;
    return g_fail;
}
