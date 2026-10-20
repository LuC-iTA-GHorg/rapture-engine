// Controller + input sources (aaron.md sec. 2.8). A Controller turns keyboard / mouse / joystick
// events into `scene->OnInput(code, x, y, user)` (codes: InputCode in Types.h).
//
// Port wiring: aaron::Window translates platform InputEvents to Windows-style messages and calls
// Scene::HandleKey / Scene::HandleMouse; the Scene forwards them to every live Controller
// (Controller::OnKey / OnMouse), which fan them out to their enabled sources.
#pragma once

#include "aaron/RObject.h"

#include <memory>
#include <vector>

namespace aaron {

class Scene;
class Controller;

// Platform key (SDL_Keycode, as delivered in InputEvent::key) -> Windows virtual-key code.
// Letters/digits map to their ASCII upper case, arrows/F-keys/Ctrl/Shift/Alt to the AVK_* values.
u32 SdlKeyToVk(int sdlKeycode);
// SDL_Keymod bits -> AMOD_* bits.
u32 SdlModToAmod(int sdlMod);

// ---------------------------------------------------------------------------------------------
// InputSource: base of the three sources (class owned by its Controller).
class InputSource {
public:
    // The constructor attaches the source to `owner`, which takes ownership (as the original: Shoot only does
    // `new KeyboardSource(controller, 1)` and drops the pointer). Aaron ord 35 0x10009a41.
    explicit InputSource(Controller* owner);
    virtual ~InputSource() = default;                      // Aaron ord 71 0x10009ac4 (removes itself from the controller's source array)
    virtual void OnKey(u32 vk, bool down) { (void)vk; (void)down; }
    virtual void OnMouse(u32 msg, int x, int y, u32 keys) { (void)msg; (void)x; (void)y; (void)keys; }
    virtual void OnJoystick(int dx, int dy, u32 buttons) { (void)dx; (void)dy; (void)buttons; }
    // Aaron ord 588 0x10009b27 (InputSource::Enable): stores the flags word (bit0 = direction/move, bit1 = buttons) - 0 disables the
    // source - and, if the flags changed and the controller is not suspended, calls Activate(flags != 0) (vtable slot 4).
    // Controller::EnableSource(type, flags) gives `flags` to the matching source and 0 to the others.
    void Enable(u32 flags);
    // Aaron ord 469 0x10009b72 (InputSource::Reactivate): Activate(on) if the source is enabled. Called by Controller::Suspend on its 0<->1
    // transitions with the argument it received (0 = suspending -> Activate(false); nonzero = resuming -> Activate(true)).
    void Reactivate(bool on) { if (flags_ != 0) Activate(on); }
    // vtable slot 4 (pure virtual in the base, 0x100153e3): the subclasses register / unregister with the system. Port: the keyboard and
    // mouse sources are fed directly by Scene::HandleKey / HandleMouse, so they only record the state (IsActive); the JoystickSource
    // registers with its JoystickDev.
    virtual void Activate(bool on) { active_ = on; }
    bool IsActive() const { return active_; }
    bool Enabled() const { return flags_ != 0; }
    u32 Flags() const { return flags_; }
    virtual int Type() const { return 0; }                 // InputSourceType (original vtable slot 2)
    virtual void Reset() {}                                // original slot 3 (Controller::Suspend / reset)
    virtual void OnBounds(const RECT& r) { (void)r; }      // original slot 5 (Controller::SetBounds); Aaron ord 453 0x10003520 = the empty one

protected:
    Controller* owner_;
    u32 flags_ = 0; // +0x1c
    bool active_ = false;
};

// Aaron ord 36 0x10009d92: JoystickSource(Controller*, joyId). Shoot passes `joyId | 0x8000` ("pick the best present device, preferring
// joyId"). Fields (original): +0x20 joystick id, +0x24/+0x28 x/y range, +0x2c/+0x30 centre, +0x34 dead zone = (xmax - cx) / 3.
// WP-4: backed by JoystickDev (aaron/Joystick.h) -> platform/Joystick.h (SDL2).
class JoystickSource : public InputSource {
public:
    JoystickSource(Controller* owner, u32 joyId);
    ~JoystickSource() override;
    void OnJoystick(int dx, int dy, u32 buttons) override;
    int Type() const override { return SRC_JOYSTICK; }          // Aaron ord 292 0x1000a100 (returns 3)
    // Aaron ord 449 0x10009e6f (vtable slot 4): when enabling, resolves the id (0x8000 flag -> PickDevice(id & 0xf, 0)), registers with the
    // application's JoystickDev[id], reads its range and derives centre / dead zone. When disabling (or if the device does not answer
    // joyGetPos) unregisters.
    void Activate(bool on) override;
    // Aaron 0x10009f80 (vtable slot 1): MM_JOYnMOVE -> (flags & 1): SetDirectionFromVector(x - cx, y - cy, dead zone) + SetPosition(x, y,
    // range); MM_JOYnBUTTONDOWN / UP -> (flags & 2): SetButton((wParam & 0xf00) >> 4, down). lParam = x | y << 16 (unsigned words).
    void OnMessage(u32 msg, uintptr_t wParam, intptr_t lParam);
    // Aaron ord 197 0x10009dd1 (static, cdecl): `preferred` if that device exists and answers, else the first index >= `startFrom` that does,
    // else `preferred` again. (Ghidra shows `return param_1` in both branches; objdump 0x10009e58 returns the loop variable.)
    static u32 PickDevice(u32 preferred, u32 startFrom);
    u32 DeviceId() const { return id_; }
    int CenterX() const { return cx_; }
    int CenterY() const { return cy_; }
    int DeadZone() const { return thr_; }

private:
    u32 id_;                    // +0x20
    int rangeX_ = 0, rangeY_ = 0, cx_ = 0, cy_ = 0, thr_ = 0; // +0x24 .. +0x34
    class JoystickDev* dev_ = nullptr; // device we are registered with
};

// Aaron ord 37 0x1000a132: KeyboardSource(Controller*, flags). 256 entries {flags, down}. Defaults:
// arrows -> directions 1/4/2/8 (TODO order), Ctrl(0x11) -> button 0x10, Alt(0x12) -> 0x20, Space -> 0x40, Shift -> 0x80.
class KeyboardSource : public InputSource {
public:
    KeyboardSource(Controller* owner, u32 flags);
    // Aaron ord 645 (unused by Shoot.*; Shoot.exe FUN_00401aac remaps keys from the "FKeys" strings)
    void MapKey(u32 flags, u32 vk, bool replace);
    u32 KeyFlags(u32 vk) const { return map_[vk & 0xff].flags; } // what a key is mapped to (direction / button bit; 0 = unmapped)
    void OnKey(u32 vk, bool down) override;
    // Aaron ord 293 0x1000e360 (shared with the Scene's "return 1" slots): vtable slot 2 returns 1.
    int Type() const override { return SRC_KEYBOARD; }
    // Aaron ord 578 0x1000a4a9 (vtable slot 3): direction accumulator = 0, every key "up".
    void Reset() override;
    // Aaron ord 450 0x1000a28e (slot 4): registers / unregisters with the scene's FocusMgr (which broadcasts the key messages). Port: the
    // Scene feeds the controllers directly, so only IsActive() changes.
    void Activate(bool on) override { InputSource::Activate(on); }
    // Aaron ord 297 0x1000a24c: the virtual key mapped to `flags`; 0 if none (also the result for VK 0 itself).
    u32 FindKey(u32 flags) const;

private:
    struct Entry { u32 flags = 0; bool down = false; };
    Entry map_[256];
    u32 dirs_ = 0; // +0x20 OR of the direction bits of the keys currently down
};

// Aaron ord 38 0x1000a7e6: MouseSource(Controller*, flags). Rectangle bounds + 8 directions from a table;
// emits IN_ABSOLUTE positions on movement and IN_BTN1/IN_BTN2 for the mouse buttons.
class MouseSource : public InputSource {
public:
    MouseSource(Controller* owner, u32 flags);
    void OnMouse(u32 msg, int x, int y, u32 keys) override;
    int Type() const override { return SRC_MOUSE; }                // Aaron ord 294 0x1000abd0 (returns 2)
    void OnBounds(const RECT& r) override;
    void SetBounds(const RECT& r);         // Aaron 0x1000aae3
    // Aaron ord 650 0x1000aab0: SetBounds(RECT{l, t, r, b}).
    void SetBounds(int l, int t, int r, int b) { RECT rc; rc.left = l; rc.top = t; rc.right = r; rc.bottom = b; SetBounds(rc); }
    // Aaron ord 451 0x1000a896 (slot 4): on: CursorMgr registration + OnBounds(the controller's bounds) + (flags & 1, mode != 4) warp the pointer to
    // the centre of the bounds; off: unregister. Port: bounds + warp only (CursorMgr::SetPos is a no-op with SDL's relative window).
    void Activate(bool on) override;

private:
    void on_move(int x, int y);            // Aaron 0x1000a632
    int lastX_ = 0, lastY_ = 0;            // +0x44/+0x48
    RECT b_;                               // +0x20..+0x2c
    int w_ = 0, h_ = 0, cx_ = 0, cy_ = 0, thr_ = 0; // +0x30 +0x34 +0x38 +0x3c +0x40
};

// ---------------------------------------------------------------------------------------------
// Controller (0x74 bytes): created by Scene::CreateController(mode, user) (ord 167); owned by the Scene.
class Controller : public Subscriber {
public:
    Controller(Scene* scene, u32 mode, u32 user);                // Aaron ord 34 0x10009162
    // Aaron ord 70 0x1000928d: deletes every source (the source array is a PtrArray subclass whose dtor is ord 75 0x10009ce0: DeleteAll, then
    // ~PtrArray), then ~Subscriber. Port: sources_ is a vector of unique_ptr.
    ~Controller();
    // Aaron ord 183 0x1000932e: Suspend(0) = suspend (++counter), Suspend(nonzero) = resume (--counter). On the
    // 0<->1 transitions every source is reset, direction/buttons are released (with their release events) and
    // OnInput(IN_ONOFF, resumed, 0) is emitted. Returns the counter.
    int Suspend(int on);
    int SuspendCount() const { return suspended_; }   // +0x28
    bool Suspended() const { return suspended_ != 0; }
    // Aaron ord 576 0x10009639 (Controller::ReleaseAll): reset every source, direction 0, release every pressed button
    // (0x10..0x800, with release events) and clear the state. Called by Suspend on the 0<->1 transitions.
    void ReleaseAll();
    // Aaron ord 587 0x100096c7: enable/disable one source class (InputSourceType; Shoot calls EnableSource(ctl, 3)).
    // Original semantics (0x100096c7): the source of class `type` gets flags = `flags`, every other source gets 0.
    void EnableSource(int type, u32 flags);
    // Aaron ord 607 0x1000975d: movement bounds (mouse/joystick positions are clamped/scaled to this rect).
    void SetBounds(const RECT* r);
    // Aaron ord 608 0x1000972a: SetBounds(RECT{l, t, r, b}).
    void SetBounds(int l, int t, int r, int b);
    // Aaron ord 185 0x10009422: Enable(flags) on every source whose Type() == `type` (the others keep their flags, unlike EnableSource).
    void SetSourceFlags(int type, u32 flags);
    // Aaron ord 291 0x10009481: the first source of `type`. QUIRK kept: when none matches it returns the LAST source (the loop variable);
    // nullptr only if the controller has no source at all.
    InputSource* FindSource(int type) const;
    int SourceCount() const { return int(sources_.size()); }

