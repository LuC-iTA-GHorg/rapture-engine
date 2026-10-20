# HANDOFF — rapture-engine, status as of 2026-10-19

For picking the work up again on the **engine** (person or assistant). Read `../CLAUDE.md` first, then this file, then `COMPLETENESS.md`.

## Where we are

- **Repository**: `rapture-engine`, branch `master`. Locally it is the `engine/` submodule of a game repo (a full clone: run `git checkout master` before working).
  History before 2026-10-19 lives in the game repo (up to `52970c4`).
- **The engine runs**: the reference game (MuShoot, repo `mushoot-rapture-port`) is playable from the title screen to the credits with `moses`, `aaron`, `arcade` and `host` reimplemented.
  The other 3 games of the family are not ported yet: Flying Frenzy already has the whole engine API; Flounder Quest is missing a part of it (6 Aaron ordinals; `CDibTile` done in WP-1); Rooftop Ruckus is missing 2 Aaron ordinals.
- **WP-6 done** (branch `wp6`): `tools/queuemsg_usage.py` measures the QUEUEMSG opcodes used by the 4 games (table in `COMPLETENESS.md`; unused: 6, 11, 12, 101) and `Pkg1_Test` now also covers ANIM (100) and iris X/Y (102..105).
- **`HostConfig` exists** (`host/HostConfig.h`): no game name in the engine; each game fills in a struct of constants and calls `rapture::HostMain`.
- **Standalone build**: `engine\tools\build-engine.bat test` builds the `rapture` library + `rapture_tests` without any game and runs the tests that need no game data.
- **`tools/new-game.ps1`** creates the skeleton of a new game repo **with this repo as a submodule** (tested on 2026-10-19 on a scratch folder with the Flying files: initial commit, `.gitmodules` and pointer correct).

## What is verified (2026-10-19, full build from the game repo, working directory = game folder)

| test | result |
|---|---|
| `--engine-test 1` / `2 .` / `3` / `4` | 140 checks, 0 errors / OK on 5 archives (1934 records, 0 failed) / 279 / 105 |
| `--game-test arcade` | 233 checks, 0 errors |
| `--game-test host` | 172 checks, 0 errors |
| `--game-test mushoot` (in the game repo) | 170 checks, 0 errors |
| `--autoplay` `hit_hun.txt --fast 4` | exit 0, one hit = +750 points, ammo 8 -> 4 |
| standalone engine build (`engine\tools\build-engine.bat test`) | OK: Pkg1 140, pkg3 269, Pkg4 34, arcade 233, host 20 (tests without game data skip part of their checks), 0 errors |
| fresh `git clone --recurse-submodules` of the game repo + `build.bat` (with `engine/` emptied: automatic `git submodule update --init`) | OK, executable built, `--game-test arcade` 233 checks |

**Not verified**: real audio and MIDI (autoplay uses the `dummy` driver), joystick, CD audio, the 3 games not yet ported, writing `.rrs` archives (does not exist yet).

## API coverage (from `engine_coverage.py`)

moses.dll: 344 exports, 268 `implemented`, 7 `missing` (3 are `CDibTile`, 4 are internal constructors/destructors of replaced GDI classes), 60 `unused`, 9 `n.a.`.
Aaron.dll: 555 ordinals, 180 `implemented`, 8 `missing` (used by Flounder/Ruckus), 341 `unused` (no game imports them), 26 `n.a.`.
26/26 QUEUEMSG opcodes, 9/11 resource types (type 9 `DIBTILE` missing; type 4 palette is not used by any archive), 0/4 `rapture.ini` options.
Tables and work packages: **`COMPLETENESS.md`**.

## To do (in order)

1. WP-1 (`CDibTile` + resource type 9) and WP-2 (8 Aaron ordinals 77/138/144/226/234/237/488/576) DONE and merged into master on 2026-10-19. No Flounder archive contains type 9 records (quest.dll uses `CDibTile` in another way: to be checked when Flounder is ported). Tables regenerated: **no gap in the API imported by any of the 4 games** (MuShoot 155, Flying 154, Flounder 162, Ruckus 157 symbols used, 0 missing). `tools/engine_coverage.py` must be run from the game checkout (it looks for the game folders two levels up). Doubts: the class of ordinal 144 is inferred; ordinal 138 is simplified (see `aaron.md` 2.10).
2. Reverse engineering: **done on 2026-10-19** (repo split). The analysis material (Ghidra project for moses.dll, Aaron.dll and the host `Shoot.exe`, notes, function name tables, generic scripts) is **optional and not part of this repository**; nothing in the build or the tests requires it.
   Name tables recorded there: 642 rows (moses 99, Aaron 180, Shoot.exe 203, `Shoot.dll` in the Arcade range >= 0x10013de0 160).
   To improve: `shoot_exe.md` and `shoot_dll_b.md` (analysis notes, not in this repository) still cite some paths of the game repo's analysis output; `aaron_ordinals.tsv` is a hand-made table with 150 rows (the 180 rows of `function_names.tsv` are the source).
3. Port Flying Frenzy (WP-3) with `new-game.ps1` as a dress rehearsal of the two-repo flow.
4. The rest of `COMPLETENESS.md` (WP-4..12), up to the editor.

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

Branch `limits` of this repository (not pushed, not merged; the game repository still points at `master`). Full account in **`docs/LIMITS.md`**.

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

- **All 4 games are ported** on this engine: MuShoot (`mushoot-rapture-port`, reference game), Flying Frenzy (`flying-frenzy-rapture-port`), Flounder Quest (`flounder-quest-rapture-port`) and Rooftop Ruckus (`rooftop-ruckus-rapture-port`). Each is a private repo with this repo (`engine/`) and `rapture-engine-re` (`engine-re/`) as submodules, all pointing at the same engine commit.
- **Engine fixes that came out of each port**:
  - Rooftop Ruckus: `Pkg4` dialog test made robust (focus check before the click, independent of the game's pictures and of the hovered scene below).
  - Flying Frenzy: `Timer::Start` on a running timer no longer registers a second listener.
  - Flounder Quest: `Broadcast2` / `OnAdvise2`; refcounted dynamic resources (slot 0xff, Aaron ordinal 138); `Timer::OnHideChange` (ordinal 515); autoplay `keydown` / `keyup` commands.
  - Integration pass (Flounder): `--engine-test 11` measured the audible length against the whole wave; Flounder `sebfx.rrs#2` is 2.3 s with long silent padding, so the expected length is now the audible span of the source (test-only change).
- **Verification on the final commit** (each game, build from clean, working directory = game folder): `--engine-test 1/2 ./3/4/9/10/11 .`, `--game-test arcade` 233, `host` 172, per-game suite (MuShoot 145, Flying 220, Flounder 59, Ruckus 189), `--rrs-test . --roundtrip`, and one autoplay script reaching gameplay: all green.
- **Scope**: Linux and macOS are out of scope for the user (the portable build exists and is tested only on Windows/MinGW). Still needing the user: real audio/MIDI on speakers, a real gamepad.
