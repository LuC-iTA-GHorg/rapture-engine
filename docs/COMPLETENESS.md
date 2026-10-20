# Engine COMPLETENESS — plan to make it COMPLETE and faithful

**Goal.** `rapture-engine` must become a **complete and faithful** rewrite of Rapture Technologies (moses.dll + Aaron.dll +
the "Arcade" framework + host), not just what the games already ported need: every moses export (344), every Aaron ordinal (555),
every animation-queue opcode, every `.rrs` resource type, every `rapture.ini` option, each with **a status** and a test.
Ultimate purpose: a future **editor** (open/modify `.rrs` archives, compose scenes, try animations) built on the same library.

## 1. How to read this document

* The "Generated status" section is produced by `python -I engine/tools/engine_coverage.py` (re-run it after every engine change:
  it also rewrites `ENGINE_API_COVERAGE.md`). **Do not edit by hand** the block between the two markers; the rest of the file may be edited.
* Statuses: `implemented` / `partial` / `missing` / `unused` (none of the 4 games uses it: required anyway for completeness) / `n.a.`.
* Method: textual search for the original address (`// moses.dll 0x1000xxxx`, `// Aaron ord N 0x1000xxxx`) in `engine/moses` and `engine/aaron`
  (heuristic: `missing` entries are certain, `implemented` ones very probable). **Known limits**: constructors/destructors and `operator new/delete` are
  often replaced by C++'s own and so carry no address (they show up as `unused`/`missing` although covered: note the
  address in the comment when touching the class); the `unused` Aaron ordinals are the ones no game imports, but Aaron.dll may
  call them internally (not measured: see WP-2).
* "Used by": `M F Q R` = MuShoot, Flying, Flounder, Ruckus (direct imports of the exe and scene DLLs).
* 1:1 fidelity, original addresses in comments, no Visual Studio, no visible windows in tests: rules in `../CLAUDE.md`.

## 2. Definition of "complete" (acceptance criteria)

1. Every export/ordinal/opcode/type/option has status `implemented` (or a justified `n.a.`) and **at least one headless test** (`--engine-test`, `--game-test`) or a
   documented check against the original behaviour.
2. `engine_coverage.py` reports no `missing`/`partial` for any game of the family; `unused` entries are reduced to zero or justified (dead code
   of the original, e.g. `ComposeRects`/`DisplayRects`).
3. All `.rrs` archives of the 4 games decode (`--rrs-test`) and **re-write** byte for byte (needed by the editor: WP-7).
4. The 4 games start with `--autoplay` up to the first level (WP-3, WP-8).

## 3. Work packages for sub-agents (in priority order)

For each package: read `../CLAUDE.md`, `README.md` (architecture), the cited analysis notes (provenance; optional and not part of this repository) first;
work in your own worktree/folder, do not touch other packages' files,
keep the build green (`engine/tools/build-engine.bat test`), add tests, **do not commit** (the integrator does), re-run `engine_coverage.py`.

