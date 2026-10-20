# WP-4: Aaron.dll, ordinals not used by any game (agent wp4, 2026-10-20)

Branch `wp4` of rapture-engine. Goal: port 1:1 **all** 341 ordinals of `Aaron.dll` that no game imports (297 distinct addresses: many ordinals
share the same address; they are the virtuals re-exported per class), grouped by class, with headless tests. Final state: **all covered**;
12 ordinals are `n.a.` (per-class `operator new/delete` thunks, `malloc`/`free` of Aaron's static CRT: the port uses the global `new`/`delete`, as already
done for the 26 previously known ones), nothing is left to do. `python tools/engine_coverage.py` on this tree gives: Aaron `yes` 517, `n.a.` 38 (26 + these 12), `partial` 0.
(The integrator regenerates `COMPLETENESS.md` / `ENGINE_API_COVERAGE.md`; they were not touched here.)

## 1. How it was done

* Each address was read in the decompiled output of `Aaron.dll` (derived from analysis of the binaries) and, where the decompiled output was ambiguous, with `objdump -d -M intel` on `Aaron.dll`; the classes were
  reconstructed from the **vtables** (`0x1001d480..0x1001e260`, read with pefile: every slot is an exported address, so a vtable tells which class each shared
  ordinal belongs to). The `Class::Method` names are invented (like all Aaron names) and are recorded in the function-name table from the binary analysis (292 new rows).
* Each function carries `// Aaron ord N 0x...` in the code; ordinals that are inline or provided by C++ (class ids, vtable thunks, destructors) are listed one per line in
  `aaron/Wp4Index.cpp` (comments only: `engine_coverage.py` looks for the address in the bodies of the `.cpp` files). The ordinal -> port table is also in `aaron/Ordinals.h` (`kOrdinalsWp4`).
* New files: `aaron/Containers.{h,cpp}` (MemBuffer, LinkItem/LinkList), `aaron/Cursors.{h,cpp}`, `aaron/Joystick.{h,cpp}` + `platform/Joystick.{h,cpp}` (SDL2),
  `aaron/ControlWin.{h,cpp}`, `aaron/Path.{h,cpp}`, `aaron/Wp4Index.cpp`; tests `tests/AaronCore_Test.cpp`, `AaronApp_Test.cpp`, `AaronWidgets_Test.cpp`
  (called from `RunPkg3Tests` / `RunPkg4Tests`: `--engine-test 3` = 539 checks, `--engine-test 4` = 105; all headless, no window, no game).

## 2. What is there per group

| group | classes / ordinals | notes |
|---|---|---|
| A containers | `MemBuffer` (ord 4, 60, 145-147, 161, 180, 201, 303, 351, 403, 405, 437, 567, 571, 661, 709), `LinkItem`/`LinkList` (39, 40, 72, 73, 155, 189, 419, 443), `PtrArray` extensions (45, 157, 162, 176, 190, 442), `TreeNode`/`TreeRoot` (55-57, 82, 139, 140, 191, 404, 412, 655, 660, 698), `RObject` (2, 59, 137, 366, 562) | `MemBuffer` = a buffer that reserves with `VirtualAlloc(MEM_RESERVE)` and commits page by page: in the port, a `calloc` with "committed" accounting (4096-byte page) and the process statistics (ord 303). |
| B window / application | `Window` (50, 79, 165, 166, 285, 387, 395, 400, 445, 522, 542, 572, 583, 585, 593, 638, 641, 667, 672, 687, 691), `Application` (53, 54, 148, 168, 170, 194, 195, 393, 447, 496, 528, 543, 573, 579, 623, 626, 670), `AudioMgr` (533, 574, 654, 692, 695) | `Window::WindowProc` (400) is the body of the window proc: it maps Win32 messages onto `Scene`/`FocusMgr`/`CursorMgr`; `Application::WndProc` (401) falls back to it for everything it does not handle. `OnWindowMessage` is vtable slot 3. |
| C module / paths | `Module` (41-43, 74, 383, 416, 417, 439, 702), `CombinePath` (438), `ExecutablePath` | `Module::Unload` drops the function pointer and `GetCreateFn` re-resolves it from the static registry (no `LoadLibrary`). |
| D registry | `RegKey` (12, 67, 171, 175, 251, 624) | `DeleteValue` also deletes the sub-key of the same name (original quirk); `QueryString` has the original semantics (returns the length). |
| E input / joystick | `InputSource`/`Controller` (35, 70, 71, 75, 185, 291, 469, 588, 608), `KeyboardSource` (293, 297, 450, 578), `MouseSource` (294, 451, 650), `JoystickSource` (36, 197, 292, 449), `JoystickDev` (295, 408, 498, 602, 699), `JoystickMgr` (314) | joystick on SDL2 (GameController: left stick + D-pad + A/B/X/Y; otherwise Joystick: axes 0/1, hat 0, buttons 0-3). `JoystickDev::Poll` replaces `joySetCapture(10 ms)`: same `MM_JOYnMOVE`/`BUTTONDOWN`/`BUTTONUP` messages, same wParam/lParam encoding. |
| F managers | `FocusMgr` (153, 200, 499, 563, 610, 633, 636), `CursorMgr` (154, 208, 286, 500, 564, 611, 616, 618, 639), `PickMgr` (136, 152, 181, 330, 560, 570, 609), `IdleTimer` (642), `AccelTable` (188, 537, 556) | `FocusMgr::OnMessage` / `CursorMgr::OnMessage` are the "Win32-style" entry points; the port's internal path (`Scene::HandleKey/HandleMouse`) remains the one used by the game. |
| G cursors | `Cursor` (6, 63, 131, 367), `ResCursor` (10, 599), `DibCursor` (8, 65, 133, 597), `AnimCursor` (7, 64, 132, 596), `CursorPair` (9, 66, 134, 598) | the five global cursor sets (ord 614) are real `ResCursor` objects; the native handle is a token. |
| H widgets / sprites | `PolySprite` (18, 19), `AnimPolySprite` (25, 26), `TextSprite` (21), `ControlWin` (30, 69, 219, 235, 284, 347, 358, 379, 394, 396, 444, 467, 486, 540, 554, 591, 605, 637, 666, 668, 683, 690), `EditControl` (31, 589, 669), class ids and vtable thunks (211-214, 260-268, 368-380, 384, 385, 391, 478-485, 511-514, 547-553, 675-682), per-class `Nop*` (223, 224, 227, 229, 231-233, 235, 238, 239), `AnimSprite` timing (353, 664), `AnimateList` (143, 144), `Widget::ToggleCaptured` (506), `Timer::PostNotify` (525) | see section 3 for `ControlWin` and section 4 for the slot 7 entries. |
| I Scene | 179, 184, 186, 198, 199, 202, 338, 392, 413, 440, 441, 446, 532, 536, 537, 556, 622, 653, `ResTable` (192, 193, 205, 352, 427, 580) | |
| J misc | ord 1 (`kCopyright`, the only export that is data), 62 | |

## 3. Win32 plumbing with no meaning in SDL2: equivalent or justified no-op

