# rapture-engine – unofficial SDL2 port of the Rapture Technologies engine

A from-scratch reimplementation, in portable C++17 on top of SDL2, of the **Rapture Technologies
engine** (`moses.dll` – drawing, animation, `.rrs` resources, audio – and `Aaron.dll` – application
framework, 1999) used by four of Disney Interactive's *Hot Shots* games, plus the shared "Arcade"
library (scores, lives, sequences, pop-ups) and the host program (title screen, Options menu).
Every function follows the behaviour of the original, and its comment carries the **original
address** (`// moses.dll 0x10006e80`, `// Aaron ord 539 0x1000de09`) so that coverage can be measured.

> **You need your own copy of the original games.** This repository contains **no game data, no
> original code and no original assets**: only the engine written for this project and the tools
> that read the files of your own copy. See [NOTICE.md](NOTICE.md).

## Games that use it

`moses.dll` and `Aaron.dll` are **byte-identical** in these 4 games; the host executable is the
same program with a few different strings/ids (`HostConfig`), and it contains the Arcade library.

| Game | Port repository | Status |
|---|---|---|
| Disney's MuShoot (Mushu) | `mushoot-rapture-port` | playable, reference game |
| Flying Frenzy | (to be created, `tools/new-game.ps1`) | every export/ordinal it imports is implemented (not yet ported) |
| Flounder Quest | (to be created) | every export/ordinal it imports is implemented (not yet ported) |
| Rooftop Ruckus | (to be created) | every export/ordinal it imports is implemented (not yet ported) |

Each game lives in its own repository that uses this one as a git submodule (`engine/`). Naming
convention: `rapture-engine` + `<game>-rapture-port` (the other engine family:
`7thlevel-engine` + `<game>-7thlevel-port`).

## Layout

| Folder | Contents |
|---|---|
| `platform/` | replacements for Win32/GDI/DirectX/WinMM: SDL2 window, ARGB32 bitmaps, audio mixer, ADPCM, MIDI, AVI (MS Video 1), `.rrs` reader/writer |
| `moses/` | `moses.dll` rewritten (namespace `moses`): `CDrawView/CDrawObj`, `CAnimView/CAnimObj`, `QUEUEMSG`, `CResMgr`, `CAudioLib`, `CMem`... |
| `aaron/` | `Aaron.dll` rewritten (namespace `aaron`): `Application`, `Scene`, `Widget`, sprites, input, registry (backed by an `.ini` file)... |
| `arcade/` | the "Arcade" framework: `CGameBase`, `CArcadeGame`, players, properties, events/sequences, HUD, high scores |
| `host/` | the host executable: `HostApp`, `TitleScene`, pop-ups (Options, New game, Save...), `HostMain`, **`HostConfig`** |
| `autoplay/` | `--autoplay`: the game driven by a script, hidden window, no real input |
| `tests/` | headless tests of packages 1..4, Arcade, host, `.rrs` decoding |
| `tools/` | `build-engine.bat`, `setup-build-env.ps1`, `engine_coverage.py`, `new-game.ps1`, `rrs_extract.py`, `rrs_pack.py`, `extract_host_strings.py` |
| `docs/` | documentation (below) |
| `third_party/` | `stb_truetype` (MIT / public domain) |

## Building and testing the engine alone

```
engine\tools\build-engine.bat          configure and build into <root>\build-engine\ (library `rapture` + executable `rapture_tests`)
engine\tools\build-engine.bat test     ... and run the tests that need no game data (--engine-test 1,3,4,9, --game-test arcade|host)
engine\tools\build-engine.bat clean
```

