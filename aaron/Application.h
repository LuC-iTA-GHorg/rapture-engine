// Application: the single application object (Aaron ord 52, aaron.md sec. 2.1-2.3).
// Shoot.exe derives HostApp (0x668 bytes) from it and overrides LoadString / OnDestroy / LoadScene and adds
// its own virtuals (CreateObject = popup factory, FeedKeySequences) after the 14 below.
//
// MAIN LOOP (deliberate port of Application::MessageLoop 0x10010c6b - see README):
//
//   int Run(cmdline):  MosesInitLibrary; ParseCommandLine; InitInstance; MessageLoop; MosesFreeLibrary
//   MessageLoop:       while (RunFrame()) {}
//   RunFrame:          PumpPlatformEvents()                    // PeekMessage/Translate/Dispatch
//                      handle pending AMSG_LOADSCENE
//                      ServiceAnimViews()                      // scene->RunFrame(now): msg queue, timers, idle,
//                                                              //   CAnimView::Service(), CAudioLib::Service()
//                      PresentIfDirty()                        // ONE present per iteration (difference #1)
//                      pace: SDL_Delay(sleepMs) / FrameTimer   // original: Sleep(sleepMs) when no message was pending
//
// There are no threads: the only other thread is the platform audio callback, which never calls into the engine.
#pragma once

#include "aaron/Joystick.h"
#include "aaron/Managers.h"
#include "aaron/Module.h"
#include "aaron/RegKey.h"
#include "aaron/ResArchive.h"
#include "aaron/Window.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aaron {

class Application : public Window {
public:
    // Aaron ord 52 0x1000f354. Copies APPINIT (name, scene dll / createSceneFn, registry key).
    explicit Application(const APPINIT* init);
    // Aaron ord 53 0x1000f486: Application(title, sceneDll, extra): builds an APPINIT (size 0x39c, name, sceneDll, +0x38c = extra) and
    // calls the common initialiser (0x1000f2d0). Aaron ord 54 0x1000f667: Application(title, createSceneFn, extra): same with the
    // createSceneFn (+0x184) instead of a DLL name.
    Application(const char* title, const char* sceneDll, u32 extra);
    Application(const char* title, CreateSceneFn createSceneFn, u32 extra);
    ~Application() override;                               // Aaron ord 81 0x1000f843   slot 0 (-> 0x1000e782 Window::~Window)

    // The one Application (Aaron ord 130 0x10007ad5, global DAT_10023cc8). Shoot.dll reaches the Shoot.exe app through it.
    static Application* GetApp();

    // ---- virtual interface, vtable 0x1001e0f0 (slots 0..13), same order ------------------------------------
    // slot 1: GetClassId() = CID_APPLICATION (inherited declaration from Window)
    u32 GetClassId() const override { return CID_APPLICATION; } // Aaron ord 388 0x100118e0 (slot 1)
    void OnSize(int w, int h) override { (void)w; (void)h; }    // slot 2: Aaron ord 522 0x1000f2c0 (the Window one is inherited)
    virtual void Reserved2() {}                            // (older name of slot 2)
    // slot 3: Application::WndProc replaces Window::WindowProc (ord 400 -> 401)
    bool OnWindowMessage(u32 msg, uintptr_t wParam, intptr_t lParam, intptr_t* result) override { return WndProc(msg, wParam, lParam, result); }
    // slot 3  Aaron ord 401 0x100003cf: application-level messages. Handles AMSG_FREELIBRARY(0xc00), AMSG_LOADSCENE(0xc02),
    // AMSG_PAUSE(0xc04) (Broadcaster), WMSG_COMMAND(0x111) -> OnCommand, WMSG_SYSCOMMAND(0x112) -> OnSysCommand,
    // WMSG_CLOSE(0x10) -> OnQueryClose -> quit, WMSG_ACTIVATEAPP(0x1c) -> broadcaster + audio Activate.
    // Returns true if handled; *result gets the return value.
    virtual bool WndProc(u32 msg, uintptr_t wParam, intptr_t lParam, intptr_t* result);
    // slot 4  Aaron ord 275 0x1000027c: localized string. Shoot.exe overrides (loads id + 0x5006 from its resources;
    // port: a string table inside Shoot.exe's port). Default: empty.
    virtual int LoadString(u32 id, char* buf, int len);
    // slot 5: Aaron ord 461/462/555 0x10003540 (the shared "return 0" thunk)
    virtual int OnCommand(uintptr_t wParam, intptr_t lParam) { (void)wParam; (void)lParam; return 0; }
    virtual bool OnSysCommand(u32 wParam);                 // slot 6  Aaron ord 524 0x100010ae: returns (wParam == 0xF100) = block the Alt menu
    // slot 7  Aaron ord 463 0x10000cdc: creates the main window. Port: Window::CreateNativeWindow + SetDisplayMode.
    virtual bool CreateMainWindow(void* hParent, WININFO* info);
    virtual void OnDestroy();                              // slot 8  Aaron ord 464 0x10000d71 (restore video mode, MosesFreeLibrary). Shoot: saves WaveVol/MidiVol first.
    // slot 9  Aaron ord 491 0x1000103e: request a scene change to `module` (posted, executed at the next frame).
    // Shoot.exe overrides it to load the volumes (AudioMgr::LoadVolumes) first. Title scene: GetModule("Shoot.DLL","CreateScene",0) + LoadScene.
    virtual void LoadScene(Module* module);
    virtual void Reserved10() {}                           // slot 10 (reserved, returns 0): Aaron ord 494/502/504 0x100118f0
    // slot 11 Aaron ord 472 0x10000d8a: shows an error (codes AaronError) and returns nonzero to RETRY.
    // Port: stderr + SDL_ShowSimpleMessageBox; string ids from LoadString.
    virtual int ReportError(u32 code, void* p, void* q, int retry);
    virtual bool ParseArg(char opt, const char* value);    // slot 12 Aaron ord 510 0x10001070 (`-x val` / `/x val`; bare argument = scene dll)
    virtual bool OnQueryClose() { return true; }           // slot 13: Aaron ord 517/518 0x1000e360 (the shared "return 1" thunk)