| original | in the port | reason |
|---|---|---|
| `HWND`/`HDC` of `Window` (+8/+0xc), `GetHWnd` (396), `Window::Attach` (672) | `Window::NativeHandle()` is a token (address of the platform window, or the value passed to `Attach`) | SDL has a single window per process, created by the `platform/` layer. |
| `Window::Create` (166), `CreateInParent` (165), `SetPos` (445), `Resize` (691), `Show` (687), `Invalidate` (572) | create the 640x480 platform window (`CreateNativeWindow`), record position/size/SW_* state | the logical size is fixed; visibility of the SDL window belongs to the `platform/` layer (hidden in `--autoplay`). |
| `ShowPlacement`/`SavePlacement` (583/585) | `WINPLACEMENT` (44 bytes) saved as the binary `Placement` value of the `RegKey`, like the original | they really work, they just do not move a native window. |
| `PostMessage`/`SendMessage` (542/593) | per-window queue drained by `PumpPlatformEvents` / direct call of `OnWindowMessage` | no Windows message pump. |
| `SetIcon` (641) | `LoadIconResource` (virtual, default `false` = "icon not found") | no Win32 icon resources; the host can override it with `SDL_SetWindowIcon`. |
| `ActivateExisting` (638) | searches only among the windows of the process (`SetClassName`) | no cross-process `FindWindow`. |
| `IsDialogMessage` (543) | returns 0 without doing anything | the original discards the result of the loop and always returns 0; there are no native dialogs. |
| `Application::MessageBox` (447), `Scene::MessageBox` (446) | `SetMessageBoxHook` (the host can use `SDL_ShowSimpleMessageBox`); without a hook: stderr + first button | |
| `CursorMgr::SetPos` (618), `RefreshPointer` (208, 560) | no-op | `SetCursorPos` / `GetCursorPos` + `SetCursorPos` (re-posting the pointer to make `WM_SETCURSOR` be sent again): SDL reports the pointer continuously. |
| `ResCursor` (10): `LoadCursor` | handle = token `(hInst != 0) << 16 | resId` | no Win32 cursors; system ids (0x7f00 IDC_ARROW ...) can be mapped to `SDL_CreateSystemCursor`. |
| `ControlWin`/`EditControl` (30, 31, ...) | **model** of the state (text, style, rectangle, enabled/visible, font, limit, selection, colors) and response to the messages that the original sends with `SendMessage` (`WM_SETTEXT`/`GETTEXT`/`GETTEXTLENGTH`, `WM_SETFONT`, `EM_SETSEL`/`GETSEL`/`LIMITTEXT`, `WM_ENABLE`) | SDL has no native controls; nothing is drawn. No game creates them. |
| `WM_ACTIVATEAPP`: `SetThreadPriority(+-1)`, `WM_DISPLAYCHANGE` (0x7e) | not ported | thread priority / full-screen window resizing are not needed with SDL. |
| `Application::OnDestroy`: `FUN_1001131a` (ord 579) | `ResetDisplayMode()` -> `ChangeDisplayMode(0,0,0,0)` (no-op in the port) | |

## 4. Findings about the original (bugs / quirks, all annotated in the code)

* `LinkList::ForEach` (189) and `TreeNode::ForEachChild` (191) advance the pointer to the next element **before** the `Release` and release the next element instead of the
  current one (verified with `objdump` at `0x10001d75`): in the original this is a use-after-free; the port does not AddRef/Release (the refcount base is 0) and reads the next element before the callback.
* `TreeNode::NotifyTree` (`0x10002818`) recurses passing `this` again instead of the child: infinite recursion if a child has children. Never reached (the hook is empty). The port recurses correctly.
* `TreeNode::HasAncestor` (412): if the ancestor is not found the original dereferences null; the port returns `false`. `AdoptRun` with `pos == kAtLast` reverses the order (it walks from last to first).
* `JoystickSource::PickDevice` (197): the decompiler shows `return param_1` in both branches, but the assembly (`0x10009e58`) returns the loop variable; after the choice the id permanently loses the `0x8000` bit.
* `Controller::FindSource` (291) returns the **last** source when none matches. `DibCursor`/`AnimCursor` keep the hot spot only if `id != 0`; `AnimCursor::Handle` **adds** `hotY` (DibCursor subtracts it).
* `ControlWin::Capture` (467, slot 5) is `EnableWindow(flag)` but `Widget::EnableCount` calls it with 1 when it **disables**: a control disabled by `EnableCount(0)` is actually enabled.
* `RegKey::DeleteValue` (175) calls `RegDeleteValue` **and** `RegDeleteKey` with the same name. `Application::ParseArg` (510) recognises the option `' '` (space), not 0: `ParseCommandLine` passes `' '` for bare tokens.
* **vtable slot 7 (the one that calls `HideCount`)**: it is the nop `0x10003520` for Widget/Button/Dialog/Node, but it is `CAnimObj::Pause(flag)` (`0x10006860`) for AnimSprite, AnimRectSprite, AnimTextSprite and
  AnimPolySprite. The port (pkg4) had attributed `0x100032cb` to Button as slot 7: it is **slot 18** (Draw). New virtual `Widget::OnHideChange(bool)`: hiding a subtree with `HideCount(1)` pauses the animation
  queues of the animated sprites and `HideCount(0)` resumes them; the default remains `NopVirtual2()` (so Button keeps redrawing as before).
* Ord 144 (`0x10004ed2`) is `AnimSprite::AnimateList` (CDrawDib at +0xdc); `DibSprite::AnimateList` (+0x40) is ord 143 (`0x10005702`). WP-2 had inferred the wrong class for 144.
* `Ordinals.h` has the two addresses of `CursorMgr::SetCursor` (614) and `ShowCursor` (685) wrong by one nibble (`0x1000373f`/`0x10003876`): they are `0x1001373f`/`0x10013876`.
* The `Node` constructor (ord 32) calls `TreeNode(parent, pos=1)` (ord 56): widgets always attach at the end of the parent.

## 5. Changes to existing code (all additive in the API; behaviour changed where indicated)

* `RObject::GetObjClassId` (virtual, new), `PtrArray`/`TreeNode`/`TreeRoot`/`Module`/`RegKey`/`Window`/`Application`/`Scene`/`InputSource`/`Controller`/`FocusMgr`/`CursorMgr`/`PickMgr`/`IdleTimer`/`AccelTable`/`AudioMgr`/`ResTable`: new methods only, `APPINIT::extra38c` and `ClassId` new.
* **Behaviour changed** (fidelity to the original): (1) `InputSource::Enable` now calls `Activate` if the controller is not suspended, and `Controller::Suspend` calls the sources' `Reactivate` on transitions;
  (2) `HideCount` pauses animated sprites (see above); (3) `Widget` calls `PickMgr::Hold/Release` if the style has bit `0x70`, and `Scene::ForgetWidget` on deletion;
  (4) `AccelTable::Push(bool passThrough = false)` (the default changes nothing); (5) `Application::Activate` does not re-send `WM_ACTIVATEAPP` if the state is unchanged, and `CreateMainWindow` sends it once at the start
  (in the original it comes from the system); `Application` owns the two `JoystickDev`s and polls them every 10 ms in `RunFrame`; `JoystickMgr::CountDevices` now counts SDL joysticks (previously 0);
  (6) `Application::ParseArg` also accepts `' '`; `OnDestroy` calls `ResetDisplayMode`; `Scene::~Scene` deletes the registered cursors before the resources.

## 6. What remains / limits

* Nothing of the `unused` ordinals remains to be ported. Not verified with a window or with a game (project rule): the joystick is tested only with fake devices (`pjoy::SetFake`), real SDL enumeration
  is called only by `JoystickMgr::CountDevices`/`Probe`; `Window::Create` is not exercised by the tests (it would open an SDL window, even if hidden).
* `ControlWin` is a model, not a control: if a future game wanted a text box it would have to draw it itself.
* The sprite cursor classes (`DibCursor`/`AnimCursor`) are ported and connected to `CursorMgr` but the game does not create them: interaction with the real pointer (hiding the OS cursor when `Handle` returns 0) is not wired up
  (the port hides the cursor with `CursorMgr::ShowCursor`).

## 7. Ordinal -> status -> note table

