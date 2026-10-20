// Joystick / game controller polling on SDL2 (replaces WinMM joyGetNumDevs / joyGetPos / joyGetDevCaps / joySetCapture).
//
// The Windows API reports a position as two 16 bit numbers (0..65535, centre ~32767) and the buttons as a bit mask (JOY_BUTTON1..4 =
// 0x1..0x8). This layer converts SDL GameControllers (left stick or D-pad + A/B/X/Y) and plain SDL Joysticks (axes 0/1 or hat 0 +
// buttons 0..3) to exactly that, so aaron::JoystickDev can keep the original arithmetic. SDL's joystick subsystem is initialised on first
// use and needs no window, so the headless tests can call it.
//
// SetFake() installs a scripted device (tests / --autoplay): fakes replace the real devices completely while at least one is set.
#pragma once

namespace pjoy {

struct State {
    int x = 32767, y = 32767; // 0..65535, centre 32767 (WinMM convention)
    unsigned buttons = 0;     // bit0..bit3 = JOY_BUTTON1..JOY_BUTTON4
};

constexpr int kMaxDevices = 2; // Aaron keeps two JoystickDev objects (ids 0 and 1)

int NumDevices();                        // devices that answer (WinMM: joyGetNumDevs + joyGetPos == JOYERR_NOERROR)
bool Present(int index);                 // joyGetPos(index) succeeds
bool Read(int index, State& out);        // current position; false if the device is absent
void Range(int index, int& xmin, int& ymin, int& xmax, int& ymax); // joyGetDevCaps: always 0..65535 on both axes
void SetFake(int index, const State* s); // nullptr removes the fake
void ClearFakes();
void Shutdown();                         // closes the SDL devices (the subsystem stays initialised)

} // namespace pjoy
