// Joystick tests (limits branch, task 5): `<game>.exe --engine-test 10`.  The platform layer (platform/Joystick.cpp) is exercised through SDL
// virtual joysticks, the Aaron layer (JoystickDev / JoystickSource) through fake devices (tests/AaronApp_Test.cpp, --engine-test 3) and through a
// virtual device (test_input_joystick_sdl there).  No hardware, no window.
#include "tests/EngineTests.h"

#include "platform/Joystick.h"
#include "tests/VirtualJoy.h"

#include <cstdio>

namespace {

int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                          \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

#ifdef RAPTURE_HAVE_VIRTUAL_JOY
void TestPlain() {
    std::printf("[joystick: SDL joystick path (axes, hat, buttons, hot-unplug)]\n");
    pjoy::ClearFakes();
    pjoy::Shutdown();
    VirtualJoy a;
    if (!a.Attach()) {
        std::printf("  SKIP: no SDL virtual joystick available\n");
        return;
    }
    if (SDL_NumJoysticks() != 1) { // real hardware attached: the device index would not be ours
        std::printf("  (a real joystick is attached: the scripted checks need an otherwise empty system) SKIP\n");
        return;
    }
    // WinMM semantics: 0..65535 per axis, centre in the middle, buttons 1..4 in bits 0..3, Y grows downwards
    CHECK(pjoy::NumDevices() == 1);
    CHECK(pjoy::Present(0) && !pjoy::Present(1) && !pjoy::Present(-1) && !pjoy::Present(pjoy::kMaxDevices));
    pjoy::State s;
    CHECK(pjoy::Read(0, s));
    CHECK(s.x == 32768 && s.y == 32768 && s.buttons == 0); // SDL centre 0 -> 32768 (WinMM nominal centre 32767: one unit, far below the 1/3 threshold)
    CHECK(!pjoy::Read(1, s) && !pjoy::Read(5, s));
    int x0, y0, x1, y1;
    pjoy::Range(0, x0, y0, x1, y1);
    CHECK(x0 == 0 && y0 == 0 && x1 == 65535 && y1 == 65535);

    a.Axis(0, 32767);
    a.Axis(1, -32768);
    CHECK(pjoy::Read(0, s) && s.x == 65535 && s.y == 0);
    a.Axis(0, -32768);
    a.Axis(1, 32767);
    CHECK(pjoy::Read(0, s) && s.x == 0 && s.y == 65535);
    a.Axis(0, 16384);
    CHECK(pjoy::Read(0, s) && s.x == 16384 + 32768);
    a.Axis(0, 0);
    a.Axis(1, 0);
    for (int b = 0; b < 6; ++b) {
        a.Button(b, true);
        CHECK(pjoy::Read(0, s));
        CHECK(s.buttons == (b < 4 ? (1u << b) : 0u)); // only the first four buttons exist for the game
        a.Button(b, false);
    }
    a.Button(0, true);
    a.Button(3, true);
    CHECK(pjoy::Read(0, s) && s.buttons == 9);
    a.Button(0, false);
    a.Button(3, false);
    // the hat overrides the axes to full deflection (like a digital POV)
    a.Hat(0, SDL_HAT_LEFT);
    CHECK(pjoy::Read(0, s) && s.x == 0 && s.y == 32768);
    a.Hat(0, SDL_HAT_RIGHT | SDL_HAT_DOWN);
    CHECK(pjoy::Read(0, s) && s.x == 65535 && s.y == 65535);
    a.Hat(0, SDL_HAT_UP);
    CHECK(pjoy::Read(0, s) && s.x == 32768 && s.y == 0);
    a.Hat(0, SDL_HAT_CENTERED);
    CHECK(pjoy::Read(0, s) && s.x == 32768 && s.y == 32768);

    // a second device in slot 1, a third one is beyond kMaxDevices
    VirtualJoy b2;
    CHECK(b2.Attach(2, 4, 0, "Rapture virtual stick 2"));
    CHECK(pjoy::NumDevices() == 2 && pjoy::Present(1));
    b2.Axis(0, 32767);
    CHECK(pjoy::Read(1, s) && s.x == 65535 && s.y == 32768);
    CHECK(pjoy::Read(0, s) && s.x == 32768);
    VirtualJoy c;
    CHECK(c.Attach(2, 4, 0, "Rapture virtual stick 3"));
    CHECK(SDL_NumJoysticks() == 3 && pjoy::NumDevices() == 2);
    c.Detach();
    // fakes replace the SDL devices completely while one is set
    pjoy::State f;
    f.x = 1000;
    f.buttons = 2;
    pjoy::SetFake(0, &f);
    CHECK(pjoy::NumDevices() == 1 && pjoy::Present(0) && !pjoy::Present(1));
    CHECK(pjoy::Read(0, s) && s.x == 1000 && s.buttons == 2 && !pjoy::Read(1, s));
    pjoy::ClearFakes();
    CHECK(pjoy::NumDevices() == 2 && pjoy::Read(1, s) && s.x == 65535);
    b2.Detach();
    CHECK(!pjoy::Read(1, s));
    CHECK(pjoy::NumDevices() == 1 && !pjoy::Present(1));

    // hot-unplug of the only device: the read fails, the slots are released, a later device is picked up again
    a.Detach();
    CHECK(!pjoy::Read(0, s));
    CHECK(!pjoy::Present(0) && pjoy::NumDevices() == 0);
    VirtualJoy again;
    CHECK(again.Attach());
    again.Axis(0, 32767);
    CHECK(pjoy::Present(0) && pjoy::Read(0, s) && s.x == 65535);
    again.Detach();
    CHECK(!pjoy::Read(0, s));
    pjoy::Shutdown();
}

void TestGamepad() {
    std::printf("[joystick: SDL game controller path (left stick, D-pad, A/B/X/Y)]\n");
    pjoy::ClearFakes();
    pjoy::Shutdown();
    if (SDL_WasInit(SDL_INIT_JOYSTICK) && SDL_NumJoysticks() != 0) {
        std::printf("  (a real joystick is attached) SKIP\n");
        return;
    }
    VirtualJoy a;
    if (!a.Attach(2, 6, 1, "Rapture virtual pad")) {
        std::printf("  SKIP: no SDL virtual joystick available\n");
        return;
    }
    CHECK(a.MapAsGamepad());
    CHECK(SDL_IsGameController(a.index) == SDL_TRUE);
    pjoy::State s;
    CHECK(pjoy::Present(0) && pjoy::Read(0, s));
    CHECK(s.x == 32768 && s.y == 32768 && s.buttons == 0);
    a.Axis(0, 20000);
    a.Axis(1, -20000);
    CHECK(pjoy::Read(0, s) && s.x == 52768 && s.y == 12768);
    a.Axis(0, 0);
    a.Axis(1, 0);
    a.Button(0, true);  // A -> JOY_BUTTON1
    CHECK(pjoy::Read(0, s) && s.buttons == 1);
    a.Button(0, false);
    a.Button(1, true);  // B -> JOY_BUTTON2
    a.Button(2, true);  // X -> JOY_BUTTON3
    CHECK(pjoy::Read(0, s) && s.buttons == 6);
    a.Button(1, false);
    a.Button(2, false);
    a.Button(3, true);  // Y -> JOY_BUTTON4
    CHECK(pjoy::Read(0, s) && s.buttons == 8);
    a.Button(3, false);
    a.Button(5, true);  // not mapped to the four buttons
    CHECK(pjoy::Read(0, s) && s.buttons == 0);
    a.Button(5, false);
    a.Hat(0, SDL_HAT_LEFT | SDL_HAT_UP);   // D-pad overrides the stick
    CHECK(pjoy::Read(0, s) && s.x == 0 && s.y == 0);
    a.Hat(0, SDL_HAT_RIGHT);
    CHECK(pjoy::Read(0, s) && s.x == 65535 && s.y == 32768);
    a.Hat(0, SDL_HAT_DOWN);
    CHECK(pjoy::Read(0, s) && s.x == 32768 && s.y == 65535);
    a.Hat(0, SDL_HAT_CENTERED);
    a.Detach();
    CHECK(!pjoy::Read(0, s));
    pjoy::Shutdown();
}
#endif

} // namespace

int RunJoystickTests() {
    std::printf("[joystick]\n");
#ifdef RAPTURE_HAVE_VIRTUAL_JOY
    TestPlain();
    TestGamepad();
#else
    std::printf("  SKIP: SDL older than 2.0.14 (no virtual joysticks)\n");
#endif
    // the scripted-device (fake) API is the path of the Aaron-level tests and of --autoplay
    pjoy::ClearFakes();
    pjoy::State f;
    f.x = 5;
    pjoy::SetFake(1, &f);                    // a fake in slot 1 only: slot 0 is absent
    CHECK(pjoy::NumDevices() == 1 && !pjoy::Present(0) && pjoy::Present(1));
    pjoy::State s;
    CHECK(!pjoy::Read(0, s) && pjoy::Read(1, s) && s.x == 5);
    pjoy::SetFake(7, &f);                    // out of range: ignored
    CHECK(pjoy::NumDevices() == 1);
    pjoy::SetFake(1, nullptr);
    pjoy::ClearFakes();
    std::printf("joystick: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail;
}
