# WORKPLAN — implementation of the engine in 4 parallel packages (+ game packages)

Prerequisite: read `README.md` (this folder). The headers in `moses/` and `aaron/` are the
**contract**: they are already compiled by `HeadersCompile.cpp` (`& .\build.bat` from `src` must stay green).
References to analysis notes (`moses.md`, `aaron.md`, `shoot_exe.md`, ...) and to decompiled output are provenance remarks: that analysis material was derived from the binaries
and is kept outside this repository; nothing here requires it to build or test.

## Common rules (valid for everyone)

1. **File ownership.** A package writes *only* the `.cpp` files (and the private headers `*_priv.h`/`*Raster.h`) listed below. The `.cpp` files
   of the other packages are not touched. `engine/platform/` is not modified (if a platform header prevents compilation: report it, do not fix it).
2. **Public headers.** Every header has a single owner (table below). The owner may modify it **only additively** (new methods/fields,
   comments, fixing a `TODO`) and must note every change in `engine/CHANGES.md` (one line: date, header, what, why). Changing or removing a signature
   already used by another package requires writing it in `CHANGES.md` under "REQUESTS" and waiting for the integrator. The other packages never edit the header.
3. **Fidelity.** Logic 1:1 with the original (addresses in comments `// moses.dll 0x…`), quirks preserved and flagged. If the original is unclear:
   look at the disassembly/decompiler output of the original binary at the given address, then `// TODO(pkgN)` and proceed with the simplest hypothesis.
4. **Build always green.** All the `.cpp` files under `source/` go into the same `mushoot.exe` (CMake `GLOB_RECURSE`). If your code references a symbol of another
   package not yet delivered, define it as a **weak symbol** in `engine/stubs/PkgN_Stubs.cpp` (`__attribute__((weak))`): when the real (strong) one arrives the
   linker prefers it, without duplicate errors; the stub is deleted at integration.
5. **Headless tests.** Every package provides `int RunPkgNTests(int, char**)` in `engine/tests/PkgN_Test.cpp` (declared in `tests/EngineTests.h`);
   `mushoot.exe --engine-test N` runs it (dispatch in `main.cpp`, added by package 3 — until then the others can call it from a temporary local `main`).
   No window, no real audio: only `Surface` and comparisons of pixels/numbers. **Do not launch the game.**
6. **No `<windows.h>`** in the `.cpp` files (README §2), no SDL outside the platform except `SDL_GetTicks` already exposed as `::Window::Ticks()`.
7. Comments and documentation in English. No `git commit` by the agents.

## Header ownership

| header | package |
|---|---|
| `moses/Types.h`, `QueueMsg.h`, `DibData.h`, `DrawObj.h`, `DrawGdi.h`, `DrawView.h`, `AnimView.h`, `AnimObjs.h` | **1** |
| `moses/Mem.h`, `ResMgr.h`, `AudioLib.h` | **2** |
| `aaron/Types.h`, `RObject.h`, `RegKey.h`, `Module.h`, `Window.h`, `Application.h`, `Scene.h`, `Managers.h`, `Input.h`, `ResArchive.h`, `Ordinals.h`, `Aaron.h` | **3** |
| `aaron/Widget.h`, `Sprites.h` | **4** |
| `moses/Moses.h`, `HeadersCompile.cpp`, `tests/EngineTests.h`, `README.md`, `WORKPLAN.md`, `CHANGES.md` | integrator |

## Recommended order and dependencies

```
Pkg1 ──► Pkg3 (Scene is a CAnimView)  ──► Pkg4 (the widgets use Scene/ResTable/MsgQueue)
Pkg2 ──► Pkg3 (ResArchive : CResMgr, AudioMgr : CAudioLib)
Pkg1 (DibData.cpp) ──► Pkg2 (CResMgr creates CDibData)
```
Milestone M0 (immediately, before the rest): **Pkg1 delivers `moses/DibData.cpp`** (and Pkg3 `aaron/RObject.cpp`): they are used by everyone. Then the work proceeds in parallel thanks to the weak stubs.

---------------------------------------------------------------------------------------------------

## Package 1 — moses: drawing, composition, animation

