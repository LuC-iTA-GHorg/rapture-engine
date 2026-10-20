// Widget: node of the scene tree = tree node + resource + (in the sprite subclasses) a moses
// drawable (aaron.md sec. 2.7). Concrete widget classes are in Sprites.h.
//
// Idiom (every widget in Shoot.*):
//     w = new Button(scene_or_parent, resId, x, y, level, cmd, style, mode);
//     w = Widget::InitOrDelete(w);          // Create(); on failure delete and return nullptr
// The parent owns the widget from the moment it is constructed (TreeNode child list).
#pragma once

#include "aaron/RObject.h"
#include "moses/DrawObj.h"

#include <cstddef>

namespace aaron {

class Scene;
class Widget;

// First constructor argument of every widget: either a parent Widget or the Scene itself (meaning
// "child of the scene's root widget"). Original: one pointer, disambiguated at run time.
struct WidgetParent {
    Widget* widget = nullptr;
    Scene* scene = nullptr;
    WidgetParent(Widget* w) : widget(w) {}
    WidgetParent(Scene* s) : scene(s) {}
    WidgetParent(std::nullptr_t) {}
};

// Widget state bits (+0x3c)
enum WidgetState : u32 {
    // Verified on Widget::OnMouse (0x10004978): bit 0x01 = focus change in progress (transient),
    // 0x02 = mouse captured by this widget, 0x04 = pressed visual (pointer inside while captured), 0x08 = toggled,
    // 0x10 = "pressed flag" (SetPressedFlag).
    WST_FOCUSING = 0x01,
    WST_OVER = 0x02,     // (name kept) = captured by the mouse
    WST_PRESSED = 0x04,
    WST_PICK = 0x10,
    WST_TOGGLE = 0x08,
};

class Widget : public TreeNode {
public:
    // Aaron ord 58 0x1000449a. (parent|scene, resId, x, y, level, cmdId, style)
    Widget(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style);
    ~Widget() override;                                  // Aaron ord 84 0x10004534   slot 0