    // ---- exported non-virtual API --------------------------------------------------------------------------
    // Aaron ord 711 0x1000156d Application::Run(hInst, hPrev, cmdline, show) -> exit code. Port: Run(cmdline).
    int Run(const char* cmdline);
    void Quit();                                           // Aaron ord 196 0x10000263 (PostQuitMessage / WM_CLOSE): ends the loop
    // Aaron ord 169 0x1000fe1c GetModule(dll, function, baseDir) -> cached Module (AddRef'd by the cache).
    // Port: looks the pair up in the static scene registry (RegisterScene) - no LoadLibrary. nullptr + ReportError(ERR_SCENE_*) if missing.
    Module* GetModule(const char* dllName, const char* funcName, const char* baseDir);
    RegKey* GetMachineKey();                               // Aaron ord 357 0x100002a1 (HKLM, lazy)
    RegKey* GetUserKey();                                  // Aaron ord 393 0x10010338 (HKCU, lazy)
    // Aaron ord 168 0x1000ffa7 (Ghidra's "CMFCToolBarButton" parameter is a false match): the cached Module whose create function is `fn`
    // (Module::HasCreateFn, ord 416), or a new Module(fn) (ord 41) which registers itself in the application's cache.
    Module* GetModuleForFn(CreateSceneFn fn);
    void ForgetModule(Module* m);                          // ~Module: leaves the cache (the cache's reference is NOT released)
    // Aaron ord 623 0x100113d6: lstrcpy(+0x1a0, path): the registry path used by the keys created LATER (existing keys keep theirs - kept).
    void SetRegistryPath(const char* path);
    // Aaron ord 626 0x100113f7: baseDir (+0x29f) = CombinePath(<exe path>, name) (Module::BuildPath of the exe module, ord 439).
    void SetBaseDir(const char* name);
    // Aaron ord 670 0x10011540: lstrcpy(+0x1c, title) then SetTitleText. Aaron ord 148 0x1000fba4: the title "<name> - <text>".
    void SetTitle(const char* title);
    void SetStatusTitle(const char* text);
    // Aaron ord 447 0x10010c3a: MessageBox(owner window, text, title = application name, flags) -> IDOK (1), IDCANCEL (2), IDABORT (3),
    // IDRETRY (4), IDIGNORE (5), IDYES (6), IDNO (7). Port: SetMessageBoxHook() routes it to the host's dialog (SDL_ShowSimpleMessageBox in
    // HostApp); without a hook the text goes to stderr and the first button of the MB_* type is "pressed".
    using MessageBoxHook = std::function<int(const char* title, const char* text, u32 flags)>;
    static void SetMessageBoxHook(MessageBoxHook hook);
    int MessageBox(const char* text, u32 flags);
    // Aaron ord 528 0x100110c7: tokenises `cmdline` IN PLACE and calls ParseArg for each token: `-x` / `/x` -> ParseArg('x', rest of the
    // token after the option letter); a quoted token or a bare word -> ParseArg(' ', token) (the opt is a SPACE, not 0).
    void ParseCommandLine(char* cmdline);
    // Aaron ord 579 0x1001131a: CDrawSys::ChangeDisplayMode(null, 0, 0, 0) = restore the desktop mode.
    void ResetDisplayMode();

