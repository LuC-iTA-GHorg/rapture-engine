// ControlWin / EditControl: widgets wrapping a native Win32 child control (class id 0x20104). aaron.md sec. 2.7. WP-4.
//
// Original: ControlWin is a Widget (0x78+ bytes) whose "sprite" is a window created with CreateWindowEx as a child of the scene's window; the
// HWND lives at +0x40, the original window procedure at +0x44 (the control is sub-classed with 0x10006f50 so mouse / key / focus messages reach
// Aaron's managers), +0x48..+0x58 hold the text / background colours used by WM_CTLCOLOR*. EditControl (ord 31) is the same with the class "EDIT"
// (string at 0x10023040) and two helpers (EM_LIMITTEXT, EM_SETSEL).
//
// SDL2 has no native child controls. These classes therefore keep the control's STATE (text, style, rectangle, enabled / visible, font, limit,
// selection, colours) and answer the messages the original sends with SendMessage (WM_SETTEXT / GETTEXT / GETTEXTLENGTH, WM_SETFONT / GETFONT,
// EM_SETSEL / GETSEL / LIMITTEXT / GETLIMITTEXT, WM_ENABLE) so that code written for the original works, but nothing is drawn: a game that wants
// a text box must draw it (CDrawText). Shoot.* and the other three games never create one.
#pragma once

#include "aaron/Widget.h"

#include <string>

