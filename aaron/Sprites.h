// Concrete widgets used by Shoot.exe / Shoot.dll (aaron.md sec. 2.7): Button, Dialog, DibSprite,
// AnimSprite, RectSprite, AnimRectSprite, AnimTextSprite, Timer (+ Poly/Text sprites for completeness).
//
// Each sprite = Widget + the moses drawable (multiple inheritance, as the original: AnimSprite has
// CAnimObj at +0x40 and CDrawDib at +0xdc). Consequences for game code:
//   * `Show(bool)` is the Widget one; the moses `Show()` is reached as `CDrawObj::Show()`.
//   * `GetId()` is the moses one (CDrawObj/CAnimObj id = the Widget* back-pointer, see
//     Widget::GetFromObj); the widget's command id is `GetCmdId()`.
// Constructor argument lists come from the call sites in Shoot.* (the first arg is `parent|scene`);
// positions/levels are in centred scene coordinates, `level` is the z-order (0x26fc UI, 0x7fffffff top).
#pragma once

#include "aaron/Widget.h"
#include "moses/AnimObjs.h"

namespace aaron {

// WP-4: THE PER-CLASS VIRTUAL THUNKS OF THE ORIGINAL. Aaron.dll exports one address per (class, slot) pair, so many ordinals share an address; each
// thunk adjusts `this` to the embedded moses object (CDrawObj at +0x40 for DibSprite / Rect / Poly / Text sprites; CAnimObj at +0x40 and CDrawObj
// at +0xdc for AnimSprite / AnimRect / AnimPoly / AnimText) and calls the real function. The port gets all of them from C++ inheritance:
//   slot 12 GetAnimObj  (+0x40)   Aaron ord 211,212,213,214 0x100068d0     slot 13 GetDrawObj (+0x40)  Aaron ord 260,261,262,263,264 0x100068d0
//   slot 13 GetDrawObj  (+0xdc)   Aaron ord 265 0x100067f0 (AnimSprite);  ord 266,267,268 0x10006900 (AnimPoly / AnimRect / AnimText sprite)
//   slot 6  Create      (+0x40)   Aaron ord 478,479,480,481 0x10006a10: Widget::Create then CDrawObj::Init   (Dib / Poly / Rect / Text sprite)
//   slot 6  Create      (+0xdc)   Aaron ord 482 0x10006820 (AnimSprite);  ord 483,484,485 0x10006980 (AnimPoly / AnimRect / AnimText sprite)
//   slot 7  hide change (+0x40)   Aaron ord 511 0x10006860 (AnimSprite);  ord 512,513,514 0x10006930: CAnimObj::Pause(hidden) - see OnHideChange
//   slot 9  Show                  Aaron ord 675,676,677,678 0x10006a50 = Widget::Show (Dib / Poly / Rect / Text);
//                                 ord 679,680,681,682 0x100068b0 = AnimSprite::Show (ord 142) (AnimSprite / AnimPoly / AnimRect / AnimText)
//   slot 24 HitTest             Aaron ord 546 0x10003cd0 (DibSprite);  ord 547,548,549 0x100069d0 (+0x40: Poly / Rect / Text sprite);
//                                 ord 550 0x10006880 (AnimSprite, +0xdc);  ord 551,552,553 0x10006950 (AnimPoly / AnimRect / AnimText sprite, +0xdc)
//   slot 1  GetClassId          Aaron ord 370,371,373,374,375,377 0x100069c0 (0x20103: Dib / Anim / AnimPoly ... sprites);
//                                 ord 372,376 0x10006a00 (0x20106: the GDI sprites Rect / Text / Poly ...)
// The empty per-class slot-3 virtual (Widget::OnReleaseHook) is exported once per class: see Nop226 (DibSprite), Nop227 (PolySprite), Nop228 (RectSprite),
// Nop229 (TextSprite), Nop230 (AnimSprite), Nop231 (AnimPolySprite), Nop232 (AnimRectSprite), Nop233 (AnimTextSprite) below.

// ---------------------------------------------------------------------------------------------
// DibSprite: static picture. `frame` selects the frame of a group resource (class 0x8b).
class DibSprite : public Widget, public moses::CDrawDib {
public:
    // Aaron ord 16 0x100055a4: DibSprite(parent, resId, x, y, level, cmdId, style)
    DibSprite(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style);
    // Aaron ord 17 0x10005647: same + initial frame (Shoot: (parent, res, 0, 0, 0x26fc, 0, 0, 0))
    DibSprite(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style, u32 frame);
    ~DibSprite() override;                       // Aaron ord 109 / delete: via ~Widget

