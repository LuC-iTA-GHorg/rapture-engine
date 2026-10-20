# Engine (`engine/`) — faithful port of moses.dll + Aaron.dll

> Note (2026-10-19): this engine is the `rapture-engine` repo, the `engine/` submodule of a per-game repo. MuShoot (`mushoot-rapture-port`) is the **reference game**
> cited in the examples (`mushoot.exe`, `Shoot.exe`/`Shoot.dll`). The analysis of the engine binaries (moses, Aaron, host `Shoot.exe`) and of `Shoot.dll` (the MuShoot scene)
> was done on the original binaries; those notes are not part of this repository and are mentioned only as provenance.
> This is an unofficial fan project for preservation and interoperability: it contains no game data and no original or decompiled code; you must own the original games.

This folder describes the rewritten engine. The implementations (`.cpp`) were split into 4 independent work packages (see `WORKPLAN.md`).
The game code (`Shoot.exe` / `Shoot.dll`) sits above it and is transcribed 1:1 from the original behaviour.

Reference sources (analysis notes, not part of this repository): `moses.md`, `aaron.md`, `aaron_ordinals.tsv`, `rrs_format.md`, `shoot_exe.md`,
`shoot_dll_a.md`, `shoot_dll_b.md`. Project rules (as in the Mahjong port): 1:1 logic, **every function
carries the original address in a comment** (`// moses.dll 0x10006e80`, `// Aaron ord 539 0x1000de09`), the original's quirks
are **preserved** and flagged with a comment, and where the original is unclear there is a `TODO`.

## 1. Original → port map

```
 original                                port
 ───────────────────────────────────     ────────────────────────────────────────────────────────────
 Shoot.exe / Shoot.dll (game)            game/…            (to be written, see WORKPLAN §Game)
 Aaron.dll  (application framework)      engine/aaron/*    namespace aaron
 moses.dll  (graphics/anim/audio/res.)   engine/moses/*    namespace moses
 WinMM/DirectSound/MCI/GDI/BitBlt        engine/platform/*        (already done: SDL2, mixer, Rrs, Dib, Movie, Midi…)
```

| original | port (header) | notes |
|---|---|---|
| `CDibObj/CDibData/CDibList/CDibSect` | `moses/DibData.h` | pixel = platform `::Dib`/`::Surface` ARGB32 |
| `CDrawObj`, `CDrawDib` | `moses/DrawObj.h` | `DRAWOBJ_PRIV` is a member `p_` |
| `CDrawGdi/Rect/Poly/Text` | `moses/DrawGdi.h` | GDI replaced by `GdiCtx` + software rasterizer |
| `CDrawView`, `CDrawSys` | `moses/DrawView.h` | compositor + dirty rect + `PresentTo` |
| `CAnimView`, `CAnimObj` | `moses/AnimView.h` | `Service()` called by the main loop |
| `CAnimDib/Rect/Text/Poly` | `moses/AnimObjs.h` | multiple inheritance like the original |
| `QUEUEMSG` + opcodes | `moses/QueueMsg.h` | flat struct + factories `qm::Delay(...)` etc. |
| `CResfile`, `CResMgr` | `moses/ResMgr.h` | on top of `rrs::Archive`; refcount per `id & 0xffff` |
| `CAudioLib`, `PLAYSOUNDINFO` | `moses/AudioLib.h` | on top of `Audio`, `MidiPlayer`, `Movie`; no threads |
| `CMem`, `MosesInitLibrary`, path | `moses/Mem.h` | `CFileIO`, `CObjCache/CGdiCache`, `CDibTile`, `CDrawMeta` **not ported** (unused) |
| `RObject/PtrArray/TreeNode/Broadcaster` | `aaron/RObject.h` | |
| `RegKey` | `aaron/RegKey.h` | on an `.ini` file in `%APPDATA%\<folder>\<game>.ini (HostConfig)` |
| `Module` + `GetProcAddress` | `aaron/Module.h` | static registry `RegisterScene("Shoot.DLL","CreateScene",fn)` |
| `Window`, `Application` | `aaron/Window.h`, `Application.h` | `Application : Window : RObject` like the original |
| `Scene` (17 virtual slots) | `aaron/Scene.h` | |
| `Widget`, `Node` | `aaron/Widget.h` | |
| `Button, Dialog, DibSprite, AnimSprite, RectSprite, AnimRectSprite, AnimTextSprite, Timer` | `aaron/Sprites.h` | |
| `Controller`, `*Source`, `JoystickMgr` | `aaron/Input.h` | |
| `PickMgr, FocusMgr, CursorMgr, IdleTimer, TimerMgr, AccelTable, MsgQueue, AudioMgr` | `aaron/Managers.h` | |
| `ResArchive`, `ResTable` | `aaron/ResArchive.h` | |
| constants: message ids, styles, input codes, errors | `aaron/Types.h` | |
| the 150 ordinals used by Shoot.* | `aaron/Ordinals.h` + comments `// Aaron ord N 0x…` | verified: every ordinal has a declaration |