namespace aaron {

class ControlWin : public Widget {
public:
    // Aaron ord 30 0x10006fb2: ControlWin(parent, exStyle, className, title, style, x, y, w, h, menu, param, widgetStyle).
    //   * Widget base: (parent, resId -1, cmdId = `menu` (HMENU: the control id), style = widgetStyle & 0xffffff8f);
    //   * (x, y) is the CENTRE of the control in scene coordinates: ToScreen, then minus (w / 2, h / 2);
    //   * `style` becomes WS_CHILD (| WS_DISABLED if the widget is disabled), WS_VISIBLE is cleared to 0x40000000 / 0x48000000;
    //   * widgetStyle & 0x8000 copies the parent window's font (WM_GETFONT -> WM_SETFONT);
    //   * style 0x10000 / 0x20000 (ES_CENTER / ES_RIGHT) set the widget style bits 0x100 / 0x200.
    // The "window" is not created when className is null / empty (CreateWindowEx failure): NativeHandle() == nullptr and Create() fails.
    ControlWin(WidgetParent parent, u32 exStyle, const char* className, const char* title, u32 style, int x, int y, int w, int h,
               uintptr_t menu, void* param, u32 widgetStyle);
    ~ControlWin() override;                                     // Aaron ord 69 0x1000715a (DestroyWindow, then ~Widget)
    u32 GetClassId() const override { return CID_CONTROLWIN; }  // Aaron ord 379 0x10007a60
    static void Nop235() {}                                     // Aaron ord 235 0x100071f5: the empty per-class virtual (slot 3)
    // Aaron ord 396 0x10007a70 (vtable slot 10): the control's own window handle (Widget::GetHWnd returns the scene's)
    void* GetHWnd() override { return handle_; }
    void* NativeHandle() const { return handle_; }
    // Aaron ord 467 0x1000770d (vtable slot 5, replaces Widget::Capture): EnableWindow(flag). QUIRK kept: Widget::EnableCount calls slot 5 with
    // 1 when it DISABLES the widget, so a disabled ControlWin is actually enabled by EnableCount(0) - the original behaves this way.
    void Capture(bool flag) override { EnableControl(flag); }
    void EnableControl(bool enable) { enabled_ = enable; }
    bool ControlEnabled() const { return enabled_; }
    // Aaron ord 486 0x1000772b (vtable slot 6): fails without a window; sub-classes it (state: `subclassed`) and runs Widget::Create.
    bool Create() override;
    // Aaron ord 683 0x100078d7 (vtable slot 9): hide: SW_HIDE then ShowChildren(0); show: ShowChildren(1) then SW_SHOW.
    void Show(bool show) override;
    bool ControlVisible() const { return visible_; }
    // Aaron ord 554 0x1000779e (vtable slot 24): PtInRect(client rect in scene coordinates (ord 219), point)
    bool HitTest(int x, int y) override;
    // Aaron ord 219 0x100071be: GetClientRect then FromScreen on both corners (QUIRK: the origin is subtracted from the size, too).
    RECT GetClientRectScene() const;
    // Aaron ord 347 0x1000721e: {right, bottom} of the client rect (= width, height). ord 284 0x10007202: width. ord 394 0x10007271: height.
    void GetClientSize(i32* out2) const;
    i32 GetClientWidth() const;
    i32 GetClientHeight() const;
    // Aaron ord 444 0x10007627: SetWindowPos(ToScreen(x, y), w, h, SWP_NOZORDER). Aaron ord 690 0x10007922: SetWindowPos(0, 0, w, h, NOMOVE | NOZORDER).
    void SetPos(int x, int y, int w, int h);
    void Resize(int w, int h);
    // Aaron ord 666 0x1000786b: SetWindowText. ord 358 0x1000724f: GetWindowText(buf, n) (truncates to n - 1 characters).
    void SetText(const char* text);
    void GetText(char* buf, int n) const;
    const std::string& Text() const { return text_; }
    // Aaron ord 637 0x100077fd: SetFont(face, height): a font from the scene's GDI cache (LOGFONT: lfHeight = height, face) -> WM_SETFONT.
    void SetFont(const char* face, int height);
    // Aaron ord 668 0x1000788e (Ghidra: "RealizedChore::Initialize", a false library match): SetTextColor(color, transparent): text colour +
    // background mode (1 TRANSPARENT when `transparent`, else OPAQUE) used by WM_CTLCOLOR*. ord 605 0x100078b7: SetBackColor(color): the brush.
    void SetTextColor(u32 color, int transparent);
    void SetBackColor(u32 color);
    // vtable slot 25 (0x1000767a): WM_CTLCOLOR* reply. Returns nonzero when a background colour was set (the original also returns the brush).
    int OnCtlColor(u32* textColor, int* bkMode, u32* backColor) const;
    // Aaron ord 540 0x10007778 / ord 591 0x100077d7: PostMessage / SendMessage to the control. Port: PostMessage is delivered at once.
    void PostMessage(u32 msg, uintptr_t wParam, intptr_t lParam) { SendMessage(msg, wParam, lParam); }
    virtual intptr_t SendMessage(u32 msg, uintptr_t wParam, intptr_t lParam);
    // The sub-class procedure (0x10006f50 -> 0x1000728d): destroy / focus / enable / set-cursor / key / mouse messages are routed to the scene's
    // FocusMgr, CursorMgr and PickMgr. Returns nonzero when handled.
    int PreProcess(u32 msg, uintptr_t wParam, intptr_t lParam);
    u32 WinStyle() const { return winStyle_; }
    const RECT& ScreenRect() const { return rect_; }
    bool IsSubclassed() const { return subclassed_; }
    bool HasParentFont() const { return parentFont_; }
    int FontHeight() const { return fontHeight_; }
    const std::string& FontFace() const { return fontFace_; }

protected:
    void* handle_ = nullptr;      // +0x40 (the port: a token)
    bool subclassed_ = false;     // +0x44 holds the old window procedure
    bool enabled_ = true;
    bool visible_ = false;
    bool parentFont_ = false;
    u32 winStyle_ = 0;
    u32 exStyle_ = 0;
    std::string class_, text_, fontFace_;
    int fontHeight_ = 0;
    RECT rect_;                   // window rectangle in "screen" (window) coordinates
    u32 textColor_ = 0, backColor_ = 0;   // +0x48 / +0x4c
    int transparent_ = 0;                 // +0x50
    bool textColorSet_ = false, backSet_ = false; // +0x54 / +0x58
    // EDIT state
    unsigned limit_ = 0x7ffe;     // default EDIT limit
    int selStart_ = 0, selEnd_ = 0;
};

// Aaron ord 31 0x1000794c: EditControl(parent, exStyle, title, style, x, y, w, h, menu, param, widgetStyle) = ControlWin with class "EDIT".
class EditControl : public ControlWin {
public:
    EditControl(WidgetParent parent, u32 exStyle, const char* title, u32 style, int x, int y, int w, int h, uintptr_t menu, void* param,
                u32 widgetStyle);
    // Aaron ord 669 0x1000799e: EM_LIMITTEXT. ord 589 0x100079be: EM_SETSEL.
    void SetLimit(unsigned n) { SendMessage(0xc5, n, 0); }
    void SetSel(int start, int end) { SendMessage(0xb1, uintptr_t(start), end); }
    unsigned Limit() const { return limit_; }
    int SelStart() const { return selStart_; }
    int SelEnd() const { return selEnd_; }
    intptr_t SendMessage(u32 msg, uintptr_t wParam, intptr_t lParam) override;
};

} // namespace aaron