    u32 GetClassId() const override { return CID_SPRITE; }
    bool Create() override;
    void Show(bool show) override;               // Widget::Show: Hide/Show the CDrawDib
    moses::CDrawObj* GetDrawObj() override { return static_cast<moses::CDrawDib*>(this); }
    void ReleaseRes() override;
    bool LoadRes() override;
    bool HitTest(int x, int y) override;         // pixel-perfect via CDrawObj::PtInObject
    void SetFrame(u32 frame);                    // TODO: not an Aaron export; helper = CDrawDib::Animate(GetIndexedResId(res, frame))
    u32 Frame() const { return frame_; }
    // Aaron ord 143 0x10005702 (DibSprite::AnimateList, cdecl varargs: this, count, id0, id1, ...): copies the `count` resource ids
    // and calls CDrawDib::Animate(ids, count, nullptr) on the CDrawDib part (+0x40). (WP-4: the +0xdc twin, ord 144 0x10004ed2, belongs to
    // AnimSprite - see AnimSprite::AnimateList; WP-2 had attributed ord 144 to this class.)
    void AnimateList(u32 count, ...);
    // Aaron ord 226 0x10005791 (DibSprite::Nop226): empty function.
    static void Nop226() {}
    // Aaron ord 546 0x10003cd0 (slot 24, DibSprite::HitTest): see the thunk map above.

protected:
    u32 frame_ = 0;
};

// ---------------------------------------------------------------------------------------------
// AnimSprite: animated picture driven by a QUEUEMSG queue. Widget + CAnimDib.
class AnimSprite : public Widget, public moses::CAnimDib {
public:
    // Aaron ord 22 0x10004c7c: AnimSprite(parent, resId, x, y, level, cmdId, style)   (RESOLVED from the decompiled ctor:
    //   the two trailing parameters are cmdId and style; style bit 1 = created hidden. Shoot's (..., level, 0, 1) = cmd 0, hidden).
    AnimSprite(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style);
    // Aaron ord 23 0x10004e07: + initial frame (Animate(frame) if frame != -1)
    AnimSprite(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style, u32 frame);
    // Aaron ord 24 0x10004d2c: + starts a queue (QStart(msgs, count)) unless style & 1 (hidden)
    AnimSprite(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style,
               moses::QUEUEMSG** msgs, u32 count);
    ~AnimSprite() override;

    u32 GetClassId() const override { return CID_SPRITE; }
    bool Create() override;
    void Show(bool show) override;               // Aaron ord 142 0x100045c3 (AnimSprite::Show) - same as Widget::Show (ord 178)
    moses::CDrawObj* GetDrawObj() override { return static_cast<moses::CDrawDib*>(this); }
    moses::CAnimObj* GetAnimObj() override { return static_cast<moses::CAnimDib*>(this); } // slot 12 (Aaron ord 211-214 0x100068d0)
    // Aaron ord 511 0x10006860 (slot 7): hiding a sprite (HideCount 0 -> 1) pauses its animation queue, showing it again resumes it.
    void OnHideChange(bool hidden) override { moses::CAnimObj::Pause(hidden ? 1 : 0); }
    // Aaron ord 144 0x10004ed2 (AnimSprite::AnimateList): the same as DibSprite's, on the CDrawDib at +0xdc.
    void AnimateList(u32 count, ...);
    // Aaron ord 353 0x10004f71 / ord 664 0x10004f94: the two timing fields of the CAnimObj (+0x6c = speed, +0x5c = motion delay), read / written
    // directly (no SetSpeed / SetMotion logic).
    void GetTiming(u32* speed, u32* motion) const { *speed = speed_; *motion = motion_delay_; }
    void SetTiming(u32 speed, u32 motion) { speed_ = speed; motion_delay_ = motion; }
    void ReleaseRes() override;
    bool LoadRes() override;
    bool HitTest(int x, int y) override;
    // Aaron ord 696 0x10004fb3 AnimSprite::Play(resId, msgs, count, stopFirst): if `resId` differs from the loaded one
    // does ReleaseRes/LoadRes (new resource), then CAnimView::QStart(this, msgs, count). stopFirst != 0 stops the
    // running queue first. Shoot: Play(piVar[4], piVar[5], piVar[6], 1).
    void Play(u32 resId, moses::QUEUEMSG** msgs, u32 count, int stopFirst);
    // Aaron ord 230 0x10004f64 (AnimSprite::Nop230): see Widget::OnReleaseHook.

