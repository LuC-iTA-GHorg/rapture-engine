// Button (Aaron.dll 0x10003129 and helpers 0x10002d90..0x10003620). Package 4.
#include "aaron/Scene.h"
#include "aaron/Sprites.h"

namespace aaron {

// FUN_10002d90 prefix (Button base): the parent decides the notify bits. If the parent is a Dialog (GetPickFlags == 0x80)
// style |= 0x18 (notify the parent via OnNotify, tab-stop-less pickable), otherwise style |= 0x10.
static u32 ButtonBaseStyle(WidgetParent parent, u32 style) {
    Widget* pw = parent.widget;
    if (!pw && parent.scene) pw = parent.scene->RootWidget();
    u32 flags = pw ? pw->GetPickFlags() : 0;
    return style | (flags == 0x80 ? 0x18u : 0x10u);
}

// Aaron 0x10003129 (ord 13)
Button::Button(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style, int mode)
    : Widget(parent, resId, x, y, level, cmdId, ButtonBaseStyle(parent, style)),
      moses::CDrawDib(static_cast<moses::CDrawView*>(parent.widget ? parent.widget->GetScene() : parent.scene), level) {
    mode_ = mode;                                // FUN_10002d90: this+0x40 = mode
    if (mode == 1) {                             // FUN_10003093: register as the default button of the enclosing Dialog
        for (Widget* w = ParentWidget(); w; w = w->ParentWidget()) {
            if (w->GetPickFlags() == 0x80) {
                static_cast<Dialog*>(w)->SetDefaultButton(this);
                break;
            }
        }
    }
    InitDrawObj(static_cast<moses::CDrawDib*>(this), style | 0x10); // FUN_10004720(this, resId, x, y, style | 0x10)
    // frames_ already holds the fallback table {0,0,0,0,1,3,1,4}
    u32 n = 0;
    for (; n < 8; ++n) {
        u32 id = scene_ ? scene_->GetIndexedResId(resId, n) : kNoRes;
        if (id == kNoRes) break;
        frames_[n] = id;
    }
    for (u32 k = n; k < 8; ++k) frames_[k] = frames_[frames_[k]]; // missing states copy an earlier one (chained, ascending)
}

Button::~Button() {} // Aaron 0x100033d0: ~CDrawDib then ~Widget

// Aaron 0x10002e38
int Button::StateIndex() const {
    int idx;
    if (disable_ == 0) {
        if ((state_ & 0x12) == 0x10 && (style_ & 0x1000)) idx = 6;
        else idx = (state_ & 4) != 0;
    } else {
        idx = 2;
    }
    if (state_ & 8) {
        switch (idx) {
        case 0: idx = 3; break;
        case 1: idx = 4; break;
        case 2: idx = 5; break;
        case 6: idx = 7; break;
        }
    }
    return idx;
}

// Aaron 0x100032cb (slot 18)
void Button::Draw() {
    if ((style_ & 1) == 0) moses::CDrawDib::Animate(frames_[StateIndex()], nullptr);
}

// Aaron 0x1000330b (slot 9)
void Button::Show(bool show) {
    if (!show) {
        ShowChildren(false);
        moses::CDrawObj::Hide();
    } else {
        Draw();
        ShowChildren(show);
    }
}

// Aaron 0x100035b0
bool Button::Create() { return Widget::Create() && moses::CDrawObj::Init() != 0; }

// Aaron 0x100035f0
bool Button::HitTest(int x, int y) { return moses::CDrawObj::IsPicked(x, y) != 0; }

int Button::OnMouse(u32 msg, u32 wParam, intptr_t lParam) { return Widget::OnMouse(msg, wParam, lParam); }

void Button::SetPressedFlag(bool on) { Widget::SetPressedFlag(on); }

// Aaron 0x10002fb8 (ord 256)
u32 Button::GetPickFlags() {
    u32 r = 0;
    switch (mode_) {
    case 0: r = 4; break;
    case 1: r = 2; break;
    case 4:
    case 9: r = 1; break;
    }
    return r | 0x40;
}

// Aaron 0x10002f13 (slot 23)
void Button::PostNotify(u32 code) {
    if (code != 0) {
        if (mode_ == 3) {
            SetToggled((state_ & 8) == 0);
        } else if (mode_ == 9) {
            Widget* w = this;
            do {
                if (w->GetPickFlags() & 1) static_cast<Button*>(w)->SetToggled(w == this);
                w = w->TabSibling(1);
            } while (w && w != this);
        }
    }
    Widget::PostNotify(code);
}

// Aaron 0x10003025 (ord 411)
bool Button::IsToggled() const { return (state_ & 8) != 0; }

// Aaron 0x10003047 (ord 612)
void Button::SetToggled(bool on) {
    if ((state_ & 8) != (on ? 8u : 0u)) {
        state_ ^= 8;
        Draw();
    }
}

// Aaron 0x100030d8 (ord 665)
void Button::SetMode(int mode) {
    mode_ = mode;
    if (mode == 1) {
        for (Widget* w = ParentWidget(); w; w = w->ParentWidget()) {
            if (w->GetPickFlags() == 0x80) {
                static_cast<Dialog*>(w)->SetDefaultButton(this);
                break;
            }
        }
    }
    Draw();
}

} // namespace aaron
