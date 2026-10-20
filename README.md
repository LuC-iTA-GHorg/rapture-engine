# rapture-engine – unofficial SDL2 reimplementation of the Rapture Technologies engine

A from-scratch reimplementation, in portable C++17 on top of SDL2, of the **Rapture Technologies
engine** (1999) used by four of Disney Interactive's *Hot Shots* games: `moses.dll` (drawing,
animation, `.rrs` resources, audio) and `Aaron.dll` (application framework), plus the shared
"Arcade" library (scores, lives, sequences, pop-ups) and the host program (title screen, Options
menu). Every function follows the behaviour of the original, and its comment carries the
**original address** (`// moses.dll 0x10006e80`, `// Aaron ord 539 0x1000de09`) so that coverage
can be measured.

> **Unofficial fan project, for preservation and interoperability.** Not affiliated with or
> endorsed by The Walt Disney Company or Rapture Technologies. This repository contains **no game
> data, no original code and no decompiled code**: only the engine written for this project and
> tools that read the files of *your own copy* of the games. See [NOTICE.md](NOTICE.md).

## Status

The engine is **complete**: every `moses.dll` export (335 implemented, 9 not applicable), every
`Aaron.dll` ordinal (517 implemented, 38 not applicable), all 26 animation-queue (`QUEUEMSG`)
opcodes, all 11 `.rrs` resource types (including writing, with a byte-identical round trip of
the archives of the four games) and all 4 `rapture.ini` options are in place and covered by
headless tests. Per-item status: [docs/COMPLETENESS.md](docs/COMPLETENESS.md); known limits
(software MIDI sequencer, EMF renderer, disk-audio verification, joystick tests):
[docs/LIMITS.md](docs/LIMITS.md).

The four games run on it: Disney's MuShoot, Flying Frenzy, Flounder Quest and Rooftop Ruckus.
Each game port is a separate, **private** repository that uses this one as a git submodule and
contains only that game's own code; those ports are not distributed, and running one requires the
original game files from your own copy. This repository is the shared part, and is useful on its
own as a library, a test suite and documentation of the engine and of the `.rrs` format.

Platform: developed and tested on **Windows** only (MinGW-w64). A `RAPTURE_FORCE_PORTABLE` build
exists that disables every Win32-specific path, but Linux and macOS are untested and not a goal.

## Requirements

Windows, PowerShell and Git. Nothing is installed on the system and Visual Studio is not needed:
`tools/setup-build-env.ps1` finds, or downloads as portable copies, MinGW-w64, CMake and SDL2
(locations are remembered in `.buildtools.json`, which is not in git). Python 3 is needed only for
the helper scripts in `tools/`.

## Build and test

```
tools\build-engine.bat                  build the library `rapture` and the test executable `rapture_tests`
tools\build-engine.bat test             ... and run the tests that need no game data
tools\build-engine.bat portable test    the same with every Win32-specific path off
tools\build-engine.bat clean
```

