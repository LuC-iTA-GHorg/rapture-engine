// "Limits" suite (branch limits): software MIDI sequencer, joystick with fake devices, portable-build checks.
// `<game>.exe --engine-test 10`.  Headless, no audio device, no window.
#include "tests/EngineTests.h"

#include <cstdio>

int RunPkg10Tests(int, char**) {
    int fail = 0;
    fail += RunMidiSeqTests();
    fail += RunEmfTests();
    fail += RunPortableTests();
    std::printf("== limits: %d failures ==\n", fail);
    return fail ? 1 : 0;
}
