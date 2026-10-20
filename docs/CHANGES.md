# CHANGES / REQUESTS on the engine headers

Format: `YYYY-MM-DD | header | pkg | what and why`. Only additive changes by the header's owner (see WORKPLAN.md).

## Changes

2026-10-18 | moses/DrawObj.h | 1 | comments of `DRAWOBJ_PRIV::prev/next` corrected: from the original behaviour (link_object 0x10006e80, unlink 0x10006f60, Display 0x100070f0) `prev` (+04) points towards the HEAD (highest level) and `next` (+08) towards the TAIL; Display walks tail->head via `prev`, the pick tests walk head->tail via `next`. Comment only, no signature change.
2026-10-18 | moses/DrawGdi.h | 1 | `GdiCtx`: added `exclude`/`hasExclude` (ExcludeClipRect of the iris hole), `textColor` (SetTextColor), `font` (selected font) and a forward declaration of `LOGFONT_LITE`. Additive.
2026-10-18 | main.cpp | 1 | added `--engine-test N` (minimal, additive; package 3 adds the same). `engine/stubs/Pkg1_Stubs.cpp` defines provisional RunPkg2/3/4Tests (folder deleted by the integrator).
2026-10-20 | moses/DrawGdi.h | wp5 | `GdiCtx`: `selPen/selBrush/selFont` (objects selected with `gdi::SelectObject`); `CDrawRect::pen_cache_/brush_cache_`, `CDrawPoly::pen_cache_`, `CDrawText::font_cache_` (pointers to the GDI caches of ObjCache.h, nullptr = values applied directly as before). Additive.
2026-10-20 | moses/ResMgr.h | wp5 | `CResfile`: explicit constructor/destructor/copy, `SetLoadProc`, `LoadKeyResource`, `load`, `load_palette`, `File()` (CFileIO); `CResMgr::operator=`. `CResMgr` is no longer copyable by construction (copy deleted; the original copied it by value with a double free). Additive except for the copy.
2026-10-20 | moses/DibData.h | wp5 | `CDibPal`, `CDibData::Read/Write/Print`, `CDibSect::SetPalette(const CDibPal*)` (previously `void*`), `Palette/ColorTable/DeletePalette`; the destructors of `CDibData/CDibList` are no longer inline `= default`.
2026-10-20 | moses/AudioLib.h | wp5 | `CdPlay/CdStop/CdIsPlaying/CdIsPaused/CdGetMaxTrack` are now real (virtual discs made of WAV files, see CdAudio.h) + `CdGetPosition/CdGetLengths/CdPause/CdSetFolder/Cd()`, `SoundCreate(CFileIO*, off, size)`. The existing signatures remain.
2026-10-20 | moses/AnimView.h, AnimObjs.h, DrawView.h | wp5 | `CAnimObj::operator=`; `AnimObjs.h` includes `DrawMeta.h` (CAnimMeta/CDrawMeta); `CDrawView::SetPalette(const CDibPal*)` (previously `void*`) and `Profile(ms)`.
2026-10-20 | moses/GdiRaster.h | wp5 | `gdi::TextAscent`, namespace `gdi::ttf` (portable stb_truetype backend). Private moses header.
2026-10-20 | platform/Sound.h, platform/Midi.h | wp5 | `LoadWav/LoadWavFile`; `MidiPlayer::BreakLoops()` (inline). Additive.

## Implementation notes / deliberate deviations of package 1

