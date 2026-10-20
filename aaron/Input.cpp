// Controller + input sources, key translation. Package 3.
// Aaron Controller::Controller ord 34 0x10009162, Suspend ord 183 0x1000932e, EnableSource ord 587 0x100096c7,
// SetBounds ord 607 0x1000975d, KeyboardSource ord 37 0x1000a132, MouseSource ord 38 0x1000a7e6.
#include "aaron/Input.h"

#include "aaron/Application.h"
#include "aaron/Joystick.h"
#include "aaron/Scene.h"

#include <cmath>

namespace aaron {

// ---------------------------------------------------------------------------------------------
// Key translation. SDL keycodes are spelled out numerically (SDLK_SCANCODE_MASK = 1<<30) because the engine does not
// include SDL headers (README sec. 2).
u32 SdlKeyToVk(int k) {
    const int M = 1 << 30;
    if (k >= 'a' && k <= 'z') return u32(k - 'a' + 'A'); // letters -> upper case ASCII
    if (k >= '0' && k <= '9') return u32(k);
    switch (k) {
    case 8: return AVK_BACK;            // SDLK_BACKSPACE
    case 9: return AVK_TAB;
    case 13: return AVK_RETURN;
    case 27: return AVK_ESCAPE;
    case 32: return AVK_SPACE;
    case 42: return AVK_MULTIPLY;       // SDLK_ASTERISK  (main-row '*': port convenience for keyboards without keypad)
    case 43: return AVK_ADD;            // SDLK_PLUS
    case 45: return AVK_SUBTRACT;       // SDLK_MINUS
    case M | 72: return AVK_PAUSE;      // SDL_SCANCODE_PAUSE
    case M | 79: return AVK_RIGHT;
    case M | 80: return AVK_LEFT;
    case M | 81: return AVK_DOWN;
    case M | 82: return AVK_UP;
    case M | 85: return AVK_MULTIPLY;   // SDL_SCANCODE_KP_MULTIPLY
    case M | 86: return AVK_SUBTRACT;   // KP_MINUS
    case M | 87: return AVK_ADD;        // KP_PLUS
    case M | 88: return AVK_RETURN;     // KP_ENTER
    case M | 224: case M | 228: return AVK_CONTROL; // LCTRL / RCTRL
    case M | 225: case M | 229: return AVK_SHIFT;   // LSHIFT / RSHIFT
    case M | 226: case M | 230: return AVK_MENU;    // LALT / RALT
    default: break;
    }
    if (k >= (M | 58) && k <= (M | 69)) return AVK_F1 + u32(k - (M | 58)); // F1..F12
    return 0;
}

u32 SdlModToAmod(int m) {
    u32 r = 0;
    if (m & 0x0003) r |= AMOD_SHIFT; // KMOD_LSHIFT | KMOD_RSHIFT
    if (m & 0x00c0) r |= AMOD_CTRL;  // KMOD_LCTRL | KMOD_RCTRL
    if (m & 0x0300) r |= AMOD_ALT;   // KMOD_LALT | KMOD_RALT
    return r;
}

// ---------------------------------------------------------------------------------------------
// Controller tables (Aaron data 0x1001df04..0x1002304c, read from the binary). Index = min(mode, 3).
//   mode 0: vertical only {up, down}; 1: horizontal only {right, left}; 2: 4 directions; 3 (and 4: absolute): 8 directions.
static const u8 kDirTable[8] = {1, 2, 4, 8, 5, 6, 10, 9};                  // DAT_1001df24
static const u32 kDirFirst[4] = {0, 2, 0, 0};                              // DAT_1001df2c
static const u32 kDirEnd[4] = {2, 4, 4, 8};                                // DAT_1001df3c
static const int kAngDiv[4] = {180, 180, 90, 45};                          // DAT_1001df04
static const int kAngOff[4] = {0, 90, 45, 22};                             // DAT_1001df14
static const u8 kAngDir[4][8] = {                                          // PTR_DAT_1002304c
    {1, 2, 0, 0, 0, 0, 0, 0},
    {4, 8, 0, 0, 0, 0, 0, 0},
    {4, 1, 8, 2, 0, 0, 0, 0},
    {4, 5, 1, 9, 8, 10, 2, 6},
};

Controller::Controller(Scene* scene, u32 mode, u32 user) : scene_(scene), mode_(mode), user_(user) {
    modeIdx_ = mode < 4 ? mode : 3; // 0x10009971
    // SetBounds(CDrawView::GetDisplayBox)
    moses::RECT box;
    scene->GetDisplayBox(box);
    bounds_ = box;
    Application* app = Application::GetApp();
    if (app) { // subscribes to WM_ACTIVATEAPP / pause (Application+0x3d8 and TimerMgr +0x3e4)
        app->AppBroadcaster().AddListener(this);
        app->Timers().AddListener(this);
    }
}

Controller::~Controller() {} // sources are owned (unique_ptr); ~Subscriber unlinks from the broadcasters

void Controller::AddSource(InputSource* src) { sources_.emplace_back(src); }

void Controller::Emit(u32 code, int x, int y) { scene_->OnInput(code, x, y, user_); }

// 0x10009544: WMSG_ACTIVATEAPP -> Suspend(active); AMSG_PAUSE -> Suspend(value == 0)
void Controller::OnAdvise(int code, intptr_t value, Broadcaster* src) {
    (void)src;
    if (code == int(WMSG_ACTIVATEAPP)) Suspend(value != 0 ? 1 : 0);
    else if (code == int(AMSG_PAUSE)) Suspend(value == 0 ? 1 : 0);
}

int Controller::Suspend(int on) { // 0x1000932e. Suspend(0) suspends, Suspend(!=0) resumes (counter)
    bool trans = false;
    if (on == 0) {
        int before = suspended_++;
        trans = before == 0;
    } else if (suspended_ != 0) {
        --suspended_;
        trans = suspended_ == 0;
    }
    if (trans) {
        for (auto& s : sources_) s->Reactivate(on != 0); // 0x10009b72 (ord 469): enabled sources register / unregister (joystick)
        ReleaseAll(); // FUN_10009639 (ord 576)
        Emit(IN_ONOFF, suspended_ == 0 ? 1 : 0, 0);
    }
    return suspended_;
}

void Controller::ReleaseAll() { // ord 576 0x10009639
    for (auto& s : sources_) s->Reset();
    SetDirection(0);
    for (u32 b = 0x10; int(b) < 0x801; b <<= 1) SetButton(b, false);
    state_ = 0;
}

void Controller::EnableSource(int type, u32 flags) { // 0x100096c7
    for (auto& s : sources_) s->Enable(s->Type() == type ? flags : 0);
}

void Controller::SetBounds(int l, int t, int r, int b) { // ord 608 0x1000972a
    RECT rc;
    rc.left = l;
    rc.top = t;
    rc.right = r;
    rc.bottom = b;
    SetBounds(&rc);
}

void Controller::SetSourceFlags(int type, u32 flags) { // ord 185 0x10009422
    for (auto& s : sources_)
        if (s->Type() == type) s->Enable(flags);
}

InputSource* Controller::FindSource(int type) const { // ord 291 0x10009481
    InputSource* r = nullptr;
    for (const auto& s : sources_) {
        r = s.get();
        if (r->Type() == type) break;
    }
    return r; // QUIRK: the last source when none matched
}

void Controller::SetBounds(const RECT* r) { // 0x1000975d
    bounds_ = *r;
    for (auto& s : sources_) s->OnBounds(bounds_);
}

bool Controller::DirectionAllowed(u32 dir) const { // 0x100094e0
    if (dir == 0) return true;
    for (u32 i = kDirFirst[modeIdx_]; i < kDirEnd[modeIdx_]; ++i)
        if (dir == kDirTable[i]) return true;
    return false;
}

void Controller::SetDirection(u32 dir) { // 0x10009827
    if (DirectionAllowed(dir) && dir != (state_ & 0xf)) {
        state_ = (state_ & 0xfffffff0u) | dir;
        if (mode_ < 4) Emit(state_ & 0xf, 0, 0);
    }
}

void Controller::SetButton(u32 bit, bool down) { // 0x1000958d
    if (!down) {
        if (state_ & bit) {
            state_ &= ~bit;
            Emit(bit | IN_RELEASE, 0, 0);
        }
    } else if (!(state_ & bit)) {
        state_ |= bit;
        if (suspended_ == 0) Emit(bit, 0, 0);
    }
}

void Controller::SetPosition(int x, int y, int srcW, int srcH) { // 0x100099a7
    if (srcW <= 0 || srcH <= 0) return;
    int nx = bounds_.left + int(uint32_t(x) * uint32_t(bounds_.Width()) / uint32_t(srcW));
    int ny = bounds_.top + int(uint32_t(y) * uint32_t(bounds_.Height()) / uint32_t(srcH));
    if (nx != posX_ || ny != posY_) {
        posX_ = nx;
        posY_ = ny;
        if (mode_ == 4) Emit(IN_ABSOLUTE, nx, ny);
    }
}

void Controller::SetDirectionFromVector(int dx, int dy, int threshold) { // 0x10009897
    u32 dir = 0;
    double dist = std::sqrt(double(dx) * dx + double(dy) * dy);
    if (dist > double(threshold)) {
        // screen y grows downwards; the original angle is counter-clockwise from +x (TODO: x87 code not decoded, assumed)
        int ang = int(std::atan2(double(-dy), double(dx)) * 180.0 / 3.14159265358979323846);
        int a = ((ang + 360) % 360 + kAngOff[modeIdx_]) % 360;
        dir = kAngDir[modeIdx_][(a / kAngDiv[modeIdx_]) & 7];
    }
    SetDirection(dir);
}

void Controller::OnKey(u32 vk, bool down) {
    if (suspended_) return;
    for (auto& s : sources_) s->OnKey(vk, down);
}

void Controller::OnMouse(u32 msg, int x, int y, u32 keys) {
    if (suspended_) return;
    for (auto& s : sources_) s->OnMouse(msg, x, y, keys);
}

// ---------------------------------------------------------------------------------------------
InputSource::InputSource(Controller* owner) : owner_(owner) { // 0x10009a41
    owner->AddSource(this);
}

void InputSource::Enable(u32 flags) { // ord 588 0x10009b27
    if (flags != flags_) {
        flags_ = flags;
        if (!owner_->Suspended()) Activate(flags != 0);
    }
}

JoystickSource::JoystickSource(Controller* owner, u32 joyId) : InputSource(owner), id_(joyId) {} // 0x10009d92
JoystickSource::~JoystickSource() {                                                           // 0x10009ac4 (+ the safety the port needs)
    if (dev_) dev_->RemoveListener(this);
}
void JoystickSource::OnJoystick(int dx, int dy, u32 buttons) { (void)dx; (void)dy; (void)buttons; } // (hook kept for source compatibility)

u32 JoystickSource::PickDevice(u32 preferred, u32 startFrom) { // ord 197 0x10009dd1
    Application* app = Application::GetApp();
    auto have = [app](u32 i) { return i < 2 && app && app->Joystick(int(i)) != nullptr; };
    if (!have(preferred) || !JoystickDev::Probe(int(preferred))) {
        for (u32 i = startFrom; i < 2; ++i)
            if (have(i) && JoystickDev::Probe(int(i))) return i;
    }
    return preferred;
}

void JoystickSource::Activate(bool on) { // ord 449 0x10009e6f
    InputSource::Activate(on);
    if (id_ & 0x8000) id_ = PickDevice(id_ & 0xf, 0); // QUIRK: the picked id replaces the stored one for good (the 0x8000 bit is lost)
    Application* app = Application::GetApp();
    JoystickDev* dev = (id_ < 2 && app) ? app->Joystick(int(id_)) : nullptr;
    if (!dev) return;
    if (!on || !JoystickDev::Probe(int(id_))) {
        dev->RemoveListener(this);
        dev_ = nullptr;
    } else {
        dev->AddListener(this);
        dev_ = dev;
        unsigned r[4];
        dev->GetRange(r); // xmin, ymin, xmax, ymax
        rangeX_ = int(r[2] - r[0]);
        rangeY_ = int(r[3] - r[1]);
        cx_ = int((r[2] + r[0]) / 2);
        cy_ = int((r[3] + r[1]) / 2);
        thr_ = (int(r[2]) - cx_) / 3;
    }
}

void JoystickSource::OnMessage(u32 msg, uintptr_t wParam, intptr_t lParam) { // 0x10009f80
    int x = int(uint32_t(lParam) & 0xffff), y = int((uint32_t(lParam) >> 16) & 0xffff);
    bool down = false;
    switch (msg) {
    case MMSG_JOY1MOVE:
    case MMSG_JOY2MOVE:
        if (flags_ & 1) {
            owner_->SetDirectionFromVector(x - cx_, y - cy_, thr_);
            owner_->SetPosition(x, y, rangeX_, rangeY_);
        }
        break;
    case MMSG_JOY1BUTTONDOWN:
    case MMSG_JOY2BUTTONDOWN:
        down = true;
        [[fallthrough]];
    case MMSG_JOY1BUTTONUP:
    case MMSG_JOY2BUTTONUP:
        if (flags_ & 2) owner_->SetButton(u32((wParam & 0xf00) >> 4), down);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------------
// KeyboardSource 0x1000a132. NOTE (original): the constructor argument only decides whether the default key map is
// installed; the source's own flags (+0x1c: bit0 directions, bit1 buttons) stay 0 until Controller::EnableSource.
KeyboardSource::KeyboardSource(Controller* owner, u32 flags) : InputSource(owner) {
    if (flags != 0) {
        MapKey(1, 0x26, true);    // up
        MapKey(4, 0x27, true);    // right
        MapKey(2, 0x28, true);    // down
        MapKey(8, 0x25, true);    // left
        MapKey(0x10, 0x11, true); // Ctrl
        MapKey(0x20, 0x12, true); // Alt
        MapKey(0x40, 0x20, true); // Space
        MapKey(0x80, 0x10, true); // Shift
    }
}

void KeyboardSource::MapKey(u32 flags, u32 vk, bool replace) { // 0x1000a4eb
    if ((flags & 0xfffffff0u) == 0) {
        if (!owner_->DirectionAllowed(flags & 0xf)) return;
    } else {
        if (!(flags >= 0x10 && flags <= 0x800 && (flags & (flags - 1)) == 0)) return; // one button bit
    }
    if (replace) {
        for (u32 i = 0; i < 256; ++i) {
            if (map_[i].flags == flags) {
                if (map_[i].down) OnKey(i, false);
                map_[i].flags = 0;
            }
        }
    }
    map_[vk & 0xff].flags = flags;
    map_[vk & 0xff].down = false;
}

u32 KeyboardSource::FindKey(u32 flags) const { // ord 297 0x1000a24c
    for (u32 i = 0; i < 256; ++i)
        if (map_[i].flags == flags) return i;
    return 0;
}

void KeyboardSource::Reset() {                     // ord 578 0x1000a4a9
    for (Entry& e : map_) e.down = false;
    dirs_ = 0;
}

void KeyboardSource::OnKey(u32 vk, bool down) { // 0x1000a2ff
    Entry& e = map_[vk & 0xff];
    if (e.flags == 0) return;
    if (down) {
        if (e.down) return;
        e.down = true;
        if ((e.flags & 0xfffffff0u) == 0) {
            dirs_ |= e.flags;
            if (flags_ & 1) owner_->SetDirection(dirs_);
        } else if (flags_ & 2) {
            owner_->SetButton(e.flags, true);
        }
    } else {
        if (!e.down) return;
        e.down = false;
        if ((e.flags & 0xfffffff0u) == 0) {
            dirs_ &= ~e.flags;
            if (flags_ & 1) owner_->SetDirection(dirs_);
        } else if (flags_ & 2) {
            owner_->SetButton(e.flags, false);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// MouseSource 0x1000a7e6. The original's second constructor argument is a RECT* (null = the controller's bounds); the
// header keeps a `flags` argument which is ignored (Shoot passes 0).
MouseSource::MouseSource(Controller* owner, u32 flags) : InputSource(owner) {
    (void)flags;
    SetBounds(owner->Bounds());
}

void MouseSource::SetBounds(const RECT& r) { // 0x1000aae3
    b_ = r;
    w_ = r.right - r.left;
    h_ = r.bottom - r.top;
    cx_ = (r.right + r.left) / 2;
    cy_ = (r.bottom + r.top) / 2;
    thr_ = (r.right - cx_) / 3;
}

void MouseSource::Activate(bool on) { // ord 451 0x1000a896
    InputSource::Activate(on);
    Scene* sc = owner_->GetScene();
    if (!on || !sc) return;
    OnBounds(owner_->Bounds()); // the controller's current bounds (+0x30..+0x3c of the Controller)
    if ((flags_ & 1) && owner_->Mode() != 4) sc->Cursor().SetPos(cx_, cy_);
}

void MouseSource::OnBounds(const RECT& r) { // 0x1000aa81: only in absolute mode (4) does the controller push its bounds
    if (owner_->Mode() == 4) SetBounds(r);
}

void MouseSource::on_move(int x, int y) { // 0x1000a632
    if (x == lastX_ && y == lastY_) return;
    lastX_ = x;
    lastY_ = y;
    if (x < b_.left || b_.right < x || y < b_.top || b_.bottom < y) {
        // outside the rectangle: the original clamps the cursor back (CursorMgr warp, FUN_10013812)
        int cx = x > b_.right ? b_.right : x;
        cx = cx < b_.left ? b_.left : cx;
        int cy = y > b_.bottom ? b_.bottom : y;
        cy = cy < b_.top ? b_.top : cy;
        Scene* sc = owner_->GetScene();
        if (sc) sc->Cursor().SetPos(cx, cy);
    } else {
        owner_->SetDirectionFromVector(x - cx_, y - cy_, thr_);
        owner_->SetPosition(x - b_.left, y - b_.top, w_, h_);
    }
}

void MouseSource::OnMouse(u32 msg, int x, int y, u32 keys) { // 0x1000a98d
    if (keys & 0x08) return; // Ctrl held (GetKeyState(VK_CONTROL) < 0): ignored
    switch (msg) {
    case WMSG_MOUSEMOVE:
        if (flags_ & 1) on_move(x, y);
        break;
    case WMSG_LBUTTONDOWN:
        if (flags_ & 2) owner_->SetButton(0x10, true);
        break;
    case WMSG_LBUTTONUP:
        if (flags_ & 2) owner_->SetButton(0x10, false);
        break;
    case WMSG_RBUTTONDOWN:
        if (flags_ & 2) owner_->SetButton(0x20, true);
        break;
    case WMSG_RBUTTONUP:
        if (flags_ & 2) owner_->SetButton(0x20, false);
        break;
    default:
        break;
    }
}

} // namespace aaron