    void AddSource(InputSource* src);      // takes ownership (called by the InputSource constructor)
    // Emit (called by the sources): scene->OnInput(code, x, y, user_)
    void Emit(u32 code, int x, int y);
    // ---- state machine used by the sources (Aaron 0x10009827 / 0x1000958d / 0x100099a7), port additions (pkg3) ----
    bool DirectionAllowed(u32 dir) const;         // 0x100094e0: dir==0 or in the table of the current mode
    void SetDirection(u32 dir);                   // emits the 4-bit direction when it changes (mode < 4)
    void SetButton(u32 bit, bool down);           // emits `bit` / `bit | IN_RELEASE`
    void SetPosition(int x, int y, int srcW, int srcH); // 0x100099a7: scaled absolute position (mode 4)
    void SetDirectionFromVector(int dx, int dy, int threshold); // 0x10009897 (angle -> direction table)
    // Subscriber of the application broadcaster (WMSG_ACTIVATEAPP) and of the app TimerMgr (AMSG_PAUSE): Aaron 0x10009544
    void OnAdvise(int code, intptr_t value, Broadcaster* src) override;
    // Feeds from Scene::HandleKey / HandleMouse.
    void OnKey(u32 vk, bool down);
    void OnMouse(u32 msg, int x, int y, u32 keys);
    const RECT& Bounds() const { return bounds_; }
    u32 User() const { return user_; }
    u32 Mode() const { return mode_; }
    u32 State() const { return state_; }
    Scene* GetScene() const { return scene_; }

private:
    Scene* scene_;
    u32 mode_, user_;
    u32 modeIdx_ = 0;      // +0x24 min(mode, 3)
    u32 state_ = 0;        // +0x2c direction (low nibble) | pressed buttons
    int posX_ = 0, posY_ = 0; // +0x50/+0x54 last absolute position
    int suspended_ = 0;
    RECT bounds_;
    std::vector<std::unique_ptr<InputSource>> sources_;
};

} // namespace aaron
