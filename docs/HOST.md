# G2 - Shoot.exe (`engine/host/`, namespace `shoot`)

> Note (2026-10-19): this engine is the `rapture-engine` repo, the `engine/` submodule of a per-game repo. MuShoot (`mushoot-rapture-port`) is the **reference game**
> cited in the examples (`mushoot.exe`, `Shoot.exe`/`Shoot.dll`). The analysis of the host `Shoot.exe` (moses, Aaron) and of `Shoot.dll` (the MuShoot scene) was done
> on the original binaries and is not part of this repository; it is mentioned only as provenance.

Faithful port of `Shoot.exe` (host + "Title Screen" scene + popup library). Each function carries its address as `// Shoot.exe 0x...`.
Derived from analysis of the binaries (notes and decompilation are not part of this repository).

## Files
| file | contents |
|---|---|
| `HostApp.h/.cpp` | `CShootApp` (strings, volumes, `FeedKeySequences`, `CreateObject` = G1's `IPopupFactory`, `CreateCredits`), `RunHost` (= `WinMain` 0x401b6f) |
| `Strings.inc` | Italian RT_STRING table (23 entries, ids 0x5007..0x501d), **generated from the user's own executable** by `engine/tools/extract_host_strings.py` |
| `Popups.h`, `Popups.cpp`, `Popups2.cpp` | `CPopup` (base on `aaron::Dialog`), `COptionsMenu`, `CNewGameDlg`/`CNewGameDlg2`, `CSaveDlg`, `CRestoreDlg`, `CListHit`, `CAudioDlg`, `CHighScoreDlg`, `CNameEntryDlg`/`CNameEntryDlg2`, confirmations (`CQuitConfirmDlg`, `CResetScoresConfirmDlg`, `CSaveConfirmDlg`, `CRestoreConfirmDlg`), `CPlayAgainDlg`, `CCreditsScreen` |
| `TitleScene.h/.cpp` | `CTitleScene` ("Title Screen"), `CreateTitleScene` (= `CreateScene` 0x40958d) |
| `ShootExeTest.cpp` | `<game>.exe --game-test host` |

`game/main.cpp`: the normal path now calls `shoot::RunHost`; `--selftest`, `--rrs-test`, `--movie`, `--engine-test`, `--game-test` remain.
The game must be started with the game folder as the current directory (if `mus.rrs` is not there, `main` tries the exe folder and the two above it).

## How it is wired
* `CShootApp::BuildAppInit` replicates WinMain: title = string 0x5014 ("Disney SparaMu"), registry key
  `Software\Disney Interactive\MuShoot HotShot\IT` (strings 0x5017 + 0x501a + 0x501d), initial scene = `CreateTitleScene`.
* `RunHost` registers the app as `IPopupFactory` (`SetPopupFactory`): `CGameBase::CreatePopup` (G1) calls `CreateObject`. Types in `PopupType`.
* `CShootApp::LoadString(id)` = `GetString` 0x4010ab (resource `id + 0x5006`; 0x5010 is a `printf` with the title). `OnDestroy` saves `WaveVol/MidiVol`,
  `LoadScene` loads them (0x401a3c / 0x401a71).
* The title scene loads `Shoot.DLL`/`CreateScene` with `Application::GetModule`: **G3 must call `aaron::RegisterScene("Shoot.DLL", "CreateScene", ...)`**.
  As long as it is not registered, `GetModule` prints the error on stderr and the title closes the application (graceful handling, not in the original).
* Second visit (coming back from the game with `CMD_LEAVE`, G1): MIDI `MusFX.RRS` 0x1850000 + modal credits; at the end of the credits `Application::Quit`.
* Intro: `DILogo.avi`/`DDLogo.avi` are sound kind 3 of Frenzy.RRS (0x64850000/1), played with `Scene::PlaySoundSimple`; title = `AnimSprite`
  0x658b0025 with the queue {Anim 2000 ms, Anim 83 ms frames 1..3, Show(0), Notify 0x1400} (table 0x4184e8 decoded).
* Credits: queue rebuilt at run time (the table at 0x4184f8.. holds the text height): Delay 500, Move(0, 240-top), SetSpeed(20,20), Show, LineTo(0, -(240-top), 1 px/step), Delay 1000, Call(close).

## Dependencies / notes for the other packages
* **Save/Restore list table** (`ArcadeGameDef::listTable`, `scene+0x778`): G1 types it as `const HSTableDef*`, but the real content is an
  array of 24+ integers (`SlotListLayout` in `Popups.h`: rows, z, step, score/level/icon columns, rectangle, highlight colour, slot count).
  Mushoot: `Shoot.dll 0x10031a80` = `{6, 9990, 28, -147,-72,12,8,128, -30,-72,12,3,128, 93,-73, -190,-87,168, 2752446, -185,-72,12,2,128, 0, 2}`.
  G3 must provide an `int[26]` with that layout (cast to `HSTableDef*`); the popups read it back with `reinterpret_cast`.
  Resources used by the lists: `spare22to25[0..3]` = score digits, level digits, (icon, ignored: it is the fixed `0x8b000d`), slot-number digits.
* `FeedKeySequences(IKeySequenceSink*)`: the original calls `listener->vtbl[7](seq, key, first)` on an input object of Shoot.dll (the port's `Controller` has no
  key sequences). `IKeySequenceSink` is the interface G3 can implement ("A" = 0x10, "DB" = 0x20; purpose unknown: keyboard cheats).
* Popups 8 / 6 / 7 / 0x10 / 0x11 use `CArcadeGame::hsTable`; Save/Restore use the scene's `SerializeGlobals(key, save)` (vtbl 0x114).
* Code written against engine master 7d7bbf9 (`FocusMgr::Lock` fixed, `OnObjectEvent2(Widget*, intptr_t a,b,c)`): `CTitleScene` overrides `OnObjectEvent2`.

## Preserved quirks
* `Dialog::Close` deletes the popup: no handler touches `this` afterwards. `CCreditsScreen::OnKey`: the original calls `Dialog::OnKey` on the popup just deleted (SPACE is not handled by the base); here it returns 0.
* The "Help" button of the Options menu calls vtbl +0x90 of the scene = `CGameBase::ShowMenu` (not WinHelp).
* `CNewGameDlg2::OnClose`: closing with 0 (ESC) starts the new game anyway; result 2 = `PostCommand`.
* The confirmations (`CConfirmDlg`) free `0x860043` without having loaded it (the engine's refcount stops at 0). `CQuitConfirmDlg::PostCmd1012` is dead code.
* The direct Restore (`DoRestore`, empty slot) is unreachable: OK is disabled on empty slots; on occupied ones it asks for confirmation (`CRestoreConfirmDlg`).
* `CListHit`: cell width is rounded up if the *quotient* is non-zero (the height uses the remainder). Division by zero avoided.
* A SPACE/ESC on the first logo sets `step = 1` by hand and the end-of-sound brings it to 2: a single skip also skips DDLogo.
* Device selection in New game: same crossed logic (mouse/keyboard/joystick not shareable if `simultaneous`); the creation order of the buttons (radio rings with style 0x200) is the original one.
* The receivers missing from the decompilation (`Widget::EnableCount/SetToggled/IsToggled`) were recovered by disassembling (capstone) the `mov ecx,[this+off]` instructions.

## Tests (`--game-test host`, run from the game folder)
169 checks: string table, `LoadString`, key sequences, factory for every type (creation/closing on a headless `CArcadeGame` scene with the real `mus.rrs`),
options menu -> sub-popups, New game (2 players + hard -> `SetNumPlayers`, `CMD_NEWGAME`), record entry + initials (persistence in the registry),
record table + reset with confirmation, Save/Restore on `Save\<n>` (arrows, OK enabling, confirmations, mouse on `CListHit`), Audio -> `SetMute`, Play again, credits,
title scene with a fake clock (intro -> command 0x2400, skip keys, second visit -> 0x1401). Without `mus.rrs`/`Frenzy.RRS` in the current folder the groups are SKIPPED.

**Not verified** (needs a window/audio): real rendering of the popups, AVI playback, on-screen credits scrolling, `Controller` with the chosen devices,
`Shoot.DLL` (G3).