`HeadersCompile.cpp` includes all the headers (one at a time, then the umbrella `Moses.h`/`Aaron.h`) and contains a few
`static_assert`s; it contains no code. If a header is added it must be added there.

## 2. Conventions

* **Namespaces**: `moses` and `aaron`; the platform is in the global namespace. **Careful**: `aaron::Window` has the same name
  as `::Window` (platform). In the game always write `aaron::Window`; the SDL window is reached with `aaron::Window::Platform()`.
  Game code may use `using namespace moses;` but not `using namespace aaron;` at global scope.
* **`<windows.h>`**: the `.cpp` files of the engine and of the game do **not** include it (only the platform's `*_win.cpp` do). `windows.h`
  redefines `PostMessage`, `LoadString`, `PlaySound`… and would break the link (`Scene::PostMessage` → `PostMessageA`).
  `moses/Types.h` does a defensive `#undef`.
* **64-bit**: the original is 32-bit. Everything that carries a pointer in an integer uses `intptr_t/uintptr_t`:
  `CDrawObj::GetId/SetId` (holds the `Widget*`), the `a,b,c` fields of `QUEUEMSG` (`{5, fn, this, 0}`), `wParam/lParam`
  of the messages. `u32` stays `u32` where it is a resource/command ID. The original's static tables
  (QUEUEMSG etc.) must be **retyped**, never copied from memory; the `qm::…` factories make the transcription mechanical.
* Callbacks were `__stdcall`: in the port they are normal functions.
* `HWND/HDC/HINSTANCE` do not exist: where the original passes them (`CDrawView::Init(hwnd, hdc, w, h, bpp)`) the parameter is
  an ignored `void*`, so `ChangeColorDepth(16)`, `Init(...)` etc. are transcribed unchanged.
* Code comments are in English or Italian; documentation is in English.

## 3. Coordinate system

* **Scene and view coordinates are centred** on the surface: 640×480 → `x ∈ [-320, 320)`, `y ∈ [-240, 240)`,
  **y pointing down**. All `CDrawObj`s, scenes, widgets and game code work in these coordinates.
* `RECT` is half-open (`right/bottom` excluded) everywhere. `CDibSect` has bounds `[-(w>>1), -(h>>1), -(w>>1)+w, -(h>>1)+h]`.
* A bitmap is **centred on its hot spot**: `right = w/2 + origin.x; left = right - w; bottom = h/2 + origin.y; top = bottom - h`
  (`CDibObj::center_bounds`, moses.md §2). Do **not** use the platform's `BlitHot` (different rule): `CDibData::DibBlt`
  computes the top-left corner from the bounds and uses `::Blit`.
* The platform mouse arrives in `0..639 × 0..479`; `aaron::Window` subtracts `Scene::Origin()` = `(w/2, h/2)` before
  passing it to the scene (`Scene::HandleMouse`). The framebuffer and the view surface have the same size: the copy
  (`CDrawView::PresentTo`) is one to one (surface pixel `(x - left, y - top)`).
* Levels (z-order): higher number = on top; on a tie the last one inserted goes on top. `SetMinComposeLevel` etc. as in the original.

## 4. The main loop (and rendering)

The original has no fixed frame rate: `Application::MessageLoop` does `PeekMessage`; if there is nothing it does `Sleep(sleepMs)`;
then `ServiceAnimViews()` → `CAnimView::Service()` (based on `timeGetTime`). Drawing is **immediate**: every
`CDrawObj::Update()` recomposes the dirty area (`CDrawView::Display`) and immediately does a `BitBlt` onto the window.

In the port (`Application::RunFrame`, one iteration):

1. `Window::PumpPlatformEvents()` drains `::Window::PollEvent` and translates the events into Windows-style messages
   (`WMSG_*`, `aaron/Types.h`): mouse → `Scene::HandleMouse` (CursorMgr → captured widget or `PickMgr` → `Widget::OnMouse`;
   then the `Controller`s), keyboard → `Scene::HandleKey` (AccelTable, AudioMgr `* + -`, FocusMgr → `Widget::OnKey`, listener, Controller).
2. pending scene change (`AMSG_LOADSCENE`, see §6).
3. `ServiceAnimViews()` → for the active scene `Scene::RunFrame(now)`: `MsgQueue::Dispatch`, `TimerMgr::Tick` (tick `0xc05`, `Timer`
   widgets), `IdleTimer`, **`CAnimView::Service()`**, `CAudioLib::Service()`. Skipped if the application's `TimerMgr` is paused.
4. `Window::PresentIfDirty()` → `CDrawView::PresentTo(framebuffer)` + `::Window::Present()` **only once per iteration**.
5. wait: `SDL_Delay(sleepMs)` (default 1 ms) / `FrameTimer`. Time (`CAnimView::Now()`) is `SDL_GetTicks()` in ms.

`CAnimView::Service()` has the granularity of the iteration, like the original (`timeGetTime`): a fixed frame rate is not needed.

## 5. Memory management and object ownership

Choice: **plain `new`/`delete`, explicit ownership as in the original; no `unique_ptr` in the public APIs**
(`unique_ptr` is allowed inside the `.cpp` files and for some private members). The original used a per-class `operator new/delete`
on top of `malloc` (ordinals 85–122): in the port they are all `::operator new/delete` (`RObject` provides them).

| object | who owns it / how it is freed |
|---|---|
| widgets (`Button`, `AnimSprite`, …) | the **parent**: `new` + `Widget::InitOrDelete(w)` (calls `Create()`, `delete` on error); the parent deletes them in `TreeNode::DeleteChildren`. Game code only keeps non-owning pointers |
| `Scene` | the `Window` (`LoadSceneNow` destroys it with `Scene::Destroy` + `delete`) |
| `Module` | cache of the `Application` with `AddRef/Release` |
| `ResArchive` | shared between scenes (key = path), `AddRef/Release` |
| resource objects (`CDibData`, `CDibList`, `CAudioSnd`, blobs) | `CResMgr`; whoever obtains them from `Load` never deletes them: `Free(id)` (refcount per `id & 0xffff`; above it sits the `ResTable` refcount) |
| `CDrawObj` | the code that creates it (usually the widget that embeds it); the destructor does `Hide()` and detaches from the view |
| `CAnimObj` | same; the destructor detaches from the `CAnimView` (`detach_object`) |
| `Controller` and its `*Source`s | the `Scene` (the `*Source` registers itself in the constructor and the `Controller` takes ownership) |
| objects with `RObject` (events, properties of the arcade framework) | refcount: `Release()` at 0 → `delete this` |

Deliberate difference: `CDrawObj` has a **virtual destructor** (the original does not: vtable slot 0 was `Copy()`); `Copy()` stays virtual.

## 6. How a scene comes to life

```
main()                                  // host: Shoot.exe WinMain (0x401b6f), port: game/main.cpp
  RegisterScene("Shoot.DLL", "CreateScene", &shoot_dll::CreateScene);     // no LoadLibrary
  APPINIT ai; ai.createSceneFn = &title::CreateScene; ai.name = …; ai.regKey = "Software\\Disney Interactive\\MuShoot HotShot\\US";
  CShootApp app(&ai);                    // derives from aaron::Application (ord 52)
  return app.Run(cmdline);               // ord 711
Application::Run → InitInstance → CreateMainWindow (640x480 SDL window) → LoadScene(Module(createSceneFn))
Window::LoadSceneNow(module, arg):
  Scene* s = module->CreateScene(this, arg)       // = fn(host)  → `new CTitleScene(host)` → Scene::Scene(host, "Title Screen")
                                                  //   then CDrawView::Init(0,0,640,480,16), s->module = module, s->arg = arg
  s->Initialize():  managers (Cursor/Focus/Pick/Idle/Timers/Accel/MsgQueue/ResTable) + TreeRoot/RootWidget + SetResProc
                    OpenResources → Resize(w,h) → OnPreCreate → OnCreate → OnStart → Service → OnReady
```
* `CreateScene` has the signature `Scene* (*)(aaron::Window* host)` (`CreateSceneFn`); allocation is a plain `new`.
* Scenes are changed **only** through `Application::LoadScene(Module*)` → message `AMSG_LOADSCENE` → executed
  at the start of the next iteration (never inside a scene callback). Title → game:
  `app->LoadScene(app->GetModule("Shoot.DLL","CreateScene",0))`.
* A scene's resources are opened with `OpenArchive(slot, "mus.rrs", 0)` (slot 0 mus, 1 musfx, 3 muslin, 100/101 Frenzy/FrenzyLZ).
  32-bit resource id = `slot<<24 | class<<16 | index` (`aaron/Types.h`, `MakeResId`).
* The scene is a `CAnimView`: sprites are created with `new AnimSprite(scene_or_parent_widget, resId, x, y, level, …)`.

## 7. Message dispatch

* **Scene commands**: `scene->PostMessage(0x111, cmd, param)` → `MsgQueue` (FIFO, 100 entries) → drained in `RunFrame` →
  `Scene::OnMessage(0x111, wp, lp)` → `Scene::OnCommand(cmd, param)`. Never recursive: messages posted during the drain
  are processed in the next iteration. `0xc06` (deferred widget notification) → `Widget::OnNotify(id, widget)`; `0xc08/9` → sound listener.
* **Widget notifications**: `Widget::OnNotify(id, src)`: if the style has bit `8` it goes to `parent->OnNotify`, otherwise to `scene->OnCommand(id, …)`.
* **Animations**: `QM_NOTIFY` → `CAnimView::OnNotify` → `Scene::OnObjectEvent(obj, a, b, c)` → `Widget::GetFromObj(obj->Target())->OnNotify`.
  `QM_POST` → `Scene::PostMessage`; `QM_SEND` → immediate dispatch; `QM_CALL` → synchronous callback via `CAnimView::QCall`.
* **Input**: mouse → capture or `PickMgr` (`CDrawView::IsPicked` from the head = highest level, `Widget::GetFromObj`); keyboard → AccelTable
  (`PostMessage(0x111, cmd, param)`), `FocusMgr`; `Controller` → `Scene::OnInput(code, x, y, user)` (`InputCode` codes).
* **Application messages** (`Application::WndProc`, `0xc00…0xc09`): `0xc02` scene change, `0xc04` pause (Broadcaster), `0xc05` tick…

## 8. Threads

None. The only other thread is SDL's audio callback (`platform/Audio`), which **never** calls back into the engine: sound
start/end notifications, MIDI loops and AVI are handled in `CAudioLib::Service()` from the main thread. That is why `MsgQueue` has no mutex.

## 9. Deliberate differences from the original

1. **Presentation**: no immediate `BitBlt`. `CDrawView::Display` composes straight away into the software surface (same maths, same order)
   but only updates a dirty rectangle + flag; the main loop presents **once per iteration** (`PresentTo` + `Present`).
2. **Pixel format**: ARGB32 decoded once (alpha 0 = transparent `0x0000` pixel). No 1/4/8/16-bit `CDibBits` classes, no run-time RLE,
   no palettes: `Expand()` is a no-op; `bpp` and `ChangeColorDepth(16)` are accepted and ignored. `SetSurfaceColor` still receives the raw RGB555 value.
3. **GDI** → software rasterizer (`GdiCtx` in place of `HDC`): rectangles, ellipses, polygons, text (also "rich"). `CDrawMeta` not ported.
4. **Registry** → `.ini` (`RegKey`); **LoadLibrary** → static scene registry; **window** → SDL (`platform/Window`).
5. **Audio**: no DirectSound/MCI/threads; `SoundCreate` takes an already-parsed `::Sound`; AVI (kind 3) is played with `PlayMovie` (blocking) instead of MCIWnd.
6. **Virtual destructor** on `CDrawObj`/`CResMgr`; per-class allocators replaced by `new/delete`.
7. **64-bit ids** (`GetId`, `QUEUEMSG::a/b/c`, `wParam/lParam`).
8. **Joystick**: `JoystickMgr::CountDevices()` returns 0 until the platform has a backend.
9. **Cursors**: `CursorMgr::SetCursor(n)` is a placeholder (only show/hide via `::Window::ShowCursor`).
10. **Not ported**: `CFileIO`, `CObjCache/CGdiCache`, `CDibTile`, `CDrawMeta/CAnimMeta`, `ControlWin`, `PolySprite/TextSprite` (ord 18,19,21,25,26 unused), About box, CD audio (no-op).

## 10. Tests and debugging

* Every work package exposes `int RunPkgNTests()` (`engine/tests/EngineTests.h`) called by `<game>.exe --engine-test N` (main.cpp: package 3).
  The tests are headless where possible (only `Surface`, no window).
* Do not start the game with windows on the desktop without a reason (project rule). Visual comparisons are done by saving the `Surface` as PNG/PPM.
* If the original is unclear: say so with `// TODO(...)` and, if needed, leave a `DebugMsg` — do not invent.
