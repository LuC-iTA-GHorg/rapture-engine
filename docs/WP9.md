# WP-9: rapture.ini

Status: done, test `--engine-test 9` (`tests/Ini_Test.cpp`, 63 checks). User documentation: `docs/RAPTURE_INI.md`.

* `moses/Ini.*`: `IniGetInt` 0x1000ff40, `IniWriteInt` 0x1000ff60, `IniWriteString` 0x1000ffa0 on `./rapture.ini`. Windows `GetPrivateProfileInt`
  semantics reproduced (case-insensitive, decimal only, empty -> default, default sign-extended from short).
* `CDrawSys` (0x10009360) now reads `[drawlib] ComposeRects/DisplayRects/NoDirectDraw` as shorts (also triggered from `CDrawView::Init`). `NoDirectDraw`
  is stored; `ChangeDisplayMode` still returns 1 (port has no mode change; returning 0 would break `Application::SetDisplayMode`).
* Findings: only 4 `IniGetInt` calls exist in moses.dll, none in Aaron.dll; IniWriteInt/IniWriteString have no callers.
* `moses/DebugOverlay.*`: opt-in overlay (`[port] DebugRects=1` or `--debug-rects`), hooks: `CDrawView::Display` (records) and `PresentTo`
  (full repaint + draw). Dead flags stay dead.
* `RemapDrums`: `CAudioLib` ctor reads `[audio] RemapDrums` into `remap_drums_` (+0x3a); `MidiRemapDrumsMsg` / `MidiPrepareCompact` in
  `platform/Midi.*` (remap `| 0xf` on low nibble 9, then mute test on the remapped channel, as in thread_proc 0x10012de3).
  `AudioLib.cpp::PrepareMidi` now delegates to it (behaviour unchanged when the option is off).
* Public header changes (additive): `CDrawSys::CDrawSys/ReloadIni/NoDirectDraw`, `CAudioLib::RemapDrums`, `tests/EngineTests.h: RunPkg9Tests`;
  `HostMain` handles `--engine-test 9` and `--debug-rects`; `build-engine.bat test` runs package 9.
* Integrator: update the `rapture.ini` table in `docs/COMPLETENESS.md` (4 options now implemented), and the game repo's
  `docs/DEBUG_CHEATS_BUGS.md` 2.1 row ("Port: no rapture.ini reading", i.e. the port did not read rapture.ini).
