// Widget / Node (Aaron.dll widget layer, aaron.md sec. 2.7). Package 4.
// Every function carries the original address (Aaron.dll 0x1000xxxx).
#include "aaron/Scene.h"
#include "aaron/Widget.h"

namespace aaron {

// ---------------------------------------------------------------------------------------------
// Aaron 0x1000449a (FUN_1000449a) + 0x10004343 (FUN_10004343) + Node::Node 0x10003ec4.
// NOTE: the original base constructor takes (parent, resId, cmdId, style) only: x, y and level are used by the derived
// sprite constructors for the moses part. The port stores them (x_, y_, level_) for reference.
Widget::Widget(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style)
    : cmdId_(cmdId), style_(style), resId_(resId), level_(level), x_(x), y_(y) {
    Widget* pw = parent.widget;
    if (parent.widget) {
        scene_ = parent.widget->scene_;
    } else if (parent.scene) {
        scene_ = parent.scene;
        pw = parent.scene->RootWidget(); // children of the scene hang from its root widget (+0x53c)
    }
    if (pw) pw->AddChild(this);
    if ((style_ & 0x70) != 0 && scene_) scene_->Picker().Hold(); // 0x1000449a: PickMgr::Hold (FUN_10012167, ord 136)
}

// Aaron 0x10004534 (Widget::~Widget)
Widget::~Widget() {
    if (scene_) {
        // FUN_1000ca7d: drop every reference the scene managers hold to this widget (pick, mouse capture, focus).
        // TODO(pkg3): MsgQueue has no "purge widget" API, so already posted AMSG_NOTIFY for this widget may dangle.
        scene_->Cursor().Forget(this); // capture + hover (a dangling hover_ crashed CursorMgr::OnMouse after a dialog closed)
        if (scene_->Focus().GetFocus() == this) scene_->Focus().SetFocus(nullptr);
        scene_->ForgetWidget(this);    // WP-4: modal / preferred widget, picker hover (the other two lines above stay for the port's own tracking)
        if ((style_ & 0x70) != 0) scene_->Picker().Release(); // 0x10004534: PickMgr::Release (FUN_10012403, ord 570)
    }
    // Aaron 0x10004386 (ord 77): ReleaseRes() non-virtually (the derived part is already gone) then ~TreeNode.
    // The original ~TreeNode (0x10002944) deletes the remaining children first; the port does it here.
    ReleaseBase(this);
    DeleteChildren();
}

void Widget::ToggleCaptured(bool pressed) { // ord 506 0x10004a0d
    state_ ^= WST_OVER;
    SetPressedVisual(pressed);
}

// Aaron 0x10004136 (ord 460)
void Widget::OnNotify(u32 id, Widget* src) {
    if ((style_ & 8) == 0 || parent_ == nullptr) {
        // scene->OnCommand(id, src): the original passes the widget pointer as the 2nd argument.
        if (scene_) scene_->OnCommand(id, reinterpret_cast<uintptr_t>(src));
    } else {
        static_cast<Widget*>(parent_)->OnNotify(id, src);
    }
}

// Aaron 0x100048e1 (ord 470). Slot 5. Called by EnableCount with 1 (disable) / 0 (enable): on "off" drops the scene's
// references (FUN_1000ca7d), on "on" FUN_10012355 (cursor refresh, nothing to do here); then Draw().
void Widget::Capture(bool on) {
    if (!on && scene_) {
        if (scene_->Cursor().Captured() == this) scene_->Cursor().Capture(nullptr);
        if (scene_->Focus().GetFocus() == this) scene_->Focus().SetFocus(nullptr);
    }
    Draw();
}

// Aaron 0x10004934 (ord 490) + FUN_1000445f. Show first (unless created hidden), then LoadRes and Activate.
bool Widget::Create() {
    Show((style_ & 1) == 0);
    return LoadAndActivate(); // Aaron 0x10004934: Show(), then FUN_1000445f (ord 488)
}

// Aaron ord 77 0x10004386
void Widget::ReleaseBase(Widget* w) {
    if (w->resObj_ && w->scene_) { // Widget::ReleaseRes body (0x100043d7), called non-virtually
        w->scene_->FreeResource(w->resId_);
        w->resObj_ = nullptr;
    }
}

// Aaron ord 488 0x1000445f
bool Widget::LoadAndActivate() {
    return LoadRes() && (Activate(), true); // Activate (ord 487) always returns 1 in the original
}

// Aaron 0x10004274 (ord 631): returns the previous style.
void Widget::SetStyle(u32 style) {
    u32 old = style_;
    style_ = style;
    u32 diff = old ^ style;
    if (diff & 2) {
        if ((style & 2) == 0) {
            while (disable_ != 0) EnableCount(1);
        } else {
            EnableCount(0);
        }
    }
    if (diff & 4) {
        if ((style & 4) == 0) {
            while (hide_ != 0) HideCount(0);
        } else {
            HideCount(1);
        }
    }
}

// Aaron 0x100047fa (ord 178)
void Widget::Show(bool show) {
    moses::CDrawObj* o = GetDrawObj();
    if (!show) {
        ShowChildren(false);
        if (o) o->Hide();
    } else {
        if (o) o->Show();
        ShowChildren(show);
    }
}

void* Widget::GetHWnd() { return scene_ ? scene_->GetHWnd() : nullptr; } // 0x100034c0 (scene+0x528)

u32 Widget::GetPickFlags() { return 0; } // 0x100034e0

// Aaron 0x100043d7 (ord 206)
void Widget::ReleaseRes() {
    if (resObj_) {
        scene_->FreeResource(resId_);
        resObj_ = nullptr;
    }
}

// Aaron 0x1000441e (ord 428): -1 = no resource -> success.
bool Widget::LoadRes() {
    if (resId_ == kNoRes) return true;
    resObj_ = scene_ ? scene_->LoadResource(resId_) : nullptr;
    return resObj_ != nullptr;
}

// Aaron 0x100034f0: returns style & 0x400 (!). Faithful odd result is kept in the ord 418 export; the port's IsEnabled
// (used by nobody in Shoot) follows the disable counter.
bool Widget::IsEnabled() const { return disable_ == 0; }

// Aaron 0x1000485a (ord 458)
void Widget::SetPressedFlag(bool on) {
    if (!on) state_ &= ~0x10u; else state_ |= 0x10u;
    if (style_ & 0x1000) Draw();
    if (style_ & 0x400) {
        // FUN_100137e7: CursorMgr+0x4c = on; refresh cursor. TODO(pkg3): CursorMgr has no "pressed cursor" API.
    }
}

int Widget::OnKey(u32 msg, u32 vk, u32 lParam) { return OnKeyDefault(msg, vk, lParam); } // slot 20
int Widget::OnPick(u32 msg, int x, int y) { return OnPickDefault(msg, x, y); }           // slot 21
int Widget::OnKeyDefault(u32, u32, u32) { return 0; }  // 0x10004969
int Widget::OnPickDefault(u32, int, int) { return 0; } // 0x10003530

// Aaron 0x10004a35 (FUN_10004a35)
void Widget::SetPressedVisual(bool on) {
    if ((state_ & 4) != (on ? 4u : 0u)) {
        state_ ^= 4;
        Draw();
    }
}

// Aaron 0x100046b0 (FUN_100046b0) with FUN_100136cc (CursorMgr capture) + FUN_10004a0d.
void Widget::MousePress() {
    if ((state_ & 2) != 0) return;
    if (scene_ && disable_ == 0) {
        scene_->Cursor().Capture(this);   // SetCapture
        state_ ^= 2;                      // FUN_10004a0d(w, 1)
        SetPressedVisual(true);
    }
    state_ |= 1;                          // transient "focusing" bit
    if (scene_) scene_->Focus().SetFocus(this);
    state_ &= ~1u;
}

// Aaron 0x1000463e (FUN_1000463e)
void Widget::MouseRelease(bool notify) {
    if ((state_ & 2) == 0) return;
    bool fire = notify && (state_ & 4) != 0;
    if (scene_ && scene_->Cursor().Captured() == this) scene_->Cursor().Capture(nullptr); // ReleaseCapture
    state_ ^= 2;                          // WM_CAPTURECHANGED handler: FUN_10004a0d(w, 0)
    SetPressedVisual(false);
    if (fire) PostNotify(1);
}

// Aaron 0x10004978 (ord 505). lParam packs x | y << 16 as 16-bit signed (Win32).
int Widget::OnMouse(u32 msg, u32 wParam, intptr_t lParam) {
    (void)wParam;
    switch (msg) {
    case 0x200: // WM_MOUSEMOVE
        if (state_ & 2) {
            int x = static_cast<int16_t>(lParam & 0xffff);
            int y = static_cast<int16_t>((static_cast<uint32_t>(lParam) >> 16) & 0xffff);
            SetPressedVisual(HitTest(x, y));
        }
        break;
    case 0x201: // WM_LBUTTONDOWN
    case 0x203: // WM_LBUTTONDBLCLK
        MousePress();
        break;
    case 0x202: // WM_LBUTTONUP
        MouseRelease(true);
        break;
    }
    return 1;
}

// Aaron 0x10004602 (ord 159): deferred notification -> Scene::OnMessage(0xc06) -> widget->OnNotify(cmdId, widget).
void Widget::PostNotify(u32 code) {
    if (code != 0 && scene_) scene_->PostMessage(AMSG_NOTIFY, cmdId_, reinterpret_cast<intptr_t>(this));
}

bool Widget::HitTest(int x, int y) { // slot 24: Widget has no graphics (sprites override)
    (void)x; (void)y;
    return false;
}

// Aaron 0x10003d20 (ord 163)
Widget* Widget::InitOrDelete(Widget* w) {
    if (w != nullptr && !w->Create()) {
        delete w;
        w = nullptr;
    }
    return w;
}

// Aaron 0x10003f27 (ord 182): recursive (children first).
void Widget::EnableCount(int enable) {
    for (TreeNode* c = first_; c; c = c->NextSibling()) static_cast<Widget*>(c)->EnableCount(enable);
    if (enable == 0) {
        int old = disable_++;
        if (old == 0) {
            style_ |= 2;
            Capture(true);
        }
    } else if (disable_ != 0 && --disable_ == 0) {
        style_ &= ~2u;
        Capture(false);
    }
}

// Aaron 0x100041ad (FUN_100041ad)
int Widget::HideCount(int hide) {
    for (TreeNode* c = first_; c; c = c->NextSibling()) static_cast<Widget*>(c)->HideCount(hide);
    if ((style_ & 0x2000) != 0) return 0;
    if (hide == 0) {
        if (hide_ != 0 && --hide_ == 0) {
            style_ &= ~4u;
            OnHideChange(false);
        }
    } else {
        int old = hide_++;
        if (old == 0) {
            style_ |= 4;
            OnHideChange(true);
        }
    }
    return hide_;
}

// Aaron 0x1000430b (ord 684)
void Widget::ShowChildren(bool show) {
    for (TreeNode* c = first_; c; c = c->NextSibling()) static_cast<Widget*>(c)->Show(show);
}

// Aaron 0x100040f8 (ord 487)
void Widget::Activate() {
    if (style_ & 4) HideCount(1);
    if (style_ & 2) EnableCount(0);
}

// Aaron 0x10003fe8 (FUN_10003fe8): circular sibling step.
static Widget* RingStep(Widget* w, int forward) {
    TreeNode* n = forward ? w->NextSibling() : w->PrevSibling();
    if (!n && w->Parent()) n = forward ? w->Parent()->FirstChild() : w->Parent()->LastChild();
    return static_cast<Widget*>(n);
}

// Aaron 0x10004044 (FUN_10004044)
Widget* Widget::TabSibling(int forward) {
    Widget* p;
    if (forward == 0) {
        if ((style_ & 0x200) == 0) {
            p = RingStep(this, 0);
        } else {
            Widget* l = this;
            do { l = RingStep(l, 1); p = l; } while ((l->style_ & 0x200) == 0);
        }
    } else {
        p = RingStep(this, 1);
        Widget* l = this;
        if (p && (p->style_ & 0x200) != 0) {
            for (;; l = RingStep(l, 0)) {
                p = l;
                if ((l->style_ & 0x200) != 0) break;
            }
        }
    }
    return p;
}

// Aaron 0x10004720 (FUN_10004720)
void Widget::InitDrawObj(moses::CDrawObj* o, u32 style) {
    o->SetId(reinterpret_cast<uintptr_t>(this));
    if (resId_ != kNoRes) o->SetResId(resId_);
    o->SetPosition(x_, y_, 0);
    uint8_t pick = 0;
    if ((style & 0x70) == 0) {
        pick = (style & 0x1480) == 0;
    } else {
        if (style & 0x20) pick = 2;
        if (style & 0x40) pick |= 4;
    }
    o->SetPickMode(pick);
    if (style & 0x800) o->SetScrollable(1);
}

// ---------------------------------------------------------------------------------------------
// Node: Aaron 0x10003ec4. Shoot always passes (parent, 0, 0). In the original `a`/`b` are the +0x24/+0x28 fields (id, style).
Node::Node(WidgetParent parent, u32 a, u32 b) : Widget(parent, kNoRes, 0, 0, 0, a, b) {}

Node::Node(TreeRoot* holder, Scene* scene) : Widget(WidgetParent(scene), kNoRes, 0, 0, 0, 0, 0) { // ord 33 0x10003e72 / ord 48 0x10003d70
    if (holder) holder->SetRoot(this); // becomes THE root of the holder's tree
}

} // namespace aaron
