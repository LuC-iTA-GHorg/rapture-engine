// Application: the single application object, main loop. Package 3.
// Aaron Application ctor ord 52 0x1000f354, Run ord 711 0x1000156d, InitInstance 0x100107e1, MessageLoop 0x10010c6b,
// ServiceAnimViews 0x10011339, SetDisplayMode 0x10011420, GetModule ord 169 0x1000fe1c, WndProc ord 401 0x100003cf.
#include "aaron/Application.h"

#include "aaron/Path.h"
#include "aaron/Scene.h"
#include "moses/AnimView.h"
#include "moses/Mem.h"
#include "platform/Window.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>

namespace aaron {

static Application* g_app = nullptr; // DAT_10023cc8

Application* Application::GetApp() { return g_app; } // ord 130 0x10007ad5

// CAudioLib::SetSoundProc callback (Aaron 0x10011d.. AudioMgr): turns start/end notifications into scene messages
// (AMSG_SOUNDSTART/END to the listener given to PlaySoundSimple, AMSG_RESFREED to release the resource of a finished sound).
static void sound_thunk(void* data, u32 sndId, moses::SNDNOTIFY n, moses::PLAYSOUNDINFO* info) {
    Application* app = static_cast<Application*>(data);
    Scene* s = app ? app->GetScene() : nullptr;
    if (!s) return;
    SceneListener* l = s->SoundListenerFor(sndId, n == moses::SND_ENDED);
    if (n == moses::SND_STARTED) {
        if (l) s->PostMessage(AMSG_SOUNDSTART, sndId, reinterpret_cast<intptr_t>(l));
    } else {
        s->PostMessage(AMSG_RESFREED, sndId, 0);
        if (l) s->PostMessage(AMSG_SOUNDEND, sndId, reinterpret_cast<intptr_t>(l));
    }
    (void)info;
}

Application::Application(const APPINIT* init) {
    if (init) init_ = *init;
    // Original (0x1000f2d0): the APPINIT copy is clamped to 0x39c bytes; the requested bpp defaults to 16.
    // Original: the HWND/HDC members of Window stay 0 until CreateMainWindow.
    g_app = this;
    for (int i = 0; i < 2; ++i) joy_[i].reset(new JoystickDev(i)); // built in InitInstance in the original
}

Application::Application(const char* title, const char* sceneDll, u32 extra) { // ord 53 0x1000f486
    init_.size = 0x39c;
    if (title) std::snprintf(init_.name, sizeof init_.name, "%s", title);
    if (sceneDll) std::snprintf(init_.sceneDll, sizeof init_.sceneDll, "%s", sceneDll);
    init_.extra38c = extra;
    g_app = this;
    for (int i = 0; i < 2; ++i) joy_[i].reset(new JoystickDev(i));
}

Application::Application(const char* title, CreateSceneFn createSceneFn, u32 extra) { // ord 54 0x1000f667
    init_.size = 0x39c;
    if (title) std::snprintf(init_.name, sizeof init_.name, "%s", title);
    init_.createSceneFn = createSceneFn;
    init_.extra38c = extra; // (the original stores it at +0x3a4 after the copy = APPINIT+0x38c)
    g_app = this;
    for (int i = 0; i < 2; ++i) joy_[i].reset(new JoystickDev(i));
}

Application::~Application() {
    for (auto& j : joy_) j.reset(); // unsubscribe from broadcaster_ / timers_ while they still exist
    if (scene_) {
        Scene* s = scene_;
        scene_ = nullptr;
        Module* m = s->GetModule();
        s->Destroy();
        delete s;
        if (m) m->Release();
    }
    std::vector<Module*> mods;
    mods.swap(modules_); // ~Module calls ForgetModule: iterate over a copy
    for (Module* m : mods) m->Release();
    if (sceneModule_) { sceneModule_->Release(); sceneModule_ = nullptr; }
    for (ResArchive* a : archives_) a->Release();
    archives_.clear();
    if (g_app == this) g_app = nullptr;
}

// ---------------------------------------------------------------------------------------------
int Application::LoadString(u32 id, char* buf, int len) { // ord 275 0x1000027c (default: empty)
    (void)id;
    if (buf && len > 0) buf[0] = 0;
    return 0;
}

bool Application::OnSysCommand(u32 wParam) { return wParam == 0xF100; } // ord 524 0x100010ae (blocks the Alt menu)

int Application::ReportError(u32 code, void* p, void* q, int retry) { // ord 472 0x10000d8a
    (void)p; (void)q; (void)retry;
    lastError_ = code; // +0x3cc
    char text[256] = {0};
    LoadString(code & 0xffff, text, int(sizeof text));
    if (code == ERR_VIDEO_MODE) std::fprintf(stderr, "Aaron error 0x%08x: %d x %d: unsupported video mode\n", code, reqWidth_, reqHeight_);
    else std::fprintf(stderr, "Aaron error 0x%08x %s\n", code, text);
    return 0; // no message box in the port: never ask for a retry
}

bool Application::ParseArg(char opt, const char* value) { // ord 510 0x10001070
    if (opt == 0 || opt == ' ') { // bare argument = scene dll (the original passes a SPACE, see ParseCommandLine; Run() passes 0)
        std::snprintf(init_.sceneDll, sizeof init_.sceneDll, "%s", value ? value : "");
        return true;
    }
    return false;
}

bool Application::WndProc(u32 msg, uintptr_t wParam, intptr_t lParam, intptr_t* result) { // ord 401 0x100003cf
    intptr_t dummy = 0;
    if (!result) result = &dummy;
    *result = 0;
    switch (msg) {
    case AMSG_FREELIBRARY: // wParam = Module*
        if (wParam) reinterpret_cast<Module*>(wParam)->Release();
        return true;
    case AMSG_LOADSCENE: // posted scene change: wParam = Module*, lParam = arg
        PostLoadScene(reinterpret_cast<Module*>(wParam), uintptr_t(lParam));
        return true;
    case AMSG_PAUSE:
        PauseApp(wParam != 0);
        return true;
    case WMSG_COMMAND:
        *result = OnCommand(wParam, lParam);
        return true;
    case WMSG_SYSCOMMAND:
        *result = OnSysCommand(u32(wParam)) ? 1 : 0;
        return true;
    case WMSG_CLOSE: // WM_CLOSE -> OnQueryClose -> DestroyWindow -> WM_NCDESTROY: OnDestroy + PostQuitMessage
        if (OnQueryClose()) Quit();
        return true;
    case 0x11: // WM_QUERYENDSESSION: the answer of OnQueryClose (slot 13)
        *result = OnQueryClose() ? 1 : 0;
        return true;
    case 6: // WM_ACTIVATE: only forwarded to the application broadcaster
        NotifyApp(6, intptr_t(wParam));
        return true;
    case WMSG_ACTIVATEAPP:
        Activate(wParam != 0); // (the original also lowers / raises the thread priority by one step: not ported)
        return true;
    case 0x7e: // WM_DISPLAYCHANGE: the original resizes the window back to the requested w x h; the SDL window keeps its logical size
        return false;
    case 0x30f: // WM_QUERYNEWPALETTE
    case 0x311: // WM_PALETTECHANGED (ignored when this very window caused it: wParam = the window handle)
        if (msg == 0x311 && wParam == reinterpret_cast<uintptr_t>(handle_) && handle_) return true;
        if (scene_) scene_->RealizePalette(); // 8-bit only: a no-op in the port (the original invalidates the window when it changed)
        *result = 1;
        return true;
    case MMSG_JOY1MOVE: case MMSG_JOY1BUTTONDOWN: case MMSG_JOY1BUTTONUP:
        if (joy_[0]) joy_[0]->Dispatch(msg, wParam, lParam);
        return true;
    case MMSG_JOY2MOVE: case MMSG_JOY2BUTTONDOWN: case MMSG_JOY2BUTTONUP:
        if (joy_[1]) joy_[1]->Dispatch(msg, wParam, lParam);
        return true;
    case AMSG_ABOUT:
        OnAbout();
        return true;
    default:
        return Window::WindowProc(msg, wParam, lParam, result); // everything else goes to the window procedure body (ord 400)
    }
}

int Application::PauseApp(bool on) {
    bool before = timers_.Paused();
    int n = timers_.PauseCounter(on); // broadcasts (0xc04, 1/0) on the transitions
    bool after = timers_.Paused();
    if (before != after) {
        audio_.Lib().Pause(after ? 1 : 0);
        if (scene_) scene_->Pause(after ? 1 : 0); // CAnimView::Pause: shifts the animation timers on resume
    }
    return n;
}

void Application::Activate(bool active) {
    // Aaron 0x10013aaf: WM_ACTIVATEAPP(0x1c) -> pause counter (deactivated = pause); the Broadcaster is advised too
    // (Controllers suspend themselves, Aaron 0x10009544), and the audio lib is (de)activated.
    NotifyApp(int(WMSG_ACTIVATEAPP), active ? 1 : 0);
    audio_.Lib().Activate(active ? 1 : 0);
    PauseApp(!active);
}

void Application::NotifyApp(int code, intptr_t value) { // ord 496 0x10011941
    if (code == int(WMSG_ACTIVATEAPP)) {
        if (active_ == (value != 0)) return; // unchanged: not re-broadcast
        active_ = value != 0;
    }
    broadcaster_.Broadcast(code, value, &broadcaster_);
}

void Application::RegisterWindow(Window* w, bool dialog) { // 0x1000fc05
    if (std::find(windows_.begin(), windows_.end(), w) == windows_.end()) windows_.push_back(w);
    if (dialog && std::find(dialogWindows_.begin(), dialogWindows_.end(), w) == dialogWindows_.end()) dialogWindows_.push_back(w);
}

void Application::UnregisterWindow(Window* w) {
    windows_.erase(std::remove(windows_.begin(), windows_.end(), w), windows_.end());
    dialogWindows_.erase(std::remove(dialogWindows_.begin(), dialogWindows_.end(), w), dialogWindows_.end());
}

Window* Application::FindWindowByClass(const char* className) const {
    for (Window* w : windows_)
        if (w->handle_ && !w->destroyed_ && w->className_ == className) return w;
    return nullptr;
}

int Application::ForEachWindow(PtrArray::ForEachFn fn, void* a, void* b) { // ord 194 0x10010215
    int r = 1;
    std::vector<Window*> snap = windows_;
    for (size_t i = 0; r != 0 && i < snap.size(); ++i) r = fn(snap[i], a, b);
    return r;
}

int Application::ForEachScene(PtrArray::ForEachFn fn, void* a, void* b) { // ord 195 0x1001023c
    int r = 1;
    if (scene_) r = fn(scene_, a, b);
    std::vector<Window*> snap = windows_;
    for (size_t i = 0; r != 0 && i < snap.size(); ++i)
        if (snap[i]->GetScene()) r = fn(snap[i]->GetScene(), a, b);
    return r;
}

int Application::IsDialogMessage(u32 msg, uintptr_t wParam, intptr_t lParam) { // ord 543 0x10011261
    (void)msg; (void)wParam; (void)lParam;
    return 0; // the original discards the loop result and returns 0
}

void Application::InvalidateWindows(bool updateNow) { // ord 573 0x100112cb
    std::vector<Window*> snap = windows_;
    for (Window* w : snap) w->Invalidate(updateNow);
}

void Application::ResetDisplayMode() { moses::DrawSys().ChangeDisplayMode(nullptr, 0, 0, 0); } // ord 579 0x1001131a

Module* Application::GetModuleForFn(CreateSceneFn fn) { // ord 168 0x1000ffa7
    for (Module* m : modules_)
        if (m->HasCreateFn(fn)) return m;
    Module* m = new Module(fn); // ord 41: registers itself... in the port the cache is filled here
    m->AddRef();
    modules_.push_back(m);
    return m;
}

void Application::ForgetModule(Module* m) {
    modules_.erase(std::remove(modules_.begin(), modules_.end(), m), modules_.end());
}

void Application::SetRegistryPath(const char* path) { // ord 623 0x100113d6
    std::snprintf(init_.regKey, sizeof init_.regKey, "%s", path ? path : "");
}

void Application::SetBaseDir(const char* name) { // ord 626 0x100113f7
    baseDir_ = CombinePath(ExecutablePath(), name ? name : "");
}

void Application::SetTitle(const char* title) { // ord 670 0x10011540
    std::snprintf(init_.name, sizeof init_.name, "%s", title ? title : "");
    SetTitleText(init_.name);
}

void Application::SetStatusTitle(const char* text) { // ord 148 0x1000fba4
    std::string s = init_.name;
    s += " - "; // DAT_100230dc
    s += text ? text : "";
    SetTitleText(s.c_str());
}

static Application::MessageBoxHook g_mbHook;
void Application::SetMessageBoxHook(MessageBoxHook hook) { g_mbHook = std::move(hook); }

int Application::MessageBox(const char* text, u32 flags) { // ord 447 0x10010c3a
    if (g_mbHook) return g_mbHook(init_.name, text ? text : "", flags);
    std::fprintf(stderr, "[%s] %s\n", init_.name, text ? text : "");
    switch (flags & 0xf) { // the first button of each MB_* type
    case 2: return 3; // MB_ABORTRETRYIGNORE -> IDABORT
    case 3: return 6; // MB_YESNOCANCEL -> IDYES
    case 4: return 6; // MB_YESNO -> IDYES
    case 5: return 4; // MB_RETRYCANCEL -> IDRETRY
    default: return 1; // MB_OK / MB_OKCANCEL -> IDOK
    }
}

void Application::ParseCommandLine(char* p) { // ord 528 0x100110c7
    while (*p) {
        while (*p == ' ') ++p;
        char opt;
        char* arg;
        switch (*p) {
        case '\0':
            return;
        case '"':
            opt = ' ';
            arg = ++p;
            while (*p && *p != '"') ++p;
            break;
        case '-':
        case '/':
            opt = p[1];
            arg = p[1] ? p + 2 : p + 1; // QUIRK: "-" alone makes CharNext step over the terminator in the original
            p = arg;
            while (*p && *p != ' ') ++p;
            break;
        default:
            opt = ' ';
            arg = p;
            while (*p && *p != ' ') ++p;
            break;
        }
        bool more = *p != 0;
        if (more) *p++ = 0;
        ParseArg(opt, arg);
    }
}

// ---------------------------------------------------------------------------------------------
RegKey* Application::GetMachineKey() { // ord 357 0x100002a1
    if (!machineKey_) machineKey_.reset(new RegKey(RegKey::HKLM, init_.regKey));
    return machineKey_.get();
}

RegKey* Application::GetUserKey() { // ord 393
    if (!userKey_) userKey_.reset(new RegKey(RegKey::HKCU, init_.regKey));
    return userKey_.get();
}

Module* Application::GetModule(const char* dllName, const char* funcName, const char* baseDir) { // ord 169 0x1000fe1c
    (void)baseDir;
    if (!funcName || !*funcName) funcName = "CreateScene";
    for (Module* m : modules_)
        if (m->Matches(dllName, funcName)) return m;
    CreateSceneFn fn = FindScene(dllName, funcName);
    if (!fn) {
        ReportError(IsSceneModuleRegistered(dllName) ? ERR_SCENE_FN_NOT_FOUND : ERR_SCENE_DLL_NOT_FOUND, nullptr, nullptr, 0);
        return nullptr;
    }
    Module* m = new Module(dllName, funcName, fn);
    m->AddRef(); // the cache's reference
    modules_.push_back(m);
    return m;
}

ResArchive* Application::AcquireArchive(const char* path, const char* baseDir) {
    if (!path || !*path) return nullptr;
    std::string full = path;
    if (baseDir && *baseDir) {
        bool abs = full.size() > 1 && (full[1] == ':' || full[0] == '/' || full[0] == '\\');
        if (!abs) full = std::string(baseDir) + "/" + full;
    }
    for (char& c : full) if (c == '\\') c = '/';
    auto lower = [](std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; };
    std::string key = lower(full);
    for (ResArchive* a : archives_) {
        if (lower(a->FullPath()) == key) {
            a->AddRef();
            return a;
        }
    }
    ResArchive* a = new ResArchive(&audio_.Lib());
    if (!a->OpenPath(path, baseDir)) {
        delete a;
        return nullptr;
    }
    a->AddRef(); // cache
    archives_.push_back(a);
    a->AddRef(); // caller
    return a;
}

// ---------------------------------------------------------------------------------------------
bool Application::SetDisplayMode(int w, int h, int bpp) { // 0x10011420
    // Original: CDrawSys::ChangeDisplayMode + GetDeviceCaps checks; the port's window is always 640x480.
    if (!moses::DrawSys().ChangeDisplayMode(nullptr, u32(w), u32(h), u32(bpp))) {
        ReportError(ERR_VIDEO_MODE, nullptr, nullptr, 0);
        return false;
    }
    return true;
}

bool Application::CreateMainWindow(void* hParent, WININFO* info) { // ord 463 0x10000cdc
    (void)hParent;
    WININFO def;
    if (!info) info = &def;
    reqWidth_ = info->width;
    reqHeight_ = info->height;
    reqBpp_ = info->bpp;
    const char* title = info->title ? info->title : init_.name;
    if (!CreateNativeWindow(title, info->fullscreen)) return false;
    audio_.Lib().SetHostWindow(&plat_);
    if (!SetDisplayMode(reqWidth_, reqHeight_, reqBpp_)) return false;
    NotifyApp(int(WMSG_ACTIVATEAPP), 1); // the first WM_ACTIVATEAPP the original receives right after the window is created
    return true;
}

void Application::OnDestroy() { // ord 464 0x10000d71
    audio_.Lib().SoundStop(nullptr);
    ResetDisplayMode(); // FUN_1001131a (ord 579); then MosesFreeLibrary (done by Run())
    plat_.Destroy();
}

void Application::LoadScene(Module* module) { // ord 491 0x1000103e
    PostLoadScene(module, 0);
}

bool Application::InitInstance() { // 0x100107e1
    // RegKeys are lazy (GetUserKey/GetMachineKey). Broadcaster, TimerMgr, AudioMgr are members.
    audio_.Lib().SetSoundProc(&sound_thunk, this);
    if (init_.createSceneFn) {
        sceneModule_ = new Module(init_.sceneDll, "CreateScene", init_.createSceneFn);
        sceneModule_->AddRef();
    } else if (init_.sceneDll[0]) {
        sceneModule_ = GetModule(init_.sceneDll, "CreateScene", baseDir_.c_str());
        if (sceneModule_) sceneModule_->AddRef();
    }
    if (!sceneModule_) return false;
    if (!CreateMainWindow(nullptr, nullptr)) return false;
    LoadScene(sceneModule_); // vtbl[9]: executed at the start of the first loop iteration
    return true;
}

// Command line: `-x val` / `/x val` (option char + value), a bare argument is the scene dll (ParseArg(0, arg)).
static std::vector<std::string> split_cmdline(const char* s) {
    std::vector<std::string> out;
    std::string cur;
    bool inq = false, have = false;
    for (; s && *s; ++s) {
        char c = *s;
        if (c == '"') { inq = !inq; have = true; }
        else if (!inq && std::isspace((unsigned char)c)) {
            if (have) { out.push_back(cur); cur.clear(); have = false; }
        } else { cur += c; have = true; }
    }
    if (have) out.push_back(cur);
    return out;
}

int Application::Run(const char* cmdline) { // ord 711 0x1000156d
    moses::MosesInitLibrary();
    std::vector<std::string> args = split_cmdline(cmdline);
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a.size() >= 2 && (a[0] == '-' || a[0] == '/')) {
            char opt = a[1];
            std::string val = a.size() > 2 ? a.substr(2) : std::string();
            bool nextUsed = false;
            if (val.empty() && i + 1 < args.size() && args[i + 1][0] != '-' && args[i + 1][0] != '/') {
                val = args[i + 1];
                nextUsed = true;
            }
            if (ParseArg(opt, val.c_str()) && nextUsed) ++i;
        } else {
            ParseArg(0, a.c_str());
        }
    }
    exitCode_ = 0;
    if (InitInstance()) MessageLoop();
    else exitCode_ = 1;
    OnDestroy();
    RegKey::FlushAll();
    moses::MosesFreeLibrary();
    return exitCode_;
}

