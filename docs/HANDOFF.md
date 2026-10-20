# HANDOFF — rapture-engine, status as of 2026-10-20

For picking the work up again on the **engine** (person or assistant). Read `../CLAUDE.md` first, then this file, then `COMPLETENESS.md`.

## Where we are

- **The engine is complete**: `COMPLETENESS.md` reports moses 335 implemented + 9 n.a., Aaron 517 + 38 n.a., QUEUEMSG opcodes 26/26, `.rrs` resource types 11/11 (including writing, with a byte-identical round trip of the 20 archives of the four games), `rapture.ini` options 4/4.
  Known limits and what was done about them (software MIDI sequencer, EMF renderer, disk-audio verification, `RAPTURE_FORCE_PORTABLE`, SDL virtual-joystick tests): `LIMITS.md`.
- **All four games run on it** (MuShoot, Flying Frenzy, Flounder Quest, Rooftop Ruckus). Each port is a separate private repository that uses this repository as its `engine/` submodule; they are not distributed and need the user's own original game files.
- **Repository**: `rapture-engine`, branch `master` (work on `master`, not a detached HEAD, when it is checked out as a submodule).
- **`HostConfig`** (`host/HostConfig.h`): the engine contains no game name; each game fills in a struct of constants and calls `rapture::HostMain`.
- **Standalone build**: `tools\build-engine.bat test` builds the `rapture` library + `rapture_tests` without any game and runs the tests that need no game data (`--engine-test 1,3,4,9,10,11`, `--game-test arcade|host`); `portable test` does the same with `RAPTURE_FORCE_PORTABLE=ON`.
- **Tools**: `tools/new-game.ps1` (skeleton of a new game repository with this one as a submodule), `rrs_extract.py` / `rrs_pack.py`, `engine_coverage.py` (regenerates `ENGINE_API_COVERAGE.md` and the generated block of `COMPLETENESS.md`; needs the original game folders), `audio_disk_check.py`.
- **Autoplay harness**: `AUTOPLAY.md`.
- **Scope**: Windows (MinGW-w64) only. Linux and macOS are untested and out of scope.

## Still needing a human

Listening to sounds, voices and MIDI on real speakers and a real synthesiser, a real gamepad, a Windows reference run of the EMF renderer against GDI.

## Verified (details in the update sections below and in `LIMITS.md`)

`--engine-test 1, 2 ., 3, 4, 9, 10, 11`, `--game-test arcade` (233) and `host` (172), the per-game suites, `--rrs-test . --roundtrip` and one autoplay script reaching gameplay per game: all green on the final integration commit, also in the `RAPTURE_FORCE_PORTABLE` build.
The analysis material used to establish the behaviour is not part of this repository.

## Practical notes

- Antivirus software may flag freshly built executables (false positive); add an exclusion if needed. Never start the game with a visible window when running automated checks: use `--autoplay`.
- The exe is a GUI-subsystem program: test output only with `Start-Process -NoNewWindow -Wait -RedirectStandardOutput`. Arguments with spaces: enclose them in quotes inside the `-ArgumentList` string.
- The Ghidra project (kept outside this repository) is binary: do not open it twice, no commits while Ghidra is open.
- The sub-agent process and the standard prompts are in `REUSE.md`.

## Reverse engineering (repo split, 2026-10-19)

This material is optional and is not part of this repository; it is described only as provenance of the reimplementation.

- Ghidra project `Rapture`: rebuilt with `re.bat rebuild` (about 4 minutes): functions recovered 66 (moses) / 173 (Aaron) / 118 (Shoot.exe), names applied 99 / 180 / 203, 126 by-ordinal imports of `Shoot.exe` resolved.
- Comparison of the exported analysis output with the one before the split: `decomp.c`, `library_decomp.c`, `unwind_decomp.c`, `functions.tsv`, `exports.txt` **identical** for the 3 binaries, except one line
  (return type of `FUN_100035f0` in moses: `undefined4 *` -> `void *`); `info.txt` and `imports.txt` differ only in the order of the caller lists (same set) and in the "original name" column
  of the Aaron imports of `Shoot.exe` (`AARON_ord27` -> `Ordinal_27`).
- For a new game: `new-game.ps1` generates a `re/game.cfg` and a `re.bat` that use these scripts; add the rows of the new `lib_start.tsv` to `<game>/re/lib_start.tsv`.
- `Shoot.dll` in the Arcade range: the names are the `Shoot.dll` rows of `function_names.tsv`; the game repo applies them together with its own. `ApplyNames.java` accepts several `.tsv` files.

## Update 2026-10-20 — engine complete