| ord | address | name | status | where | note |
|---:|---|---|---|---|---|
| 1 | `0x1001de04` | `kCopyright` | implemented | `aaron/Aaron.h:26` | data export: the copyright string |
| 2 | `0x10001022` | `RObject::RObject` | implemented | `aaron/RObject.h:24` |  |
| 4 | `0x10001100` | `MemBuffer::MemBuffer` | implemented | `aaron/Containers.cpp:17` |  |
| 6 | `0x10008350` | `Cursor::Cursor` | implemented | `aaron/Cursors.cpp:8` |  |
| 7 | `0x100086c4` | `AnimCursor::AnimCursor` | implemented | `aaron/Cursors.cpp:47` |  |
| 8 | `0x100084bc` | `DibCursor::DibCursor` | implemented | `aaron/Cursors.cpp:20` |  |
| 9 | `0x1000896c` | `CursorPair::CursorPair` | implemented | `aaron/Cursors.cpp:78` |  |
| 10 | `0x10008441` | `ResCursor::ResCursor` | implemented | `aaron/Cursors.cpp:16` |  |
| 12 | `0x10008d8d` | `RegKey::RegKey_Root` | implemented | `aaron/RegKey.h:30` |  |
| 18 | `0x100057c0` | `PolySprite::PolySprite_Line` | implemented | `aaron/SpriteWidgets.cpp:162` |  |
| 19 | `0x100058b9` | `PolySprite::PolySprite_Points` | implemented | `aaron/SpriteWidgets.cpp:162` |  |
| 21 | `0x10005b0d` | `TextSprite::TextSprite` | implemented | `aaron/SpriteWidgets.cpp:197` |  |
| 25 | `0x10005055` | `AnimPolySprite::AnimPolySprite_Line` | implemented | `aaron/SpriteWidgets.cpp:178` |  |
| 26 | `0x10005167` | `AnimPolySprite::AnimPolySprite_Points` | implemented | `aaron/SpriteWidgets.cpp:178` |  |
| 30 | `0x10006fb2` | `ControlWin::ControlWin` | implemented (model) | `aaron/ControlWin.cpp:12` | model of a native child control (no SDL equivalent) |
| 31 | `0x1000794c` | `EditControl::EditControl` | implemented (model) | `aaron/ControlWin.cpp:236` | class EDIT |
| 33 | `0x10003e72` | `Node::Node_TreeRoot` | implemented | `aaron/Widget.cpp:322` |  |
| 39 | `0x10001c64` | `LinkList::LinkList` | implemented | `aaron/Containers.h:103` |  |
| 40 | `0x10001b74` | `LinkItem::LinkItem` | implemented | `aaron/Containers.cpp:150` |  |
| 41 | `0x1000b0e3` | `Module::Module_Fn` | implemented | `aaron/Module.cpp:57` |  |
| 42 | `0x1000b067` | `Module::Module_Dll` | implemented | `aaron/Module.cpp:59` |  |
| 43 | `0x1000afe0` | `Module::Module_Exe` | implemented | `aaron/Module.cpp:64` |  |
| 45 | `0x10001f78` | `PtrArray::PtrArray_Copy` | implemented | `aaron/RObject.cpp:33` |  |
| 48 | `0x10003d70` | `Node::Node_TimerRoot` | implemented | `aaron/Widget.cpp:322` | Node(TreeRoot*) + TimerMgr subscription; the port's Scene does not need the subscription |
| 50 | `0x1000e750` | `Window::Window` | implemented | `aaron/Window.cpp:18` |  |
| 53 | `0x1000f486` | `Application::Application_TitleDll` | implemented | `aaron/Application.cpp:50` |  |
| 54 | `0x1000f667` | `Application::Application_TitleFn` | implemented | `aaron/Application.cpp:59` |  |
| 55 | `0x10002c1c` | `TreeRoot::TreeRoot` | implemented | `aaron/RObject.h:123` |  |
| 56 | `0x100028d6` | `TreeNode::TreeNode_Parent` | implemented | `aaron/RObject.cpp:147` |  |
| 57 | `0x1000286b` | `TreeNode::TreeNode_Root` | implemented | `aaron/RObject.cpp:146` |  |
| 59 | `0x10001990` | `RObject::~RObject` | implemented | `aaron/RObject.h:28` | base-class destructor |
| 60 | `0x100011ff` | `MemBuffer::~MemBuffer` | implemented | `aaron/Containers.cpp:31` |  |
| 62 | `0x10013f40` | `TimerMgr::~TimerMgr` | implemented | `aaron/Managers.h:49` | compiler-generated destructor chain of the Broadcaster+Subscriber classes (C++ destructors) |
| 63 | `0x100083d2` | `Cursor::~Cursor` | implemented | `aaron/Cursors.cpp:12` |  |
| 64 | `0x100087e0` | `AnimCursor::~AnimCursor` | implemented | `aaron/Cursors.cpp:58` |  |
| 65 | `0x1000859b` | `DibCursor::~DibCursor` | implemented | `aaron/Cursors.cpp:31` |  |
| 66 | `0x100089cb` | `CursorPair::~CursorPair` | implemented | `aaron/Cursors.cpp:83` |  |
| 67 | `0x10008de5` | `RegKey::~RegKey` | implemented | `aaron/RegKey.cpp:129` |  |
| 69 | `0x1000715a` | `ControlWin::~ControlWin` | implemented (model) | `aaron/ControlWin.cpp:37` |  |
| 70 | `0x1000928d` | `Controller::~Controller` | implemented | `aaron/Input.h:142` | sources are unique_ptr-owned |
| 71 | `0x10009ac4` | `InputSource::~InputSource` | implemented | `aaron/Input.cpp:225` |  |
| 72 | `0x10001c96` | `LinkList::~LinkList` | implemented | `aaron/Containers.cpp:205` |  |
| 73 | `0x10001be1` | `LinkItem::~LinkItem` | implemented | `aaron/Containers.cpp:152` |  |
| 74 | `0x1000b141` | `Module::~Module` | implemented | `aaron/Module.cpp:66` |  |
| 75 | `0x10009ce0` | `ControllerSources::~ControllerSources` | implemented | `aaron/Input.h:142` | DeleteAll + ~PtrArray: unique_ptr vector in the port |
| 79 | `0x1000e782` | `Window::~Window` | implemented | `aaron/Window.cpp:20` |  |
| 82 | `0x10002c53` | `TreeRoot::~TreeRoot` | implemented | `aaron/RObject.cpp:263` |  |
| 86 | `0x100086a2` | `AnimCursor::operator_new` | n.a. | `aaron/RObject.h:47` | per-class malloc thunk |
| 87 | `0x1000849a` | `DibCursor::operator_new` | n.a. | `aaron/RObject.h:48` | per-class malloc thunk |
| 91 | `0x1000579e` | `PolySprite::operator_new` | n.a. | `aaron/RObject.h:49` | per-class malloc thunk |
| 93 | `0x10005aeb` | `TextSprite::operator_new` | n.a. | `aaron/RObject.h:50` | per-class malloc thunk |
| 95 | `0x10005033` | `AnimPolySprite::operator_new` | n.a. | `aaron/RObject.h:51` | per-class malloc thunk: global operator new |
| 98 | `0x10009140` | `Controller::operator_new` | n.a. | `aaron/RObject.h:52` | per-class malloc thunk |
| 105 | `0x100086b3` | `AnimCursor::operator_delete` | n.a. | `aaron/RObject.h:47` | per-class free thunk |
| 106 | `0x100084ab` | `DibCursor::operator_delete` | n.a. | `aaron/RObject.h:48` | per-class free thunk |
| 110 | `0x100057af` | `PolySprite::operator_delete` | n.a. | `aaron/RObject.h:49` | per-class free thunk |
| 112 | `0x10005afc` | `TextSprite::operator_delete` | n.a. | `aaron/RObject.h:50` | per-class free thunk |
| 114 | `0x10005044` | `AnimPolySprite::operator_delete` | n.a. | `aaron/RObject.h:51` | per-class free thunk: global operator delete |
| 117 | `0x10009151` | `Controller::operator_delete` | n.a. | `aaron/RObject.h:52` | per-class free thunk |
| 131 | `0x10008bf0` | `Cursor::Activate` | implemented | `aaron/Cursors.h:6` | returns the id |
| 132 | `0x100088e8` | `AnimCursor::Activate` | implemented | `aaron/Cursors.cpp:62` |  |
| 133 | `0x1000865e` | `DibCursor::Activate` | implemented | `aaron/Cursors.cpp:35` |  |
| 134 | `0x10008a83` | `CursorPair::Activate` | implemented | `aaron/Cursors.cpp:85` |  |
| 136 | `0x10012167` | `PickMgr::Hold` | implemented | `aaron/Managers.cpp:419` |  |
| 137 | `0x10001043` | `RObject::AddRef` | implemented | `aaron/RObject.h:29` |  |
| 139 | `0x100029a7` | `TreeNode::AdoptRun` | implemented | `aaron/RObject.cpp:204` |  |
| 140 | `0x10002a03` | `TreeNode::AdoptChildrenOf` | implemented | `aaron/RObject.cpp:218` |  |
| 143 | `0x10005702` | `DibSprite::AnimateList` | implemented | `aaron/SpriteWidgets.cpp:28` | the +0x40 twin of ord 144 (WP-2 had attributed 144 to DibSprite) |
| 145 | `0x10001260` | `MemBuffer::Append` | implemented | `aaron/Containers.cpp:75` |  |
| 146 | `0x100012e9` | `MemBuffer::AppendLine` | implemented | `aaron/Containers.cpp:86` |  |
| 147 | `0x100012be` | `MemBuffer::AppendString` | implemented | `aaron/Containers.cpp:82` |  |
| 148 | `0x1000fba4` | `Application::SetStatusTitle` | implemented | `aaron/Application.cpp:265` |  |
| 152 | `0x100121ca` | `PickMgr::OnWidgetDestroyed` | implemented | `aaron/Managers.h:264` |  |
| 153 | `0x10012ce2` | `FocusMgr::OnWidgetDestroyed` | implemented | `aaron/Managers.cpp:237` |  |
| 154 | `0x10013353` | `CursorMgr::OnWidgetDestroyed` | implemented | `aaron/Managers.cpp:354` |  |
| 155 | `0x10001ce8` | `LinkList::DeleteAll` | implemented | `aaron/Containers.cpp:207` |  |
| 157 | `0x10002610` | `PtrArray::Reset` | implemented | `aaron/RObject.h:80` |  |
| 161 | `0x1000134b` | `MemBuffer::Decommit` | implemented | `aaron/Containers.cpp:121` |  |
| 162 | `0x10002150` | `PtrArray::Shrink` | implemented | `aaron/RObject.cpp:106` |  |
| 165 | `0x1000e7d4` | `Window::CreateInParent` | implemented | `aaron/Window.cpp:92` | SDL: 640x480 platform window; recorded geometry only |
| 166 | `0x1000e829` | `Window::Create` | implemented | `aaron/Window.cpp:83` |  |
| 168 | `0x1000ffa7` | `Application::GetModuleForFn` | implemented | `aaron/Application.cpp:239` | Ghidra's CMFCToolBarButton parameter is a false library match |
| 170 | `0x1001008e` | `Application::AcquireArchive` | implemented | `aaron/Application.h:140` |  |
| 171 | `0x10008e1e` | `RegKey::OpenSubKey` | implemented | `aaron/RegKey.h:39` |  |
| 175 | `0x10008edd` | `RegKey::DeleteValue` | implemented | `aaron/RegKey.cpp:215` | also deletes the sub key of that name (original quirk) |
| 176 | `0x100024fb` | `PtrArray::DeleteAt` | implemented | `aaron/RObject.cpp:130` |  |
| 179 | `0x1000cbf2` | `Scene::PickWidget` | implemented | `aaron/Scene.cpp:43` |  |
| 180 | `0x100013ef` | `MemBuffer::Reset` | implemented | `aaron/Containers.cpp:130` |  |
| 181 | `0x100121ec` | `PickMgr::SetEnabled` | implemented | `aaron/Managers.cpp:431` |  |
| 184 | `0x1000cc1a` | `Scene::EnableCount` | implemented | `aaron/Scene.cpp:44` |  |
| 185 | `0x10009422` | `Controller::SetSourceFlags` | implemented | `aaron/Input.cpp:134` |  |
| 186 | `0x1000cc39` | `Scene::SuspendControllers` | implemented | `aaron/Scene.h:166` |  |
| 188 | `0x10007d4f` | `AccelTable::ForEach` | implemented | `aaron/Managers.cpp:134` |  |
| 189 | `0x10001d2d` | `LinkList::ForEach` | implemented | `aaron/Containers.cpp:211` | original releases the NEXT item (UAF bug); port does not |
| 190 | `0x100021c5` | `PtrArray::ForEach` | implemented | `aaron/RObject.cpp:116` |  |
| 191 | `0x10002a86` | `TreeNode::ForEachChild` | implemented | `aaron/RObject.cpp:235` | original releases the NEXT child (bug); port does not |
| 192 | `0x1000ba17` | `ResTable::ForEachSlot` | implemented | `aaron/ResArchive.cpp:78` |  |
| 193 | `0x1000ba76` | `ResTable::ForEachResource` | implemented | `aaron/ResArchive.cpp:84` |  |
| 194 | `0x10010215` | `Application::ForEachWindow` | implemented | `aaron/Application.cpp:211` |  |
| 195 | `0x1001023c` | `Application::ForEachScene` | implemented | `aaron/Application.cpp:218` |  |
| 197 | `0x10009dd1` | `JoystickSource::PickDevice` | implemented | `aaron/Input.cpp:230` |  |
| 198 | `0x1000cc88` | `Scene::GetIndexedResObject` | implemented | `aaron/Scene.cpp:55` |  |
| 199 | `0x1000ccd6` | `Scene::GetResObject` | implemented | `aaron/Scene.cpp:47` |  |
| 200 | `0x10012d7f` | `FocusMgr::SetFocusState` | implemented | `aaron/Managers.cpp:199` |  |
| 201 | `0x10001444` | `MemBuffer::Free` | implemented | `aaron/Containers.cpp:36` |  |
| 202 | `0x1000b797` | `Scene::FreeDynamicResource` | implemented | `aaron/SceneRes.cpp:39` |  |
| 205 | `0x1000b699` | `ResTable::Free_Thunk` | implemented | `aaron/ResArchive.h:48` |  |
| 208 | `0x10013392` | `CursorMgr::RefreshPointer` | implemented | `aaron/Managers.h:215` | no-op: GetCursorPos/SetCursorPos re-post of the pointer position |
| 211 | `0x100068d0` | `DibSprite::GetDrawObj` | implemented | `aaron/Sprites.h:21` | thunk +0x40 (ords 211-214 GetAnimObj of the Anim sprites, 260-264 GetDrawObj of Dib/Poly/Rect/Text sprites) |
| 212 | `0x100068d0` | `DibSprite::GetDrawObj` | implemented | `aaron/Sprites.h:21` | thunk +0x40 (ords 211-214 GetAnimObj of the Anim sprites, 260-264 GetDrawObj of Dib/Poly/Rect/Text sprites) |
| 213 | `0x100068d0` | `DibSprite::GetDrawObj` | implemented | `aaron/Sprites.h:21` | thunk +0x40 (ords 211-214 GetAnimObj of the Anim sprites, 260-264 GetDrawObj of Dib/Poly/Rect/Text sprites) |
| 214 | `0x100068d0` | `DibSprite::GetDrawObj` | implemented | `aaron/Sprites.h:21` | thunk +0x40 (ords 211-214 GetAnimObj of the Anim sprites, 260-264 GetDrawObj of Dib/Poly/Rect/Text sprites) |
| 219 | `0x100071be` | `ControlWin::GetClientRectScene` | implemented (model) | `aaron/ControlWin.cpp:57` |  |
| 223 | `0x100032be` | `Button::Nop223` | implemented | `aaron/Sprites.h:221` | empty slot-3 virtual of the Button vtable |
| 224 | `0x10002fab` | `Button::Nop224` | implemented | `aaron/Sprites.h:221` | empty slot-3 virtual of the Button-like vtable 0x1001d4e0 |
| 227 | `0x100059b6` | `PolySprite::Nop227` | implemented | `aaron/Sprites.h:137` |  |
| 229 | `0x10005c71` | `TextSprite::Nop229` | implemented | `aaron/Sprites.h:168` |  |
| 231 | `0x1000527d` | `AnimPolySprite::Nop231` | implemented | `aaron/Sprites.h:154` |  |
| 232 | `0x100053be` | `AnimRectSprite::Nop232` | implemented | `aaron/Sprites.h:185` |  |
| 233 | `0x10005575` | `AnimTextSprite::Nop233` | implemented | `aaron/Sprites.h:205` |  |
| 235 | `0x100071f5` | `ControlWin::Nop235` | implemented (model) | `aaron/ControlWin.h:33` |  |
| 238 | `0x10003e2d` | `Node::Nop238` | implemented | `aaron/Widget.h:162` |  |
| 239 | `0x1000484d` | `Node::Nop239` | implemented | `aaron/Widget.h:163` |  |
| 251 | `0x10008fdf` | `RegKey::QueryString` | implemented | `aaron/RegKey.cpp:227` |  |
| 257 | `0x10003d10` | `Dialog::GetPickFlags` | implemented | `aaron/Sprites.h:264` | returns 0x80 |
| 259 | `0x10003580` | `Button::GetDrawObj` | implemented | `aaron/Sprites.h:225` | CDrawObj at +0x44: C++ inheritance (thunk) |
| 260 | `0x100068d0` | `DibSprite::GetDrawObj` | implemented | `aaron/Sprites.h:21` | thunk +0x40 (ords 211-214 GetAnimObj of the Anim sprites, 260-264 GetDrawObj of Dib/Poly/Rect/Text sprites) |
| 261 | `0x100068d0` | `DibSprite::GetDrawObj` | implemented | `aaron/Sprites.h:21` | thunk +0x40 (ords 211-214 GetAnimObj of the Anim sprites, 260-264 GetDrawObj of Dib/Poly/Rect/Text sprites) |
| 262 | `0x100068d0` | `DibSprite::GetDrawObj` | implemented | `aaron/Sprites.h:21` | thunk +0x40 (ords 211-214 GetAnimObj of the Anim sprites, 260-264 GetDrawObj of Dib/Poly/Rect/Text sprites) |
| 263 | `0x100068d0` | `DibSprite::GetDrawObj` | implemented | `aaron/Sprites.h:21` | thunk +0x40 (ords 211-214 GetAnimObj of the Anim sprites, 260-264 GetDrawObj of Dib/Poly/Rect/Text sprites) |
| 264 | `0x100068d0` | `DibSprite::GetDrawObj` | implemented | `aaron/Sprites.h:21` | thunk +0x40 (ords 211-214 GetAnimObj of the Anim sprites, 260-264 GetDrawObj of Dib/Poly/Rect/Text sprites) |
| 265 | `0x100067f0` | `AnimSprite::GetDrawObj` | implemented | `aaron/Sprites.h:22` | CDrawObj at +0xdc (inheritance thunk) |
| 266 | `0x10006900` | `AnimPolySprite::GetDrawObj` | implemented | `aaron/Sprites.h:22` | thunk +0xdc (ords 266-268: AnimPoly/AnimRect/AnimText sprites) |
| 267 | `0x10006900` | `AnimPolySprite::GetDrawObj` | implemented | `aaron/Sprites.h:22` | thunk +0xdc (ords 266-268: AnimPoly/AnimRect/AnimText sprites) |
| 268 | `0x10006900` | `AnimPolySprite::GetDrawObj` | implemented | `aaron/Sprites.h:22` | thunk +0xdc (ords 266-268: AnimPoly/AnimRect/AnimText sprites) |
| 284 | `0x10007202` | `ControlWin::GetClientWidth` | implemented (model) | `aaron/ControlWin.cpp:79` |  |
| 285 | `0x1000e8f4` | `Window::GetClientHeight` | implemented | `aaron/Window.cpp:62` |  |
| 286 | `0x100133b7` | `CursorMgr::GetUserFlag` | implemented | `aaron/Managers.cpp:375` |  |
| 291 | `0x10009481` | `Controller::FindSource` | implemented | `aaron/Input.cpp:139` | returns the last source when none matches (original quirk) |
| 292 | `0x1000a100` | `JoystickSource::Type` | implemented | `aaron/Input.h:68` | returns 3 |
| 293 | `0x1000e360` | `Scene::OnPreCreate` | implemented | `aaron/Application.h:76` | shared "return 1" thunk (ords 293/489/508/517/518: OnPreCreate/OnCreate/OnStart, Keyboard type, OnQueryClose) |
| 294 | `0x1000abd0` | `MouseSource::Type` | implemented | `aaron/Input.h:121` | returns 2 |
| 295 | `0x100128e1` | `JoystickDev::GetRange` | implemented | `aaron/Joystick.cpp:24` | SDL2 platform/Joystick |
| 297 | `0x1000a24c` | `KeyboardSource::FindKey` | implemented | `aaron/Input.cpp:320` |  |
| 303 | `0x100014e6` | `MemBuffer::Stats` | implemented | `aaron/Containers.cpp:145` |  |
| 330 | `0x10012283` | `PickMgr::PickEnabled` | implemented | `aaron/Managers.cpp:414` |  |
| 338 | `0x1000cd6d` | `Scene::GetResType` | implemented | `aaron/Scene.cpp:61` |  |
| 347 | `0x1000721e` | `ControlWin::GetClientSize` | implemented (model) | `aaron/ControlWin.cpp:74` |  |
| 351 | `0x10001511` | `MemBuffer::Info` | implemented | `aaron/Containers.cpp:138` |  |
| 352 | `0x1000b6b9` | `ResTable::GetLastLoad` | implemented | `aaron/ResArchive.cpp:105` |  |
| 353 | `0x10004f71` | `AnimSprite::GetTiming` | implemented | `aaron/Sprites.h:89` |  |
| 358 | `0x1000724f` | `ControlWin::GetText` | implemented (model) | `aaron/ControlWin.cpp:107` |  |
| 366 | `0x100010c0` | `RObject::GetObjClassId` | implemented | `aaron/RObject.h:35` | returns 0x20000 |
| 367 | `0x10008be0` | `Cursor::GetObjClassId` | implemented | `aaron/Cursors.h:27` | 0x20108 |
| 368 | `0x10003570` | `Button::GetClassId` | implemented | `aaron/Sprites.h:220` | 0x20107 |
| 369 | `0x10003d00` | `Dialog::GetClassId` | implemented | `aaron/Sprites.h:263` | 0x20109 |
| 370 | `0x100069c0` | `DibSprite::GetClassId` | implemented | `aaron/Sprites.h:30` | 0x20103 for 6 classes |
| 371 | `0x100069c0` | `DibSprite::GetClassId` | implemented | `aaron/Sprites.h:30` | 0x20103 for 6 classes |
| 372 | `0x10006a00` | `RectSprite::GetClassId` | implemented | `aaron/Sprites.h:31` | 0x20106 for the GDI sprites (ords 372, 376) |
| 373 | `0x100069c0` | `DibSprite::GetClassId` | implemented | `aaron/Sprites.h:30` | 0x20103 for 6 classes |
| 374 | `0x100069c0` | `DibSprite::GetClassId` | implemented | `aaron/Sprites.h:30` | 0x20103 for 6 classes |
| 375 | `0x100069c0` | `DibSprite::GetClassId` | implemented | `aaron/Sprites.h:30` | 0x20103 for 6 classes |
| 376 | `0x10006a00` | `RectSprite::GetClassId` | implemented | `aaron/Sprites.h:31` | 0x20106 for the GDI sprites (ords 372, 376) |
| 377 | `0x100069c0` | `DibSprite::GetClassId` | implemented | `aaron/Sprites.h:30` | 0x20103 for 6 classes |
| 378 | `0x10006f40` | `Timer::GetClassId` | implemented | `aaron/Sprites.h:306` | 0x20105 |
| 379 | `0x10007a60` | `ControlWin::GetClassId` | implemented (model) | `aaron/ControlWin.h:32` | 0x20104 |
| 380 | `0x10004c10` | `Node::GetClassId` | implemented | `aaron/Types.h:58` | 0x20100 |
| 381 | `0x10001e30` | `LinkList::GetObjClassId` | implemented | `aaron/Containers.h:105` | 0x20002 |
| 382 | `0x10001e40` | `LinkItem::GetObjClassId` | implemented | `aaron/Containers.h:85` | 0x20001 |
| 383 | `0x1000b460` | `Module::GetClassId` | implemented | `aaron/Module.h:41` | 0x20083 |
| 384 | `0x10004c20` | `ResNode::GetClassId` | implemented | `aaron/Types.h:59` | 0x20101: CID_CONTAINER1 (the Widget base part; no separate class in the port) |
| 385 | `0x1000c160` | `ResArchive::GetClassId` | implemented | `aaron/ResArchive.h:22` | 0x20084 |
| 386 | `0x1000e350` | `Scene::GetObjClassId` | implemented | `aaron/Scene.h:50` | 0x20082 |
| 387 | `0x1000f2b0` | `Window::GetClassId` | implemented | `aaron/Types.h:54` | 0x20081 |
| 388 | `0x100118e0` | `Application::GetClassId` | implemented | `aaron/Application.h:49` | 0x20080 |
| 389 | `0x10002d70` | `TreeRoot::GetObjClassId` | implemented | `aaron/RObject.h:125` | 0x20004 |
| 390 | `0x10002d80` | `TreeNode::GetBaseClassId` | implemented | `aaron/RObject.h:149` | 0x20003 |
| 391 | `0x10004c30` | `Container::GetClassId` | implemented | `aaron/Types.h:60` | 0x20102: CID_CONTAINER2 (enum only; never instantiated by any game) |
| 392 | `0x1000ce39` | `Scene::GetUserKey` | implemented | `aaron/Scene.cpp:116` |  |
| 393 | `0x10010338` | `Application::GetUserKey` | implemented | `aaron/Application.h:86` |  |
| 394 | `0x10007271` | `ControlWin::GetClientHeight` | implemented (model) | `aaron/ControlWin.cpp:80` |  |
| 395 | `0x1000e9c0` | `Window::GetClientWidth` | implemented | `aaron/Window.cpp:61` |  |
| 396 | `0x10007a70` | `ControlWin::GetHWnd` | implemented (model) | `aaron/ControlWin.h:34` |  |
| 400 | `0x1000ea12` | `Window::WindowProc` | implemented | `aaron/Window.cpp:197` | Win32 messages mapped onto Scene/FocusMgr/CursorMgr |
| 403 | `0x10001557` | `MemBuffer::Insert` | implemented | `aaron/Containers.cpp:93` |  |
| 404 | `0x10002ae1` | `TreeNode::WedgeUnder` | implemented | `aaron/RObject.cpp:222` |  |
| 405 | `0x100015f9` | `MemBuffer::InsertString` | implemented | `aaron/Containers.cpp:102` |  |
| 408 | `0x10012995` | `JoystickDev::Probe` | implemented | `aaron/Joystick.cpp:22` |  |
| 412 | `0x10002b22` | `TreeNode::HasAncestor` | implemented | `aaron/RObject.cpp:229` | original crashes (null deref) when the ancestor is not found |
| 413 | `0x1000cf9c` | `Scene::ContainsWidget` | implemented | `aaron/Scene.cpp:69` |  |
| 416 | `0x1000b213` | `Module::HasCreateFn` | implemented | `aaron/Module.h:45` | Ghidra: CMFCToolBar::IsDragButton (false library match) |
| 417 | `0x1000b1c4` | `Module::Matches` | implemented | `aaron/Module.cpp:71` |  |
| 419 | `0x10001d88` | `LinkList::Contains` | implemented | `aaron/Containers.cpp:222` |  |
| 427 | `0x1000b6d3` | `ResTable::Load_Thunk` | implemented | `aaron/ResArchive.h:47` |  |
| 437 | `0x10001628` | `MemBuffer::Lock` | implemented | `aaron/Containers.h:51` |  |
| 438 | `0x10014270` | `CombinePath` | implemented | `aaron/Path.cpp:51` |  |
| 439 | `0x1000b30b` | `Module::BuildPath` | implemented | `aaron/Module.cpp:90` |  |
| 440 | `0x1000d04d` | `Scene::ToScreen` | implemented | `aaron/Scene.cpp:74` |  |
| 441 | `0x1000d09d` | `Scene::FromScreen` | implemented | `aaron/Scene.cpp:81` |  |
| 442 | `0x10002277` | `PtrArray::MoveElement` | implemented | `aaron/RObject.cpp:122` |  |
| 443 | `0x10001c33` | `LinkItem::Reposition` | implemented | `aaron/Containers.cpp:196` |  |
| 444 | `0x10007627` | `ControlWin::SetPos` | implemented (model) | `aaron/ControlWin.cpp:87` |  |
| 445 | `0x1000eeb4` | `Window::SetPos` | implemented | `aaron/Window.cpp:100` | recorded in the placement |
| 446 | `0x1000d0ed` | `Scene::MessageBox` | implemented | `aaron/Scene.cpp:88` |  |
| 447 | `0x10010c3a` | `Application::MessageBox` | implemented | `aaron/Application.cpp:275` | hook, default: stderr + first button |
| 449 | `0x10009e6f` | `JoystickSource::Activate` | implemented | `aaron/Input.cpp:240` |  |
| 450 | `0x1000a28e` | `KeyboardSource::Activate` | implemented | `aaron/Input.h:103` |  |
| 451 | `0x1000a896` | `MouseSource::Activate` | implemented | `aaron/Input.cpp:372` |  |
| 453 | `0x10003520` | `Widget::NopVirtual` | implemented | `aaron/Input.h:52` | shared empty virtual (slot 2/7/19, ControlWin/Button/..., InputSource::OnBounds) |
| 456 | `0x10003520` | `Widget::NopVirtual` | implemented | `aaron/Input.h:52` | shared empty virtual (slot 2/7/19, ControlWin/Button/..., InputSource::OnBounds) |
| 461 | `0x10003540` | `Scene::OnCommand` | implemented | `aaron/Application.h:62` | shared "return 0" thunk (ords 461/462/555: Scene::OnCommand, Application::OnCommand) |
| 462 | `0x10003540` | `Scene::OnCommand` | implemented | `aaron/Application.h:62` | shared "return 0" thunk (ords 461/462/555: Scene::OnCommand, Application::OnCommand) |
| 467 | `0x1000770d` | `ControlWin::Capture` | implemented (model) | `aaron/ControlWin.h:37` | EnableWindow (inverted by EnableCount - original quirk) |
| 469 | `0x10009b72` | `InputSource::Reactivate` | implemented | `aaron/Input.cpp:107` |  |
| 471 | `0x10003520` | `Widget::NopVirtual` | implemented | `aaron/Input.h:52` | shared empty virtual (slot 2/7/19, ControlWin/Button/..., InputSource::OnBounds) |
| 473 | `0x10003520` | `Widget::NopVirtual` | implemented | `aaron/Input.h:52` | shared empty virtual (slot 2/7/19, ControlWin/Button/..., InputSource::OnBounds) |
| 475 | `0x1000e370` | `Scene::OnQuery` | implemented | `aaron/Scene.h:69` |  |
| 478 | `0x10006a10` | `PolySprite::Create` | implemented | `aaron/SpriteWidgets.cpp:174` | (ords 478-481) |
| 479 | `0x10006a10` | `PolySprite::Create` | implemented | `aaron/SpriteWidgets.cpp:174` | (ords 478-481) |
| 480 | `0x10006a10` | `PolySprite::Create` | implemented | `aaron/SpriteWidgets.cpp:174` | (ords 478-481) |
| 481 | `0x10006a10` | `PolySprite::Create` | implemented | `aaron/SpriteWidgets.cpp:174` | (ords 478-481) |
| 482 | `0x10006820` | `AnimSprite::Create` | implemented | `aaron/Sprites.h:24` | Widget::Create + CDrawObj::Init (+0xdc) |
| 483 | `0x10006980` | `AnimPolySprite::Create` | implemented | `aaron/SpriteWidgets.cpp:190` | (ords 483-485) |
| 484 | `0x10006980` | `AnimPolySprite::Create` | implemented | `aaron/SpriteWidgets.cpp:190` | (ords 483-485) |
| 485 | `0x10006980` | `AnimPolySprite::Create` | implemented | `aaron/SpriteWidgets.cpp:190` | (ords 483-485) |
| 486 | `0x1000772b` | `ControlWin::Create` | implemented (model) | `aaron/ControlWin.cpp:41` |  |
| 489 | `0x1000e360` | `Scene::OnPreCreate` | implemented | `aaron/Application.h:76` | shared "return 1" thunk (ords 293/489/508/517/518: OnPreCreate/OnCreate/OnStart, Keyboard type, OnQueryClose) |
| 492 | `0x1000e380` | `Scene::OnInput` | implemented | `aaron/Scene.h:70` |  |
| 494 | `0x100118f0` | `Application::Reserved10` | implemented | `aaron/Application.h:71` | shared "return 0" thunk (ords 494/502/504) |
| 496 | `0x10011941` | `Application::NotifyApp` | implemented | `aaron/Application.cpp:187` |  |
| 498 | `0x10012a59` | `JoystickDev::Dispatch` | implemented | `aaron/Joystick.cpp:43` |  |
| 499 | `0x10012dd0` | `FocusMgr::OnMessage` | implemented | `aaron/Managers.cpp:246` |  |
| 500 | `0x100135c0` | `CursorMgr::OnMessage` | implemented | `aaron/Managers.cpp:359` |  |
| 502 | `0x100118f0` | `Application::Reserved10` | implemented | `aaron/Application.h:71` | shared "return 0" thunk (ords 494/502/504) |
| 504 | `0x100118f0` | `Application::Reserved10` | implemented | `aaron/Application.h:71` | shared "return 0" thunk (ords 494/502/504) |
| 506 | `0x10004a0d` | `Widget::ToggleCaptured` | implemented | `aaron/Widget.cpp:41` |  |
| 508 | `0x1000e360` | `Scene::OnPreCreate` | implemented | `aaron/Application.h:76` | shared "return 1" thunk (ords 293/489/508/517/518: OnPreCreate/OnCreate/OnStart, Keyboard type, OnQueryClose) |
| 511 | `0x10006860` | `AnimSprite::OnHideChange` | implemented | `aaron/Sprites.h:25` | CAnimObj::Pause(hidden) |
| 512 | `0x10006930` | `AnimPolySprite::OnHideChange` | implemented | `aaron/Sprites.h:25` | CAnimObj::Pause (ords 512-514) |
| 513 | `0x10006930` | `AnimPolySprite::OnHideChange` | implemented | `aaron/Sprites.h:25` | CAnimObj::Pause (ords 512-514) |
| 514 | `0x10006930` | `AnimPolySprite::OnHideChange` | implemented | `aaron/Sprites.h:25` | CAnimObj::Pause (ords 512-514) |
| 517 | `0x1000e360` | `Scene::OnPreCreate` | implemented | `aaron/Application.h:76` | shared "return 1" thunk (ords 293/489/508/517/518: OnPreCreate/OnCreate/OnStart, Keyboard type, OnQueryClose) |
| 518 | `0x1000e360` | `Scene::OnPreCreate` | implemented | `aaron/Application.h:76` | shared "return 1" thunk (ords 293/489/508/517/518: OnPreCreate/OnCreate/OnStart, Keyboard type, OnQueryClose) |
| 519 | `0x1000e3a0` | `Scene::OnObjectEvent2` | implemented | `aaron/Scene.h:76` |  |
| 522 | `0x1000f2c0` | `Window::OnSize` | implemented | `aaron/Application.h:50` | empty virtual slot 2 (Window and Application) |
| 525 | `0x10006d83` | `Timer::PostNotify` | implemented | `aaron/TimerWidget.cpp:32` |  |
| 528 | `0x100110c7` | `Application::ParseCommandLine` | implemented | `aaron/Application.cpp:287` |  |
| 532 | `0x1000dc96` | `Scene::HideCount` | implemented | `aaron/Scene.cpp:45` |  |
| 533 | `0x10011dea` | `AudioMgr::SoundPlay` | implemented | `aaron/Managers.cpp:492` |  |
| 536 | `0x1000dcb5` | `Scene::PlaySoundInfo` | implemented | `aaron/SceneRes.cpp:129` |  |
| 537 | `0x1000dde7` | `Scene::PopAccelerators` | implemented | `aaron/Managers.cpp:127` |  |
| 538 | `0x1001369a` | `CursorMgr::Nop538` | implemented | `aaron/Managers.h:212` |  |
| 540 | `0x10007778` | `ControlWin::PostMessage` | implemented (model) | `aaron/ControlWin.h:70` | delivered at once |
| 542 | `0x1000efea` | `Window::PostMessage` | implemented | `aaron/Window.cpp:114` | queue drained by PumpPlatformEvents |
| 543 | `0x10011261` | `Application::IsDialogMessage` | implemented | `aaron/Application.cpp:227` | original always returns 0; no native dialogs |
| 547 | `0x100069d0` | `PolySprite::HitTest` | implemented | `aaron/SpriteWidgets.cpp:176` | IsPicked +0x40 (ords 547-549) |
| 548 | `0x100069d0` | `PolySprite::HitTest` | implemented | `aaron/SpriteWidgets.cpp:176` | IsPicked +0x40 (ords 547-549) |
| 549 | `0x100069d0` | `PolySprite::HitTest` | implemented | `aaron/SpriteWidgets.cpp:176` | IsPicked +0x40 (ords 547-549) |
| 550 | `0x10006880` | `AnimSprite::HitTest` | implemented | `aaron/Sprites.h:29` | IsPicked (+0xdc) |
| 551 | `0x10006950` | `AnimPolySprite::HitTest` | implemented | `aaron/SpriteWidgets.cpp:195` | IsPicked +0xdc (ords 551-553) |
| 552 | `0x10006950` | `AnimPolySprite::HitTest` | implemented | `aaron/SpriteWidgets.cpp:195` | IsPicked +0xdc (ords 551-553) |
| 553 | `0x10006950` | `AnimPolySprite::HitTest` | implemented | `aaron/SpriteWidgets.cpp:195` | IsPicked +0xdc (ords 551-553) |
| 554 | `0x1000779e` | `ControlWin::HitTest` | implemented (model) | `aaron/ControlWin.cpp:82` |  |
| 555 | `0x10003540` | `Scene::OnCommand` | implemented | `aaron/Application.h:62` | shared "return 0" thunk (ords 461/462/555: Scene::OnCommand, Application::OnCommand) |
| 556 | `0x1000de5a` | `Scene::PushAccelerators` | implemented | `aaron/Managers.cpp:118` |  |
| 557 | `0x100136a5` | `CursorMgr::Nop557` | implemented | `aaron/Managers.h:212` |  |
| 560 | `0x10012355` | `PickMgr::RefreshPointer` | implemented | `aaron/Managers.cpp:444` | no-op: SDL reports the pointer continuously |
| 562 | `0x1000105b` | `RObject::Release` | implemented | `aaron/RObject.h:31` |  |
| 563 | `0x10012f45` | `FocusMgr::ClearModal` | implemented | `aaron/Managers.h:141` |  |
| 564 | `0x100136b2` | `CursorMgr::ReleaseCapture` | implemented | `aaron/Managers.cpp:350` |  |
| 567 | `0x10001643` | `MemBuffer::Remove` | implemented | `aaron/Containers.cpp:106` |  |
| 570 | `0x10012403` | `PickMgr::Release` | implemented | `aaron/Managers.cpp:425` |  |
| 571 | `0x100016ea` | `MemBuffer::RemoveString` | implemented | `aaron/Containers.cpp:116` |  |
| 572 | `0x1000f010` | `Window::Invalidate` | implemented | `aaron/Window.cpp:136` |  |
| 573 | `0x100112cb` | `Application::InvalidateWindows` | implemented | `aaron/Application.cpp:232` |  |
| 574 | `0x10011e25` | `AudioMgr::Reset` | implemented | `aaron/Managers.cpp:494` |  |
| 577 | `0x10003510` | `Widget::NopVirtual2` | implemented | `aaron/Scene.h:63` | shared empty virtual (Scene::OnReady/OnClose, Widget slot 18, ...) |
| 578 | `0x1000a4a9` | `KeyboardSource::Reset` | implemented | `aaron/Input.cpp:326` |  |
| 579 | `0x1001131a` | `Application::ResetDisplayMode` | implemented | `aaron/Application.cpp:237` |  |
| 580 | `0x1000b75d` | `ResTable::ResetLastLoad` | implemented | `aaron/ResArchive.cpp:106` |  |
| 583 | `0x1000f041` | `Window::ShowPlacement` | implemented | `aaron/Window.cpp:162` | placement stored in the RegKey as binary "Placement" |
| 585 | `0x1000f0b2` | `Window::SavePlacement` | implemented | `aaron/Window.cpp:174` |  |
| 588 | `0x10009b27` | `InputSource::Enable` | implemented | `aaron/Input.cpp:217` |  |
| 589 | `0x100079be` | `EditControl::SetSel` | implemented (model) | `aaron/ControlWin.h:107` | EM_SETSEL |
| 591 | `0x100077d7` | `ControlWin::SendMessage` | implemented (model) | `aaron/ControlWin.cpp:140` |  |
| 593 | `0x1000f0f2` | `Window::SendMessage` | implemented | `aaron/Window.cpp:118` |  |
| 596 | `0x100088ae` | `AnimCursor::Handle` | implemented | `aaron/Cursors.cpp:72` |  |
| 597 | `0x1000862d` | `DibCursor::Handle` | implemented | `aaron/Cursors.cpp:41` |  |
| 598 | `0x100089e7` | `CursorPair::Handle` | implemented | `aaron/Cursors.cpp:89` |  |
| 599 | `0x10008c10` | `ResCursor::Handle` | implemented | `aaron/Cursors.h:7` |  |
| 602 | `0x10012a7a` | `JoystickDev::AddListener` | implemented | `aaron/Joystick.cpp:33` |  |
| 605 | `0x100078b7` | `ControlWin::SetBackColor` | implemented (model) | `aaron/ControlWin.cpp:126` |  |
| 608 | `0x1000972a` | `Controller::SetBounds_LTRB` | implemented | `aaron/Input.cpp:125` |  |
| 609 | `0x1001236b` | `PickMgr::SetHover` | implemented | `aaron/Managers.cpp:446` |  |
| 610 | `0x10012f5a` | `FocusMgr::SetModal` | implemented | `aaron/Managers.cpp:228` |  |
| 611 | `0x100136cc` | `CursorMgr::SetCapture` | implemented | `aaron/Managers.cpp:338` |  |
| 616 | `0x100137e7` | `CursorMgr::SetNumber` | implemented | `aaron/Managers.cpp:317` |  |
| 618 | `0x10013812` | `CursorMgr::SetPos` | implemented | `aaron/Managers.cpp:384` | no-op in the port (SetCursorPos); documented |
| 622 | `0x1000dfb8` | `Scene::SetRegistryPath` | implemented | `aaron/Scene.cpp:98` |  |
| 623 | `0x100113d6` | `Application::SetRegistryPath` | implemented | `aaron/Application.cpp:252` |  |
| 624 | `0x100090c2` | `RegKey::SetString` | implemented | `aaron/RegKey.h:57` |  |
| 625 | `0x10003c7b` | `Dialog::SetDefaultButton` | implemented | `aaron/Sprites.h:267` |  |
| 626 | `0x100113f7` | `Application::SetBaseDir` | implemented | `aaron/Application.cpp:256` |  |
| 633 | `0x10012fae` | `FocusMgr::SetFocusWidget` | implemented | `aaron/Managers.cpp:212` | window focus assumed (SDL: always) |
| 636 | `0x1001303e` | `FocusMgr::SetFocusToWindow` | implemented | `aaron/Managers.cpp:198` |  |
| 637 | `0x100077fd` | `ControlWin::SetFont` | implemented (model) | `aaron/ControlWin.cpp:114` |  |
| 638 | `0x1000f118` | `Window::ActivateExisting` | implemented | `aaron/Window.cpp:189` | only windows of this process (no FindWindow across processes) |
| 639 | `0x10013869` | `CursorMgr::SetUserFlag` | implemented | `aaron/Managers.cpp:374` |  |
| 641 | `0x1000f173` | `Window::SetIcon` | implemented | `aaron/Window.cpp:185` | no Win32 icon resources: hook LoadIconResource |
| 642 | `0x100125b1` | `IdleTimer::SetTimeout` | implemented | `aaron/Managers.cpp:75` |  |
| 650 | `0x1000aab0` | `MouseSource::SetBounds_LTRB` | implemented | `aaron/Input.h:124` |  |
| 653 | `0x1000dfd9` | `Scene::SetPaletteRes` | implemented | `aaron/Scene.cpp:100` |  |
| 654 | `0x10011f23` | `AudioMgr::SetPan` | implemented | `aaron/Managers.cpp:495` |  |
| 655 | `0x10002b58` | `TreeNode::Reparent` | implemented | `aaron/RObject.cpp:189` |  |
| 660 | `0x10002cd5` | `TreeRoot::SetRoot` | implemented | `aaron/RObject.cpp:271` |  |
| 661 | `0x10001727` | `MemBuffer::SetSize` | implemented | `aaron/Containers.cpp:47` |  |
| 664 | `0x10004f94` | `AnimSprite::SetTiming` | implemented | `aaron/Sprites.h:89` |  |
| 666 | `0x1000786b` | `ControlWin::SetText` | implemented (model) | `aaron/ControlWin.cpp:103` |  |
| 667 | `0x1000f1d4` | `Window::SetTitleText` | implemented | `aaron/Window.cpp:179` |  |
| 668 | `0x1000788e` | `ControlWin::SetTextColor` | implemented (model) | `aaron/ControlWin.cpp:120` | Ghidra: Concurrency::details::RealizedChore::Initialize (false library match) |
| 669 | `0x1000799e` | `EditControl::SetLimit` | implemented (model) | `aaron/ControlWin.h:107` | EM_LIMITTEXT |
| 670 | `0x10011540` | `Application::SetTitle` | implemented | `aaron/Application.cpp:260` |  |
| 672 | `0x1000f1fb` | `Window::Attach` | implemented | `aaron/Window.cpp:64` | handle is a token (SDL has one platform window) |
| 675 | `0x10006a50` | `PolySprite::Show` | implemented | `aaron/SpriteWidgets.cpp:175` | = Widget::Show (ords 675-678) |
| 676 | `0x10006a50` | `PolySprite::Show` | implemented | `aaron/SpriteWidgets.cpp:175` | = Widget::Show (ords 675-678) |
| 677 | `0x10006a50` | `PolySprite::Show` | implemented | `aaron/SpriteWidgets.cpp:175` | = Widget::Show (ords 675-678) |
| 678 | `0x10006a50` | `PolySprite::Show` | implemented | `aaron/SpriteWidgets.cpp:175` | = Widget::Show (ords 675-678) |
| 679 | `0x100068b0` | `AnimSprite::Show_Thunk` | implemented | `aaron/SpriteWidgets.cpp:191` | -> AnimSprite::Show (ord 142); ords 679-682 (AnimSprite and the Anim Poly/Rect/Text sprites) |
| 680 | `0x100068b0` | `AnimSprite::Show_Thunk` | implemented | `aaron/SpriteWidgets.cpp:191` | -> AnimSprite::Show (ord 142); ords 679-682 (AnimSprite and the Anim Poly/Rect/Text sprites) |
| 681 | `0x100068b0` | `AnimSprite::Show_Thunk` | implemented | `aaron/SpriteWidgets.cpp:191` | -> AnimSprite::Show (ord 142); ords 679-682 (AnimSprite and the Anim Poly/Rect/Text sprites) |
| 682 | `0x100068b0` | `AnimSprite::Show_Thunk` | implemented | `aaron/SpriteWidgets.cpp:191` | -> AnimSprite::Show (ord 142); ords 679-682 (AnimSprite and the Anim Poly/Rect/Text sprites) |
| 683 | `0x100078d7` | `ControlWin::Show` | implemented (model) | `aaron/ControlWin.cpp:47` |  |
| 687 | `0x1000f24a` | `Window::Show` | implemented | `aaron/Window.cpp:141` | SDL window visibility belongs to the platform layer |
| 690 | `0x10007922` | `ControlWin::Resize` | implemented (model) | `aaron/ControlWin.cpp:98` |  |
| 691 | `0x1000f217` | `Window::Resize` | implemented | `aaron/Window.cpp:108` |  |
| 692 | `0x10011f6d` | `AudioMgr::SoundStop` | implemented | `aaron/Managers.cpp:502` |  |
| 695 | `0x10011f93` | `AudioMgr::StopAll` | implemented | `aaron/Managers.cpp:492` |  |
| 698 | `0x10002b95` | `TreeNode::ForEachFromHere` | implemented | `aaron/RObject.cpp:249` |  |
| 699 | `0x10012aa0` | `JoystickDev::RemoveListener` | implemented | `aaron/Joystick.cpp:38` |  |
| 702 | `0x1000b3c9` | `Module::Unload` | implemented | `aaron/Module.cpp:83` |  |
| 709 | `0x10001943` | `MemBuffer::Unlock` | implemented | `aaron/Containers.h:53` |  |
