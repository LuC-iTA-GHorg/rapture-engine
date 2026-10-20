// An SDL "virtual joystick" (SDL >= 2.0.14) for the headless tests: a real SDL device (no hardware, no window) whose axes, hat and buttons
// the test drives, so the SDL path of platform/Joystick.cpp (open, read, hot-unplug) runs exactly as it would with a gamepad.
#pragma once

#include <SDL.h>

#include <string>

#if SDL_VERSION_ATLEAST(2, 0, 14)
#define RAPTURE_HAVE_VIRTUAL_JOY 1

struct VirtualJoy {
    int index = -1;               // SDL device index at attach time
    SDL_Joystick* joy = nullptr;  // the handle of the test itself (needed by the SetVirtual* calls)

    // false if the SDL joystick subsystem or the virtual device is not available
    bool Attach(int axes = 2, int buttons = 6, int hats = 1, const char* name = "Rapture virtual stick") {
        if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) return false;
        SDL_VirtualJoystickDesc d;
        SDL_zero(d);
        d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        d.type = SDL_JOYSTICK_TYPE_UNKNOWN;
        d.naxes = Uint16(axes);
        d.nbuttons = Uint16(buttons);
        d.nhats = Uint16(hats);
        d.name = name;
        index = SDL_JoystickAttachVirtualEx(&d);
        if (index < 0) return false;
        joy = SDL_JoystickOpen(index);
        return joy != nullptr;
    }
    void Detach() {
        if (joy) SDL_JoystickClose(joy);
        joy = nullptr;
        if (index >= 0) SDL_JoystickDetachVirtual(index);
        index = -1;
    }
    ~VirtualJoy() { Detach(); }

    void Axis(int a, int v) { SDL_JoystickSetVirtualAxis(joy, a, Sint16(v)); }
    void Button(int b, bool down) { SDL_JoystickSetVirtualButton(joy, b, down ? 1 : 0); }
    void Hat(int h, Uint8 v) { SDL_JoystickSetVirtualHat(joy, h, v); }
    // game controller mapping for this device: left stick = axes 0/1, D-pad = hat 0, A/B/X/Y = buttons 0..3
    bool MapAsGamepad() {
        char guid[64];
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index), guid, sizeof guid);
        std::string m = std::string(guid) +
                        ",Rapture virtual pad,a:b0,b:b1,x:b2,y:b3,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,leftx:a0,lefty:a1,";
        return SDL_GameControllerAddMapping(m.c_str()) >= 0;
    }
};
#endif