    // CAnimDib/CDrawObj ambiguity helpers
    uintptr_t GetId() const { return moses::CDrawObj::GetId(); }
    void SetId(uintptr_t id) { moses::CDrawObj::SetId(id); }
};

// ---------------------------------------------------------------------------------------------
// RectSprite: rectangle (CDrawRect). Also the base of Shoot's CListHit (hit-test grid, Shoot.exe 0x408746).
class RectSprite : public Widget, public moses::CDrawRect {
public:
    // Aaron ord 20 0x100059e5: RectSprite(parent, x, y, level, cmdId, style, const RECT* rc, ellipse)   (RESOLVED: resId is -1,
    //   the two ints after `parent` are x, y).  Shoot: (parent, 0, 0, 0x2706, 0, 0, &rc, 0) and (scene, 0, 0, 0x26fc, id, 0x71, &rc, 0).
    RectSprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, const RECT* rc, int ellipse);
    ~RectSprite() override;

    u32 GetClassId() const override { return CID_GDISPRITE; }
    bool Create() override;
    void Show(bool show) override;
    moses::CDrawObj* GetDrawObj() override { return static_cast<moses::CDrawRect*>(this); }
    bool HitTest(int x, int y) override;
    // Aaron ord 228 0x10005ade (RectSprite::Nop228): see Widget::OnReleaseHook.
};

// PolySprite: polyline / polygon (CDrawPoly at +0x40; class id 0x20106).
class PolySprite : public Widget, public moses::CDrawPoly {
public:
    // Aaron ord 18 0x100057c0: a line from p0 to p1 (resId -1, level, cmdId, style).
    PolySprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, POINT* p0, POINT* p1);
    // Aaron ord 19 0x100058b9: `n` points, closed (polygon) or open (polyline).
    PolySprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, POINT* pts, u32 n, int closed);
    ~PolySprite() override;
    u32 GetClassId() const override { return CID_GDISPRITE; }
    bool Create() override;
    void Show(bool show) override;
    moses::CDrawObj* GetDrawObj() override { return static_cast<moses::CDrawPoly*>(this); }
    bool HitTest(int x, int y) override;
    static void Nop227() {}  // Aaron ord 227 0x100059b6
};

// AnimPolySprite: animated polyline (CAnimPoly: CAnimObj at +0x40, CDrawPoly at +0xdc).
class AnimPolySprite : public Widget, public moses::CAnimPoly {
public:
    // Aaron ord 25 0x10005055 (line) / ord 26 0x10005167 (points)
    AnimPolySprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, POINT* p0, POINT* p1);
    AnimPolySprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, POINT* pts, u32 n, int closed);
    ~AnimPolySprite() override;
    u32 GetClassId() const override { return CID_SPRITE; }  // 0x20103 (ord 370-377 0x100069c0)
    bool Create() override;
    void Show(bool show) override;
    moses::CDrawObj* GetDrawObj() override { return static_cast<moses::CDrawPoly*>(this); }
    moses::CAnimObj* GetAnimObj() override { return static_cast<moses::CAnimObj*>(this); }
    void OnHideChange(bool hidden) override { moses::CAnimObj::Pause(hidden ? 1 : 0); } // ord 512-514 0x10006930
    bool HitTest(int x, int y) override;
    static void Nop231() {}  // Aaron ord 231 0x1000527d
};

