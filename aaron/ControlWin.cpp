// ControlWin / EditControl (WP-4): state model of the native child controls. See ControlWin.h.
#include "aaron/ControlWin.h"

#include "aaron/Scene.h"

#include <algorithm>
#include <cstring>

namespace aaron {

ControlWin::ControlWin(WidgetParent parent, u32 exStyle, const char* className, const char* title, u32 style, int x, int y, int w, int h,
                       uintptr_t menu, void* param, u32 widgetStyle) // ord 30 0x10006fb2
    : Widget(parent, kNoRes, 0, 0, 0, u32(menu), widgetStyle & 0xffffff8fu), exStyle_(exStyle) {
    (void)param; // lpParam of CreateWindowEx
    POINT p;
    p.x = x;
    p.y = y;
    if (scene_) scene_->ToScreen(&p, 1);
    rect_.left = p.x - w / 2;
    rect_.top = p.y - h / 2;
    rect_.right = rect_.left + w;
    rect_.bottom = rect_.top + h;
    if (disable_ == 0) winStyle_ = (style & 0xf7ffffffu) | 0x40000000u;
    else winStyle_ = style | 0x48000000u;
    if (className && *className) {
        class_ = className;
        text_ = title ? title : "";
        handle_ = this; // CreateWindowEx succeeded
    }
    u32 ws = widgetStyle; // the original hands the UNMASKED value to SetStyle (the Widget base got `& 0xffffff8f`)
    if (widgetStyle & 0x8000) parentFont_ = true; // WM_GETFONT of the parent window -> WM_SETFONT
    if (winStyle_ & 0x10000) ws |= 0x100;
    if (winStyle_ & 0x20000) ws |= 0x200;
    Widget::SetStyle(ws);
}

ControlWin::~ControlWin() { // ord 69 0x1000715a
    handle_ = nullptr; // DestroyWindow
}

bool ControlWin::Create() { // ord 486 0x1000772b
    if (!handle_) return false;
    subclassed_ = true; // SetWindowLong(GWL_USERDATA = this) + SetWindowLong(GWL_WNDPROC = 0x10006f50)
    return Widget::Create();
}

void ControlWin::Show(bool show) { // ord 683 0x100078d7
    if (!show) {
        visible_ = false; // ShowWindow(SW_HIDE)
        ShowChildren(false);
    } else {
        ShowChildren(true);
        visible_ = true;  // ShowWindow(SW_SHOW)
    }
}

RECT ControlWin::GetClientRectScene() const { // ord 219 0x100071be
    RECT r;
    r.left = 0;
    r.top = 0;
    r.right = rect_.right - rect_.left;
    r.bottom = rect_.bottom - rect_.top;
    if (scene_) {
        POINT pts[2] = {{r.left, r.top}, {r.right, r.bottom}};
        scene_->FromScreen(pts, 2);
        r.left = pts[0].x;
        r.top = pts[0].y;
        r.right = pts[1].x;
        r.bottom = pts[1].y;
    }
    return r;
}

void ControlWin::GetClientSize(i32* out2) const { // ord 347 0x1000721e
    out2[0] = rect_.right - rect_.left;
    out2[1] = rect_.bottom - rect_.top;
}

i32 ControlWin::GetClientWidth() const { return rect_.right - rect_.left; }   // ord 284 0x10007202
i32 ControlWin::GetClientHeight() const { return rect_.bottom - rect_.top; }  // ord 394 0x10007271

bool ControlWin::HitTest(int x, int y) { // ord 554 0x1000779e
    RECT r = GetClientRectScene();
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom; // PtInRect
}

void ControlWin::SetPos(int x, int y, int w, int h) { // ord 444 0x10007627
    POINT p;
    p.x = x;
    p.y = y;
    if (scene_) scene_->ToScreen(&p, 1);
    rect_.left = p.x;
    rect_.top = p.y;
    rect_.right = p.x + w;
    rect_.bottom = p.y + h;
}

void ControlWin::Resize(int w, int h) { // ord 690 0x10007922
    rect_.right = rect_.left + w;
    rect_.bottom = rect_.top + h;
}

void ControlWin::SetText(const char* text) { // ord 666 0x1000786b
    if (handle_) SendMessage(0xc, 0, reinterpret_cast<intptr_t>(text ? text : ""));
}

void ControlWin::GetText(char* buf, int n) const { // ord 358 0x1000724f
    if (!buf || n <= 0) return;
    int k = std::min<int>(n - 1, int(text_.size()));
    std::memcpy(buf, text_.data(), size_t(k));
    buf[k] = 0;
}

void ControlWin::SetFont(const char* face, int height) { // ord 637 0x100077fd
    fontFace_ = face ? face : "";
    fontHeight_ = height;
    SendMessage(0x30, 1, 0); // WM_SETFONT with the HFONT of the GDI cache (the port keeps the description)
}

void ControlWin::SetTextColor(u32 color, int transparent) { // ord 668 0x1000788e
    textColor_ = color;
    transparent_ = transparent;
    textColorSet_ = true;
}

void ControlWin::SetBackColor(u32 color) { // ord 605 0x100078b7
    backColor_ = color;
    backSet_ = true;
}

int ControlWin::OnCtlColor(u32* textColor, int* bkMode, u32* backColor) const { // vtable slot 25, 0x1000767a
    if (textColorSet_) {
        if (textColor) *textColor = textColor_;
        if (bkMode) *bkMode = transparent_ ? 1 : 2; // SetBkMode(2 - (transparent != 0)): 1 TRANSPARENT, 2 OPAQUE
    }
    if (backSet_ && backColor) *backColor = backColor_;
    return backSet_ ? 1 : 0;
}

intptr_t ControlWin::SendMessage(u32 msg, uintptr_t wParam, intptr_t lParam) { // ord 591 0x100077d7 (model of the control's window procedure)
    if (!handle_) return 0;
    switch (msg) {
    case 0xc: // WM_SETTEXT
        text_ = lParam ? reinterpret_cast<const char*>(lParam) : "";
        if (text_.size() > limit_) text_.resize(limit_);
        return 1;
    case 0xd: { // WM_GETTEXT(wParam = size, lParam = buffer)
        if (!lParam || wParam == 0) return 0;
        char* buf = reinterpret_cast<char*>(lParam);
        size_t k = std::min<size_t>(wParam - 1, text_.size());
        std::memcpy(buf, text_.data(), k);
        buf[k] = 0;
        return intptr_t(k);
    }
    case 0xe: // WM_GETTEXTLENGTH
        return intptr_t(text_.size());
    case 0x30: // WM_SETFONT
        return 0;
    case 0x31: // WM_GETFONT
        return fontHeight_ ? 1 : 0;
    case 0xa: // WM_ENABLE
        enabled_ = wParam != 0;
        return 0;
    case 0xb0: { // EM_GETSEL
        return intptr_t(uint32_t(selStart_) | (uint32_t(selEnd_) << 16));
    }
    case 0xb1: { // EM_SETSEL (end -1: to the end of the text)
        int n = int(text_.size());
        int s = int(wParam), e = int(lParam);
        if (s < 0) { s = e = 0; } // -1: remove the selection (caret stays)
        else if (e < 0 || e > n) e = n;
        selStart_ = std::min(s, n);
        selEnd_ = e;
        return 0;
    }
    case 0xc5: // EM_LIMITTEXT (0: the maximum)
        limit_ = wParam ? unsigned(wParam) : 0x7ffeu;
        return 0;
    case 0x425: // EM_GETLIMITTEXT
        return intptr_t(limit_);
    default:
        return 0;
    }
}

int ControlWin::PreProcess(u32 msg, uintptr_t wParam, intptr_t lParam) { // 0x10006f50 -> 0x1000728d
    Scene* sc = scene_;
    if (!sc) return 0;
    if (msg < 0x103) {
        if (msg >= 0x100) { // keys: FocusMgr first, then the pick handler (slot 21)
            sc->Focus().OnMessage(sc, msg, wParam, lParam);
            return OnPickDefault(msg, int(wParam), int(lParam));
        }
        switch (msg) {
        case 2: // WM_DESTROY: the sub-class is removed, the window is gone
            subclassed_ = false;
            handle_ = nullptr;
            break;
        case 7: case 8: // WM_SETFOCUS / KILLFOCUS
            sc->Focus().SetFocusState(this, msg == 7);
            break;
        case 10: // WM_ENABLE
            EnableCount(int(wParam));
            break;
        case 0x20: // WM_SETCURSOR
            sc->Cursor().OnMessage(sc, msg, wParam, lParam);
            break;
        default:
            break;
        }
        return 0;
    }
    if (msg == 0x104 || msg == 0x105 || msg == 0x106) {
        sc->Focus().OnMessage(sc, msg, wParam, lParam);
        return OnPickDefault(msg, int(wParam), int(lParam));
    }
    if ((msg >= 0xa0 && msg <= 0xa9) || (msg >= 0x200 && msg <= 0x209)) { // (non-client) mouse: control-relative -> scene coordinates
        int x = int(int16_t(uint32_t(lParam) & 0xffff)), y = int(int16_t((uint32_t(lParam) >> 16) & 0xffff));
        if (msg >= 0x200) { // MapWindowPoints(control -> scene window)
            x += rect_.left;
            y += rect_.top;
        }
        POINT p;
        p.x = x;
        p.y = y;
        sc->FromScreen(&p, 1);
        intptr_t packed = intptr_t(uint32_t(uint16_t(p.x)) | (uint32_t(uint16_t(p.y)) << 16));
        sc->Cursor().OnMessage(sc, msg, wParam, packed);
        sc->Picker().SetHover(this);
        return OnPickDefault(msg, p.x, p.y);
    }
    return 0;
}

EditControl::EditControl(WidgetParent parent, u32 exStyle, const char* title, u32 style, int x, int y, int w, int h, uintptr_t menu,
                         void* param, u32 widgetStyle) // ord 31 0x1000794c
    : ControlWin(parent, exStyle, "EDIT", title, style, x, y, w, h, menu, param, widgetStyle) {}

intptr_t EditControl::SendMessage(u32 msg, uintptr_t wParam, intptr_t lParam) { return ControlWin::SendMessage(msg, wParam, lParam); }

} // namespace aaron