    // ---- virtual interface in vtable order (0x1001d4e0) -------------------------------------------------
    u32 GetClassId() const override { return CID_NODE; } // slot 1 (derived classes return their own id)
    virtual void OnReserved2() {}                        // slot 2 (nop; TODO unknown)
    // Aaron ord 453 0x10003520 (Widget::NopVirtual) "release hook" - slot 3.
    // Also re-exported as ord 225 0x10003973 (Dialog::Nop225), ord 228 0x10005ade (RectSprite::Nop228),
    // ord 230 0x10004f64 (AnimSprite::Nop230), ord 236 0x10003fdb (Node::Nop236): the same empty virtual per class.
    virtual void OnReleaseHook() {}
    // Aaron ord 460 0x10004136. style & 8 -> parent->OnNotify(id, this), else scene->OnCommand(id, param).
    // Shoot's popups override this as "OnButton(id, state)" (Shoot.exe vtable slot 4).
    virtual void OnNotify(u32 id, Widget* src);          // slot 4
    virtual void Capture(bool on);                       // slot 5   Aaron ord 470 0x100048e1 (SetCapture, ord 611, on the CursorMgr)
    virtual bool Create();                               // slot 6   Aaron ord 490 0x10004934 (loads the resource, creates the sprite)
    // Aaron ord 459 0x10003510 (Widget::NopVirtual2): empty virtual (slot 18 in the vtable dump of 0x1001d4e0; also Scene::OnReady, slot 8 of the
    // scene vtable). QUIRK of the port: HideCount() calls it as "slot 7" and Button overrides it (see Sprites.h).
    virtual void NopVirtual2() {}                        // slot 7   TODO unknown purpose
    // WP-4: what the original's slot 7 really is (0x1001d4fc = the empty Aaron ord 453 thunk for Widget / Button / Dialog / Node ..., but
    // 0x10006860 = CAnimObj::Pause(flag) for AnimSprite / AnimRectSprite / AnimTextSprite / AnimPolySprite, Aaron ord 511-514): called by
    // HideCount() on every 0->1 (hidden = true) / 1->0 transition. Default: the port's historic NopVirtual2().
    virtual void OnHideChange(bool hidden) { (void)hidden; NopVirtual2(); }
    virtual void SetStyle(u32 style);                    // slot 8   Aaron ord 631 0x10004274
    virtual void Show(bool show);                        // slot 9   Aaron ord 178 0x100047fa (also exported as AnimSprite::Show, ord 142 0x100045c3)
    virtual void* GetHWnd();                             // slot 10  Aaron ord 397 (scene's window; unused by Shoot.*, opaque in the port)
    virtual u32 GetPickFlags();                          // slot 11  Aaron ord 256: 1/2/4|0x40 (Dialog returns 0x80); TODO exact
    // slot 12 (vtable +0x30): returns the embedded CAnimObj (AnimSprite::Show uses it to QStop; others: null). (pkg4: was OnReserved12)
    virtual moses::CAnimObj* GetAnimObj() { return nullptr; }
    virtual moses::CDrawObj* GetDrawObj() { return nullptr; } // slot 13  sprites return their embedded CDrawObj (Aaron ord 211-214, 260-264)
    virtual void ReleaseRes();                           // slot 14  Aaron ord 206 0x100043d7 (Scene::FreeResource of the loaded resource)
    virtual bool LoadRes();                              // slot 15  Aaron ord 428 0x1000441e (Scene::LoadResource)
    virtual bool IsEnabled() const;                      // slot 16  style & 0x400 TODO / disable counter == 0
    virtual void SetPressedFlag(bool on);                // slot 17  Aaron ord 458 0x1000485a
    virtual void Draw() {}                               // slot 18
    virtual void OnReserved19() {}                       // slot 19  TODO
    // slot 19 (Aaron ord 453 0x10003520, the empty virtual): focus gained / lost. FocusMgr::SetFocusState calls it; the default forwards to
    // OnReserved19() so old overrides keep working.
    virtual void OnFocus(bool on) { (void)on; OnReserved19(); }
    virtual int OnKey(u32 msg, u32 vk, u32 lParam);      // slot 20  default = OnKeyDefault
    virtual int OnPick(u32 msg, int x, int y);           // slot 21  default = OnPickDefault
    virtual int OnMouse(u32 msg, u32 wParam, intptr_t lParam); // slot 22  Aaron ord 505 0x10004978 (lParam = x | y << 16 as Win32? TODO: port passes (x, y) packed)
    virtual void PostNotify(u32 code);                   // slot 23  Aaron ord 159 0x10004602: posts AMSG_NOTIFY to the scene (deferred widget->OnNotify)
    virtual bool HitTest(int x, int y);                  // slot 24  Aaron ord 545-553

    // ---- non-virtual API ------------------------------------------------------------------------------
    // Aaron ord 163 0x10003d20: calls Create(); on failure deletes the widget and returns nullptr.
    static Widget* InitOrDelete(Widget* w);
    template <class T>
    static T* Attach(T* w) { return static_cast<T*>(InitOrDelete(w)); } // typed convenience (port addition)
    // Aaron ord 710 0x10004c40 (Widget::GetFromObj): the CDrawObj's id is the owning Widget*.
    static Widget* GetFromObj(moses::CDrawObj* obj) { return obj ? reinterpret_cast<Widget*>(obj->GetId()) : nullptr; }
    // Aaron ord 182 0x10003f27: enable counter. EnableCount(0) disables (++counter), EnableCount(1) enables (--counter).
    void EnableCount(int enable);
    // Aaron ord 684 0x1000430b: Show/Hide all children.
    void ShowChildren(bool show);
    // Aaron ord 487 0x100040f8 (Widget::Activate): activate the widget (default button of a dialog / click without mouse). TODO exact.
    void Activate();
    // Aaron ord 77 0x10004386 (ResNode::~ResNode, vtable 0x1001d6d8 = the Widget base part): non-virtual ReleaseRes() then ~TreeNode.
    // Port: static helper called by ~Widget (the TreeNode part is destroyed by C++).
    static void ReleaseBase(Widget* w);
    // Aaron ord 488 0x1000445f (Widget::LoadAndActivate): LoadRes() (virtual slot 15) and, if it succeeded, Activate(); true if both.
    bool LoadAndActivate();
    // Aaron ord 237 0x10004411 (Widget::Nop237): empty function (same family as the other per-class "Nop" exports).
    static void Nop237() {}
    // Aaron 0x100041ad (FUN_100041ad, style 0x2000 excluded): recursive "hide counter" (+0x30). hide != 0 increments,
    // 0 decrements; on the 0->1 / 1->0 transition sets/clears style bit 4 and calls slot 7 (NopVirtual2) with the flag.
    int HideCount(int hide);
    // Aaron ord 506 0x10004a0d (Widget::ToggleCaptured): state ^= 2 (the "captured" bit) then SetPressedVisual(pressed). Used by CursorMgr::SetCapture
    // (pressed = true when the capture starts) and by its WM_CAPTURECHANGED handling (false when it ends).
    void ToggleCaptured(bool pressed);
    // Aaron 0x10004044 (FUN_10004044): tab-stop ring navigation among siblings (style 0x200 marks a group start).
    // forward != 0: next sibling, or (if that one starts a new group) the first widget of the current group.
    Widget* TabSibling(int forward);
    // Aaron 0x10004a35 (FUN_10004a35): sets/clears the "pressed" visual bit (state 4) and redraws (slot 18) on change.
    void SetPressedVisual(bool on);
    // Aaron 0x100046b0 / 0x1000463e (FUN_100046b0 / FUN_1000463e): mouse press (capture) and release helpers of Widget::OnMouse.
    void MousePress();
    void MouseRelease(bool notify);
    // Default bodies of slots 20/21:
    int OnKeyDefault(u32 msg, u32 vk, u32 lParam);       // Aaron ord 495 0x10004969
    int OnPickDefault(u32 msg, int x, int y);            // Aaron ord 497 0x10003530