WP-4 (Aaron: all 341 unused ordinals), WP-5 (moses: 4 missing + 56 unused, portable stb_truetype text, CD audio from a folder of WAVs, EMF metafiles),
WP-7 (`.rrs` writing: byte-for-byte round trip of all 20 archives of the 4 games, also rebuilt from their contents) and WP-9 (`rapture.ini`, `--debug-rects` overlay)
are merged into `master`. `docs/COMPLETENESS.md`: moses 335 implemented + 9 n.a., Aaron 517 + 38 n.a., opcodes 26/26, resource types 11/11, ini options 4/4.
Green tests: `--engine-test 1` (223 + Moses5 156), `2 .`, `3` (549), `4` (176), `9` (63), `--game-test arcade|host|mushoot`, `--rrs-test . --roundtrip`, autoplay
`hit_hun`, `options_menu`, `level1_clear`, `gameover`, `hawk_horseman` (`--fast 4`).
Known limits (state of 2026-10-19; MIDI sequencer, EMF records, portable build and joystick tests were done on branch `limits` on 2026-10-20, see the update below and `LIMITS.md`): MIDI without live mute/tempo (a software sequencer is needed), some EMF records unsupported, physical CDs unsupported, non-Windows build untried,
joystick tried only with fake devices; details in `docs/WP4.md`, `WP5.md`, `WP7.md`, `WP9.md`.
Next: porting the other games (WP-3, WP-8).

## Update 2026-10-20 (branch `limits`): the known limits

Merged into `master`. Full account in **`docs/LIMITS.md`**.

* **MIDI**: software sequencer (`platform/MidiSeq.*`, `Midi.*`): the delta unit is 1 ms (verified in the moses MIDI thread), live mute (incl. the drum remap 10 -> 16), live tempo with the original arithmetic, loop,
  stop, Pause/Activate = stop + restart from the beginning (as the original). Backends: winmm `midiOutShortMsg` (`Midi_win.cpp`), no-op/logging elsewhere, recording + virtual clock for tests.
* **Real audio without speakers**: `--engine-test 11 [GAME_FOLDER]`, `tools/audio_disk_check.py`, SDL `disk` driver, `--autoplay` keeps `SDL_AUDIODRIVER=disk` (see `AUTOPLAY.md`, last section).
  Do not set `SDL_DISKAUDIODELAY` (SDL 2.32 on Windows names the file after it).
* **EMF**: all record types of MS-EMF except EMF+ and undecodable bitmaps (`Emf_Test.cpp`, 86 checks).
* **Portable**: `-DRAPTURE_FORCE_PORTABLE=ON` / `build-engine.bat portable test`; `RAPTURE_WIN32` (`platform/Config.h`) guards every optional Win32 backend. No WSL distribution here: no real Linux build yet.
* **Joystick**: reviewed against the original, SDL virtual-device tests, background polling hint.
* New tests: `--engine-test 10` (MIDI sequencer 160+, EMF 86, joystick 58, platform 2/5) and `--engine-test 11` (disk audio, 58 checks with the game folder). `--engine-test 3` now has 562 checks (+13, virtual joystick chain). Everything else unchanged:
  `1` 223 + Moses5 156, `2 .` OK on 5 archives (1934 records), `4` 176, `9` 63, `--game-test arcade` 233 / `host` 172 / `mushoot` 145, `--rrs-test . --roundtrip` OK. Same results in the `RAPTURE_FORCE_PORTABLE` build.
* **Needs the user**: listening to sounds/voices/MIDI on real speakers and a real synthesiser, a real gamepad, a real Linux/macOS build (WSL or a Linux box), a Windows reference run of the EMF renderer against GDI.

## Update 2026-10-20 — integration: the four-game family

- **All 4 games are ported** on this engine: MuShoot (the reference game), Flying Frenzy, Flounder Quest and Rooftop Ruckus. Each is a private repo with this repo as its `engine/` submodule, all pointing at the same engine commit.
- **Engine fixes that came out of each port**:
  - Rooftop Ruckus: `Pkg4` dialog test made robust (focus check before the click, independent of the game's pictures and of the hovered scene below).
  - Flying Frenzy: `Timer::Start` on a running timer no longer registers a second listener.
  - Flounder Quest: `Broadcast2` / `OnAdvise2`; refcounted dynamic resources (slot 0xff, Aaron ordinal 138); `Timer::OnHideChange` (ordinal 515); autoplay `keydown` / `keyup` commands.
  - Integration pass (Flounder): `--engine-test 11` measured the audible length against the whole wave; Flounder `sebfx.rrs#2` is 2.3 s with long silent padding, so the expected length is now the audible span of the source (test-only change).
- **Verification on the final commit** (each game, build from clean, working directory = game folder): `--engine-test 1/2 ./3/4/9/10/11 .`, `--game-test arcade` 233, `host` 172, per-game suite (MuShoot 145, Flying 220, Flounder 59, Ruckus 189), `--rrs-test . --roundtrip`, and one autoplay script reaching gameplay: all green.
- **Scope**: Linux and macOS are out of scope for the user (the portable build exists and is tested only on Windows/MinGW). Still needing the user: real audio/MIDI on speakers, a real gamepad.