    // ---- application-level window lists (WP-4) -------------------------------------------------------------
    // Original: PtrArrays at +0x3ec (scenes), +0x400 (windows), +0x414 (dialog windows), +0x428 (modules), +0x43c (archives).
    void RegisterWindow(Window* w, bool dialog);           // Aaron 0x1000fc05 (FUN_1000fc05)
    void UnregisterWindow(Window* w);                      // Aaron 0x1000e565 (part of Window::Detach)
    Window* FindWindowByClass(const char* className) const;
    // Aaron ord 194 0x10010215: fn(window, a, b) over the window list (+0x400) while fn returns nonzero; Aaron ord 195 0x1001023c: the same
    // over the scene list (+0x3ec; the port has one scene per window - the application's own, then the registered windows').
    int ForEachWindow(PtrArray::ForEachFn fn, void* a, void* b);
    int ForEachScene(PtrArray::ForEachFn fn, void* a, void* b);
    int WindowCount() const { return int(windows_.size()); }
    int DialogWindowCount() const { return int(dialogWindows_.size()); }
    // Aaron ord 543 0x10011261: IsDialogMessage over the dialog windows (+0x414). The original ALWAYS returns 0 (the loop result is
    // discarded); the port has no native dialogs, so it returns 0 without doing anything.
    int IsDialogMessage(u32 msg, uintptr_t wParam, intptr_t lParam);
    // Aaron ord 573 0x100112cb: Window::Invalidate(updateNow) on every window of the list (+0x400).
    void InvalidateWindows(bool updateNow);
    // Aaron ord 496 0x10011941 (the application broadcaster's Broadcast override): WM_ACTIVATEAPP (0x1c) is dropped when the active flag
    // (+0x18 of the broadcaster) already has that value; otherwise the flag is updated, then Broadcaster::Broadcast.
    void NotifyApp(int code, intptr_t value);
    bool AppActive() const { return active_; }
    JoystickDev* Joystick(int id) const { return (id == 0 || id == 1) ? joy_[id].get() : nullptr; } // Application+0x3dc / +0x3e0
    // Shown for AMSG_ABOUT (0xc07, Ctrl+Shift+Alt+V): the original opens the About dialog (resource 0x65). Hosts may override.
    virtual void OnAbout() {}

    // ---- port pieces of the loop (all documented in the README) ---------------------------------------------
    bool InitInstance();                                   // Aaron 0x100107e1: RegKeys, Broadcaster, TimerMgr, scene Module, window, first scene
    int MessageLoop();                                     // Aaron ord ... 0x10010c6b
    bool RunFrame();                                       // one iteration; false when the application must end
    void ServiceAnimViews();                               // Aaron 0x10011339: skipped while the app TimerMgr is paused
    bool SetDisplayMode(int w, int h, int bpp);            // Aaron 0x10011420 -> CDrawSys::ChangeDisplayMode (no-op) ; false if unsupported
    // Shared archive cache (key = normalized path). Returns an AddRef'd ResArchive or nullptr. Aaron ord 170 0x1001008e: CombinePath(baseDir
    // (default +0x29f), name), then the archive of the cache whose path equals it (AddRef) or a new ResArchive(path).
    ResArchive* AcquireArchive(const char* path, const char* baseDir);

    AudioMgr& Audio() { return audio_; }                   // +0x3e8
    TimerMgr& Timers() { return timers_; }                 // +0x3e4
    Broadcaster& AppBroadcaster() { return broadcaster_; } // +0x3d8 (AMSG_PAUSE, activate)
    const APPINIT& Init() const { return init_; }
    int ReqBpp() const { return reqBpp_; }
    u32 SleepMs() const { return sleepMs_; }
    void SetSleepMs(u32 ms) { sleepMs_ = ms; }             // +0x3c8
    bool IsQuitting() const { return quit_; }
    // port addition (pkg3), Aaron 0x10013b12 + WM_ACTIVATEAPP / Pause key: counted application pause. on=true pauses
    // (TimerMgr counter, broadcast 0xc04, audio Pause, CAnimView::Pause of the scene), on=false resumes. Returns the counter.
    int PauseApp(bool on);
    // WMSG_ACTIVATEAPP entry (deactivated -> pause, activated -> resume)
    void Activate(bool active);

protected:
    APPINIT init_;                                         // +0x18
    int reqWidth_ = 640, reqHeight_ = 480, reqBpp_ = 16;   // +0x3a8..
    std::unique_ptr<RegKey> userKey_, machineKey_;         // +0x3c0 / +0x3c4
    u32 sleepMs_ = 1;                                      // +0x3c8 (port default 1: keeps the CPU idle between frames)
    Broadcaster broadcaster_;                              // +0x3d8
    TimerMgr timers_;                                      // +0x3e4
    AudioMgr audio_;                                       // +0x3e8
    std::vector<Module*> modules_;                         // +0x428 (cache, AddRef'd)
    std::vector<ResArchive*> archives_;                    // +0x43c (shared, AddRef'd)
    std::string baseDir_;                                  // +0x29f
    Module* sceneModule_ = nullptr;
    bool quit_ = false;
    int exitCode_ = 0;
    bool active_ = false;                                  // +0x18 of the broadcaster: WM_ACTIVATEAPP state (initially 0, like the original)
    u32 lastError_ = 0;                                    // +0x3cc
    std::vector<Window*> windows_, dialogWindows_;         // +0x400 / +0x414
    std::unique_ptr<JoystickDev> joy_[2];                  // +0x3dc / +0x3e0
};

} // namespace aaron
