// aaron::Window: the host window of the scenes (Aaron ord 50, aaron.md sec. 2.1). In the original
// the Application IS-A Window (Application : Window : RObject); same here.
//
// Port: owns the platform window (::Window = SDL window + 640x480 framebuffer, source/platform/Window.h),
// translates its InputEvents into the Windows-style messages Aaron's managers expect, and presents
// the active scene's surface once per main-loop iteration.
//
// NAME CLASH: this class is `aaron::Window`; the platform class is `::Window`. Always spell
// `aaron::Window` in game code.
#pragma once

#include "aaron/Module.h"
#include "aaron/RObject.h"
#include "platform/Window.h"

#include <deque>
#include <string>
#include <vector>

namespace aaron {

class Scene;

class RegKey;

class Window : public RObject {
public:
    Window();                                    // Aaron ord 50 0x1000e750 (all fields 0)
    ~Window() override;                          // Aaron ord 79 0x1000e782 -> Detach (0x1000e565): leaves the application's window list, hides, destroys
    virtual u32 GetClassId() const { return CID_WINDOW; } // Aaron ord 387 0x1000f2b0 (vtable slot 1)
    // vtable slot 2: Aaron ord 522 0x1000f2c0 (empty): the client area changed size (called by WM_WINDOWPOSCHANGED).
    virtual void OnSize(int w, int h) { (void)w; (void)h; }
    // vtable slot 3: the window procedure body. Window: ord 400 0x1000ea12 (WindowProc below); Application replaces it with WndProc
    // (ord 401), which falls back to Window::WindowProc for everything it does not handle. Returns true when handled (no DefWindowProc).
    virtual bool OnWindowMessage(u32 msg, uintptr_t wParam, intptr_t lParam, intptr_t* result) { return WindowProc(msg, wParam, lParam, result); }
    // Aaron ord 400 0x1000ea12 Window::WindowProc(msg, wParam, lParam, *result). Port notes:
    //   * WM_PAINT -> scene->Paint(null, null) (the port repaints from the framebuffer, there is no HDC); WM_ERASEBKGND swallowed;
    //   * keys 0x100-0x102 / 0x104-0x106 and WM_SETFOCUS/KILLFOCUS -> FocusMgr::OnMessage; mouse 0x200-0x209 -> lParam (screen = window
    //     coordinates here) minus the scene origin -> CursorMgr::OnMessage; WM_SETCURSOR (HTCLIENT) and WM_CAPTURECHANGED -> CursorMgr;
    //   * WM_COMMAND -> scene->OnMessage(0x111, ...) (its result is the "handled" value); 0xc02 -> LoadSceneNow(Module*, arg);
    //     any other message >= 0x1400 -> *result = scene->OnMessage(...);
    //   * WM_WINDOWPOSCHANGED (0x47): lParam carries the SWP_* flags instead of a WINDOWPOS*; without SWP_NOSIZE (1) -> OnSize(w, h) and
    //     scene->Resize(w, h);   WM_NCDESTROY (0x82): marks the window destroyed (the original also deletes it - see SetDeleteOnDestroy);
    //   * WM_CTLCOLOR* (0x133-0x136, 0x138): no native controls in the port -> not handled.
    bool WindowProc(u32 msg, uintptr_t wParam, intptr_t lParam, intptr_t* result);
    // Original WM_NCDESTROY does `delete this` for windows created by the framework. The port only does it on request.
    void SetDeleteOnDestroy(bool on) { deleteOnDestroy_ = on; }
    bool IsDestroyed() const { return destroyed_; }
    // Port helper: the AMOD_* modifier bits of the last key event (the original asks GetKeyState).
    u32 KeyMods() const { return curMods_; }