**`.cpp` files (all new, all its own):**
```
engine/moses/DibData.cpp      CDibObj, CDibData, CDibList, CDibSect               (M0)
engine/moses/DrawObj.cpp      CDrawObj, CDrawDib
engine/moses/DrawView.cpp     CDrawView, CDrawSys/DrawSys(), dirty rect + PresentTo
engine/moses/DrawGdi.cpp      CDrawGdi, CDrawRect, CDrawPoly (+ iris hole)
engine/moses/DrawText.cpp     CDrawText, CTextHelper ("rich" text, mini-language below)
engine/moses/GdiRaster.h/.cpp private rasteriser: rectangle/round/ellipse/lines/polygons on GdiCtx, font + text measurement
engine/moses/AnimView.cpp     CAnimView (Service, QStart/QStop/QCall, Pause, OnNotify, Now())
engine/moses/AnimObj.cpp      CAnimObj (state machine of the QUEUEMSGs)
engine/moses/AnimObjs.cpp     CAnimDib, CAnimRect (iris), CAnimText, CAnimPoly
engine/tests/Pkg1_Test.cpp    RunPkg1Tests
engine/stubs/Pkg1_Stubs.cpp   (if needed)
```
**To be implemented with care (behaviour of the original moses.dll):**
* `CDibObj::center_bounds` / `GetBounds(flip)` / `OffsetPos`; `CDibData::DibBlt` (flip X/Y, local srcRect, three DROPs, `solid`→rop 1, `flag21`→nothing; **do not** use `BlitHot`), `GetPixel` (0/1), `DibSet`, `CDibList` (union, order = z).
* `CDrawView::Display` exactly as the original: background (`surface_color`/DIB/none), list from the **lowest** level, `visible`, `level >= min_compose_level`,
  clip per object, `scrollable`; then dirty rect + flag (no blit). `SetLevel`/`link_object` (descending order, at equal level the last inserted goes on top), `SetMinComposeLevel`,
  `SetClipBox`, `SetRedraw(on, repaint)`, `IsPicked` (from the head), `PtInObject`, `IsContacting`, `Scroll`, `SetResProc/CallResProc`.
  `PresentTo(fb, all)` copies the dirty rectangle into the 640x480 framebuffer (the surfaces share the same origin).
