// CDrawView: the view = surface + level-ordered list of CDrawObj (moses.md sec. 3).
//
// PRESENTATION (deliberate difference #1 - see engine/README.md)
//   Original: every change composes the dirty area (Display) AND immediately BitBlt()s it on the
//   window DC; there is no double buffer or vsync.
//   Port: Display(rect) still composes IMMEDIATELY into the software surface (same maths, same
//   order of operations), but instead of a BitBlt it only unions the rect into a dirty
//   rectangle and raises a flag. The main loop calls PresentTo(framebuffer) ONCE per iteration:
//   it copies the dirty part of the surface into the platform framebuffer and the caller then
//   calls ::Window::Present(). No SDL call is made from inside Display/Update.
#pragma once

#include "moses/DibData.h"
#include "moses/DrawObj.h"

namespace moses {

// ResProc: moses.md sec. 3 (SetResProc / CallResProc 0x100070b0).
// mode 1 = load: returns a CDibObj* (or nullptr) and may rewrite *id; mode 0 = release.
// Aaron implements it (Scene::ResProc, resource ids -> CResMgr).
using RESPROC = void* (*)(void* data, CDrawObj* obj, int mode, u32* id);

// moses.md sec. 3: VIEWDATA (0xa0). Public so friends can read it; treat as private elsewhere.
struct VIEWDATA {
    CDrawObj* tail = nullptr;        // +00 LOWEST level (drawn first)
    CDrawObj* head = nullptr;        // +04 HIGHEST level (hit tests start here)
    CDrawObj* cur = nullptr;         // +08 object being processed (CAnimView::QCall re-entrancy guard)
    CDibData* background = nullptr;  // +0c  nullptr = flat surface_color, (CDibData*)-1 = no background, else a picture
    bool noBackground = false;       //      port: replaces the (CDibData*)-1 sentinel
    void* hwnd = nullptr;            // +10 (ignored)
    void* hdc = nullptr;             // +14 (ignored)
    RESPROC resproc = nullptr;       // +18
    void* resproc_data = nullptr;    // +1c
    POINT dcorg;                     // +20 SetDCOrigin (ignored by the port)
    RECT bg_bounds;                  // +28
    RECT display_box;                // +38 visible rect = clipbox intersected with surface (and bg)
    RECT scroll_vis;                 // +48
    RECT surface;                    // +58 (-w/2, -h/2, -w/2+w, -h/2+h)
    RECT clipbox;                    // +68
    RECT scrollbox;                  // +78
    int min_compose_level = LEVEL_MIN; // +88
    u32 surface_color = 0;           // +8c raw colour (RGB555) used to fill when background == nullptr
    int nobjects = 0;                // +90
    int has_clip = 0;                // +94
    int has_scroll = 0;              // +98
    int redraw = 1;                  // +9c 0 = redraw suspended
};

class CDrawView : public CDibSect {
public:
    CDrawView();                      // moses.dll CDrawView::CDrawView() 0x10007000
    ~CDrawView() override;            // moses.dll CDrawView::~CDrawView

    // Init(hwnd, hdc, w, h, bpp): CDibSect::Init + redraw = 1 + Resize. hwnd/hdc are accepted and ignored.
    // Aaron calls it with the configured size (640x480) and bpp 16. Returns 1.
    int Init(void* hwnd, void* hdc, u32 w, u32 h, u32 bpp);       // moses.dll 0x10007330
    int Resize(u32 w, u32 h);                                      // moses.dll 0x100075e0
    // CDibSect::ChangeColorDepth is also reachable as CDrawView::ChangeColorDepth (Shoot import): inherited.

    // ---- composition ---------------------------------------------------------------------
    // moses.dll 0x100070f0. See moses.md sec. 3 for the pseudo-C. Port: composes into the surface,
    // unions `r` into the dirty rect, sets the dirty flag; no blit to a window.
    void Display(RECT& r);
    void Paint(void* hdc = nullptr);                               // moses.dll 0x10007470 (WM_PAINT): Display(display_box)
    void Scroll(int dx, int dy);                                   // moses.dll 0x100076f0
    void SetRedraw(int on, int repaint = 0);                       // moses.dll 0x100079e0 (on=1,repaint=1 -> redraw everything)
    void SetClipBox(const RECT* r);                                // moses.dll 0x10007890  NULL = no clip
    void SetScrollBox(const RECT* r);                              // moses.dll 0x10007a50
    const RECT* GetClipBox() const { return &v_.clipbox; }         // moses.dll 0x100072b0
    const RECT* GetScrollBox() const { return &v_.scrollbox; }     // moses.dll 0x10007320
    void GetDisplayBox(RECT& r) const { r = v_.display_box; }      // moses.dll 0x100072d0
    void* GetDC() const { return nullptr; }                        // moses.dll 0x100072c0 (no DC in the port)
    void SetDCOrigin(const POINT& p) { v_.dcorg = p; }             // moses.dll 0x10007910 (ignored)
    void SetBackground(CDibData* bg);                              // moses.dll 0x10007840: nullptr = flat colour, (CDibData*)-1 = none
    void SetSurfaceColor(u32 rawColor);                            // moses.dll 0x10007ad0  raw RGB555 (0 = black)
    int SetMinComposeLevel(int level);                             // moses.dll 0x10007970 returns the previous value; objects with level < min are NOT composed
    void SetPalette(const CDibPal* pal) { CDibSect::SetPalette(pal); } // moses.dll 0x10007990 (WP-5: forwards to CDibSect::SetPalette)
    u32 Profile(u32 ms);                                           // moses.dll 0x10007500 (WP-5): redraws the display box for `ms` ms, returns the frame count
    int RealizePalette() { return 0; }                             // moses.dll CDrawView::RealizePalette: no-op