* `CDibData::DibSet(x,y,w,h,color)` and `DibSet(RECT*,color)` work in surface PIXEL coordinates (0-based), like the original (CDrawView::Display passes `d - surface`).
* `CAnimRect::process_box(a, b, c)`: in the original the three arguments are (closing, useX, useY); the names in the header (`axes/closing/stepsDone`) are misleading but the signature is unchanged.
* `CDrawView::GetDisplayBox` (inline in the header) does not intersect with the background bounds like the original (0x100072d0): irrelevant as long as the background is full screen. TODO(integrator) if needed.
* `CDrawView::Scroll`: the original uses `ScrollDC` on the window and redraws only the strips; the port redraws the whole `scroll_vis`.
* `CDrawObj::erase_old_area(excludeNew)`: the `ExcludeClipRect` on the new bounds is not replicated (the final result is identical, the new area is recomposed right afterwards).
* `CDrawGdi::Compose`: clip exactly to `r` (the original uses right/bottom +1 of `IntersectClipRect`; the extra row/column was outside the recomposed area).
* `CDrawPoly::AddPoints` calls `recalc_bounds()` (the original does not; without it the polygon would have no bounds). `CDrawText` copy: like the original it copies only the CDrawGdi fields (font and text not copied).
* Text: Windows GDI in `GdiRaster_win.cpp` (NONANTIALIASED), fixed-metric fallback without drawing on other platforms (`GdiRaster.cpp`).
* `CAnimView::SetPostProc`: `QM_POST`/`QM_SEND` call the hook if set (otherwise the message is ignored).
2026-10-18 | moses/ResMgr.h | 2 | `free_dibobj` (protected): signature `(RESREC&, u32 id, RESTYPE type, void* ctx)` — the id is needed to re-read the pool record.
2026-10-18 | moses/AudioLib.h | 2 | `CAudioSnd`: added `chans`, `virtualPlay`; `CAudioLib`: added `EnableDevice(bool)` (headless tests), `LiveSoundCount()`, private members (notification queue, `stop_snd`, `apply_master`).
2026-10-18 | moses/Mem.h | 2 | no signature change. NOTE: `MakeFilePath(a, b)` implements the original semantics (a = path completed IN PLACE, b = base path): the parameter names in the header are misleading. `CResMgr::GetResType` returns the RAW type (alias = 1) like the original.
* 2026-10-18 | aaron/RObject.h, RObject.cpp | pkg3 | `Subscriber`: now keeps back-pointers to the `Broadcaster`s (max 4) so `~Subscriber`/`~Broadcaster` unlink themselves; `TreeNode::~TreeNode` DELETES the children (verified 0x10002944; required by pkg4) and `DeleteChildren` starts from the last child (0x10002a2a).
* 2026-10-18 | aaron/Types.h | pkg3 | `InputSourceType` corrected from the original: KEYBOARD=1, MOUSE=2, JOYSTICK=3; directions verified: Up=1, Down=2, Right=4, Left=8.
* 2026-10-18 | aaron/Input.h | pkg3 | `InputSource` has `flags_` (bit0 directions/movement, bit1 buttons; the constructor parameter does NOT set them: only `Controller::EnableSource(type, flags)` does, `flags` passed as is: Shoot uses 3) + virtuals `Type/Reset/OnBounds`; `Controller` derives from `Subscriber` (ACTIVATEAPP / AMSG_PAUSE -> `Suspend`), `Suspend(0)`=suspend, `Suspend(!=0)`=resume (counter, returns int), new `SetDirection/SetButton/SetPosition/SetDirectionFromVector/DirectionAllowed/State`; `EnableSource(int, u32 flags)`.
* 2026-10-18 | aaron/Managers.h | pkg3 | `TimerMgr::PauseCounter(bool)` (the app's pause counter, broadcasts AMSG_PAUSE 1/0 on transitions; `Paused()` includes it); `IdleTimer::Stop`; `AccelTable::Push/Pop`; `CursorMgr::SetCursor` returns the previous cursor; `MsgQueue::PurgeWidget(const Widget*)` (+ second logical buffer `cur_`).
* 2026-10-18 | aaron/Scene.h | pkg3 | `OnCommand(u32 id, uintptr_t param)` (was `u32`: the original passes a `Widget*`; the overrides of derived classes must be updated); `SuspendControllers(bool)`, `ShowOverlay(int)` (0x1000e0d3), `SoundListenerFor(id, remove)` (the PlaySoundSimple listener lives in a side table: `PLAYSOUNDINFO::user1` is u32), friends `Module`/`Window`.
* 2026-10-18 | aaron/Module.h, Application.h, Window.h | pkg3 | `IsSceneModuleRegistered`; `Application::PauseApp(bool)/Activate(bool)/ReqBpp()`; `Window::curMods_`.

Pkg3 integration notes (for pkg1/2/4):
* `Scene::Initialize` builds `treeRoot_ = new Node(WidgetParent(scene), 0, 0)` when `Scene::RootWidget()` is still nullptr (the Widget only has to store `scene_`, no linking) and `rootWidget_ = new Node(WidgetParent(treeRoot_), 0, 0)`.
* Mouse towards widgets: `Widget::OnMouse(msg, keys, lParam)` with `lParam` = `MAKELPARAM(x, y)` (x,y centred, signed 16 bit; the receiver must sign-extend). The widget the mouse has left receives `OnMouse(0x02A3 /*WM_MOUSELEAVE*/, keys, lParam)`.
* `Scene::OnObjectEvent` default: `Widget::GetFromObj(obj->Target())->OnNotify(a, widget)`.
* `Window::LoadSceneNow`: the `arg` stored in the new scene is the previous Module (or the previous arg if the same module), like `Window::LoadScene` 0x1000eeeb.
* `Scene::PlaySound` follows the original (not the parameter names in the header): flags = loop(a&1)<<2 | PSF_PAN | (b&1)=MIX | (user0&1)<<1, `info.user0`=4th argument (100), no PSF_VOLUME.
* On MinGW (COFF) `weak` symbols do NOT resolve references from other objects: the stubs are strong definitions (folder `engine/stubs/` to be deleted at integration, including the `RunPkg1/2/4Tests` of Pkg3_Stubs.cpp).
* 2026-10-18 | aaron/Widget.h | pkg4 | `OnReserved12` -> `GetAnimObj()` (slot 12, vtable +0x30: AnimSprite::Show uses it for QStop); new public methods `HideCount`, `TabSibling`, `SetPressedVisual`, `MousePress/MouseRelease`, `InitDrawObj`, `DisableCount`; enum `WidgetState` corrected (bit 1 = transient focus, 2 = captured by the mouse, 4 = pressed, 8 = toggle, 0x10 = pressed flag).
* 2026-10-18 | aaron/Sprites.h | pkg4 | TODOs resolved from the original: AnimSprite `(parent,res,x,y,level,cmdId,style[,frame|msgs,count])` (previously `style, flag`); RectSprite/AnimRectSprite `(parent,x,y,level,cmdId,style,rc,ellipse)`; AnimTextSprite `(parent,x,y,level,cmdId,style,text,fontFace,fontHeight,color)`; Timer `(parent,cmdId,periodMs,style,userData)` and now also derives from `Subscriber` (subscribes to the scene's `TimerMgr`); `Dialog::OnInit()` now returns `int` (0 = Create fails); Button: `GetPickFlags/PostNotify/Draw/StateIndex/SetMode`, table `frames_[8]`; Dialog: real fields (+0x64..+0x80), `SetDefaultButton`, `FindDialogAncestor`.

(empty below: no other changes)

## REQUESTS (signature changes or changes to other people's headers, to be approved by the integrator)

* pkg4 -> pkg3 | aaron/Scene.h | `Scene::OnCommand(u32 id, u32 param)`: the original passes the `Widget*` pointer as 2nd argument (`Widget::OnNotify` 0x10004136); in 64 bit it does not fit in `u32`: `uintptr_t` would be needed. Pkg4 passes the truncated pointer.
* pkg4 -> pkg3 | aaron/Scene.h, Managers.h | needed: `Scene::SuspendControllers(bool)` (Dialog::Create/Close, FUN_1000cc39), `AccelTable::Push()/Pop()` (FUN_1000de5a / FUN_1000dde7: dialogs mask the scene's accelerators), `CursorMgr::SetCursor` returning the previous cursor (Dialog saves +0x80), `MsgQueue::PurgeWidget(Widget*)` (FUN_1000ca7d: ~Widget must not leave pending AMSG_NOTIFY), `PickMgr` counter of pickable widgets (FUN_10012167/FUN_10012403), default colour fields of the scene (+0x694/+0x698, used by Rect/Poly sprites) and `Node` with a Subscriber for AMSG_PAUSE (FUN_10003e3a: HideCount + "Pause" overlay).
* pkg4 -> pkg3 | aaron/RObject.h | `TreeNode::~TreeNode` in the original (0x10002944) DELETES the children; the header says it does not. Pkg4 calls `DeleteChildren()` in `~Widget` (idempotent).

## Fixes requested by G1 (2026-10-18)

* aaron/Managers.h,cpp | `int FocusMgr::Lock(int unlock)`: original semantics 0x10012d3d (`Lock(0)` locks, counter++; `Lock(!=0)` unlocks, counter-- if > 0; returns the counter). No other caller in the engine.
* moses/AnimView.h,cpp | `CAnimView::OnNotify(obj, intptr_t a, b, c)` and `ANIMNOTIFYPROC(data, obj, intptr_t a, b, c)`: QM_NOTIFY carries pointers; `QUEUEMSG::a/b/c` were already `intptr_t`.
* aaron/Scene.h,cpp | `Scene::OnNotify/OnObjectEvent(obj, intptr_t a, b, c)` (the overrides of derived classes must be updated). FAITHFUL default of `OnObjectEvent` (0x1000d212): calls slot 15 `OnObjectEvent2(Widget* w, intptr_t a, b, c)` (nop); it no longer routes to `Widget::OnNotify` (whoever wanted that must override `OnObjectEvent2`).
* aaron/RObject.h,cpp | `PtrArray(int capacity)`: the argument is the INITIAL capacity (+0xc, allocated at once), growth is +1 when full (0x10002056); `Capacity()` exposed; `Insert(p, idx)` with an out-of-range idx (including -1) appends (the original's unsigned comparison).
* Item 5 (`Scene::GetMachineKey()` before `Initialize()` uses an empty `regKeyPath_`): documented only.

- WP-1: added (additive) moses::CDibTile (DibData.h), CResMgr::load_dibtile (private), rs::DibTileInfo and rs::Archive::GetDibTile (Rrs.h).
- WP-1: added (additive) moses::CDibTile (DibData.h), CResMgr::load_dibtile (private), rs::DibTileInfo and rs::Archive::GetDibTile (Rrs.h).

## WP-4 (agent wp4, 2026-10-20): unused Aaron ordinals (details in docs/WP4.md)

- aaron/ new headers: Containers.h (MemBuffer, LinkItem, LinkList), Cursors.h, Joystick.h, ControlWin.h, Path.h; platform/Joystick.h (SDL2). Additive: `RObject::GetObjClassId`, `PtrArray` (copy, Reset, Shrink, ForEach, MoveElement, DeleteAt, DisposeElement), `TreeNode` (ctor with TreeRoot* / parent, LinkTo, Reparent, AdoptRun, ...), `TreeRoot`, `Module` (ctor from function / from folder / exe, HasCreateFn, Unload, BuildPath), `Window` (WindowProc, Create, placement, PostMessage/SendMessage, ...), `Application` (ctor 53/54, ParseCommandLine, window lists, NotifyApp, Joystick(), MessageBox hook, ...), `Scene` (PickWidget, GetResObject, ToScreen, Push/PopAccelerators, SetPaletteRes, ...), `RegKey` (OpenSubKey, QueryString, Create), `FocusMgr`/`CursorMgr`/`PickMgr`/`IdleTimer`/`AccelTable`/`AudioMgr`/`ResTable`, `InputSource::Activate/Reactivate`, `Controller::SetSourceFlags/FindSource/SetBounds(int...)`, `Widget::OnHideChange/OnFocus/ToggleCaptured`, `Node(TreeRoot*, Scene*)`, sprites `PolySprite`/`AnimPolySprite`/`TextSprite`, `AnimSprite::AnimateList/GetTiming/SetTiming`, `APPINIT::extra38c`, `WINCREATE`, `WINPLACEMENT`, enum `ClassId` (values < 0x20080).
- Behaviour changes (deliberate, fidelity to the original): `InputSource::Enable` calls `Activate`; `Controller::Suspend` calls `Reactivate`; `Widget::HideCount` pauses animated sprites (new virtual `OnHideChange`, default unchanged); `Widget` uses `PickMgr::Hold/Release`; `Application::Activate` deduplicates; `JoystickMgr::CountDevices` counts SDL joysticks; `Application::ParseArg` accepts `' '`; `AccelTable::Push(bool passThrough = false)`.
- `DibSprite::AnimateList` is ord 143; ord 144 is `AnimSprite::AnimateList` (correction of WP-2).