    // ---- WP-4: the Win32 plumbing of the original Window class (Aaron ord 50..., unused by the games) ------------------------
    // SDL2 has one window per process created by the platform layer (platform/Window.h); a "native handle" is therefore only a token:
    // NativeHandle() is non-null while the window exists (the platform window's address for CreateNativeWindow, any value given to Attach()).
    void* NativeHandle() const { return handle_; }
    // Aaron ord 672 0x1000f1fb -> FUN_1000e47c: stores the handle (+8), "GetDC" (+0xc), the back pointer, and registers the window in the
    // application's window list (+0x400) and, if `accel` != 0, in the dialog list (+0x414).
    void Attach(void* nativeHandle, int accel);
    // Aaron 0x1000e4c9 (FUN_1000e4c9): DestroyWindow unless already destroyed. Port: destroys the platform window.
    void DestroyNative();
    // Aaron ord 166 0x1000e829 Window::Create(parent, WINCREATE*): CreateWindowEx + Attach + SetPos(x, y, width, height) + Show(showCmd) if nonzero.
    // `parent` != null adds WS_CHILD (kept in wc.style only). Port: CreateNativeWindow (a 640x480 SDL window); geometry and show state are
    // only recorded (SDL's logical size is fixed). Returns false if the platform window cannot be created.
    bool Create(void* parent, const WINCREATE& wc);
    // Aaron ord 165 0x1000e7d4: Create(parent, style WS_CHILD|WS_VISIBLE (0x50000000), size = the parent's client rect) - here 640 x 480.
    bool CreateInParent(void* parent);
    // Aaron ord 395 0x1000e9c0 / ord 285 0x1000e8f4: client width / height; an iconic window reports its restored size (IsIconic ->
    // GetWindowPlacement) - in the port the client area is always 640 x 480 unless minimised.
    int GetClientWidth() const;
    int GetClientHeight() const;
    // Aaron ord 445 0x1000eeb4: SetWindowPos(x, y, w, h, SWP_NOZORDER) if the window exists. Port: recorded in the placement.
    void SetPos(int x, int y, int w, int h);
    // Aaron ord 691 0x1000f217: SetWindowPos(0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER).
    void Resize(int w, int h);
    // Aaron ord 542 0x1000efea: PostMessage to this window: queued, delivered by PumpPlatformEvents through OnWindowMessage.
    void PostMessage(u32 msg, uintptr_t wParam, intptr_t lParam);
    // Aaron ord 593 0x1000f0f2: SendMessage: calls OnWindowMessage at once; returns the result (0 when not handled).
    intptr_t SendMessage(u32 msg, uintptr_t wParam, intptr_t lParam);
    // Aaron ord 572 0x1000f010: InvalidateRect(null, FALSE) (+ UpdateWindow if `updateNow`): the whole scene is repainted.
    void Invalidate(bool updateNow);
    // Aaron ord 687 0x1000f24a: ShowWindow(cmd) + UpdateWindow. Port: records the SW_* state (the SDL window's visibility belongs to the
    // platform layer - hidden in --autoplay).
    void Show(int cmd);
    int ShowState() const { return showCmd_; }
    // Aaron ord 583 0x1000f041: restores the saved placement ("Placement" binary value of `key`, 44 bytes) or, if there is none (or `key` is
    // null), plain ShowWindow(showCmd); a saved placement takes `showCmd` only if it is 2 (minimised) or 3 (maximised); then UpdateWindow.
    void ShowPlacement(RegKey* key, int showCmd);
    // Aaron ord 585 0x1000f0b2: GetWindowPlacement -> `key`->SetBinary("Placement", 44 bytes).
    void SavePlacement(RegKey* key);
    WINPLACEMENT GetPlacement() const;
    void SetPlacement(const WINPLACEMENT& wp);
    bool IsIconic() const { return showCmd_ == 2 || showCmd_ == 6 || showCmd_ == 7; }
    // Aaron ord 667 0x1000f1d4: SetWindowText if the window exists.
    void SetTitleText(const char* text);
    const std::string& TitleText() const { return title_; }
    // Aaron ord 641 0x1000f173: LoadIcon(hInstance, MAKEINTRESOURCE(id)) + WM_SETICON(ICON_BIG). There are no Win32 icon resources in the
    // port: LoadIconResource() is the hook (default: no icon -> returns false, exactly the original's "icon not found" result).
    bool SetIcon(u32 resId);
    virtual bool LoadIconResource(u32 resId) { (void)resId; return false; }
    // Aaron ord 638 0x1000f118 (static): FindWindow(className) -> bring it to the front (SetForegroundWindow, restore if minimised) -> true.
    // Port: only windows of this process can be found (class names set with SetClassName); no cross-process lookup.
    static bool ActivateExisting(const char* className);
    // Port counterpart of Window::RegisterClass (0x1000e600; the class name used by FindWindow).
    void SetClassName(const char* name) { className_ = name ? name : ""; }
    const std::string& ClassName() const { return className_; }

