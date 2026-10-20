// Entry points of the per-package engine tests (see docs/WORKPLAN.md). Each function lives in the .cpp
// owned by its package (engine/tests/PkgN_Test.cpp) and returns 0 on success, nonzero on failure.
// host/HostMain.cpp dispatches `<game>.exe --engine-test N`. Tests must be headless (no window).
#pragma once

int RunPkg1Tests(int argc, char** argv); // moses drawing / compose / animation
int RunPkg2Tests(int argc, char** argv); // moses resources / audio
int RunPkg3Tests(int argc, char** argv); // aaron app / window / scene / timers / input / registry
int RunPkg4Tests(int argc, char** argv); // aaron widgets / dialogs / text
int RunPkg9Tests(int argc, char** argv); // WP-9: rapture.ini reader/writer, debug rects overlay, RemapDrums (tests/Ini_Test.cpp)
int RunMoses5Tests();                    // WP-5: moses completeness (called at the end of RunPkg1Tests)
int RunPkg10Tests(int argc, char** argv); // limits branch: MIDI sequencer, joystick, portability (tests/Limits_Test.cpp)
int RunMidiSeqTests();                   // tests/MidiSeq_Test.cpp, returns the number of failures
int RunPkg11Tests(int argc, char** argv); // limits branch: SDL `disk` audio capture checks, argv[1] = game folder (tests/AudioDisk_Test.cpp)