// TextSprite: static text (CDrawText at +0x40; class id 0x20106). Aaron ord 21 0x10005b0d: same arguments as AnimTextSprite.
class TextSprite : public Widget, public moses::CDrawText {
public:
    TextSprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, const char* text, const char* fontFace, int fontHeight,
               moses::COLORREF color);
    ~TextSprite() override;
    u32 GetClassId() const override { return CID_GDISPRITE; }
    bool Create() override;
    void Show(bool show) override;
    moses::CDrawObj* GetDrawObj() override { return static_cast<moses::CDrawText*>(this); }
    bool HitTest(int x, int y) override;
    static void Nop229() {}  // Aaron ord 229 0x10005c71
};

// AnimRectSprite: iris effects (CAnimRect). Aaron ord 27 0x100052ac.
class AnimRectSprite : public Widget, public moses::CAnimRect {
public:
    // Shoot: (scene, 0, 0, 10000, 0, 0x2000, &rc, 0)
    AnimRectSprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, const RECT* rc, int ellipse);
    ~AnimRectSprite() override;
    u32 GetClassId() const override { return CID_GDISPRITE; }
    bool Create() override;
    void Show(bool show) override;
    moses::CDrawObj* GetDrawObj() override { return static_cast<moses::CDrawRect*>(this); }
    uintptr_t GetId() const { return moses::CDrawObj::GetId(); }
    void SetId(uintptr_t id) { moses::CDrawObj::SetId(id); }
    moses::CAnimObj* GetAnimObj() override { return static_cast<moses::CAnimObj*>(this); }
    void OnHideChange(bool hidden) override { moses::CAnimObj::Pause(hidden ? 1 : 0); } // Aaron ord 512-514 0x10006930
    static void Nop232() {}  // Aaron ord 232 0x100053be
};

// AnimTextSprite: animated rich text (credits). Aaron ord 28 0x100053ed.
class AnimTextSprite : public Widget, public moses::CAnimText {
public:
    // Shoot.exe: (parent, 0, 0, 0x7fffffff, 0, 1, 0, 0, 0, 0)
    // RESOLVED from 0x100053ed: (parent, x, y, level, cmdId, style, text, fontFace, fontHeight (0 -> 12), textColor).
    // Style 0x10000 = left aligned, 0x20000 = right, else centred.
    AnimTextSprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, const char* text,
                   const char* fontFace, int fontHeight, moses::COLORREF color);
    ~AnimTextSprite() override;
    u32 GetClassId() const override { return CID_GDISPRITE; }
    bool Create() override;
    void Show(bool show) override;
    moses::CDrawObj* GetDrawObj() override { return static_cast<moses::CDrawText*>(this); }
    uintptr_t GetId() const { return moses::CDrawObj::GetId(); }
    void SetId(uintptr_t id) { moses::CDrawObj::SetId(id); }
    moses::CAnimObj* GetAnimObj() override { return static_cast<moses::CAnimObj*>(this); }
    void OnHideChange(bool hidden) override { moses::CAnimObj::Pause(hidden ? 1 : 0); } // Aaron ord 512-514 0x10006930
    static void Nop233() {}  // Aaron ord 233 0x10005575
};

// PolySprite / AnimPolySprite / TextSprite (Aaron ord 18, 19, 21, 25, 26): not used by Shoot.*; declared above (WP-4).

// ---------------------------------------------------------------------------------------------
// Button: 3-frame picture button (up / over / down, resource class 0x8b) with radio groups and toggle.
class Button : public Widget, public moses::CDrawDib {
public:
    // Aaron ord 13 0x10003129: Button(parent, resId, x, y, level, cmdId, style, mode)
    //   `mode` = radio group: 0 normal, 1 default button, 3 checkbox, 9 radio group (difficulty/controls/players).
    //   style: 2 disabled, 8 notify parent, 0x208 default/selected  (shoot_exe.md; TODO confirm bits)
    Button(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style, int mode);
    ~Button() override;                          // Aaron delete: ord 108 0x10003118