    // ---- window ------------------------------------------------------------------------------------------
    // Creates the platform window (port of Application::CreateMainWindow / Window::RegisterClass: full screen
    // style 0x82080000 in the original; here a 640x480 logical window, fullscreen toggled with F11/Alt+Enter).
    bool CreateNativeWindow(const char* title, bool fullscreen);   // also Attach()es the platform window
    ::Window& Platform() { return plat_; }
    // Aaron ord 350 0x1000e946 Window::GetClientSize(POINT* size): size->x = width, size->y = height (640 x 480).
    void GetClientSize(POINT* size) const;

    // ---- scenes ------------------------------------------------------------------------------------------
    Scene* GetScene() const { return scene_; }
    // Window::LoadScene (0x1000eeeb; Aaron ord 527 / 697 - TODO: ord 697 appears in Shoot.exe notes as "LoadScene(module)"):
    // destroys the current scene (Scene::Destroy, delete), creates the new one with Module::CreateScene(this, arg),
    // Scene::Initialize(), releases the old Module. Normally reached through AMSG_LOADSCENE.
    bool LoadSceneNow(Module* module, uintptr_t arg);
    // Posts AMSG_LOADSCENE: the change happens at the start of the next main-loop iteration (never from inside
    // a scene callback) - this is what Application::LoadScene does.
    void PostLoadScene(Module* module, uintptr_t arg);

    // ---- loop pieces (called by Application::RunFrame) ---------------------------------------------------
    // Drains the platform event queue (::Window::PollEvent). Returns false after a kQuit event.
    // Translation: kMouseMove/Down/Up -> Scene::HandleMouse(WMSG_*, x - origin.x, y - origin.y, keys);
    //              kKeyDown/Up -> Scene::HandleKey(WMSG_KEYDOWN/UP, SdlKeyToVk(key), SdlModToAmod(mod));
    //              kText -> WMSG_CHAR. Window-close -> Application::OnQueryClose().
    bool PumpPlatformEvents();
    // If the scene's view is dirty: view.PresentTo(framebuffer) + ::Window::Present(). Once per iteration.
    void PresentIfDirty();

protected:
    ::Window plat_;
    Scene* scene_ = nullptr;
    Module* pendingModule_ = nullptr;
    uintptr_t pendingArg_ = 0;
    bool scenePending_ = false;
    void* handle_ = nullptr;      // +8  (HWND)
    bool destroyed_ = false;      // +0x14 "window destroyed" flag
    bool deleteOnDestroy_ = false;
    int showCmd_ = 1;             // SW_*
    WINPLACEMENT placement_;      // rcNormalPosition etc.
    std::string title_, className_;
    struct PostedMsg { u32 msg; uintptr_t wParam; intptr_t lParam; };
    std::deque<PostedMsg> posted_;
    bool attachedToApp_ = false;
    bool attachedAccel_ = false;
    void DrainPosted();           // delivers the queued PostMessage()s (called at the start of PumpPlatformEvents)
    u32 mouseKeys_ = 0; // button state for MK_* style flags (MK_LBUTTON 1, MK_RBUTTON 2, MK_SHIFT 4, MK_CONTROL 8, MK_MBUTTON 0x10)
    u32 curMods_ = 0;   // AMOD_* bits of the last key event (port)

    friend class Application;
};

} // namespace aaron