void Application::Quit() { quit_ = true; } // ord 196 0x10000263

int Application::MessageLoop() { // 0x10010c6b
    while (RunFrame()) {}
    return exitCode_;
}

void Application::ServiceAnimViews() { // 0x10011339
    if (timers_.Paused()) return;
    if (scene_) scene_->RunFrame(moses::CAnimView::Now());
}

bool Application::RunFrame() {
    if (!PumpPlatformEvents()) quit_ = true;
    for (auto& j : joy_)
        if (j) j->Poll(moses::CAnimView::Now()); // joySetCapture replacement: MM_JOYnMOVE / BUTTON messages every 10 ms
    if (scenePending_) { // AMSG_LOADSCENE: always at the start of an iteration, never inside a scene callback
        Module* m = pendingModule_;
        uintptr_t arg = pendingArg_;
        pendingModule_ = nullptr;
        scenePending_ = false;
        if (m) {
            if (!LoadSceneNow(m, arg)) quit_ = true; // no scene: nothing to show
            m->Release();
        }
    }
    if (quit_) return false;
    ServiceAnimViews();
    PresentIfDirty();
    RegKey::FlushIfDue(moses::CAnimView::Now());
    if (sleepMs_ && !::Window::NoSleep()) // Sleep(sleepMs); --fast N sleeps N times less (platform/Window.h)
        std::this_thread::sleep_for(std::chrono::microseconds(int64_t(sleepMs_ * 1000.0 / ::Window::TimeScale())));
    return !quit_;
}

} // namespace aaron
