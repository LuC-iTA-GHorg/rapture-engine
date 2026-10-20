// MsgQueue, TimerMgr, IdleTimer, AccelTable, FocusMgr, CursorMgr, PickMgr, AudioMgr, JoystickMgr. Package 3.
#include "aaron/Managers.h"

#include "aaron/Application.h"
#include "aaron/Cursors.h"
#include "aaron/RegKey.h"
#include "aaron/Scene.h"
#include "aaron/Widget.h"
#include "platform/Joystick.h"
#include "moses/AnimView.h"

#include <algorithm>

namespace aaron {

// ---------------------------------------------------------------------------------------------
// MsgQueue (Aaron 0x1000abe0 / 0x1000aedc / 0x1000adbf)
bool MsgQueue::Post(u32 msg, uintptr_t wParam, intptr_t lParam) { // 0x1000aedc: ignored when full (count >= capacity)
    if (int(q_.size()) >= kCapacity) return false;
    q_.push_back(QueuedMsg{msg, wParam, lParam});
    return true;
}

void MsgQueue::Dispatch(Scene* scene) { // 0x1000adbf: swap buffers, then call scene->OnMessage for every entry
    if (q_.empty() || !cur_.empty()) return; // (a nested Dispatch from inside a handler does nothing)
    cur_.swap(q_); // messages posted by the handlers go to the (new, empty) buffer: next Dispatch
    while (!cur_.empty()) {
        QueuedMsg m = cur_.front();
        cur_.pop_front();
        scene->OnMessage(m.msg, m.wParam, m.lParam);
    }
}

void MsgQueue::PurgeWidget(const Widget* w) {
    auto pred = [w](const QueuedMsg& m) { return m.msg == AMSG_NOTIFY && reinterpret_cast<const Widget*>(m.lParam) == w; };
    q_.erase(std::remove_if(q_.begin(), q_.end(), pred), q_.end());
    cur_.erase(std::remove_if(cur_.begin(), cur_.end(), pred), cur_.end());
}

// ---------------------------------------------------------------------------------------------
// TimerMgr (Aaron 0x10013b93 / Tick 0x10013c73 / pause counter 0x10013b12)
void TimerMgr::Tick(u32 nowMs) {
    if (ListenerCount() == 0) { // original: the last RemoveListener (0x10013d3e) zeroes the total; Tick is skipped (0x1000de81)
        started_ = false;
        total_ = 0;
        return;
    }
    if (!started_) { // the original resets the reference time when the first listener is added (0x10013ccc)
        started_ = true;
        last_ = nowMs;
        return;
    }
    if (nowMs != last_) {
        delta_ = nowMs - last_;
        total_ += delta_;
        last_ = nowMs;
        Broadcast(AMSG_TICK, intptr_t(total_), this);
    }
}

int TimerMgr::PauseCounter(bool on) {
    if (!on) {
        if (pauseCount_ != 0 && --pauseCount_ == 0) Broadcast(AMSG_PAUSE, 0, this);
    } else {
        int before = pauseCount_++;
        if (before == 0) Broadcast(AMSG_PAUSE, 1, this);
    }
    return pauseCount_;
}

// ---------------------------------------------------------------------------------------------
// IdleTimer (Scene+0x678). Reset = ord 581 0x10002561.
void IdleTimer::Reset() { last_ = moses::CAnimView::Now(); count_ = 0; }

void IdleTimer::SetTimeout(u32 ms) { // ord 642 0x100125b1
    timeout_ = ms;
    queryMode_ = true;
    enabled_ = ms != 0;
    if (ms) Reset();
}

void IdleTimer::Start(u32 timeoutMs, u32 command) {
    timeout_ = timeoutMs;
    command_ = command;
    last_ = moses::CAnimView::Now();
    enabled_ = timeoutMs != 0;
    queryMode_ = false;
}

void IdleTimer::Tick(Scene* scene, u32 nowMs) {
    if (!enabled_ || !scene) return;
    if (nowMs - last_ >= timeout_) {
        last_ = nowMs;
        if (queryMode_) { // 0x100124cb: ++count; if Scene::OnQuery(count) == 0 the timer stops
            ++count_;
            if (scene->OnQuery(count_) == 0) enabled_ = false;
        } else {
            scene->PostMessage(WMSG_COMMAND, command_, 0);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// AccelTable (Aaron FUN_10007bde, Add FUN_10007fe0)
void AccelTable::Add(u32 vk, u32 cmd, u32 mod, u32 param) {
    for (Entry& e : entries_) {
        if (e.vk == vk && e.mod == mod) { e.cmd = cmd; e.param = param; return; }
    }
    entries_.push_back(Entry{vk, mod, cmd, param});
}

void AccelTable::Remove(u32 vk, u32 mod) {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].vk == vk && entries_[i].mod == mod) { entries_.erase(entries_.begin() + long(i)); return; }
    }
}

void AccelTable::Push(bool passThrough) { // ord 556 0x1000de5a (FUN_10007e9f)
    Level l;
    l.entries = std::move(entries_);
    l.pass = pass_;
    stack_.push_back(std::move(l));
    entries_.clear();
    pass_ = passThrough;
}

void AccelTable::Pop() { // ord 537 0x1000dde7 (FUN_10007f58)
    if (stack_.empty()) return;
    entries_ = std::move(stack_.back().entries);
    pass_ = stack_.back().pass;
    stack_.pop_back();
}

int AccelTable::ForEach(ForEachFn fn, void* a, void* b) const { // ord 188 0x10007d4f
    int r = 1;
    for (size_t i = 0; r != 0 && i < entries_.size(); ++i) r = fn(&entries_[i], a, b);
    return r;
}

bool AccelTable::match(const std::vector<Entry>& v, Scene* scene, u32 vk, u32 mod) const {
    for (const Entry& e : v) {
        if (e.vk == vk && e.mod == mod) {
            scene->PostMessage(WMSG_COMMAND, e.cmd, e.param);
            return true;
        }
    }
    return false;
}

bool AccelTable::Translate(Scene* scene, u32 vk, u32 mod) {
    if (match(entries_, scene, vk, mod)) return true;
    bool forward = pass_; // 0x10007daa: a key unknown to a pass-through table goes to the table below
    for (int i = int(stack_.size()) - 1; forward && i >= 0; --i) {
        if (match(stack_[size_t(i)].entries, scene, vk, mod)) return true;
        forward = stack_[size_t(i)].pass;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// FocusMgr (Aaron 0x10002d3d Lock; FUN_1001303e)
int FocusMgr::Lock(int unlock) {
    // Original: Lock(0) locks (counter++), Lock(!=0) unlocks (counter-- if > 0). Returns the counter.
    if (unlock == 0) ++lock_;
    else if (lock_ > 0) --lock_;
    return lock_;
}

void FocusMgr::AddKeyListener(SceneListener* l) {
    if (l && std::find(keyListeners_.begin(), keyListeners_.end(), l) == keyListeners_.end()) keyListeners_.push_back(l);
}
void FocusMgr::RemoveKeyListener(SceneListener* l) {
    keyListeners_.erase(std::remove(keyListeners_.begin(), keyListeners_.end(), l), keyListeners_.end());
}
void FocusMgr::AddSoundListener(SceneListener* l) {
    if (l && std::find(soundListeners_.begin(), soundListeners_.end(), l) == soundListeners_.end()) soundListeners_.push_back(l);
}
void FocusMgr::RemoveSoundListener(SceneListener* l) {
    soundListeners_.erase(std::remove(soundListeners_.begin(), soundListeners_.end(), l), soundListeners_.end());
}

bool FocusMgr::OnKey(Scene* scene, u32 msg, u32 vk, u32 mods) {
    (void)scene;
    if (msg == WMSG_KEYDOWN && vk == AVK_PAUSE) { // Aaron 0x10013aaf: Pause key toggles the application pause counter
        Application* app = Application::GetApp();
        if (app) app->PauseApp(!app->Timers().Paused());
        return true;
    }
    // (Ctrl+Shift+Alt+V = About box: not ported)
    if (Locked()) return true; // input locked (Shoot transitions): keys are swallowed
    // listeners first (copy: a listener may unregister itself)
    std::vector<SceneListener*> snap = keyListeners_;
    for (SceneListener* l : snap) l->OnInput(msg, vk);
    if (focus_) return focus_->OnKey(msg, vk, mods) != 0;
    return false;
}

// FocusMgr, WP-4 part (Aaron 0x10012ad4..0x1001303e)
void FocusMgr::SetFocusState(Widget* w, bool on) { // ord 200 0x10012d7f
    if (!on) {
        if (w) {
            focus_ = nullptr;
            w->OnFocus(false);
        }
    } else if (w) {
        pref_ = w;
        focus_ = w;
        w->OnFocus(true);
    }
}

void FocusMgr::SetFocusWidget(Widget* w) { // ord 633 0x10012fae
    if (!w) {
        pref_ = nullptr;
        SetFocusState(focus_, false); // SetFocus(NULL): the window loses the focus -> WM_KILLFOCUS
    } else if (w != focus_ && w->DisableCount() == 0) {
        SetFocusState(focus_, false);
        SetFocusState(w, true);
    }
}

void FocusMgr::SetFocusToWindow(Scene* scene) { // ord 636 0x1001303e
    (void)scene;
    pref_ = nullptr;
    SetFocusState(focus_, false);
}

Widget* FocusMgr::SetModal(Widget* w) { // ord 610 0x10012f5a
    Widget* old = modal_;
    if (w != modal_) {
        if (!w) modal_ = nullptr;
        else if (w->DisableCount() == 0) modal_ = w;
    }
    return old;
}

void FocusMgr::OnWidgetDestroyed(Widget* w, Scene* scene) { // ord 153 0x10012ce2
    if (w == modal_) ClearModal();
    if (pref_ == w) {
        pref_ = nullptr;
        if (focus_ == w) SetFocusToWindow(scene);
    }
    if (focus_ == w) focus_ = nullptr; // (port: never leave a dangling pointer)
}

bool FocusMgr::OnMessage(Scene* scene, u32 msg, uintptr_t wParam, intptr_t lParam) { // ord 499 0x10012dd0
    if (msg == 7) { // WM_SETFOCUS
        if (!pref_) SetFocusState(focus_, false);
        else SetFocusWidget(pref_);
        return false;
    }
    if (msg == 8) { // WM_KILLFOCUS
        SetFocusState(focus_, false);
        return false;
    }
    Application* app = Application::GetApp();
    if (msg == WMSG_KEYUP && wParam == 0x56 && app) { // Ctrl+Shift+Alt+V
        u32 mods = app->KeyMods();
        if ((mods & AMOD_CTRL) && (mods & AMOD_SHIFT) && (mods & AMOD_ALT)) {
            app->PostMessage(AMSG_ABOUT, 0, 0);
            return false;
        }
    }
    if ((!paused_ || wParam == 0x13) && lock_ == 0) {
        if (modal_ && modal_->OnKey(msg, u32(wParam), u32(lParam)) != 0) return true;
        if (focus_ && focus_->OnKey(msg, u32(wParam), u32(lParam)) != 0) return true;
        std::vector<SceneListener*> snap = keyListeners_;
        for (SceneListener* l : snap) l->OnInput(msg, wParam);
    }
    (void)scene;
    return false;
}

// ---------------------------------------------------------------------------------------------
// CursorMgr (Aaron 0x100130a7). Cursors are placeholders (README sec. 9 #9): only show/hide.
// WP-4: the cursor objects (aaron/Cursors.h) and the five global cursor sets are real; the OS cursor is not changed.
static Cursor* g_sets[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
static u32 g_userFlag = 0; // DAT_10023dec

Cursor* CursorMgr::GlobalSet(int n) { // FUN_100133c1 builds them with (hInst, resId, id): hidden, arrow 0x7f00, wait 0x7f02, no 0x7f88, own 0x7f
    if (n < 0 || n >= 5) return nullptr;
    if (!g_sets[0]) {
        g_sets[0] = new ResCursor(nullptr, nullptr, 0, 0);
        g_sets[1] = new ResCursor(nullptr, nullptr, 0x7f00, 1);
        g_sets[2] = new ResCursor(nullptr, nullptr, 0x7f02, 0);
        g_sets[3] = new ResCursor(nullptr, nullptr, 0x7f88, 0);
        g_sets[4] = new ResCursor(nullptr, reinterpret_cast<void*>(1), 0x7f, 1); // (hInstance != 0: the DLL's own cursor resource)
    }
    return g_sets[n];
}

CursorMgr::CursorMgr() { current_ = GlobalSet(1); } // 0x100130a7: +0x44 = DAT_10023de4 (the arrow set)

CursorMgr::~CursorMgr() { DeleteCursors(); }

void CursorMgr::AddCursor(Cursor* c) {
    if (c && std::find(cursors_.begin(), cursors_.end(), c) == cursors_.end()) cursors_.push_back(c);
}

void CursorMgr::RemoveCursor(Cursor* c) {
    if (deleting_) return;
    cursors_.erase(std::remove(cursors_.begin(), cursors_.end(), c), cursors_.end());
    if (current_ == c) current_ = nullptr;
}

void CursorMgr::DeleteCursors() {
    std::vector<Cursor*> v;
    v.swap(cursors_);
    deleting_ = true;
    for (Cursor* c : v) {
        if (current_ == c) current_ = nullptr;
        delete c;
    }
    deleting_ = false;
}

void CursorMgr::SetNumber(int n) { // ord 616 0x100137e7
    if (n != number_) number_ = n;
}

Cursor* CursorMgr::SetCursorObject(Cursor* c) { // ord 614 0x1001373f, pointer form
    Cursor* old = current_;
    if (c && c != current_) {
        if (current_) current_->Activate(false);
        current_ = c;
        c->Activate(true);
    }
    return old;
}

int CursorMgr::SetCursor(int n) { // ord 614 0x1001373f; returns the previous shape number (the original returns the previous cursor object)
    int old = shape_;
    shape_ = n;
    if (n >= 0 && n < 5) SetCursorObject(GlobalSet(n));
    return old;
}

Widget* CursorMgr::SetCapture(Widget* w) { // ord 611 0x100136cc
    Widget* old = capture_;
    if (w != capture_) {
        if (!w) ReleaseCapture();
        else if (w->DisableCount() == 0) {
            capture_ = w;
            w->ToggleCaptured(true); // FUN_10004a0d(w, 1)
        }
    }
    return old;
}

void CursorMgr::ReleaseCapture() { // ord 564 0x100136b2 -> Win32 ReleaseCapture -> WM_CAPTURECHANGED
    if (capture_) OnMessage(nullptr, 0x215, 0, 0);
}

void CursorMgr::OnWidgetDestroyed(Widget* w) { // ord 154 0x10013353
    if (w == capture_) ReleaseCapture();
    if (hover_ == w) hover_ = nullptr;
}

bool CursorMgr::OnMessage(Scene* scene, u32 msg, uintptr_t wParam, intptr_t lParam) { // ord 500 0x100135c0
    if (msg == 0x20) { // WM_SETCURSOR: the pointer becomes visible again (FUN_100132f7(this, 1))
        return false;
    }
    if (msg == 0x215) { // WM_CAPTURECHANGED
        if (capture_) capture_->ToggleCaptured(false); // FUN_10004a0d(capture, 0)
        capture_ = nullptr;
        return false;
    }
    lastX_ = int(int16_t(uint32_t(lParam) & 0xffff));
    lastY_ = int(int16_t((uint32_t(lParam) >> 16) & 0xffff));
    if (!scene) return false;
    return OnMouse(scene, msg, lastX_, lastY_, u32(wParam));
}

void CursorMgr::SetUserFlag(u32 v) { g_userFlag = v; }   // ord 639 0x10013869
u32 CursorMgr::GetUserFlag() { return g_userFlag; }      // ord 286 0x100133b7

int CursorMgr::ShowCursor(int show) { // ord 685 0x10003876 (Win32 counter semantics)
    showCount_ += show ? 1 : -1;
    Application* app = Application::GetApp();
    if (app && app->Platform().IsOpen()) app->Platform().ShowCursor(showCount_ >= 0);
    return showCount_;
}

void CursorMgr::SetPos(int x, int y) { (void)x; (void)y; } // ord 618 0x10013812: SetCursorPos(ToScreen(x, y)) - no-op in the port

static intptr_t pack_xy(int x, int y) { // Win32 MAKELPARAM: low word x, high word y (receivers sign-extend)
    return intptr_t(uint32_t(uint16_t(x)) | (uint32_t(uint16_t(y)) << 16));
}

bool CursorMgr::OnMouse(Scene* scene, u32 msg, int x, int y, u32 keys) { // ord 500
    intptr_t lp = pack_xy(x, y);
    if (capture_) { // SetCapture: the capturing widget gets everything
        capture_->OnMouse(msg, keys, lp);
        return true;
    }
    Widget* w = scene->Picker().Pick(scene, x, y);
    if (hover_ && hover_ != w) { // port addition: leave notification (WM_MOUSELEAVE = 0x2A3) for the widget being left
        Widget* old = hover_;
        hover_ = nullptr;
        old->OnMouse(0x02A3, keys, lp);
    }
    hover_ = w;
    if (!w) return false;
    w->OnMouse(msg, keys, lp);
    return true;
}

// PickMgr: CDrawView::IsPicked from the head (highest level) -> the CDrawObj's id is the Widget*.
Widget* PickMgr::Pick(Scene* scene, int x, int y) {
    moses::CDrawObj* o = scene->IsPicked(x, y);
    return Widget::GetFromObj(o);
}

Widget* PickMgr::PickEnabled(Scene* scene, int x, int y) { // ord 330 0x10012283
    Widget* w = Pick(scene, x, y);
    return (w && w->DisableCount() == 0) ? w : nullptr;
}

void PickMgr::Hold() { // ord 136 0x10012167
    if (hold_ == 0 && enabled_) subscribed_ = true;
    ++hold_;
    RefreshPointer();
}

void PickMgr::Release() { // ord 570 0x10012403
    --hold_;
    if (hold_ == 0 && enabled_) subscribed_ = false;
    RefreshPointer();
}

void PickMgr::SetEnabled(bool on) { // ord 181 0x100121ec
    enabled_ = on;
    if (hold_ != 0) {
        if (!on) {
            subscribed_ = false;
            SetHover(nullptr);
        } else {
            subscribed_ = true;
            RefreshPointer();
        }
    }
}

void PickMgr::RefreshPointer() { CursorMgr::RefreshPointer(); } // ord 560 0x10012355 -> ord 208

void PickMgr::SetHover(Widget* w) { // ord 609 0x1001236b
    if (w == hover_) return;
    Widget* old = hover_;
    hover_ = w;
    if (old) old->SetPressedFlag(false);
    if (hover_) hover_->SetPressedFlag(true);
}

// ---------------------------------------------------------------------------------------------
// AudioMgr (Application+0x3e8)
AudioMgr::AudioMgr() {}

void AudioMgr::LoadVolumes(RegKey* machineKey) { // ord 584 0x10001e45
    if (!machineKey) return;
    waveVol_ = int(std::min<u32>(machineKey->GetDword("WaveVol", 100), 100));
    midiVol_ = int(std::min<u32>(machineKey->GetDword("MidiVol", 100), 100));
    lib_.volume(moses::MIX_WAVE, 1, waveVol_);
    lib_.volume(moses::MIX_MIDI, 1, midiVol_);
}

void AudioMgr::SaveVolumes(RegKey* machineKey) { // ord 586 0x10001ec0
    if (!machineKey) return;
    machineKey->SetDword("WaveVol", u32(waveVol_));
    machineKey->SetDword("MidiVol", u32(midiVol_));
}

// AudioMgr::OnMessage 0x10011c33, message WM_KEYUP (0x101): numpad '*' toggles the mute, '+' / '-' change the volume by 10.
// GetKeyState(VK_SHIFT) pressed EXCLUDES the wave mixer (type 1), VK_CONTROL pressed EXCLUDES the MIDI mixer (type 2); with no
// modifier both are changed.  The original only observes the key: it is never consumed.
bool AudioMgr::OnKey(u32 vk, u32 mods) {
    bool shift = (mods & AMOD_SHIFT) != 0, ctrl = (mods & AMOD_CTRL) != 0;
    bool doWave = !shift, doMidi = !ctrl;
    if (vk == AVK_MULTIPLY) {
        if (doWave) lib_.mute(moses::MIX_WAVE, 1, lib_.mute(moses::MIX_WAVE, 0, 0) ? 0 : 1);
        if (doMidi) lib_.mute(moses::MIX_MIDI, 1, lib_.mute(moses::MIX_MIDI, 0, 0) ? 0 : 1);
    } else if (vk == AVK_ADD || vk == AVK_SUBTRACT) {
        int d = vk == AVK_ADD ? 10 : -10;
        if (doWave) waveVol_ = lib_.volume(moses::MIX_WAVE, 1, lib_.volume(moses::MIX_WAVE, 0, 0) + d);
        if (doMidi) midiVol_ = lib_.volume(moses::MIX_MIDI, 1, lib_.volume(moses::MIX_MIDI, 0, 0) + d);
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
int JoystickMgr::CountDevices() { return pjoy::NumDevices(); } // ord 314 0x10012941 (joyGetNumDevs + joyGetPos; SDL2 through platform/Joystick.h)

// AudioMgr, WP-4 part (Aaron 0x10011dea..0x10011f93)
int AudioMgr::SoundPlay(moses::CAudioSnd* snd, moses::PLAYSOUNDINFO* info) { return lib_.SoundPlay(snd, info); } // ord 533 0x10011dea
void AudioMgr::Reset() { lib_.Reset(); }                                                                       // ord 574 0x10011e25
void AudioMgr::SetPan(int x, u32 width) {                                                                      // ord 654 0x10011f23
    if (width == 0) {
        Application* app = Application::GetApp();
        width = app ? u32(app->GetClientWidth()) >> 1 : 320u;
    }
    lib_.SetPanInfo(x, int(width));
}
void AudioMgr::SoundStop(moses::CAudioSnd* snd) { lib_.SoundStop(snd); }                                       // ord 692 0x10011f6d
void AudioMgr::StopAll(int waves, int midi, int cd) {                                                          // ord 695 0x10011f93
    if (waves) lib_.StopAllWaves();
    if (midi) lib_.StopMidi();
    if (cd) lib_.CdStop();
}

} // namespace aaron