    // ---- objects ------------------------------------------------------------------------------
    // Re-links `obj` (list ordered by DESCENDING level; at equal level the LAST inserted goes on top).
    void SetLevel(CDrawObj* obj, int level);                       // moses.dll 0x10007930 (link_object 0x10006e80)
    void SetResProc(RESPROC proc, void* data);                     // moses.dll 0x10007a30
    void* CallResProc(CDrawObj* obj, int mode, u32* id);           // moses.dll 0x100070b0
    // Top-most object picked at (x,y) (obj->IsPicked), from the head (highest level); honours scroll_vis.
    CDrawObj* IsPicked(int x, int y);                              // moses.dll 0x100073f0
    CDrawObj* PtInObject(int x, int y);                            // moses.dll 0x10007540 (CDrawObj::PtInObject, ignores pickmode)
    CDrawObj* IsContacting(CDrawObj* obj);                         // moses.dll 0x100073b0
    int ObjectCount() const { return v_.nobjects; }

    // ---- port additions: dirty tracking & presentation ----------------------------------------
    bool IsDirty() const { return dirty_; }
    const RECT& DirtyRect() const { return dirtyRect_; }           // union since the last PresentTo, view coordinates
    void MarkAllDirty();
    // Copies the dirty rectangle (or everything if `all`) of the surface into `fb` (640x480 ARGB,
    // top-left = surface top-left) and clears the dirty flag. Returns true if anything was copied.
    // The caller then calls ::Window::Present().
    bool PresentTo(::Surface& fb, bool all = false);

    // Used by CDrawObj / CAnimView (friends)
    VIEWDATA& ViewData() { return v_; }
    const VIEWDATA& ViewData() const { return v_; }

protected:
    void link_object(CDrawObj* obj);        // moses.dll 0x10006e80
    void unlink_object(CDrawObj* obj);
    void add_dirty(const RECT& r);

    VIEWDATA v_;
    bool dirty_ = false;
    RECT dirtyRect_;

    friend class CDrawObj;
};

// CDrawSys (global `DrawSys`, moses.dll 0x1001c70c): video mode switching and debug rectangles.
// Port: the window is already 640x480 and managed by platform/Window; everything is a no-op that
// reports success, so `Application::SetDisplayMode` transcribes 1:1.
class CDrawSys {
public:
    CDrawSys();      // moses.dll 0x10009360: reads rapture.ini [drawlib] ComposeRects / DisplayRects / NoDirectDraw (default 0) as shorts
    void ReloadIni(); // port: re-reads rapture.ini (tests)
    // 0x10009420: the original returns 0 at once when NoDirectDraw != 0 (no DirectDraw display-mode change) and otherwise switches the
    // mode. The port has no display-mode change at all (the window is fixed 640x480), so NoDirectDraw is stored but the call always
    // reports success (returning 0 would make Aaron fail with 0x8000000a). Documented in docs/RAPTURE_INI.md.
    int ChangeDisplayMode(void* hwnd, u32 w, u32 h, u32 bpp) { (void)hwnd; (void)w; (void)h; (void)bpp; return 1; } // 0x10009420
    int NoDirectDraw() const { return noDirectDraw_; } // CDrawSys+4
    void SetComposeRects(int on) { composeRects_ = on; }   // debug (rapture.ini [drawlib] ComposeRects)
    void SetDisplayRects(int on) { displayRects_ = on; }
    int ShowComposeRects() const { return composeRects_; }
    int ShowDisplayRects() const { return displayRects_; }

private:
    int noDirectDraw_ = 0, composeRects_ = 0, displayRects_ = 0; // +4, +8, +0xc (the last two are dead flags, also in the original)
};
CDrawSys& DrawSys(); // moses.dll `DrawSys` accessor

} // namespace moses