| WP | priority | what | files/areas | expected output |
|---|---|---|---|---|
| WP-1 | high | **`CDibTile` and resource type 9** (`CDibTile::Init/SetDib/DibBlt/GetPixel`, `load_dibtile` 0x1000c970): needed by Flounder Quest. **DONE (branch wp1, 2026-10-19)**: `CDibTile` + type 9 + `Archive::GetDibTile`; the 5 Flounder archives have no type-9 records (verified), test on a synthetic archive | `moses/DibData.*`, `moses/ResMgr.cpp`, `platform/Rrs.*` | 3 `missing` exports -> `implemented`; `--rrs-test` on the `seb*` archives; Pkg1/Pkg2 tests |
| WP-2 (**DONE, merged into master on 2026-10-19**: 8 ordinals ported with tests in `Pkg4_Test.cpp`, see `aaron.md` 2.10; `engine_coverage.py` to be re-run with the game folders) | high | **Aaron: 8 missing ordinals** (77, 138, 144, 226, 234, 237, 488, 576: used by Flounder and Ruckus): analysis of the original (`Aaron.dll` 0x10004386, 0x1000ca5f, 0x10004ed2, 0x10005791, 0x10006b1a, 0x10004411, 0x1000445f, 0x10009639), names in the function name table, port; also measure which Aaron ordinals Aaron calls internally | `aaron/*`, `Ordinals.h` | Aaron `missing` rows cleared |
| WP-3 | high | **The 3 other games on the shared host**: Flying (has everything already), Flounder, Ruckus: `HostConfig`, tables, scene (`tools/new-game.ps1`, `REUSE.md`) | game repo, not the engine | per game: `--game-test host` green and an autoplay |
| WP-4 (**DONE 2026-10-20**, see `docs/WP4.md`) | medium | **Aaron `unused`**: port the ordinals not imported by any game (window, input, joystick, registry, CD...), one group per class (`aaron.md`), with tests | `aaron/*` | Aaron table without `unused` (or justified) |
| WP-5 (**DONE 2026-10-20**, see `docs/WP5.md`) | medium | **moses `unused`/`missing`**: complete `CFileIO`, `CGdiCache`/`CObjCache` (or justified `n.a.`), `CDrawMeta`/`CAnimMeta` (metafile), `CDrawPoly`/`CAnimPoly`, `CDibSect`, `CAudioLib` CD/advanced MIDI | `moses/*`, `platform/*` | moses exports at zero or `n.a.` |
| WP-6 | medium | **QUEUEMSG opcodes**: measure which opcodes the tables of the 4 games use (script over `Shoot.dll`, `Frenzy.dll`, `quest.dll`, `Ruckus.dll`) and a test per opcode (today all 26 are in the code) | `tests/Pkg1_Test.cpp`, new script in `tools/` | opcode table with a "used by" column |
| WP-7 (**DONE 2026-10-20**, see `docs/WP7.md`) | medium | **Writing `.rrs` archives** (the editor must be able to save them): `Rrs` is read-only; add a writer (index, pool, keys, RLE DIBs, ADPCM/MIDI waves) with byte-for-byte round trip on the 4 games | `platform/Rrs.*`, `tools/rrs_extract.py` | `--rrs-test --roundtrip` green |
| WP-8 | medium | **Autoplay for all games**: `autoplay::Extension` extensions and scripts for Flying, Flounder, Ruckus | game repo | one script per game up to level 1 |
| WP-9 (**DONE 2026-10-20**, see `docs/WP9.md, docs/RAPTURE_INI.md`) | low | **`rapture.ini`**: `NoDirectDraw`, `RemapDrums` (MIDI backend), dead flags `ComposeRects`/`DisplayRects` as an optional debug overlay; `IniGetInt`/`IniWriteInt`/`IniWriteString` parser (0x1000ff40/ff60/ffa0) | `moses/*`, `aaron/RegKey.*` | 4 options `implemented` |
| WP-10 (**PARTLY DONE 2026-10-20**, branch `limits`, see `docs/LIMITS.md`) | low | **Real audio**: software MIDI sequencer with live mute/tempo and a backend interface (winmm / no-op / recording) DONE; real-mixer check without speakers through the SDL disk driver DONE (`--engine-test 11`, `tools/audio_disk_check.py`); **by-ear check of sounds, voice and MIDI still needs the user** | `platform/Audio*`, `Midi*` | check with the user present |
| WP-11 (**joystick part DONE 2026-10-20**, see `docs/LIMITS.md` section 5) | low | **Joystick, CD audio, About box** and the other PARTIAL entries of the reference game's bug/debug list; joystick reviewed against the original and tested with SDL virtual devices, real hardware still needs the user | `aaron/Input*`, `moses/AudioLib*` | PARTIAL entries closed |
| WP-12 | low | **Editor** (after WP-1..7): SDL2 app on top of the `rapture` library to browse archives, preview sprites/animations, edit and save | new folder `editor/` | prototype |

Recommended order: WP-1 and WP-2 (in parallel, different files) -> WP-3 -> WP-6/WP-7 -> WP-4/WP-5 -> the rest. After every package: `python -I engine/tools/engine_coverage.py`,
`engine/tools/build-engine.bat test`, update `HANDOFF.md`.

## 4. Generated status

<!-- BEGIN GENERATED -->
Generated by `engine/tools/engine_coverage.py` on 2026-10-20: **do not edit by hand** (re-run the script).
Statuses: `implemented` = present in the port (the original address is in a `.cpp`, or the reference game uses it); `partial` = only
declared / to be verified; `missing` = used by at least one game of the family but absent from the port; `unused` = no game of the family
imports it (for moses, not even Aaron): only needed for completeness (future editor); `n.a.` = compiler artefact.

### Summary

| category | total | implemented | partial | missing | unused | n.a. |
|---|---:|---:|---:|---:|---:|---:|
| moses.dll exports | 344 | 335 | 0 | 0 | 0 | 9 |
| Aaron.dll ordinals | 555 | 517 | 0 | 0 | 0 | 38 |
| QUEUEMSG opcodes (common 1..15, class-specific >= 100) | 26 | 26 | 0 | 0 | 0 | 0 |
| `.rrs` resource types | 11 | 11 | 0 | 0 | 0 | 0 |
| `rapture.ini` options | 4 | 4 | 0 | 0 | 0 | 0 |