* `CDrawObj::Update` / `set_pos_impl` / `set_flip_impl` / `recalc_bounds` / `erase_old_area` / `Show` / `Hide` / `IsPicked` (pickmode) / `IsContacting` (pixel scan of the intersection).
* `CDrawDib::Animate` (1–4 ids via `CallResProc(this,1,&id)`, `CDibList`, release with mode 0), `AnimateDib`, `Compose`, `get_bounds`, `GetPixel/InObject`.
* `CAnimObj`: `next_message` / `process_message` / `queue_start` / `queue_stop`, all the opcodes 1–0xf (infinite LOOP = 0, stack depth 4, `LINETO`
  `start + (end-start)*i/steps` with integer division and speed mode `ceil(max(|dx|,|dy|)/d)`, `DELAY` with `sync` without drift, `FLIP`/`MOVE` that execute the following message immediately,
  `Pause` that shifts the timers, `QM_POST/SEND` → `CAnimView::OnNotify`-like hook: **route to the already declared hook `CAnimView::SetPostProc(fn,data)`** (`ANIMPOSTPROC`; Pkg3 connects it to `Scene::PostMessage` in `Scene::Initialize`).
* `CAnimView::Service` non-reentrant (`iter`), `QCall` with guard `priv.cur`, `Now()` = `::Window::Ticks()`.
* `CAnimDib`: `set_range`, `process_animate` (delay -1 = hold, `remaining`, callback op 101), `process_animatelist`, `update_index` (wrap lo..hi, returns 0 if no redraw is needed),
  hooks 100–103, `IsRange`, `Copy`. `CAnimRect` iris (hole formula `left = i*w/(2*steps-1)`).
* GDI: rectangle/ellipse/round-rect with pen (`LOGPEN`) and brush, hole, polygons, text with `SetString(str, rich)` (mini-language) and `DT_*` alignments. Font: choose an embedded (bitmap) font or, only in a dedicated
  `*_win.cpp` file, Windows GDI; document the choice.
* Minimum tests: composition of 3 objects with different levels, flip, DROP, `Update` that moves an object (old pixels erased), `IsPicked`, `IsContacting`, sequence `LOOP/DELAY/LINETO/ANIM` with fake
  time (`SetTimeSource`), iris.

## Package 2 — moses: resources and audio

**`.cpp` files:**
```
engine/moses/Mem.cpp          CMem, FileExists/GetPathAndFile/MakeFilePath/MakeFullPath/DebugMsg/DebugStr, MosesInitLibrary/FreeLibrary
engine/moses/Resfile.cpp      CResfile (on top of rrs::Archive)
engine/moses/ResMgr.cpp       CResMgr (refcounted cache, groups, dibref, diblist, strings, GetIndexedResId)
engine/moses/AudioLib.cpp     CAudioLib (on top of Audio/MidiPlayer/PlayMovie) + CAudioSnd
engine/tests/Pkg2_Test.cpp    RunPkg2Tests (uses the real .rrs if present in the current folder, otherwise SKIP)
engine/stubs/Pkg2_Stubs.cpp   (if needed)
```
**Key points:**
* `Load(id, ctx)` per type (see the comment in `ResMgr.h`): 2→`CDibData` (from `DecodeDib`, hot-spot from the DIBREC), 7→copy with `OffsetPos(x-orig.x, y-orig.y)` (moses `load_dibref`: mind the signs, `rrs_format.md` §2),
  8→`CDibList`, 6/11→recursive `GROUPREC`, 5→`CAudioSnd` (`LoadSound` + `SoundCreate` + `SoundSetId(id)`), 3→blob, 12→string; alias (1 level); refcount per `id & 0xffff`; `Free` at 0 destroys according to the type.
  `GetObj` = already loaded object otherwise `Load` (without double increment: **verify the original code near 0x1000cd?? in a disassembly** and note it).
* `CAudioLib`: `SoundPlay` (flag `PSF_MIX`: without it, stops the instances with the same id; `PSF_LOOP`; volume 0..100 → `Audio::Play`; pan = `pan_x + pan` mapped to -1..1 with `pan_w`), `SoundStop(nullptr)` stops everything,
  MIDI (`CompactMidiToSmf` + `MidiPlayer`, priority `snd->param`: a new MIDI with lower priority is refused if a more important one is playing, `MidiSetChannel/Tempo/BreakLoops`),
  `mute/volume` per type (internal state → `Audio::SetMasterVolume` / gains), `Pause/Activate/Reset`, notifications `SND_STARTED/SND_ENDED` emitted by `Service()` on the main thread
  (`Audio::IsPlaying`), AVI kind 3 with `PlayMovie` if `SetHostWindow` is set (otherwise it notifies the end immediately).
* Tests: open every `.rrs` (if it exists), `Load` of all the ids and `FreeAll` without leaks (`CMem::GetMemInfo` is not needed: count), groups/dibref/diblist, strings of `frenzylz.rrs`; audio with a synthetic `PcmBuffer` and `Audio` not open (the calls must be safe no-ops).

## Package 3 — aaron: application, window, scene, loop, timer, input, registry

**`.cpp` files:**
```
engine/aaron/RObject.cpp      RObject, PtrArray, TreeNode, Broadcaster, Subscriber, SceneListener, Random/SeedRandom   (M0)
engine/aaron/RegKey.cpp       RegKey on .ini (%APPDATA%\MuShoot\mushoot.ini; fallback current folder)
engine/aaron/Module.cpp       static registry of the scenes (RegisterScene/FindScene) + Module
engine/aaron/Window.cpp       aaron::Window (SDL window, event translation, PresentIfDirty, LoadSceneNow)
engine/aaron/Application.cpp  Application (Run, InitInstance, MessageLoop, RunFrame, WndProc, arguments, GetModule, ReportError, SetDisplayMode)
engine/aaron/Scene.cpp        Scene (Initialize/Destroy, RunFrame, OnMessage, OnCommand…, HandleMouse/HandleKey, ResProc)
engine/aaron/SceneRes.cpp     parts of Scene: OpenArchive/LoadResource/FreeResource/LoadString/GetIndexedResId, PlaySound*/StopSound*, SetBackground/SetOverlayRes
engine/aaron/ResArchive.cpp   ResArchive, ResTable
engine/aaron/Managers.cpp     MsgQueue, TimerMgr, IdleTimer, AccelTable, FocusMgr, CursorMgr, PickMgr, AudioMgr, JoystickMgr
engine/aaron/Input.cpp        Controller, InputSource/Joystick/Keyboard/MouseSource, SdlKeyToVk, SdlModToAmod
engine/tests/Pkg3_Test.cpp    RunPkg3Tests
game/main.cpp                      ONLY the addition of `--engine-test N` (do not remove the existing modes)
engine/stubs/Pkg3_Stubs.cpp   (if needed)
```
**Key points:**
* Loop (`README §4`): `RunFrame` in the described order; a single `Present` per iteration; `AMSG_LOADSCENE` executed at the start of the iteration; `Quit()` sets the flag.
* `Scene::Initialize`: creation order and hooks (`OpenResources → Resize → OnPreCreate → OnCreate → OnStart → Service → OnReady`); `Resize` sets `origin_ = (w/2,h/2)` and calls `CDrawView::Resize`;
  `SetResProc` with `ResProcThunk` (mode 1 loads via `ResTable::Load`, ids without bit `0x800000` resolved with `GetIndexedResId(obj->GetResId(), id & 0xffff)`; mode 0 frees); a widget's `CDrawObj` has `GetId()` = `Widget*`.
* Mouse coordinates: `x - origin_.x`. `CursorMgr::OnMouse` → capture or `PickMgr`. Keyboard: `SdlKeyToVk` (ASCII capital letters, arrows 0x25–0x28, F-keys 0x70+, Ctrl/Shift/Alt 0x11/0x10/0x12, Space, Esc, Return, keypad `*`/`+`/`-`) and `SdlModToAmod`.
* `MsgQueue` FIFO with logical double buffer; `OnMessage`: `0x111` → `OnCommand`, `0xc06` → `Widget::OnNotify`, `0xc08/09` → listeners; messages `>= 0x1400` to the scene.
* `RegKey`: `.ini` format described in the comment in `RegKey.h`; lazy loading, `FlushIfDue` from the loop, `FlushAll` at exit; `GetBinary` with default; recursive `DeleteKey`. Keys used by the game: `HighScore\<n>`, `Save\<n>`, `Settings`, `GBNumPlayers`, `GBCurPlayer`, `GBP%iProp%d`, `AGLevel`, `AGP%iScore|Level|Lives|Difficulty`, `FKeys`, `WaveVol`, `MidiVol`.
* `Application::Run` / `ParseArg` (`-x val`, `/x val`, bare argument = scene dll), `GetModule` with cache and `ReportError(ERR_SCENE_*)` if the registry does not contain the pair; `AcquireArchive` shared per path.
* `AudioMgr`: keys `*` (mute wave with Shift, midi with Ctrl), `+`/`-` ±10, `LoadVolumes/SaveVolumes` (`WaveVol`/`MidiVol`).
* Tests: loop with a fake scene and fake time (anim `QStart` + `Service` + `PresentTo`), message queue, accelerator, RegKey write/re-read/DeleteKey, `Random` (MSVC sequence), `TreeNode` (DeleteChildren), `Broadcaster`.

## Package 4 — aaron: widgets, dialogs, text

**`.cpp` files:**
```
engine/aaron/Widget.cpp        Widget, Node, WidgetParent -> link to the parent/scene, InitOrDelete, EnableCount, ShowChildren, Activate, Capture, OnNotify, default OnKey/OnPick, PostNotify
engine/aaron/Button.cpp        Button (3 frames up/over/down, radio groups, toggle, mode 0/1/3/9)
engine/aaron/Dialog.cpp        Dialog (modal, Create/Close/OnKey, default button, style policies)
engine/aaron/SpriteWidgets.cpp DibSprite, AnimSprite (Play), RectSprite, AnimRectSprite, AnimTextSprite
engine/aaron/TimerWidget.cpp   Timer (Start/Stop/OnTick -> AMSG_NOTIFY)
engine/tests/Pkg4_Test.cpp     RunPkg4Tests
engine/stubs/Pkg4_Stubs.cpp    (if needed)
```
**Key points:**
* Constructors `Widget(parent|scene, resId, x, y, level, cmdId, style)`: links to the parent's `TreeNode` (scene → `RootWidget()`), `scene_ = scene`, the embedded `CDrawObj` receives `SetId((uintptr_t)this)`.
  `Create()` loads the resource (`Scene::LoadResource` / `CDrawDib::Animate(id)`) and shows the sprite (unless style `0x01` = hidden). The constructor signatures with `TODO` must be verified against
  the disassembly of Aaron (addresses in the comments; usage examples in the host exe).
* `Button`: states (normal/over/pressed/disabled × toggle) = frames of group `0x8b`; `OnMouse` press/release → `PostNotify(cmdId)` or `scene->PostMessage(0x111, cmdId, state)` depending on style; `mode` 9 = radio group (deselects the siblings
  with the same `mode`), 3 = checkbox (`IsToggled/SetToggled`), 1 = default. Verify in `Button::Button` 0x10003129.
* `Dialog`: modal panel (`Create` 0x10003980 → `OnInit`), `Close(result, notify)`, `OnKey` (Enter = default button `+0x74`, Esc = close), blocks the input of the scene underneath (style policies `0x40000…`) and restores it; `GetPickFlags` 0x80.
* `AnimSprite::Play(resId, msgs, n, stopFirst)` (changes the resource if different, then `CAnimView::QStart(this, msgs, n)`); `AnimRectSprite` = iris; `AnimTextSprite`/scrolling credits text (`CAnimText` with `SetString(str, 1)`).
* `Timer`: subscribes to the scene's `TimerMgr`; every period it increments the count and **posts `AMSG_NOTIFY`** (→ `Widget::OnNotify(id, timer)` on the parent). Clarify in the disassembly the meaning of `userData` (repetitions).
* Tests: widget tree with parent/children and `DeleteChildren`, pixel hit-test of `DibSprite`, button with simulated click (calling `OnMouse`), dialog `Close`, `Timer` with fake `TimerMgr::Tick`, `InitOrDelete` that fails.

---------------------------------------------------------------------------------------------------

## After the engine: game packages (none writes code yet)

The "Arcade/GameBoard" library exists in two identical copies (static in `Shoot.exe` and in `Shoot.dll`): in the port it is **a single one**.
Convention: `game/<area>/…`, namespace `shoot`. 1:1 transcription of the original behaviour, every function with the original address (`// Shoot.dll 0x10004154`).

| package | content | notes/sources |
|---|---|---|
| **G1 Arcade framework** `engine/arcade/` | `CGameBase`/`CArcadeGame` (commands `0xF001xxxx`, states, transitions, pause, mute, `Settings`/`GB*`/`AG*` on RegKey), players and properties (`CPlayerBase`, `CPlayer`, `CPropNumeric`, `CPropScore`, `CPropertyList`), events/sequences (`CEventList`, `CEventGroupList`, `CEventSequencer`, `CEventUserSprite`, `CEventUserFunc`, `CEventSpriteSequencer`, `CEventSoundTimer`, `CEventRandomList`), `CDisplay`/`CHSDisplay`, `CHighScores` | analysis of `Shoot.dll` (range >= 0x10013de0) and of `Shoot.exe` §6; requires aaron Scene/Widget |
| **G2 Shoot.exe** `engine/host/` + `game/main.cpp` | `CShootApp` (strings, `CreateObject` = popup factory, `FeedKeySequences`, volumes), popups (`COptionsMenu`, `CNewGameDlg`, `CSaveDlg`, `CRestoreDlg`, `CAudioDlg`, `CNameEntryDlg`, `CHighScoreDlg`, confirmations, `CPlayAgainDlg`, `CCreditsScreen`, `CListHit`), `CTitleScene` (AVI intro + title + credits), `WinMain`→`main` with `RegisterScene` | analysis of `Shoot.exe`; intro: `PlaySoundSimple` + `PlayMovie` |
| **G3 Mushoot** `game/mushoot/` | `CShootScene` ("Mushoot"), `CShootGame`/`CShootPlayer`, `CGameControl`, `CHouse/CWagon/CHun/CHorseman/CHawk/CBlast/CAimSprite/CMushuCannon/CGameSprite`, `CSpriteList/Pool`, tables of the 150 levels, Mushu's sequences/lines, tutorial | analysis of the game half of `Shoot.dll`; requires G1 and G2's popup factory (`Application::GetApp()`); `RegisterScene("Shoot.DLL","CreateScene", …)` |

Order: engine (4 packages) → G1 → G2 (playable title) → G3.