    u32 GetClassId() const override { return CID_BUTTON; } // Aaron ord 368 0x10003570
    static void Nop223() {}  // Aaron ord 223 0x100032be (Button), ord 224 0x10002fab (the base Button-like class of vtable 0x1001d4e0)
    static void Nop224() {}
    bool Create() override;
    void Show(bool show) override;
    moses::CDrawObj* GetDrawObj() override { return static_cast<moses::CDrawDib*>(this); }  // Aaron ord 259 0x10003580 (CDrawObj at +0x44)
    bool HitTest(int x, int y) override;
    int OnMouse(u32 msg, u32 wParam, intptr_t lParam) override; // = Widget::OnMouse (the original Button does not override it)
    void SetPressedFlag(bool on) override;
    // Aaron 0x10002fb8 (ord 256): mode 0 -> 4, 1 -> 2, 4/9 -> 1; | 0x40.
    u32 GetPickFlags() override;
    // Aaron 0x10002f13 (slot 23): on release: mode 3 toggles, mode 9 selects this one of the tab-group ring; then Widget::PostNotify.
    void PostNotify(u32 code) override;
    // Aaron 0x100032cb (slot 18): Animate(frames[StateIndex()]) unless style & 1.
    void Draw() override;
    // Aaron 0x100032cb is ALSO the Button's slot 7 (Widget::NopVirtual2, called by HideCount on every hide/show transition): the button redraws its
    // current state frame.  (It is the only Widget class of Aaron whose slot 7 is not the nop; no class hides its sprites there.)
    void NopVirtual2() override { Draw(); }
    // Aaron 0x10002e38: index 0..7 into the 8 frames: 0 up, 1 down, 2 disabled, 3..5 same toggled, 6 over-highlight (style 0x1000), 7 over toggled.
    int StateIndex() const;
    // Aaron 0x100030d8 (ord 665): change mode (mode 1 registers as the default button of the enclosing Dialog) and redraw.
    void SetMode(int mode);
    int Mode() const { return mode_; }
    u32 Frame(int i) const { return frames_[i & 7]; }

    bool IsToggled() const;                      // Aaron ord 411 0x10003025 (Shoot: "IsChecked")
    void SetToggled(bool on);                    // Aaron ord 612 0x10003047 (Shoot: "SetCheck")

protected:
    int mode_ = 0;                               // +40
    u32 frames_[8] = {0, 0, 0, 0, 1, 3, 1, 4};   // +0x68..+0x84: resolved resource ids (initially the fallback table)
};

// ---------------------------------------------------------------------------------------------
// Dialog: modal popup panel drawn from one bitmap (all of Shoot's popups derive from it).
class Dialog : public DibSprite {
public:
    // Aaron ord 15 0x10003620: Dialog(parent, resId, x, y, level, cmdId, style, userArg, mode)
    //   Shoot: (parent, 0x8200e4, 0, 0, 0x26fc, id, 0x20000, arg, 3)
    //   TODO: `mode` 3 and the modal-policy style bits 0x40000..0x1000000/0x400000/0x600000.
    Dialog(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style, uintptr_t userArg, int mode);
    ~Dialog() override;                          // Aaron ord 68 0x1000368c

    u32 GetClassId() const override { return CID_DIALOG; } // Aaron ord 369 0x10003d00
    u32 GetPickFlags() override { return 0x80; }  // only Dialog returns 0x80 (Aaron ord 257 0x10003d10)
    bool Create() override;                      // Aaron ord 477 0x10003980: Widget::Create + modal setup + OnInit()
    int OnKey(u32 msg, u32 vk, u32 lParam) override; // Aaron ord 493 0x10003bcb: Enter activates the default button (+0x74), Esc closes
    // Aaron 0x10003c7b (FUN_10003c7b): set by Button mode 1.
    void SetDefaultButton(Widget* b) { defaultButton_ = b; }
    // Aaron 0x1000391f (FUN_1000391f): nearest (outermost == false) or outermost enclosing Dialog (GetPickFlags == 0x80).
    Dialog* FindDialogAncestor(bool outermost);
    uintptr_t UserArg() const { return userArg_; }
    // Aaron ord 187 0x100037d3 Dialog::Close(result, notify): hides/deletes the popup (deferred), restores input
    // and posts the result to the scene. Shoot: Close(0,0) / Close(1,1).
    void Close(int result, int notify);
    // Aaron ord 225 0x10003973 (Dialog::Nop225): see Widget::OnReleaseHook.

