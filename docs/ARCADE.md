# G1 - Arcade framework (`engine/arcade/`, namespace `shoot`)

> Note (2026-10-19): this engine is the `rapture-engine` repo, the `engine/` submodule of a per-game repo. MuShoot (`mushoot-rapture-port`) is the **reference game**
> cited in the examples (`mushoot.exe`, `Shoot.exe`/`Shoot.dll`). The analysis of the host `Shoot.exe` (moses, Aaron) and of `Shoot.dll` (the MuShoot scene) was done
> on the original binaries and is not part of this repository; it is mentioned only as provenance.

Faithful port of the "Arcade/GameBoard" library (Digital Doorway), present identically in `Shoot.dll` (0x10013de0-0x1001da80, the copy used) and statically linked in
`Shoot.exe` (0x40ba1f-0x40f6d0). Each function carries its `Shoot.dll` address in a comment; the twins in Shoot.exe are cited at the top of the files.
The command ids are **0xF0001001..0xF000101F** (the earlier notes wrote 0xF001xxxx: a typo).

## Files and classes
| file | classes (Shoot.dll address) |
|---|---|
| `ArcadeTypes.h` | `ArcadeCmd` (commands/states), `PopupType`, `DisplayStyle`, `HSTableDef`, `GameDef`, `ArcadeGameDef` (31 dwords of DAT_10031ae0), default tables (e60/e90/ec0/ef0/f20/f50), `IPopupFactory` |
| `Property.h/.cpp` | `CProperty` (is a `Broadcaster`), `CPropNumeric` 0x10016e7d, `CPropScore` 0x10016e3e, `CPropertyList` 0x1001c2e0 |
| `Player.h/.cpp` | `CPlayerBase` 0x100167e0 (input: controller + 3 sources), `CPlayer` 0x100161a0 (score/lives/level/difficulty, extra life) |
| `Events.h/.cpp` | `IEvent`, `CEventList` 0x1001bca0, `CEventGroupList` 0x100177e0, `CEventSequencer` 0x100179d0, `CEventSpriteSequencer` 0x10019f50, `CEventUserSprite` 0x10017246, `CEventUserFunc` 0x10017f06, `CEventSoundTimer` 0x10019cf0, `CEventRandomSelect` 0x10013e50 (in the tsv: CEventListDerived), `CEventRandomList` 0x10013930, `CHunRandomList` 0x10013de0, `CQueueMsgArray` 0x100134b0 |
| `Display.h/.cpp` | `CDisplay` 0x1001a5cf/0x1001a72f (digit HUD, animated counter, timer), `CHSDisplay` 0x1001d940 |
| `HighScores.h/.cpp` | `CHighScoreTable` 0x1001cad0 (Shoot.exe: `CHighScores` 0x40d0b0) |
| `GameBase.h/.cpp` | `CGameBase` 0x100144a8 (scene: players, states, iris transitions, pause, mute, `Settings`/`GBxxx`) |
| `ArcadeGame.h/.cpp` | `CArcadeGame` 0x1001850f (HUD, record table, global level, new game/game over/pause commands, `AGxxx`) |
| `ArcadeTest.cpp` | `RunArcadeTests` (`<game>.exe --game-test arcade`) |

## Virtual hooks for G2/G3
For each virtual, `CGameBase` lists the original vtable offset (e.g. `+0xb8`). The ones that Mushoot (scene 0x10023d30) overrides: `CreatePlayer` (mandatory),
`Ev98/Ev9C/EvA4/EvA8(int)/EvB0`, `StateC0`..., `Resume`, `ResetGame`, `GetBonus`, `OnCreate`... A derived scene must call the bases' `DestroyMembers`
(the implementation is idempotent). `CPlayer::OnExtraLife` is the extra-life hook (slot 0x70). The user functions of the events have the signature
`void* fn(CEventUserFunc*, p1, p2, phase, data)`; `CArcadeGame::HudEventFunc` and `CGameBase::TransitionMsgCallback` are already provided.
**Popup factory (G2):** implement `IPopupFactory` (`CreateObject(parent, type, arg)`, types in `PopupType`) in `CShootApp` and register it with `SetPopupFactory`.
Save/restore (`CSaveDlg`) uses `scene->SerializeGlobals(RegKey*, save)` (vtbl 0x114), not the app.

