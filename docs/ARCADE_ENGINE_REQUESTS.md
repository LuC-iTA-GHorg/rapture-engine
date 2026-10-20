# Requests to the engine (G1 Arcade framework)

Status: points 1-4 RESOLVED in the engine (2026-10-18), G1 workarounds removed; point 5 documented.

1. [DONE] **`aaron::FocusMgr::Lock(int)` had the inverted meaning.** Original (Aaron 0x10002d3d): `Lock(0)` = lock (counter++), `Lock(!=0)` = unlock
   (counter--, if > 0). The port (`Managers.cpp`) did the opposite (`Lock(1)` locked). With the original meaning `StartTransition` calls `Lock(0)` and
   `OnSoundMessage(0xF000100A)` calls `Lock(1)`: with the engine as it was, input would stay locked forever.
   Workaround: `CGameBase::LockInput(bool)` + constant `kPortFocusLockInverted` (GameBase.h). Once the engine is fixed: set the constant to `false`.
2. [DONE] **`QM_NOTIFY`: the arguments `a,b,c` reached `CAnimView::OnNotify` truncated to `u32`.** The original passes an event pointer in `c`
   (action 1 of the transition). Workaround: the pointer was kept in `CGameBase::pendingTransitionEvent` and the message carried token 1.
   Request: `OnNotify(obj, intptr_t a, b, c)` or `QUEUEMSG` with a pointer field for NOTIFY.
3. [DONE] **`Scene::OnObjectEvent` default** called `Widget::OnNotify`; the original (slot 15) is a nop and `CGameBase` redefines it as `OnSoundMessage`.
   G1 overrides `OnObjectEvent` and does NOT forward to the widget: if the title scene (G2) expects the port's behaviour, this is a problem only for G1/G3.
4. [DONE] **`PtrArray` did not expose the capacity** (`+0xc` of the original): `CPropertyList::SetAt` (`index < capacity`) was approximated (see README, doubts).
5. [DOCUMENTED] **`Scene::GetMachineKey()`** builds `RegKey(HKLM, regKeyPath_)` with an empty `regKeyPath_` before `Initialize()` (in headless tests the key is "HKLM").
   Informational only.

Resolution: (1) `FocusMgr::Lock(int)` is now as in the original (0 = lock, !=0 = unlock, returns the counter); `LockInput`/`kPortFocusLockInverted` removed.
(2) `CAnimView::OnNotify`, `Scene::OnNotify/OnObjectEvent`, `ANIMNOTIFYPROC` now use `intptr_t a,b,c`; `pendingTransitionEvent` removed (the pointer goes in `c`).
(3) `Scene::OnObjectEvent` default = `OnObjectEvent2(Widget*, a, b, c)` (slot 15, nop), NO LONGER `Widget::OnNotify`. (4) `PtrArray::Capacity()`; the constructor takes the initial capacity and
grows by 1 (as the original); `CPropertyList::SetAt` uses `index < Capacity()`. (5) `GetMachineKey()` before `Initialize()` uses an empty `regKeyPath_`: behaviour left as is.
