// Per-scene / per-application helper managers of Aaron (aaron.md sec. 2.3, 2.5, 2.8):
// PickMgr, FocusMgr, CursorMgr, IdleTimer, TimerMgr, AccelTable, MsgQueue, AudioMgr, JoystickMgr.
#pragma once

#include "aaron/RObject.h"
#include "moses/AudioLib.h"

#include <deque>
#include <vector>

namespace aaron {

class Scene;
class Widget;
class RegKey;
class Cursor;

// ---------------------------------------------------------------------------------------------
// MsgQueue (Scene+0x684/+0x688): deferred messages. The original has 100 slots x 12 bytes, a mutex and a
// double buffer; the port is single threaded (nothing posts from the audio callback), so it is a plain FIFO.
struct QueuedMsg {
    u32 msg = 0;
    uintptr_t wParam = 0;
    intptr_t lParam = 0;
};

class MsgQueue {
public:
    static constexpr int kCapacity = 100; // original capacity; Post() returns false when full (TODO: confirm the original drops)
    bool Post(u32 msg, uintptr_t wParam, intptr_t lParam);
    // MsgQueue::Dispatch 0x1000adbf: drains the queue, calling scene->OnMessage(msg, wp, lp) for each entry.
    // Messages posted by handlers during the drain are processed in the NEXT call (double-buffer semantics).
    void Dispatch(Scene* scene);
    bool Empty() const { return q_.empty() && cur_.empty(); }
    // Aaron FUN_1000ca7d (port addition, requested by pkg4): drops the pending AMSG_NOTIFY messages addressed to `w`
    // (lParam == w), also the ones of the batch being dispatched. Called by ~Widget.
    void PurgeWidget(const Widget* w);
    void Clear() { q_.clear(); cur_.clear(); }

private:
    std::deque<QueuedMsg> q_;   // being filled
    std::deque<QueuedMsg> cur_; // batch being dispatched (second logical buffer)
};

// ---------------------------------------------------------------------------------------------
// TimerMgr (Application+0x3e4 and Scene+0x67c): broadcaster of AMSG_TICK. Tick() computes the ms elapsed
// since the previous call (CAnimView::Now) and, unless paused, broadcasts OnAdvise(AMSG_TICK, totalMs).
// Timer widgets subscribe to it. Aaron 0x10013b93.
// Aaron ord 62 0x10013f40: ~TimerMgr / the destructor of the Broadcaster + Subscriber combination (vtable 0x1001e1d0 / 0x1001e1c8) that
// FocusMgr, CursorMgr, PickMgr and TimerMgr are built on: ~Subscriber of the +0x18 part, then ~Broadcaster.
class TimerMgr : public Broadcaster {
public:
    void Tick(u32 nowMs);
    void Pause(bool on) { paused_ = on; }          // plain flag (port)
    // Aaron 0x10013b12 (the app TimerMgr's +0x30 is a PAUSE COUNTER): on=true -> ++count, broadcasts (0xc04, 1) on the 0->1 transition;
    // on=false -> --count (if >0), broadcasts (0xc04, 0) when it reaches 0. Returns the counter. (port addition, pkg3)
    int PauseCounter(bool on);
    bool Paused() const { return paused_ || pauseCount_ > 0; }
    u32 TotalMs() const { return total_; }         // +0x38 accumulated ms
    u32 LastDelta() const { return delta_; }

private:
    bool paused_ = false;
    int pauseCount_ = 0;
    bool started_ = false;
    u32 last_ = 0, total_ = 0, delta_ = 0;
};

// IdleTimer (Scene+0x678): inactivity timeout (attract mode). Every input resets it.
class IdleTimer {
public:
    void Reset();                                  // Aaron ord 581 0x10002561 (count = 0, deadline = now + period)
    // Port addition: Start(timeoutMs, cmd) posts WMSG_COMMAND(cmd) when the timeout elapses (see SetTimeout for the original semantics).
    void Start(u32 timeoutMs, u32 command);
    // Aaron ord 642 0x100125b1 (IdleTimer::SetTimeout(ms)): period = ms; 0 -> unsubscribes (FocusMgr, CursorMgr, TimerMgr) and clears the
    // deadline; otherwise subscribes and Reset()s. ORIGINAL BEHAVIOUR (0x100124cb): each time the deadline passes the count is incremented
    // and Scene::OnQuery(count) (slot 11) is called; a zero result stops the timer, otherwise the deadline moves by one period. Any input
    // message resets it. (Shoot.* never uses it: its attract mode is driven by its own Timer widgets.)
    void SetTimeout(u32 ms);
    void Stop() { enabled_ = false; }
    void Tick(Scene* scene, u32 nowMs);            // WMSG_COMMAND mode: posts the command; OnQuery mode: see SetTimeout
    u32 Count() const { return count_; }           // +0x28
    u32 Timeout() const { return timeout_; }       // +0x20
    bool Enabled() const { return enabled_; }

private:
    u32 timeout_ = 0, command_ = 0, last_ = 0, count_ = 0;
    bool enabled_ = false;
    bool queryMode_ = false;
};

// ---------------------------------------------------------------------------------------------
// AccelTable (Scene+0x680): key + modifiers -> PostMessage(WMSG_COMMAND, cmd, param) (WM_KEYDOWN only).
class AccelTable {
public:
    void Add(u32 vk, u32 cmd, u32 mod, u32 param);  // via Scene::AddAccelerator (ord 601)
    void Remove(u32 vk, u32 mod);                   // via Scene::RemoveAccelerator (ord 173)
    // true (and the command posted) if (vk, mod) matches an entry. With a pass-through table on top the keys it does not know are looked up in
    // the tables below it (the original forwards the key to the previous table: 0x10007daa, `+0x2c != 0`).
    bool Translate(Scene* scene, u32 vk, u32 mod);
    // Aaron FUN_1000de5a (ord 556) / FUN_1000dde7 (ord 537): Push(passThrough) hides the current accelerators behind a new empty table (modal
    // dialogs; `passThrough` = the new table's +0x2c), Pop() discards the top table and restores the previous one.
    void Push(bool passThrough = false);
    void Pop();
    int Depth() const { return int(stack_.size()); }
    // One accelerator = 0x10 bytes in the original: {vk, mod, cmd, param} (array at +0x1c, count at +0x20).
    struct Entry { u32 vk, mod, cmd, param; };
    // Aaron ord 188 0x10007d4f: fn(&entry, a, b) for every entry of the active table while fn returns nonzero; returns the last result.
    using ForEachFn = int (*)(const Entry* e, void* a, void* b);
    int ForEach(ForEachFn fn, void* a, void* b) const;
    int Count() const { return int(entries_.size()); }

private:
    struct Level { std::vector<Entry> entries; bool pass = false; };
    bool match(const std::vector<Entry>& v, Scene* scene, u32 vk, u32 mod) const;
    std::vector<Entry> entries_;
    bool pass_ = false;
    std::vector<Level> stack_;
};

// ---------------------------------------------------------------------------------------------
// FocusMgr (Scene+0x66c): keyboard focus + global keys. Aaron 0x10002d3d.
class FocusMgr {
public:
    // Aaron ord 712 0x10002d3d: lock/unlock the input (counter): used by Shoot during transitions.
    // Original semantics: Lock(0) = lock (counter++), Lock(!=0) = unlock (counter--, if > 0). Returns the counter.
    int Lock(int unlock);
    bool Locked() const { return lock_ > 0; }
    void SetFocus(Widget* w) { focus_ = w; }
    Widget* GetFocus() const { return focus_; }
    // ---- WP-4: the rest of the original FocusMgr (Aaron.dll 0x10012ad4..): focus state machine + modal widget ----------------------------
    // Aaron ord 200 0x10012d7f: on: preferred = focus = w, w->OnFocus(true); off: if w: focus = null, w->OnFocus(false).
    void SetFocusState(Widget* w, bool on);
    // Aaron ord 633 0x10012fae (FocusMgr::SetFocus(widget)): null -> preferred = null and the window loses the focus; a widget that differs
    // from the current one and is not disabled takes it (old: OnFocus(false), new: OnFocus(true)). Port: the window always has the focus.
    void SetFocusWidget(Widget* w);
    // Aaron ord 636 0x1001303e: preferred = null; the focus returns to the window (the focused widget is told OnFocus(false)).
    void SetFocusToWindow(Scene* scene);
    // Aaron ord 610 0x10012f5a: sets the modal widget (keys go to it first); returns the previous one. null clears it; a disabled widget is refused.
    Widget* SetModal(Widget* w);
    void ClearModal() { modal_ = nullptr; }              // Aaron ord 563 0x10012f45
    Widget* Modal() const { return modal_; }
    Widget* Preferred() const { return pref_; }
    // Aaron ord 153 0x10012ce2: a widget is being destroyed: drops it as modal / preferred (and the focus if it was focused).
    void OnWidgetDestroyed(Widget* w, Scene* scene);
    // Aaron ord 499 0x10012dd0 (FocusMgr::OnMessage): WM_SETFOCUS (7) -> restore the preferred widget; WM_KILLFOCUS (8) -> focus off;
    // Ctrl+Shift+Alt+V on key-up -> AMSG_ABOUT posted to the application; any other key message, unless paused (Pause key excepted) or locked:
    // the modal widget, then the focused widget, then the key listeners. Returns true if a widget consumed the key.
    bool OnMessage(Scene* scene, u32 msg, uintptr_t wParam, intptr_t lParam);
    bool Paused() const { return paused_; }               // +0x3c (TimerMgr pause state)
    void SetPaused(bool p) { paused_ = p; }
    // Keyboard entry from Window: dispatch to listeners, then to the focused widget (OnKey).
    // Ctrl+Shift+Alt+V -> About (not ported); Pause (0x13) -> broadcast AMSG_PAUSE.
    // Returns true if consumed.
    bool OnKey(Scene* scene, u32 msg, u32 vk, u32 mods);
    // Listener lists (Shoot.exe Title scene: "keyboard listeners" at Scene+0x66c, "sound listeners" at +0x670).
    // TODO(package 3): the real owner objects of these lists are unclear (aaron.md says 0x66c FocusMgr, 0x670 CursorMgr).
    void AddKeyListener(SceneListener* l);
    void RemoveKeyListener(SceneListener* l);
    void AddSoundListener(SceneListener* l);
    void RemoveSoundListener(SceneListener* l);
    const std::vector<SceneListener*>& KeyListeners() const { return keyListeners_; }
    const std::vector<SceneListener*>& SoundListeners() const { return soundListeners_; }

private:
    int lock_ = 0;
    Widget* focus_ = nullptr;      // +0x30
    Widget* pref_ = nullptr;       // +0x34
    Widget* modal_ = nullptr;      // +0x38
    bool paused_ = false;          // +0x3c
    std::vector<SceneListener*> keyListeners_, soundListeners_;
};

// ---------------------------------------------------------------------------------------------
// CursorMgr (Scene+0x670, 0x68 bytes): mouse capture/cursor shape/visibility.
class CursorMgr {
public:
    CursorMgr();
    ~CursorMgr();                                   // deletes the cursor objects that registered with it (Aaron 0x10013212: PtrArray::DeleteAll)
    // Aaron ord 614 0x1001373f (CursorMgr::SetCursor(n); Ordinals.h has the typo 0x1000373f): n < 5 selects one of the five global cursor sets (0 hidden, 1 arrow, 2 wait, 3 no,
    // 4 the DLL's own cursor); anything else is a Cursor* given directly. The old cursor gets Activate(0), the new one Activate(1); its result
    // (nonzero when the cursor needs pick feedback) drives PickMgr::SetEnabled (if the pointer is shown). Returns the previous shape number.
    int SetCursor(int n);
    // Port addition: select a Cursor object directly (the original passes the pointer as `param_1 >= 5`).
    Cursor* SetCursorObject(Cursor* c);
    Cursor* CurrentCursor() const { return current_; }
    // The five global cursor sets built by FUN_100133c1 (null scene); created on first use.
    static Cursor* GlobalSet(int n);
    // Cursor objects register here from their constructor (FUN_10013332 / FUN_10013373, array at +0x54).
    void AddCursor(Cursor* c);
    void RemoveCursor(Cursor* c);
    int CursorCount() const { return int(cursors_.size()); }
    void DeleteCursors();                           // PtrArray::DeleteAll of +0x54
    // Aaron ord 616 0x100137e7: the cursor "number" handed to Cursor::Handle (CursorPair selects with it); 0x1001397f refreshes the shape.
    void SetNumber(int n);
    int Number() const { return number_; }
    // Aaron ord 611 0x100136cc (CursorMgr::Capture(widget)): returns the previous capturing widget; null releases; a disabled widget is refused.
    Widget* SetCapture(Widget* w);
    // Aaron ord 564 0x100136b2: ReleaseCapture (the original calls Win32 ReleaseCapture, whose WM_CAPTURECHANGED lands in OnMessage 0x215).
    void ReleaseCapture();
    // Aaron ord 154 0x10013353: a widget is being destroyed -> ReleaseCapture if it holds the capture.
    void OnWidgetDestroyed(Widget* w);
    // Aaron ord 500 0x100135c0 (CursorMgr::OnMessage): WM_SETCURSOR (0x20) -> make the pointer visible again; WM_CAPTURECHANGED (0x215) -> the
    // capturing widget loses its pressed look and the capture; mouse messages -> position (x,y) stored, then the capture widget (or Broadcast
    // to the listeners, or the picked widget) gets the message. `lParam` packs x | y << 16 (scene coordinates).
    bool OnMessage(Scene* scene, u32 msg, uintptr_t wParam, intptr_t lParam);
    int LastX() const { return lastX_; }
    int LastY() const { return lastY_; }
    // Aaron ord 639 0x10013869 / ord 286 0x100133b7 (static): a process-wide user value (DAT_10023dec); nothing in Aaron reads it back.
    static void SetUserFlag(u32 v);
    static u32 GetUserFlag();
    // Aaron ord 538 0x1001369a / ord 557 0x100136a5: two empty virtuals of the cursor manager's vtable (slot ids of the Subscriber part).
    static void Nop538() {}
    static void Nop557() {}
    // Aaron ord 208 0x10013392 (static): GetCursorPos + SetCursorPos = re-posts the pointer position so Windows re-sends WM_SETCURSOR / WM_MOUSEMOVE.
    // Port: nothing to refresh (SDL reports the pointer continuously).
    static void RefreshPointer() {}