The output goes to `build-engine\` in the root folder: the folder that contains `engine\` when
this repository is a game's submodule, otherwise this folder. The test run executes
`--engine-test 1,3,4,9,10,11` and `--game-test arcade|host`. The tests that read original game
files (`--engine-test 2`, `--rrs-test`, `--engine-test 11 GAME_FOLDER`, the per-game suites) are
run with a game port's executable from the folder holding your own copy of the game files.

`rapture_tests.exe` is a GUI-subsystem program, so to capture its output use
`Start-Process -NoNewWindow -Wait -RedirectStandardOutput out.txt ...` from PowerShell. Antivirus
software may flag freshly built executables (false positive): add an exclusion for the build
folder if needed. The CMake option `RAPTURE_BUILD_TESTS` (ON by default) compiles the tests in.

## Using it for a game

```
git submodule add https://github.com/LuC-iTA-GHorg/rapture-engine.git engine
git clone --recurse-submodules <game repository>
```

`tools/new-game.ps1 -GameDir <game folder> -Name <name>` creates the skeleton of a new game
repository with this one as a submodule. In the game's `CMakeLists.txt`: `add_subdirectory(engine)`,
`target_link_libraries(<game> PRIVATE rapture SDL2::SDL2main)` and `rapture_copy_sdl2_dll(<game>)`.
Optionally `rapture_add_win32_resources(<game> ICON_FROM "${GAME_DATA}/<Host>.exe" VERSION ... PRODUCT ... DESCRIPTION ... FILENAME ...)`
adds an icon (read at configure time from the user's own original exe, nothing is stored in the repository), version info and a manifest ([docs/REUSE.md](docs/REUSE.md) 4.3).
The game's `main.cpp` is one line: `return rapture::HostMain(argc, argv, <Game>HostConfig());`.

Everything that differs between games lives in a `rapture::HostConfig` ([host/HostConfig.h](host/HostConfig.h)):
product name, string table, settings folder and `.ini`, archive and data file names, scene entry
point, game-specific tests and autoplay extension. The engine itself contains no game name.
Playbook: [docs/REUSE.md](docs/REUSE.md), [docs/HOST.md](docs/HOST.md).

Changes to the engine are committed inside `engine/` (on `master`, not a detached HEAD) and pushed;
then the game repository commits the new submodule pointer.

## Command line

Programs built on `HostMain` (see [host/HostMain.h](host/HostMain.h)) accept:
`--autoplay SCRIPT [--outdir DIR] [--fast N|max]`, `--settings FILE.ini`, `--selftest`,
`--rrs-test [DIR] [--roundtrip] [--dump OUT]`, `--engine-test N`, `--game-test arcade|host|<name>`,
`--movie FILE.avi`.

`--autoplay` runs the real game with a hidden window, no real input, `dummy` audio and a temporary
settings file, driven by a text script (`wait`, `waitfor`, `click`, `key`, `fire`, `shot` = PNG,
`log`...). Guide: [docs/AUTOPLAY.md](docs/AUTOPLAY.md).

Two helper scripts work on `.rrs` archives from your own game folder: `tools/rrs_extract.py` and
`tools/rrs_pack.py`.

## Layout

| Folder | Contents |
|---|---|
| `platform/` | replacements for Win32/GDI/DirectX/WinMM: SDL2 window, ARGB32 bitmaps, audio mixer, ADPCM, MIDI sequencer, AVI (MS Video 1), EMF, `.rrs` reader/writer |
| `moses/` | `moses.dll` rewritten (namespace `moses`): `CDrawView/CDrawObj`, `CAnimView/CAnimObj`, `QUEUEMSG`, `CResMgr`, `CAudioLib`... |
| `aaron/` | `Aaron.dll` rewritten (namespace `aaron`): `Application`, `Scene`, `Widget`, sprites, input, registry (backed by an `.ini` file)... |
| `arcade/` | the "Arcade" framework: `CGameBase`, `CArcadeGame`, players, properties, events/sequences, HUD, high scores |
| `host/` | the host executable: `HostApp`, `TitleScene`, pop-ups (Options, New game, Save...), `HostMain`, `HostConfig` |
| `autoplay/` | the scripted `--autoplay` driver |
| `tests/` | headless tests |
| `tools/` | build scripts, `engine_coverage.py`, `new-game.ps1`, `rrs_extract.py`, `rrs_pack.py`, `extract_host_strings.py`... |
| `docs/` | documentation (below) |
| `third_party/` | `stb_truetype` (MIT / public domain) |

## Documentation

| File | Contents |
|---|---|
| [docs/README.md](docs/README.md) | architecture, original-to-port map, conventions |
| [docs/HANDOFF.md](docs/HANDOFF.md) | current status and what is verified |
| [docs/COMPLETENESS.md](docs/COMPLETENESS.md) | status of every export, ordinal, opcode, resource type and option |
| [docs/LIMITS.md](docs/LIMITS.md) | known limits and how they were addressed |
| [docs/REUSE.md](docs/REUSE.md) | how to reuse the engine for a new game |
| [docs/AUTOPLAY.md](docs/AUTOPLAY.md), [docs/HOST.md](docs/HOST.md), [docs/ARCADE.md](docs/ARCADE.md) | autoplay, host, Arcade library |
| [docs/rrs_format.md](docs/rrs_format.md), [docs/RAPTURE_INI.md](docs/RAPTURE_INI.md) | the `.rrs` archive format, `rapture.ini` |
| [docs/ENGINE_API_COVERAGE.md](docs/ENGINE_API_COVERAGE.md) | generated: which exports each game uses and what the port covers |
| [docs/OTHER_GAMES_SURVEY.md](docs/OTHER_GAMES_SURVEY.md) | comparison of the four games |
| [docs/WORKPLAN.md](docs/WORKPLAN.md), [docs/CHANGES.md](docs/CHANGES.md) | history of work packages, header changelog |
| [CLAUDE.md](CLAUDE.md) | working rules for contributors (human or assistant) |

## How the behaviour was established

The behaviour was derived by studying the binaries of games the author owns, for
interoperability. That analysis material is not part of this repository and is not distributed.

## Third-party components

| Component | License | Where |
|---|---|---|
| SDL2 | zlib | downloaded by `setup-build-env.ps1` (not stored in the repository) |
| MinGW-w64 GCC, CMake | their own licenses | downloaded by `setup-build-env.ps1` |
| stb_truetype v1.26 | MIT / public domain | `third_party/stb/` |

## License

All rights reserved by the author: no open-source license has been granted for the code of this
project (see [LICENSE](LICENSE)). The games, their names and their data belong to their
respective owners ([NOTICE.md](NOTICE.md)).
