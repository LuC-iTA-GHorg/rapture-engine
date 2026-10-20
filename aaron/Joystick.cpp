// JoystickDev (WP-4): see Joystick.h.
#include "aaron/Joystick.h"

#include "aaron/Application.h"

#include <algorithm>

namespace aaron {

JoystickDev::JoystickDev(int id) : id_(id) { // 0x10012688
    Application* app = Application::GetApp();
    if (app) {
        app->AppBroadcaster().AddListener(this); // WMSG_ACTIVATEAPP
        app->Timers().AddListener(this);         // AMSG_PAUSE
    }
}

JoystickDev::~JoystickDev() { // 0x100127b7
    SetCapture(false);
}

bool JoystickDev::Probe(int id) { return pjoy::Present(id); } // ord 408 0x10012995

void JoystickDev::GetRange(unsigned out[4]) const { // ord 295 0x100128e1
    int a, b, c, d;
    pjoy::Range(id_, a, b, c, d);
    out[0] = unsigned(a);
    out[1] = unsigned(b);
    out[2] = unsigned(c);
    out[3] = unsigned(d);
}

void JoystickDev::AddListener(JoystickSource* s) { // ord 602 0x10012a7a
    if (s && std::find(listeners_.begin(), listeners_.end(), s) == listeners_.end()) listeners_.push_back(s);
    SetCapture(true);
}

void JoystickDev::RemoveListener(JoystickSource* s) { // ord 699 0x10012aa0
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), s), listeners_.end());
    if (listeners_.empty()) SetCapture(false);
}

void JoystickDev::Dispatch(u32 msg, uintptr_t wParam, intptr_t lParam) { // ord 498 0x10012a59
    std::vector<JoystickSource*> snap = listeners_; // a listener may unregister itself while being advised
    for (JoystickSource* s : snap) {
        if (std::find(listeners_.begin(), listeners_.end(), s) != listeners_.end()) s->OnMessage(msg, wParam, lParam);
    }
}

void JoystickDev::SetCapture(bool on) { // 0x10012857
    if (!on) {
        capturing_ = false; // joyReleaseCapture
        polled_ = false;
        return;
    }
    Application* app = Application::GetApp();
    if (!listeners_.empty() && app && app->AppActive()) {
        capturing_ = true; // joySetCapture(hwnd, id, 10, FALSE)
        polled_ = false;
        pjoy::State s;
        lastButtons_ = pjoy::Read(id_, s) ? s.buttons : 0; // Windows reports button CHANGES only: start from the current state
    }
}

void JoystickDev::OnAdvise(int code, intptr_t value, Broadcaster* src) { // 0x100129cf
    (void)src;
    Application* app = Application::GetApp();
    if (!app) return;
    if (code == int(WMSG_ACTIVATEAPP)) SetCapture(!app->Timers().Paused() && value != 0);
    else if (code == int(AMSG_PAUSE)) SetCapture(value == 0 && app->AppActive());
}

void JoystickDev::Poll(u32 nowMs, bool force) {
    if (!capturing_) return;
    if (!force && polled_ && nowMs - lastPoll_ < kPeriodMs) return;
    polled_ = true;
    lastPoll_ = nowMs;
    pjoy::State s;
    if (!pjoy::Read(id_, s)) return;
    u32 chg = s.buttons ^ lastButtons_;
    u32 pressed = chg & s.buttons, released = chg & ~s.buttons;
    lastButtons_ = s.buttons;
    // WinMM order: the position message first, then the button messages (wParam = changed buttons | state)
    Dispatch(MoveMsg(), s.buttons, intptr_t(uint32_t(s.x & 0xffff) | (uint32_t(s.y & 0xffff) << 16)));
    if (pressed) Dispatch(id_ ? MMSG_JOY2BUTTONDOWN : MMSG_JOY1BUTTONDOWN, (pressed * kJoyButtonChg) | s.buttons, 0);
    if (released) Dispatch(id_ ? MMSG_JOY2BUTTONUP : MMSG_JOY1BUTTONUP, (released * kJoyButtonChg) | s.buttons, 0);
}

} // namespace aaron