    // Dialog-specific virtual slots of the Shoot popups (Shoot.exe vtable [0x1a],[0x1b],[0x1c]):
    virtual void OnClose(int result) { (void)result; }   // slot 26
    // slot 27 (vtable +0x6c): RETURNS int in the original: Create() fails when it returns 0 (or the dialog got closed inside it).
    // (pkg4: was `void OnInit()`; default returns 1.)
    virtual int OnInit() { return 1; }                   // slot 27
    virtual void OnApply() {}                            // slot 28 (OnShow / Apply, TODO)

protected:
    uintptr_t userArg_ = 0;           // +0x64
    bool created_ = false;            // +0x68 set at the end of a successful Create (modal state installed)
    bool closing_ = false;            // +0x6c set by Close
    Dialog* parentDialog_ = nullptr;  // +0x70 nearest enclosing dialog (set by Create)
    Widget* defaultButton_ = nullptr; // +0x74
    Widget* prevFocus_ = nullptr;     // +0x78 focus owner before the dialog (restored on close)
    int mode_ = 0;                    // +0x7c cursor number used while the dialog is open (Shoot: 3)
    int prevCursor_ = 1;              // +0x80 TODO(pkg3): CursorMgr::SetCursor returns void, previous shape unknown
    void RestoreInput();              // Aaron 0x10003702 (FUN_10003702)
};

// ---------------------------------------------------------------------------------------------
// Timer: periodic notification. Posts AMSG_NOTIFY(id) to the scene every period -> parent->OnNotify(id, timer).
class Timer : public Widget, public Subscriber {
public:
    // Aaron ord 29 0x10006a70: Timer(parent|scene, cmdId, periodMs, style, userData) - RESOLVED: Node(parent, cmdId, style)
    //   then Start(periodMs, userData) (period 0 -> Stop, i.e. not started). Shoot: (this, 0x1404, 0, 8, 0) / (scene, 0x1402, 0, 0, 0).
    //   style 0x10000: when the repetitions are exhausted the timer deletes itself; 0x20000: no drift (next += period).
    Timer(WidgetParent parent, u32 cmdId, u32 periodMs, u32 style, u32 userData);
    ~Timer() override;

    u32 GetClassId() const override { return CID_TIMER; } // Aaron ord 378 0x10006f40
    // Aaron ord 525 0x10006d83 (vtable slot 23, replaces Widget::PostNotify): posts AMSG_NOTIFY(cmdId, this) to the scene whatever the argument.
    void PostNotify(u32 code) override;
    // Aaron ord 600 0x10006de3: Start(periodMs, userData). Shoot: Start(0x5dc, 1) and Start(x, 0xffffffff).
    //   userData = number of ticks before it stops (0xffffffff = forever, 1 = once): RESOLVED from 0x10006bf6.
    void Start(u32 periodMs, u32 userData);
    void Stop();                                 // Aaron ord 425 0x10006b94
    static void Nop234() {}                      // Aaron ord 234 0x10006b1a (Timer::Nop234): empty function
    // Aaron ord 452 0x10006bf6 Timer::OnMessage: receives AMSG_TICK from the scene's TimerMgr (internal).
    void OnTick(u32 totalMs, u32 deltaMs);
    void OnAdvise(int code, intptr_t value, Broadcaster* src) override; // Subscriber: code == AMSG_TICK -> OnTick(value, 0)
    void Pause(bool on);        // Aaron 0x10006d07: shifts the next deadline by the TimerMgr total (pause / resume)
    u32 Remaining() const;      // Aaron 0x10006b27
    void Restart();             // Aaron 0x10006db7: Start(period, userData) again if period != 0
    bool Running() const { return running_; }
    u32 Count() const { return count_; }

private:
    bool running_ = false;
    u32 period_ = 0, user_ = 0, count_ = 0, acc_ = 0; // +54 +50 +4c +58
    bool done_ = false;                               // +60
};

} // namespace aaron