    // Aaron 0x10004720 (FUN_10004720): common init of the moses part of every sprite: SetId(this), SetResId, SetPosition,
    // pick mode from the style bits (0x70 set: 0x20 -> 2, 0x40 -> |4; none: 1 unless style & 0x1480), scrollable (0x800).
    // `style` is the value the original passes (Button: style | 0x10).
    void InitDrawObj(moses::CDrawObj* o, u32 style);

    Scene* GetScene() const { return scene_; }
    u32 GetCmdId() const { return cmdId_; }
    u32 GetStyle() const { return style_; }
    u32 WidgetResId() const { return resId_; } // (not GetResId: sprites also inherit CDrawObj::GetResId)
    void* GetResObj() const { return resObj_; }
    bool IsHidden() const { return hide_ > 0; }
    int DisableCount() const { return disable_; } // port helper (+0x2c)
    Widget* ParentWidget() const { return static_cast<Widget*>(parent_); }

protected:
    Scene* scene_ = nullptr;   // +20
    u32 cmdId_ = 0;            // +24 command / notification id
    u32 style_ = 0;            // +28
    int disable_ = 0;          // +2c counter
    int hide_ = 0;             // +30 counter
    u32 resId_ = kNoRes;       // +34
    void* resObj_ = nullptr;   // +38 loaded resource object (owned through Scene::LoadResource/FreeResource)
    u32 state_ = 0;            // +3c WidgetState
    int level_ = 0;
    int x_ = 0, y_ = 0;        // initial position given to the constructor
};

// Node: plain container (no graphics). Aaron ord 32 0x10003ec4: Node(parent|scene, a, b); Shoot always passes (parent, 0, 0).
class Node : public Widget {
public:
    Node(WidgetParent parent, u32 a, u32 b);
    // Aaron ord 33 0x10003e72 (ResNode::ResNode(TreeRoot*)) / ord 48 0x10003d70 (the same plus a PtrArray at +0x38 and a TimerMgr subscription):
    // the node hanging directly from a TreeRoot: becomes its root and takes the owner scene of the holder. Port: the scene is reached through
    // TreeRoot::Owner().
    Node(TreeRoot* holder, Scene* scene);
    static void Nop238() {}  // Aaron ord 238 0x10003e2d
    static void Nop239() {}  // Aaron ord 239 0x1000484d
    u32 GetClassId() const override { return CID_NODE; } // Aaron ord 380 0x10004c10 (the timer-subscribing root node of the tree; plain Node: same id)
    // Aaron ord 236 0x10003fdb (Node::Nop236): see Widget::OnReleaseHook.
    // Aaron ord 33 0x10003e72 (the Node/ResNode constructor taking the TreeRoot): see the Node(TreeRoot*) overload.
    // Aaron ord 238 0x10003e2d (RootNode::Nop238), ord 239 0x1000484d (Container::Nop239): the empty slot-3 virtuals of the two other container classes.
};

} // namespace aaron