`<root>` is the folder that contains `engine\` (a game repository) or, for a clone of the engine
alone, the engine folder itself. Compiler, CMake and SDL2 are found, or downloaded as portable
copies, by `setup-build-env.ps1` (paths are remembered in `.buildtools.json`, not in git). Nothing
is installed on the system; Visual Studio is not needed (MinGW-w64 + CMake + SDL2). From
PowerShell: `& .\engine\tools\build-engine.bat test`. The CMake option `RAPTURE_BUILD_TESTS` (ON)
compiles the tests into the library (`RAPTURE_WITH_TESTS=1`) and, standalone, `rapture_tests`.
`RAPTURE_FORCE_PORTABLE=ON` (or `build-engine.bat portable test`) compiles with every Win32-specific path off (no winmm, no GDI text, no `%APPDATA%`), as on Linux/macOS, even on
MinGW; see `docs/LIMITS.md` for what was verified in that configuration and what was not (no real Linux build yet).

Antivirus software may flag freshly built executables (false positive): add an exclusion for
your build folder if needed.

## Using it from a game (submodule)

```
git submodule add https://github.com/LuC-iTA-GHorg/rapture-engine.git engine     # or: engine\tools\new-game.ps1
git clone --recurse-submodules <game repository>
```

In the game's `CMakeLists.txt`: `add_subdirectory(engine)`, then
`target_link_libraries(<game> PRIVATE rapture SDL2::SDL2main)` and `rapture_copy_sdl2_dll(<game>)`.
The game's `main.cpp` is one line: `return rapture::HostMain(argc, argv, <Game>HostConfig());`.
Changes to the engine are committed **inside** `engine/` (branch `master`, not a detached HEAD)
and pushed; then the game repository commits the new submodule pointer.

## `HostConfig` (`host/HostConfig.h`)

A struct of constants that a game compiles into `game/HostConfig<Game>.cpp`: product name, title,
the host executable's string table (generated from the user's own executable by
`tools/extract_host_strings.py`), title/registry-key string ids, settings folder and `.ini`
file, scene DLL and entry point (`CreateScene`), title sprite id, archive names, data and movie
files, `ArcadeGameDef`, the function that creates the game scene, game-specific tests
(`--game-test <name>`) and the `--autoplay` extension. Details and values measured for the 4 games:
`docs/REUSE.md` section 1.3, `docs/HOST.md`.

## Command line (`host/HostMain.h`)

`<game>.exe` (play), `--autoplay SCRIPT [--outdir DIR] [--fast N|max]`, `--settings FILE.ini`,
`--selftest`, `--rrs-test [DIR] [--dump OUT]`, `--engine-test N`, `--game-test arcade|host|<name>`,
`--movie FILE.avi` (opens a window). The programs are GUI-subsystem executables: to read their
output use `Start-Process -NoNewWindow -Wait -RedirectStandardOutput`.

## Autoplay

`--autoplay SCRIPT` runs the real game with a hidden window and input from a text script (`wait`,
`waitfor`, `click`, `key`, `button`, `fire`, `shot` = PNG, `log`...), `dummy` audio and settings in a
temporary `.ini`; `--fast N` scales the game clock. Guide: `docs/AUTOPLAY.md`.

## Documentation

| File | Contents |
|---|---|
| `CLAUDE.md` | working rules for contributors (human or assistant) |
| `docs/HANDOFF.md` | status, what is verified, what to do |
| `docs/COMPLETENESS.md` | plan for a complete engine: status of every export/ordinal/opcode/type/option, work packages |
| `docs/README.md` | architecture, original-to-port map, conventions |
| `docs/REUSE.md` | how to reuse the engine for a new game (playbook, phases) |
| `docs/AUTOPLAY.md`, `docs/HOST.md`, `docs/ARCADE.md` | `--autoplay`, host, Arcade library |
| `docs/rrs_format.md` | the `.rrs` archive format |
| `docs/ENGINE_API_COVERAGE.md` | generated: which exports/ordinals each game uses and what the port covers |
| `docs/OTHER_GAMES_SURVEY.md` | comparison of the 4 games (binaries, resources, differences) |
| `docs/WORKPLAN.md`, `docs/CHANGES.md`, `docs/ARCADE_ENGINE_REQUESTS.md` | history of work packages, header changelog, Arcade requests |

## How the behaviour was established

The behaviour was derived by studying the binaries of games the author owns, for
interoperability. That analysis material (disassembly, decompiler output, notes) is not part of
this repository and is not distributed.

## Third-party components

| Component | License | Where |
|---|---|---|
| SDL2 | zlib | downloaded by `setup-build-env.ps1` (not stored in the repository) |
| MinGW-w64 GCC, CMake | their own licenses | downloaded by `setup-build-env.ps1` |
| stb_truetype v1.26 | MIT / public domain | `third_party/stb/` |

## License

No license has been chosen yet for the code of this project: until then, all rights are reserved
by its author (see [LICENSE](LICENSE)). The games, their names and their data belong to their
respective owners ([NOTICE.md](NOTICE.md)).
