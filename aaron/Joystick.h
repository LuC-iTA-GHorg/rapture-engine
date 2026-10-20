// JoystickDev: one physical joystick (Application+0x3dc / +0x3e0 hold devices 0 and 1). aaron.md sec. 2.8.
//
// Original: a Broadcaster + Subscriber built in Application::InitInstance (0x10012688). The JoystickSources of the Controllers register as
// its listeners; while at least one is registered and the application is active and not paused, the device is "captured" with WinMM
// joySetCapture(hwnd, id, 10 ms, fChanged = FALSE), which makes Windows post MM_JOYnMOVE every 10 ms and MM_JOYnBUTTONDOWN / UP when buttons
// change. The messages arrive in Application::WndProc, which forwards them to Dispatch() (ord 498) of device 0 or 1.
//
// Port: platform/Joystick.h (SDL2 GameController / Joystick) replaces joyGetPos / joyGetDevCaps; Poll() replaces the capture and creates
// the very same messages (same ids, same wParam / lParam encoding) every 10 ms from the main loop (Application::RunFrame).
#pragma once

#include "aaron/Input.h"
#include "aaron/RObject.h"
#include "platform/Joystick.h"

#include <vector>

namespace aaron {

// WinMM message ids (mmsystem.h). Named MMSG_* to stay clear of <windows.h> macros.
enum MMSG : u32 {
    MMSG_JOY1MOVE = 0x3a0, MMSG_JOY2MOVE = 0x3a1,
    MMSG_JOY1BUTTONDOWN = 0x3b5, MMSG_JOY2BUTTONDOWN = 0x3b6,
    MMSG_JOY1BUTTONUP = 0x3b7, MMSG_JOY2BUTTONUP = 0x3b8,
};
// wParam of the button messages: JOY_BUTTONnCHG (changed) in bits 8..11 | JOY_BUTTONn (state) in bits 0..3.
constexpr u32 kJoyButtonChg = 0x100;

class JoystickDev : public RObject, public Subscriber {
public:
    static constexpr u32 kPeriodMs = 10; // joySetCapture period
    explicit JoystickDev(int id);        // Aaron 0x10012688 (not exported): subscribes to the application broadcaster and TimerMgr
    ~JoystickDev() override;             // Aaron 0x127b7: SetCapture(false) ...
    int Id() const { return id_; }

    // Aaron ord 408 0x10012995 (static): joyGetNumDevs() > id && joyGetPos(id) succeeded.
    static bool Probe(int id);
    // Aaron ord 295 0x100128e1: joyGetDevCaps -> {wXmin, wYmin, wXmax, wYmax} (0, 0, 65535, 65535 here).
    void GetRange(unsigned out[4]) const;
    // Aaron ord 602 0x10012a7a: Broadcaster::AddListener, then SetCapture(true) (which only captures if the application is active).
    void AddListener(JoystickSource* s);
    // Aaron ord 699 0x10012aa0: Broadcaster::RemoveListener, then SetCapture(false) when no listener is left.
    void RemoveListener(JoystickSource* s);
    int ListenerCount() const { return int(listeners_.size()); }
    // Aaron ord 498 0x10012a59: Broadcaster::Broadcast(msg, wParam, lParam) -> JoystickSource::OnMessage of every listener.
    void Dispatch(u32 msg, uintptr_t wParam, intptr_t lParam);
    // Aaron 0x10012857 (FUN_10012857): joySetCapture / joyReleaseCapture. on: only if there is a listener and the application is active.
    void SetCapture(bool on);
    bool Capturing() const { return capturing_; }
    // Subscriber of the application broadcaster and TimerMgr (Aaron 0x100129cf): WMSG_ACTIVATEAPP -> capture iff not paused && active;
    // AMSG_PAUSE -> capture iff not paused (value == 0) && active.
    void OnAdvise(int code, intptr_t value, Broadcaster* src) override;
    // Port: the capture. Every kPeriodMs while capturing: MM_JOYnMOVE (wParam = buttons, lParam = x | y << 16), plus a button DOWN / UP
    // message per change. `force` skips the period check (tests).
    void Poll(u32 nowMs, bool force = false);

private:
    u32 MoveMsg() const { return id_ ? MMSG_JOY2MOVE : MMSG_JOY1MOVE; }
    int id_;
    bool capturing_ = false;
    std::vector<JoystickSource*> listeners_;
    unsigned lastButtons_ = 0;
    u32 lastPoll_ = 0;
    bool polled_ = false;
};

} // namespace aaron