## Names corrected with respect to the earlier notes
- `CPropertyList` 0x1001c768/0x1001c807: the notes call them Set/AddValue; the code calls `prop->Add` and `prop->Set` respectively (here `AddValue`/`SetValue`).
- 0x1001960c is not "OnPause" but the Aaron `OnCreate` (slot 0x18); 0x100195b6 is `OnStart` (starts the first player).
- "GetGameKind" (0x10012d90, def[1]) is the **device mask** (bit0 joystick, bit1 mouse, bit2 keyboard): Mushoot = 2.
- `CGameBase::GetCurrentPlayerAlive` returns the player's controller (+0x14). "PrevLevel" 0x100198fa does `Set` (`SetLevelAll`).
- Flag +0x6ec: it is the iris state (1 = closed), not "input enabled". Transition types: 0/2/4 close (XY, Y, X), 1/3/5 open.
- `PlaySound(res, a, pan, vol, b, ...)`: `a` = LOOP, `b` = MIX (non-loop) according to the engine's reading; the calls are transcribed 1:1.

## Preserved quirks
- `CPropNumeric::Set` always notifies; with max < min (Mushoot's lives: 0, -1) every value > min becomes `max`.
- Saving `GBP%iProp%d` and `AGP%i*` writes the **snapshots** (not the current values); `AGLevel` writes the current value.
- Extra life: only one per notification even if several thresholds are exceeded; the threshold is active only after `ResetGame`/`SetExtraLifeThreshold`.
- `CPlayer::Serialize`: difficulty read from the low byte only. Arcade `GetSettingsSize` = slots*4+0x18 (exact only for 2 slots).
- `CHighScoreTable`: ties go ABOVE the old entries; `Load` reuses the buffer between rows (missing row = copy of the previous one); `CreateSprites` zeroes the initials in memory.
- `CEventList::Remove` decrements the refcount even if the event is not in the list; `CEventSequencer::Advance` stalls if `End()` returns false; `ending_` is cleared after the first `Start`.
- `CDisplay`: thousands threshold with left alignment always shows ','; `CountStep` compares unsigned.
- `CEventSpriteSequencer::AddSprite` with a failed creation returns the record not inserted (leak).

## Deviations from the engine (see `ENGINE_REQUESTS.md`)
(Inverted input lock and event pointer in QM_NOTIFY: resolved in the engine, workarounds removed), `CPlayer::SetExtraLifeThreshold` guarded against division by 0,
`CArcadeGame::DestroyMembers` chains to the base (the original does not), the sprite records are freed.

## Verified / not verified
Headless tests (`--game-test arcade`, 233 checks, temporary .ini): properties and observers, property list, extra life, persistence of `Settings`/`GB*`/`AG*`,
record table (insertion, ties, filling, 0x18-byte format, missing row), sequencer/group/nesting/Stop/modes 1-2 with a fake clock,
`CEventUserFunc` (phases 1/2/3), random selection, random lists, `CQueueMsgArray`, game-over popup flow (single/simultaneous), pause/menu resume,
iris transitions driven by `CAnimView::Service` with a fake clock.
**Not verified** (needs .rrs resources/window): drawing of `CDisplay`/table (real digits), `CEventUserSprite` with animation, sounds, `OnCreate`/pause overlay.

## Open doubts
- Semantics of `CPropertyList::SetAt` (capacity of `PtrArray`), of `Scene::SetOverlayRes`, of the 0x1000/0x2000/0x8 flags of `CDisplay`.
- The real type of the 4 resids +0x768..0x774 (def[22..25]) and of the def[26] table: used by G2 (Save/Restore).
- Whether the `DestroyMembers` of the Mushoot scene also calls that of `CArcadeGame`/`CGameBase` (here it is idempotent).