Columns `used by`: M=MuShoot, F=Flying, Q=Flounder, R=Ruckus; a dot = not imported.

### To do: `missing` and `partial` exports/ordinals (used by at least one game)

| kind | ord | address | name | status | used by |
|---|---:|---|---|---|---|
| (none) | | | | | |

### moses.dll exports not `implemented` (full list; the `implemented` ones are in `ENGINE_API_COVERAGE.md` section 3)

| ord | address | name | status | used by | used by Aaron |
|---:|---|---|---|---|---|

### Aaron.dll ordinals not `implemented` (full list)

| ord | address | name | status | used by |
|---:|---|---|---|---|

### QUEUEMSG opcodes (`moses/QueueMsg.h`; 1..15 handled by `CAnimObj`, >= 100 by the class `process_message_hook`)

| opcode | value | meaning | status |
|---|---:|---|---|
| `QM_LOOP` | 1 | a = count (0 = infinite) | implemented |
| `QM_DELAY` | 2 | a = ms | implemented |
| `QM_LOOPEND` | 3 |  | implemented |
| `QM_FLIP` | 4 | a = fx, b = fy (FlipArg); executes the NEXT message immediately (batches the redraw) | implemented |
| `QM_CALL` | 5 | ptr = QMSG_CALLBACK, b = arg a, c = arg b      (via CAnimView::QCall) | implemented |
| `QM_SUBLIST` | 6 | ptr = QUEUEMSG**, b = count                    (push a sub list) | implemented |
| `QM_GOTO` | 7 | ptr = QUEUEMSG**, b = count                    (restart with another list) | implemented |
| `QM_LINETO` | 8 | a = x, b = y, c = d, s0 = relative, s1 = mode  (see moses.md sec. 5) | implemented |
| `QM_NOTIFY` | 9 | a, b, c -> CAnimView::OnNotify(obj, a, b, c) | implemented |
| `QM_MOVE` | 10 | a = dx, b = dy, c = relative(u8) / flipaware(u8)<<8; executes the next message at once | implemented |
| `QM_POST` | 11 | a = msg, b = wParam, c = lParam   -> PostMessage(view hwnd) (port: Scene message queue) | implemented |
| `QM_SEND` | 12 | same, SendMessage (port: immediate dispatch) | implemented |
| `QM_SYNC` | 13 | a = 0/1 ("no drift" timing) | implemented |
| `QM_SHOW` | 14 | a != 0 Show, else Hide | implemented |
| `QM_SETSPEED` | 15 | a = speed (ms per LINETO step), b = motion (ms per frame, 0 = leave) | implemented |
| `QM_CLASS_BASE` | 100 |  | implemented |
| `QM_ANIM` | 100 | a = delay ms (-1 = keep), b = group<<16 / first_index, c = last_index | implemented |
| `QM_ANIM_CB` | 101 | same as QM_ANIM + ptr = ANIM_CALLBACK (called after every frame) | implemented |
| `QM_ANIMLIST` | 102 | a = delay ms, b = count (<= 4), ids[] (shows the composition of several ids once) | implemented |
| `QM_SETRANGE` | 103 | a = group, b = start, c = lo, s0 = hi   (no animation, continues at once) | implemented |
| `QM_IRIS_CLOSE_XY` | 100 |  | implemented |
| `QM_IRIS_OPEN_XY` | 101 |  | implemented |
| `QM_IRIS_CLOSE_X` | 102 |  | implemented |
| `QM_IRIS_OPEN_X` | 103 |  | implemented |
| `QM_IRIS_CLOSE_Y` | 104 |  | implemented |
| `QM_IRIS_OPEN_Y` | 105 |  | implemented |

Values >= 100 are reused by different classes (`CAnimDib` 100..103, `CAnimRect` "iris" 100..105); `CAnimText/Poly/Meta` have no opcodes of their own.
Which opcodes the tables of each game use has not been measured yet (WP-6).

### `.rrs` resource types

