#include "platform/Joystick.h"

#include <SDL.h>

namespace pjoy {

namespace {

struct Slot {
    SDL_GameController* pad = nullptr;
    SDL_Joystick* joy = nullptr;
    int sdlIndex = -1;
    bool tried = false;
};

Slot g_slots[kMaxDevices];
bool g_init = false;
bool g_fakeSet[kMaxDevices] = {false, false};
State g_fake[kMaxDevices];

bool ensure_init() {
    if (g_init) return true;
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) return false;
    SDL_JoystickEventState(SDL_DISABLE); // polled: no event queue traffic
    SDL_GameControllerEventState(SDL_DISABLE);
    g_init = true;
    return true;
}

bool any_fake() {
    for (bool f : g_fakeSet)
        if (f) return true;
    return false;
}

bool open_slot(int index) {
    Slot& s = g_slots[index];
    if (s.pad || s.joy) return true;
    if (!ensure_init()) return false;
    int n = SDL_NumJoysticks();
    if (index >= n) return false;
    if (SDL_IsGameController(index)) s.pad = SDL_GameControllerOpen(index);
    if (!s.pad) s.joy = SDL_JoystickOpen(index);
    s.sdlIndex = index;
    return s.pad || s.joy;
}

int axis_to_win(int v) { return v + 32768; } // -32768..32767 -> 0..65535

} // namespace

int NumDevices() {
    if (any_fake()) {
        int n = 0;
        for (int i = 0; i < kMaxDevices; ++i)
            if (g_fakeSet[i]) ++n;
        return n;
    }
    if (!ensure_init()) return 0;
    int n = SDL_NumJoysticks();
    return n > kMaxDevices ? kMaxDevices : n;
}

bool Present(int index) {
    if (index < 0 || index >= kMaxDevices) return false;
    if (any_fake()) return g_fakeSet[index];
    return open_slot(index);
}

bool Read(int index, State& out) {
    if (index < 0 || index >= kMaxDevices) return false;
    if (any_fake()) {
        if (!g_fakeSet[index]) return false;
        out = g_fake[index];
        return true;
    }
    if (!open_slot(index)) return false;
    Slot& s = g_slots[index];
    SDL_JoystickUpdate();
    if (s.pad) {
        if (!SDL_GameControllerGetAttached(s.pad)) {
            Shutdown();
            return false;
        }
        out.x = axis_to_win(SDL_GameControllerGetAxis(s.pad, SDL_CONTROLLER_AXIS_LEFTX));
        out.y = axis_to_win(SDL_GameControllerGetAxis(s.pad, SDL_CONTROLLER_AXIS_LEFTY));
        if (SDL_GameControllerGetButton(s.pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) out.x = 0;
        if (SDL_GameControllerGetButton(s.pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) out.x = 65535;
        if (SDL_GameControllerGetButton(s.pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) out.y = 0;
        if (SDL_GameControllerGetButton(s.pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) out.y = 65535;
        out.buttons = 0;
        if (SDL_GameControllerGetButton(s.pad, SDL_CONTROLLER_BUTTON_A)) out.buttons |= 1;
        if (SDL_GameControllerGetButton(s.pad, SDL_CONTROLLER_BUTTON_B)) out.buttons |= 2;
        if (SDL_GameControllerGetButton(s.pad, SDL_CONTROLLER_BUTTON_X)) out.buttons |= 4;
        if (SDL_GameControllerGetButton(s.pad, SDL_CONTROLLER_BUTTON_Y)) out.buttons |= 8;
        return true;
    }
    if (s.joy) {
        if (!SDL_JoystickGetAttached(s.joy)) {
            Shutdown();
            return false;
        }
        out.x = SDL_JoystickNumAxes(s.joy) > 0 ? axis_to_win(SDL_JoystickGetAxis(s.joy, 0)) : 32767;
        out.y = SDL_JoystickNumAxes(s.joy) > 1 ? axis_to_win(SDL_JoystickGetAxis(s.joy, 1)) : 32767;
        if (SDL_JoystickNumHats(s.joy) > 0) {
            Uint8 h = SDL_JoystickGetHat(s.joy, 0);
            if (h & SDL_HAT_LEFT) out.x = 0;
            if (h & SDL_HAT_RIGHT) out.x = 65535;
            if (h & SDL_HAT_UP) out.y = 0;
            if (h & SDL_HAT_DOWN) out.y = 65535;
        }
        out.buttons = 0;
        int nb = SDL_JoystickNumButtons(s.joy);
        for (int b = 0; b < 4 && b < nb; ++b)
            if (SDL_JoystickGetButton(s.joy, b)) out.buttons |= 1u << b;
        return true;
    }
    return false;
}

void Range(int index, int& xmin, int& ymin, int& xmax, int& ymax) {
    (void)index;
    xmin = ymin = 0;
    xmax = ymax = 65535;
}

void SetFake(int index, const State* s) {
    if (index < 0 || index >= kMaxDevices) return;
    g_fakeSet[index] = s != nullptr;
    if (s) g_fake[index] = *s;
}

void ClearFakes() {
    for (int i = 0; i < kMaxDevices; ++i) g_fakeSet[i] = false;
}

void Shutdown() {
    for (Slot& s : g_slots) {
        if (s.pad) SDL_GameControllerClose(s.pad);
        if (s.joy) SDL_JoystickClose(s.joy);
        s = Slot();
    }
}

} // namespace pjoy