    // Aaron ord 685 0x10003876: counter; <0 hides (SetCursor(NULL)). ShowCursor(0)/(1) as in Win32.
    int ShowCursor(int show);
    void SetPos(int x, int y);                     // Aaron ord 618 0x10013812 (unused by Shoot.*): ToScreen + SetCursorPos (port: no-op / SDL_WarpMouseInWindow)
    void Capture(Widget* w) { capture_ = w; }      // plain assignment (the faithful version is SetCapture, ord 611)
    Widget* Captured() const { return capture_; }
    Widget* Hovered() const { return hover_; }
    // Widget destruction (FUN_1000ca7d): drop every reference held to w (capture and the port's hover tracking).
    void Forget(Widget* w) { if (capture_ == w) capture_ = nullptr; if (hover_ == w) hover_ = nullptr; }
    // Aaron ord 500 CursorMgr::OnMouse: (x, y) are scene coordinates (already centred).
    // Mouse capture first, otherwise PickMgr (CDrawView::IsPicked -> Widget::GetFromObj) -> Widget::OnMouse.
    bool OnMouse(Scene* scene, u32 msg, int x, int y, u32 keys);

private:
    int shape_ = 1;
    int showCount_ = 0;
    int number_ = 0;                // +0x4c
    int lastX_ = 0, lastY_ = 0;     // +0x34 / +0x38
    Cursor* current_ = nullptr;     // +0x44
    std::vector<Cursor*> cursors_;  // +0x54
    bool deleting_ = false;
    Widget* capture_ = nullptr;     // +0x30
    Widget* hover_ = nullptr;
};

// PickMgr (Scene+0x674): hit testing of widgets.
class PickMgr {
public:
    // CDrawView::IsPicked(x,y) -> the CDrawObj's id is the owning Widget*  (Widget::GetFromObj, ord 710).
    Widget* Pick(Scene* scene, int x, int y);
    // Aaron ord 330 0x10012283 (PickMgr::PickEnabled): Pick(x, y) but only a widget that is not disabled (+0x2c == 0); otherwise null.
    Widget* PickEnabled(Scene* scene, int x, int y);
    // ---- the pick-feedback state of the original (0x10012167..0x1001236b) ----------------------------------------------------------------------
    // Aaron ord 136 0x10012167 Hold(): ++hold; the first hold with the picker enabled subscribes it to the CursorMgr. Aaron ord 570 0x10012403
    // Release(): --hold; at 0 unsubscribes. Both finish with RefreshPointer (ord 560 0x10012355 -> ord 208).
    void Hold();
    void Release();
    // Aaron ord 181 0x100121ec SetEnabled(flag): flag stored; with holds pending (un)subscribes; disabling also clears the hover widget.
    void SetEnabled(bool on);
    bool Enabled() const { return enabled_; }
    int HoldCount() const { return hold_; }
    // Aaron ord 560 0x10012355: RefreshPointer.
    static void RefreshPointer();
    // Aaron ord 609 0x1001236b SetHover(w): the widget under the pointer; the old one gets SetPressedFlag(false), the new one SetPressedFlag(true).
    void SetHover(Widget* w);
    Widget* Hover() const { return hover_; }
    // Aaron ord 152 0x100121ca: a widget is being destroyed -> SetHover(null) if it was the hover widget.
    void OnWidgetDestroyed(Widget* w) { if (hover_ == w) hover_ = nullptr; }

private:
    int hold_ = 0;          // +0x3c
    bool enabled_ = true;   // +0x40
    bool subscribed_ = false;
    Widget* hover_ = nullptr; // +0x34
};

// ---------------------------------------------------------------------------------------------
// AudioMgr (Application+0x3e8): wrapper of CAudioLib + user volume keys.
class AudioMgr {
public:
    AudioMgr();
    moses::CAudioLib& Lib() { return lib_; }
    // Aaron ord 584 0x10001e45 / ord 586 0x10001ec0: registry values `WaveVol` / `MidiVol` (0..100).
    void LoadVolumes(RegKey* machineKey);
    void SaveVolumes(RegKey* machineKey);
    // AudioMgr::OnMessage: '*' (AVK_MULTIPLY) toggles mute, '+' / '-' volume +-10, on KEY UP; Shift excludes wave, Ctrl excludes MIDI.
    bool OnKey(u32 vk, u32 mods);
    int WaveVolume() const { return waveVol_; }
    int MidiVolume() const { return midiVol_; }
    // Aaron ord 533 0x10011dea: CAudioLib::SoundPlay(snd, info) (0 without a library). ord 574 0x10011e25: CAudioLib::Reset().
    int SoundPlay(moses::CAudioSnd* snd, moses::PLAYSOUNDINFO* info);
    void Reset();
    // Aaron ord 654 0x10011f23: SetPanInfo(x, w) with w = `width`, or half the client width of the application window when 0.
    void SetPan(int x, u32 width);
    // Aaron ord 692 0x10011f6d: CAudioLib::SoundStop(snd). ord 695 0x10011f93: StopAllWaves / StopMidi / CdStop for the nonzero arguments.
    void SoundStop(moses::CAudioSnd* snd);
    void StopAll(int waves, int midi, int cd);

private:
    moses::CAudioLib lib_;
    int waveVol_ = 100, midiVol_ = 100;
};

// ---------------------------------------------------------------------------------------------
// JoystickMgr: joystick enumeration (MM_JOY* in the original).
class JoystickMgr {
public:
    // Aaron ord 314 0x10002941 JoystickMgr::CountDevices: joyGetNumDevs + joyGetPos. Port: 0 until a joystick
    // backend exists (platform Window has no joystick events). TODO(package 3): SDL_NumJoysticks().
    static int CountDevices();
};

} // namespace aaron