| type | meaning | original handler | constant in the port | status | note |
|---:|---|---|---|---|---|
| 1 | alias | `moses.dll 0x1000c490` | `RT_ALIAS` | implemented |  |
| 2 | DIB (RLE 16 bit) | `moses.dll 0x1000c0d0` | `RT_DIB` | implemented |  |
| 3 | raw blob | `moses.dll 0x1000c52e` | `RT_BLOB` | implemented |  |
| 4 | palette (RGBQUAD[n]) | `moses.dll 0x1000c180` | `RT_PALETTE` | implemented | WP-5: CResfile::load_palette -> CDibPal, loaded by CResMgr; none of the archives of the 4 games contains one |
| 5 | sound (ADPCM wave / MIDI / AVI ref) | `moses.dll 0x1000c220` | `RT_SOUND` | implemented | decoding verified (--rrs-test); waves checked through the real mixer with the SDL disk driver (--engine-test 11); MIDI through the software sequencer (--engine-test 10); not listened to |
| 6 | group of ids (4 byte header) | `moses.dll 0x1000ca60` | `RT_GROUP` | implemented |  |
| 7 | DIBREF (same bitmap, other hot spot) | `moses.dll 0x1000c8e0` | `RT_DIBREF` | implemented |  |
| 8 | DIBLIST | `moses.dll 0x1000c850` | `RT_DIBLIST` | implemented |  |
| 9 | DIBTILE (mosaic of dibs) | `moses.dll 0x1000c970` | `RT_DIBTILE` | implemented | WP-1: CDibTile + load_dibtile; none of the archives of the 4 games contains type 9: Flounder builds its CDibTile at run time (`CDibTile::Init/SetDib`) and registers it as a dynamic resource; checked against the original in the Flounder port, exercised by the scrolling levels |
| 11 | group of ids (8 byte header) | `moses.dll 0x1000ca60` | `RT_GROUP2` | implemented |  |
| 12 | string | `moses.dll 0x1000c590` | `RT_STRING` | implemented |  |

### `rapture.ini` options (read by `IniGetInt` in moses.dll; file in the current folder)

| section | key | where | effect | status | note |
|---|---|---|---|---|---|
| `[drawlib]` | `ComposeRects` | `moses.dll 0x10009360` | debug flag "show recomposed rectangles" (CDrawSys+8) | implemented | read as in the original and left dead as in the original; optional port overlay: [port] DebugRects=1 or --debug-rects (docs/RAPTURE_INI.md) |
| `[drawlib]` | `DisplayRects` | `moses.dll 0x10009360` | debug flag "show display rectangles" (CDrawSys+0xc) | implemented | read and dead as in the original; see the DebugRects overlay |
| `[drawlib]` | `NoDirectDraw` | `moses.dll 0x10009420` | ChangeDisplayMode returns 0 at once (CDrawSys+4) | implemented | read (WP-9); the port never changes the video mode, so ChangeDisplayMode stays 1 (docs/WP9.md) |
| `[audio]` | `RemapDrums` | `moses.dll (AUDIOSTATE+0x3a)` | MIDI thread: channel 10 rewritten to channel 16 | implemented | WP-9 / limits: the software MIDI sequencer remaps channel 10 to 16 before the channel mute (platform/MidiSeq.cpp) |

<!-- END GENERATED -->

## 5. Notes for whoever updates the table

* The Aaron ordinal names come from the function name tables (`function_names.tsv`, `aaron_ordinals.tsv`; `to be analysed` = still `FUN_` in the original analysis output).
  Naming a function improves the table on the next run.
* The resource types and the `rapture.ini` options are hand-written tables in `engine/tools/completeness_report.py` (checked against
  `rrs_format.md` and the reference game's bug/debug list): if a new one is discovered, add it there.
* The QUEUEMSG opcodes are read from `engine/moses/QueueMsg.h` and searched for in the `.cpp` files of `engine/moses`.
* Reverse-engineering data (notes `moses.md`, `aaron.md`, `rrs_format.md`, `function_names.tsv`, analysis output per binary; `exports.txt` is the source of the names for this table)
  is kept outside this repository and is optional.

## 6. Known limits, state after the `limits` branch (2026-10-20; details in `LIMITS.md`)

| limit | state |
|---|---|
| MIDI without live mute / tempo, delta unit assumed | **fixed**: unit verified (1 ms, moses MIDI thread 0x10012de3); software sequencer with live mute, live tempo, loop, stop, the original Pause = stop + restart; winmm backend on Windows, no-op backend elsewhere, recording backend + virtual clock in the tests (`--engine-test 10`) |
| real audio never checked | **checked without speakers**: SDL `disk` driver capture, sample-exact for 22050 Hz mono waves (`--engine-test 11`, `tools/audio_disk_check.py`); **not listened to**; MIDI is not in the capture; CD audio only as WAV folders |
| unsupported EMF records | **fixed** except EMF+ and undecodable bitmaps (list in `DrawMeta.h`, 86 tests) |
| non-Windows build never tried | **MinGW `RAPTURE_FORCE_PORTABLE=ON` builds and passes every suite**; two real porting bugs fixed (path separators, settings folder); **no real Linux/macOS build** (no WSL distribution on the machine) |
| joystick only with fake devices | **reviewed** against the original (identical logic) and tested through SDL virtual joysticks end to end; **real hardware not tried** |
| physical CD drives (MCI cdaudio) | unchanged: not reproduced |
