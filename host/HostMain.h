// HostMain: the whole `main()` of a Rapture-engine game.  The game's main.cpp is
//
//     #include "host/HostMain.h"
//     int main(int argc, char** argv) { return rapture::HostMain(argc, argv, MyGameHostConfig()); }
//
// (the file that defines main must be compiled into the executable itself, not into a library, so that SDL2main finds it.)
#pragma once

#include "host/HostConfig.h"
#include <SDL_main.h>   // main() -> SDL_main on platforms that need SDL2main

namespace rapture {

// Command line (every mode works from the game folder, i.e. the folder with the .rrs files; see mainArchive below):
//   <game>.exe                       the game: the host exe's WinMain (title screen -> the game's scene)
//   <game>.exe --settings FILE.ini   (any mode) use this settings file instead of %APPDATA%\<folder>\<file>
//   <game>.exe --autoplay SCRIPT [--outdir DIR] [--fast N|max]
//                                    the game driven by a script, hidden window, no real input (autoplay/Autoplay.cpp)
//   <game>.exe --selftest            initialise SDL and exit at once, without a window
//   <game>.exe --rrs-test [DIR] [--dump OUT]   headless decode test of the .rrs archives and AVIs (RAPTURE_WITH_TESTS)
//   <game>.exe --movie FILE.avi      play an AVI full-window (click/key skips)
//   <game>.exe --engine-test N [ARGS]          headless engine tests of work package N (1..4)   (RAPTURE_WITH_TESTS)
//   <game>.exe --game-test arcade|host|<name>  headless tests of the Arcade framework / of the Host / of the game's own
//                                    (HostConfig::gameTests)                                      (RAPTURE_WITH_TESTS)
// Installs `cfg` as the configuration in use (SetConfig), so `cfg` must outlive the call.  Returns the exit code.
int HostMain(int argc, char** argv, const HostConfig& cfg);

} // namespace rapture
