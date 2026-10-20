// aaron::Window: host of the scenes, platform event translation, scene switching. Package 3.
// Aaron Window ord 50, Window::GetClientSize ord 350 0x1000e946, Window::LoadScene ord 527 0x1000eeeb.
#include "aaron/Window.h"

#include "aaron/Application.h"
#include "aaron/Input.h"
#include "aaron/RegKey.h"
#include "aaron/Scene.h"

#include <algorithm>
#include <cstring>

namespace aaron {

// Win32 MK_* flags
static constexpr u32 kMkL = 0x01, kMkR = 0x02, kMkShift = 0x04, kMkCtrl = 0x08, kMkM = 0x10;

Window::Window() {} // ord 50 0x1000e750

Window::~Window() { // ord 79 0x1000e782 -> Detach 0x1000e565
    if (attachedToApp_) {
        Application* app = Application::GetApp();
        if (app && app != this) app->UnregisterWindow(this);
        attachedToApp_ = false;
    }
    // The scene is owned by the window (README sec. 5).
    if (scene_) {
        Scene* s = scene_;
        scene_ = nullptr;
        Module* m = s->GetModule();
        s->Destroy();
        delete s;
        if (m) m->Release();
    }
    if (pendingModule_) {
        pendingModule_->Release();
        pendingModule_ = nullptr;
    }
    plat_.Destroy();
}

bool Window::CreateNativeWindow(const char* title, bool fullscreen) {
    if (!plat_.Create(title ? title : "Aaron", fullscreen)) return false;
    title_ = title ? title : "Aaron";
    if (!handle_) Attach(&plat_, 0);
    destroyed_ = false;
    return true;
}

void Window::GetClientSize(POINT* size) const { // ord 350 0x1000e946
    // Original: GetClientRect; for an iconic window GetWindowPlacement().rcNormalPosition. Port: the logical 640 x 480 surface.
    if (IsIconic() && placement_.right > placement_.left && placement_.bottom > placement_.top) {
        size->x = placement_.right - placement_.left;
        size->y = placement_.bottom - placement_.top;
        return;
    }
    size->x = ::Window::kWidth;
    size->y = ::Window::kHeight;
}

int Window::GetClientWidth() const { POINT p; GetClientSize(&p); return p.x; }   // ord 395 0x1000e9c0
int Window::GetClientHeight() const { POINT p; GetClientSize(&p); return p.y; }  // ord 285 0x1000e8f4

void Window::Attach(void* nativeHandle, int accel) { // ord 672 0x1000f1fb -> FUN_1000e47c / FUN_1000fc05
    handle_ = nativeHandle;
    destroyed_ = false;
    Application* app = Application::GetApp();
    if (app && app != this) {
        app->RegisterWindow(this, accel != 0);
        attachedToApp_ = true;
        attachedAccel_ = accel != 0;
    }
}

void Window::DestroyNative() { // 0x1000e4c9
    if (!destroyed_ && handle_) {
        destroyed_ = true;
        if (plat_.IsOpen()) plat_.Destroy();
        handle_ = nullptr;
    }
}

bool Window::Create(void* parent, const WINCREATE& wc) { // ord 166 0x1000e829
    WINCREATE c = wc;
    if (parent) c.style |= 0x40000000; // WS_CHILD
    if (!CreateNativeWindow(c.title ? c.title : "", false)) return false;
    SetPos(c.x, c.y, c.width, c.height);
    if (c.showCmd) Show(c.showCmd);
    return true;
}

bool Window::CreateInParent(void* parent) { // ord 165 0x1000e7d4
    WINCREATE c;
    c.height = ::Window::kHeight;
    c.width = ::Window::kWidth;
    c.style = 0x50000000; // WS_CHILD | WS_VISIBLE
    return Create(parent, c);
}

void Window::SetPos(int x, int y, int w, int h) { // ord 445 0x1000eeb4
    if (!handle_) return;
    placement_.left = x;
    placement_.top = y;
    placement_.right = x + w;
    placement_.bottom = y + h;
}

void Window::Resize(int w, int h) { // ord 691 0x1000f217
    if (!handle_) return;
    placement_.right = placement_.left + w;
    placement_.bottom = placement_.top + h;
}

void Window::PostMessage(u32 msg, uintptr_t wParam, intptr_t lParam) { // ord 542 0x1000efea
    posted_.push_back(PostedMsg{msg, wParam, lParam});
}

intptr_t Window::SendMessage(u32 msg, uintptr_t wParam, intptr_t lParam) { // ord 593 0x1000f0f2
    intptr_t res = 0;
    OnWindowMessage(msg, wParam, lParam, &res);
    return res;
}

void Window::DrainPosted() {
    // messages posted while draining are delivered by the next pump (PeekMessage loop semantics of a posted queue)
    std::deque<PostedMsg> batch;
    batch.swap(posted_);
    while (!batch.empty()) {
        PostedMsg m = batch.front();
        batch.pop_front();
        intptr_t res = 0;
        OnWindowMessage(m.msg, m.wParam, m.lParam, &res);
    }
}

void Window::Invalidate(bool updateNow) { // ord 572 0x1000f010
    (void)updateNow; // UpdateWindow: the main loop presents once per iteration anyway
    if (scene_) scene_->Paint(nullptr, nullptr);
}

void Window::Show(int cmd) { // ord 687 0x1000f24a
    showCmd_ = cmd;
    placement_.showCmd = u32(cmd);
}

WINPLACEMENT Window::GetPlacement() const {
    WINPLACEMENT wp = placement_;
    wp.length = 0x2c;
    wp.showCmd = u32(showCmd_);
    if (wp.right <= wp.left || wp.bottom <= wp.top) {
        wp.right = wp.left + ::Window::kWidth;
        wp.bottom = wp.top + ::Window::kHeight;
    }
    return wp;
}

void Window::SetPlacement(const WINPLACEMENT& wp) {
    placement_ = wp;
    showCmd_ = int(wp.showCmd);
}

void Window::ShowPlacement(RegKey* key, int showCmd) { // ord 583 0x1000f041
    if (key) {
        WINPLACEMENT wp;
        if (key->GetBinary("Placement", &wp, sizeof wp, nullptr)) {
            if (showCmd == 3 || showCmd == 2) wp.showCmd = u32(showCmd);
            SetPlacement(wp);
            return;
        }
    }
    Show(showCmd);
}

void Window::SavePlacement(RegKey* key) { // ord 585 0x1000f0b2
    WINPLACEMENT wp = GetPlacement();
    if (key) key->SetBinary("Placement", &wp, sizeof wp);
}

void Window::SetTitleText(const char* text) { // ord 667 0x1000f1d4
    if (!handle_) return;
    title_ = text ? text : "";
    if (plat_.IsOpen()) plat_.SetTitle(title_.c_str());
}

bool Window::SetIcon(u32 resId) { // ord 641 0x1000f173
    return resId != 0 && LoadIconResource(resId & 0xffff);
}

bool Window::ActivateExisting(const char* className) { // ord 638 0x1000f118
    Application* app = Application::GetApp();
    if (!className || !app) return false;
    auto match = [className](const Window* w) { return w->handle_ && !w->destroyed_ && w->className_ == className; };
    if (match(app)) return true;
    return app->FindWindowByClass(className) != nullptr;
}

// Window::WindowProc 0x1000ea12 (ord 400)
bool Window::WindowProc(u32 msg, uintptr_t wParam, intptr_t lParam, intptr_t* result) {
    intptr_t dummy = 0;
    if (!result) result = &dummy;
    Scene* sc = scene_;
    auto toFocus = [&]() { if (sc) sc->Focus().OnMessage(sc, msg, wParam, lParam); };
    auto sceneMsg = [&]() -> bool { // switchD_..._caseD_9
        if (msg < AMSG_SCENE_BASE || !sc) return false;
        *result = sc->OnMessage(msg, wParam, lParam);
        return true;
    };
    if (msg > 0x102) {
        if (msg > 0x138) {
            if (msg < 0x216) {
                if (msg == 0x215) { // WM_CAPTURECHANGED
                    if (!sc) return false;
                    sc->Cursor().OnMessage(sc, msg, wParam, lParam);
                    return false;
                }
                if (msg >= 0x200 && msg < 0x20a) { // WM_MOUSEMOVE .. WM_MBUTTONDBLCLK (FUN_1000d09d: screen -> scene coordinates)
                    if (!sc) return false;
                    int x = int(int16_t(uint32_t(lParam) & 0xffff)) - sc->Origin().x;
                    int y = int(int16_t((uint32_t(lParam) >> 16) & 0xffff)) - sc->Origin().y;
                    sc->Cursor().OnMessage(sc, msg, wParam, intptr_t(uint32_t(uint16_t(x)) | (uint32_t(uint16_t(y)) << 16)));
                    return false;
                }
            } else if (msg == AMSG_LOADSCENE) {
                LoadSceneNow(reinterpret_cast<Module*>(wParam), uintptr_t(lParam));
                return true;
            }
            return sceneMsg();
        }
        if (msg == 0x138) return false; // WM_CTLCOLORSTATIC: no native controls
        switch (msg) {
        case 0x104: case 0x105: case 0x106: // WM_SYSKEYDOWN / SYSKEYUP / SYSCHAR
            toFocus();
            return false;
        case 0x111: // WM_COMMAND
            if (!sc) return false;
            return sc->OnMessage(msg, wParam, lParam) != 0;
        case 0x133: case 0x134: case 0x135: case 0x136: // WM_CTLCOLOR*
            return false;
        default:
            return sceneMsg();
        }
    }
    if (msg >= 0x100) { // WM_KEYDOWN / KEYUP / CHAR
        toFocus();
        return false;
    }
    bool handled = false;
    switch (msg) {
    case 6: // WM_ACTIVATE
        break;
    case 7: case 8: // WM_SETFOCUS / KILLFOCUS
        toFocus();
        break;
    case 0xf: // WM_PAINT
        if (sc) {
            sc->Paint(nullptr, nullptr);
            *result = 1;
            handled = true;
        }
        break;
    case 0x14: // WM_ERASEBKGND
        handled = sc != nullptr;
        break;
    case 0x20: // WM_SETCURSOR
        if (sc && wParam == reinterpret_cast<uintptr_t>(handle_) && (uint32_t(lParam) & 0xffff) == 1) {
            handled = true;
            *result = 1;
            sc->Cursor().OnMessage(sc, msg, wParam, lParam);
        }
        break;
    case 0x47: // WM_WINDOWPOSCHANGED (lParam = SWP_* flags in the port)
        if ((uint32_t(lParam) & 1) == 0) {
            POINT s;
            GetClientSize(&s);
            OnSize(s.x, s.y);
            if (sc) sc->Resize(s.x, s.y);
        }
        break;
    case 0x82: // WM_NCDESTROY
        if (!destroyed_) {
            destroyed_ = true;
            handle_ = nullptr;
            if (deleteOnDestroy_) delete this;
        }
        break;
    default:
        return sceneMsg();
    }
    return handled;
}

// Window::LoadScene 0x1000eeeb.
// Original quirk preserved: the scene's stored `arg` (Scene+0x530) is NOT the caller's argument but the PREVIOUS
// module (or, when the same module is reloaded, the previous scene's arg): the new scene can tell where it came from.
// The caller's `arg` is what the original passes to the scene's entry point (our CreateSceneFn takes only the host).
bool Window::LoadSceneNow(Module* module, uintptr_t arg) {
    (void)arg;
    Module* oldModule = nullptr;
    uintptr_t newArg = 0;
    if (scene_) {
        oldModule = scene_->GetModule();
        newArg = (oldModule == module) ? scene_->Arg() : reinterpret_cast<uintptr_t>(oldModule);
        Scene* old = scene_;
        scene_ = nullptr;
        old->Destroy();   // 0x1000cac3: SetRedraw(0,1), OnClose, initialized = 0, delete
        delete old;
    }
    if (module) module->AddRef(); // reference held by the scene (released when the scene is replaced)
    Scene* s = module ? module->CreateScene(this, newArg) : nullptr;
    bool ok = false;
    if (s) {
        scene_ = s; // FUN_1000e430: registered with the window before Initialize (OnCreate may query the host)
        if (s->Initialize()) {
            ok = true;
        } else {
            scene_ = nullptr;
            delete s;
            s = nullptr;
            Application* app = Application::GetApp();
            if (app) app->ReportError(0x80000002u, nullptr, module, 0);
        }
    }
    if (!ok && module) module->Release();
    if (oldModule) oldModule->Release(); // FUN_1000b3c9
    return ok;
}

void Window::PostLoadScene(Module* module, uintptr_t arg) {
    if (pendingModule_) pendingModule_->Release();
    pendingModule_ = module;
    if (module) module->AddRef();
    pendingArg_ = arg;
    scenePending_ = true;
}

// ---------------------------------------------------------------------------------------------
bool Window::PumpPlatformEvents() {
    bool alive = true;
    DrainPosted();
    if (!plat_.IsOpen()) { // headless (tests): nothing to poll
        Application* a = Application::GetApp();
        return !(a && a->IsQuitting());
    }
    InputEvent ev;
    while (plat_.PollEvent(ev)) {
        switch (ev.type) {
        case InputEvent::kQuit: {
            Application* app = Application::GetApp();
            if (app && app->WndProc(WMSG_CLOSE, 0, 0, nullptr)) {
                if (app->IsQuitting()) alive = false;
            } else if (!app) {
                alive = false;
            }
            break;
        }
        case InputEvent::kMouseMove:
        case InputEvent::kMouseDown:
        case InputEvent::kMouseUp: {
            u32 msg = WMSG_MOUSEMOVE, flag = 0;
            if (ev.type != InputEvent::kMouseMove) {
                bool down = ev.type == InputEvent::kMouseDown;
                if (ev.button == 1) { msg = down ? WMSG_LBUTTONDOWN : WMSG_LBUTTONUP; flag = kMkL; }
                else if (ev.button == 3) { msg = down ? WMSG_RBUTTONDOWN : WMSG_RBUTTONUP; flag = kMkR; }
                else if (ev.button == 2) { msg = down ? WMSG_MBUTTONDOWN : WMSG_MBUTTONUP; flag = kMkM; }
                else break;
                if (down) mouseKeys_ |= flag; else mouseKeys_ &= ~flag;
            }
            u32 keys = (mouseKeys_ & ~(kMkShift | kMkCtrl)) | ((curMods_ & AMOD_SHIFT) ? kMkShift : 0) |
                       ((curMods_ & AMOD_CTRL) ? kMkCtrl : 0);
            if (scene_) {
                const POINT& o = scene_->Origin();
                scene_->HandleMouse(msg, ev.x - o.x, ev.y - o.y, keys);
            }
            break;
        }
        case InputEvent::kKeyDown:
        case InputEvent::kKeyUp: {
            curMods_ = SdlModToAmod(ev.mod);
            u32 vk = SdlKeyToVk(ev.key);
            // the modifier key itself: the mod bits of the event may not include it yet on key-down
            if (vk == AVK_SHIFT) { if (ev.type == InputEvent::kKeyDown) curMods_ |= AMOD_SHIFT; else curMods_ &= ~u32(AMOD_SHIFT); }
            if (vk == AVK_CONTROL) { if (ev.type == InputEvent::kKeyDown) curMods_ |= AMOD_CTRL; else curMods_ &= ~u32(AMOD_CTRL); }
            if (vk == AVK_MENU) { if (ev.type == InputEvent::kKeyDown) curMods_ |= AMOD_ALT; else curMods_ &= ~u32(AMOD_ALT); }
            if (vk && scene_) scene_->HandleKey(ev.type == InputEvent::kKeyDown ? WMSG_KEYDOWN : WMSG_KEYUP, vk, curMods_);
            break;
        }
        case InputEvent::kText:
            if (scene_) scene_->HandleKey(WMSG_CHAR, u32((unsigned char)ev.text), curMods_);
            break;
        default:
            break;
        }
    }
    Application* app = Application::GetApp();
    if (app && app->IsQuitting()) alive = false;
    return alive;
}

void Window::PresentIfDirty() {
    if (!scene_ || !scene_->Initialized()) return;
    if (!scene_->IsDirty()) return;
    if (scene_->PresentTo(plat_.Framebuffer()) && plat_.IsOpen()) plat_.Present();
}

} // namespace aaron
