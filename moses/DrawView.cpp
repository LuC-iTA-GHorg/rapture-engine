// moses.dll port - CDrawView / CDrawSys (moses.md sec. 3). Package 1.
#include "moses/DrawView.h"
#include "moses/DebugOverlay.h"
#include "moses/Ini.h"

namespace moses {

// moses.dll 0x10007000. (The original zeroes VIEWDATA; redraw becomes 1 in Init.)
CDrawView::CDrawView() { v_.redraw = 0; }

// moses.dll 0x10007050: unlink every object (the objects survive, detached from the view)
CDrawView::~CDrawView() {
    while (v_.tail) {
        CDrawObj* o = v_.tail;
        unlink_object(o);
        o->p_.view = nullptr; // port: avoid a dangling view pointer in objects destroyed later
    }
}

// moses.dll 0x10007330
int CDrawView::Init(void* hwnd, void* hdc, u32 w, u32 h, u32 bpp) {
    (void)DrawSys(); // port: makes sure rapture.ini was read (CDrawSys ctor) before the first frame
    if (!CDibSect::Init(hdc, w, h, bpp)) return 0;
    v_.hwnd = hwnd;
    v_.hdc = hdc;
    v_.redraw = 1;
    return Resize(w, h) ? 1 : 0;
}

// moses.dll 0x100075e0
int CDrawView::Resize(u32 w, u32 h) {
    if (w == 0 || h == 0) return 0;
    if (!CDibSect::Resize(w, h)) return 0;
    v_.surface.left = -(int(w) / 2);
    v_.surface.top = -(int(h) / 2);
    v_.surface.right = v_.surface.left + int(w);
    v_.surface.bottom = v_.surface.top + int(h);
    if (!v_.has_clip) v_.display_box = v_.surface;
    else IntersectRect(&v_.display_box, &v_.clipbox, &v_.surface);
    if (v_.has_scroll) IntersectRect(&v_.scroll_vis, &v_.scrollbox, &v_.surface);
    else v_.scroll_vis = v_.surface;
    MarkAllDirty();
    return 1;
}

// moses.dll 0x10006e80 link_object. Port naming: p_.prev = towards the HEAD (higher level), p_.next = towards
// the TAIL (lower level) - the header comment had them swapped (see CHANGES.md).
void CDrawView::link_object(CDrawObj* obj) {
    int level = obj->p_.level;
    ++v_.nobjects;
    obj->p_.prev = obj->p_.next = nullptr;
    if (!v_.head) {
        v_.head = v_.tail = obj;
        return;
    }
    CDrawObj* above = nullptr;      // last object with level > new level
    CDrawObj* found = v_.head;      // first object with level <= new level
    while (found && found->p_.level > level) {
        above = found;
        found = found->p_.next;
    }
    if (!above) { // new head (also above equal levels: the LAST inserted goes on top)
        obj->p_.next = v_.head;
        v_.head->p_.prev = obj;
        v_.head = obj;
    } else if (found) {
        obj->p_.next = found;
        obj->p_.prev = above;
        above->p_.next = obj;
        found->p_.prev = obj;
    } else { // new tail
        obj->p_.prev = v_.tail;
        v_.tail->p_.next = obj;
        v_.tail = obj;
    }
}

// moses.dll 0x10006f60
void CDrawView::unlink_object(CDrawObj* obj) {
    --v_.nobjects;
    CDrawObj* hi = obj->p_.prev;
    CDrawObj* lo = obj->p_.next;
    obj->p_.prev = obj->p_.next = nullptr;
    if (!hi) v_.head = lo; else hi->p_.next = lo;
    if (!lo) v_.tail = hi; else lo->p_.prev = hi;
    if (obj == v_.cur) v_.cur = nullptr;
}

// moses.dll 0x10007930
void CDrawView::SetLevel(CDrawObj* obj, int level) {
    CDrawObj* cur = v_.cur;
    unlink_object(obj);
    obj->p_.level = level;
    link_object(obj);
    v_.cur = cur;
}

// moses.dll 0x10007a30 / 0x100070b0
void CDrawView::SetResProc(RESPROC proc, void* data) { v_.resproc = proc; v_.resproc_data = data; }
void* CDrawView::CallResProc(CDrawObj* obj, int mode, u32* id) {
    if (v_.resproc) return v_.resproc(v_.resproc_data, obj, mode, id);
    return nullptr;
}

void CDrawView::add_dirty(const RECT& r) {
    if (!dirty_) { dirtyRect_ = r; dirty_ = true; }
    else { RECT t; UnionRect(&t, &dirtyRect_, &r); dirtyRect_ = t; }
}

void CDrawView::MarkAllDirty() { dirty_ = true; dirtyRect_ = v_.surface; }

// moses.dll 0x100070f0 Display: the compositor (moses.md sec. 3).
// Port: composes into the surface and records the dirty rect instead of the final BitBlt.
void CDrawView::Display(RECT& r) {
    if (!v_.redraw || !surf_) return;
    RECT d;
    if (!IntersectRect(&d, &r, &v_.display_box)) return;
    if (!v_.background && !v_.noBackground) {
        DibSet(u32(d.left - v_.surface.left), u32(d.top - v_.surface.top), u32(d.right - d.left), u32(d.bottom - d.top),
               v_.surface_color);
    } else if (v_.background) {
        if (!IntersectRect(&d, &d, &v_.bg_bounds)) return;
        v_.background->DibBlt(this, d.left, d.top, d, 0, DROP_OPAQUE);
    }
    for (CDrawObj* o = v_.tail; o; o = o->p_.prev) { // lowest level first
        RECT c;
        if (o->p_.visible && o->p_.level >= v_.min_compose_level && IntersectRect(&c, &d, &o->p_.bounds) &&
            (!o->p_.has_clip || IntersectRect(&c, &c, &o->p_.clip)) &&
            (!o->p_.scrollable || IntersectRect(&c, &c, &v_.scroll_vis)))
            { DebugOverlay::NoteCompose(c); o->Compose(this, c); } // port debug overlay: see DebugOverlay.h
    }
    DebugOverlay::NoteDisplay(d);
    add_dirty(d);
}

// moses.dll 0x10007470
void CDrawView::Paint(void* hdc) { (void)hdc; Display(v_.display_box); }

// moses.dll 0x100076f0. The original ScrollDC()s the window and redraws only the uncovered strips; the
// port has no window DC, so the whole scroll area is recomposed after the scrollable objects moved.
void CDrawView::Scroll(int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    for (CDrawObj* o = v_.tail; o; o = o->p_.prev) {
        if (o->p_.scrollable) { // CDrawObj::scroll_offset 0x10007eb0
            OffsetRect(&o->p_.bounds, dx, dy);
            OffsetRect(&o->p_.oldbounds, dx, dy);
            o->p_.pos.x += dx;
            o->p_.pos.y += dy;
        }
    }
    RECT r = v_.scroll_vis;
    Display(r);
}

// moses.dll 0x10007840 (nullptr = flat colour, (CDibData*)-1 = none)
void CDrawView::SetBackground(CDibData* bg) {
    if (bg == reinterpret_cast<CDibData*>(intptr_t(-1))) {
        if (v_.noBackground) return;
        v_.noBackground = true;
        v_.background = nullptr;
        return;
    }
    if (!v_.noBackground && v_.background == bg) return;
    v_.noBackground = false;
    v_.background = bg;
    if (bg) {
        bg->Expand();
        v_.bg_bounds = bg->Bounds();
    }
}

// moses.dll 0x10007890
void CDrawView::SetClipBox(const RECT* r) {
    v_.has_clip = r != nullptr;
    v_.clipbox = r ? *r : v_.surface;
    IntersectRect(&v_.display_box, &v_.clipbox, &v_.surface);
}

// moses.dll 0x10007a50
void CDrawView::SetScrollBox(const RECT* r) {
    v_.has_scroll = r != nullptr;
    v_.scrollbox = r ? *r : v_.surface;
    IntersectRect(&v_.scroll_vis, &v_.scrollbox, &v_.surface);
}

void CDrawView::SetSurfaceColor(u32 rawColor) { v_.surface_color = rawColor; } // 0x10007ad0

int CDrawView::SetMinComposeLevel(int level) { // 0x10007970
    int old = v_.min_compose_level;
    v_.min_compose_level = level;
    return old;
}

// moses.dll 0x100079e0
void CDrawView::SetRedraw(int on, int repaint) {
    if (on == 0) { if (v_.redraw == 0) return; }
    else if (v_.redraw != 0) return;
    v_.redraw = on;
    if (on != 0 && repaint != 0) Display(v_.display_box);
}

// moses.dll 0x100073f0
CDrawObj* CDrawView::IsPicked(int x, int y) {
    if (v_.display_box.left <= x && x < v_.display_box.right && v_.display_box.top <= y && y < v_.display_box.bottom) {
        for (CDrawObj* o = v_.head; o; o = o->p_.next) {
            if ((!o->p_.scrollable ||
                 (v_.scroll_vis.left <= x && x < v_.scroll_vis.right && v_.scroll_vis.top <= y && y < v_.scroll_vis.bottom)) &&
                o->IsPicked(x, y))
                return o;
        }
    }
    return nullptr;
}

// moses.dll 0x10007540
CDrawObj* CDrawView::PtInObject(int x, int y) {
    if (v_.display_box.left <= x && x < v_.display_box.right && v_.display_box.top <= y && y < v_.display_box.bottom) {
        for (CDrawObj* o = v_.head; o; o = o->p_.next)
            if (o->PtInObject(x, y)) return o;
    }
    return nullptr;
}

// moses.dll 0x100073b0
CDrawObj* CDrawView::IsContacting(CDrawObj* obj) {
    if (obj) {
        for (CDrawObj* o = v_.head; o; o = o->p_.next)
            if (o->IsContacting(obj)) return o;
    }
    return nullptr;
}

// Port: copy the dirty rectangle into the 640x480 framebuffer (same origin as the surface).
bool CDrawView::PresentTo(::Surface& fb, bool all) {
    if (!surf_) return false;
    if (DebugOverlay::Enabled()) all = true; // overlay lines are not part of the surface: repaint the whole frame so they do not stay behind
    if (!all && !dirty_) return false;
    RECT r = all ? v_.surface : dirtyRect_;
    IntersectRect(&r, &r, &v_.surface);
    dirty_ = false;
    dirtyRect_ = RECT{};
    if (IsRectEmpty(&r)) return false;
    int sx0 = r.left - v_.surface.left, sy0 = r.top - v_.surface.top;
    int w = r.right - r.left, h = r.bottom - r.top;
    for (int y = 0; y < h; ++y) {
        int fy = sy0 + y;
        if (fy < 0 || fy >= fb.Height()) continue;
        for (int x = 0; x < w; ++x) {
            int fx = sx0 + x;
            if (fx < 0 || fx >= fb.Width()) continue;
            fb.Row(fy)[fx] = surf_->Row(sy0 + y)[sx0 + x] | 0xff000000u; // framebuffer is opaque
        }
    }
    DebugOverlay::Draw(fb, v_.surface);
    return true;
}

// moses.dll 0x10009360 CDrawSys::CDrawSys: IniGetInt results are stored as shorts (sign-extended to int)
CDrawSys::CDrawSys() { ReloadIni(); }
void CDrawSys::ReloadIni() {
    composeRects_ = int(i16(IniGetInt("drawlib", "ComposeRects", 0)));
    displayRects_ = int(i16(IniGetInt("drawlib", "DisplayRects", 0)));
    noDirectDraw_ = int(i16(IniGetInt("drawlib", "NoDirectDraw", 0)));
    DebugOverlay::LoadIni(); // port: [port] DebugRects=1 turns the overlay on
}

// moses.dll `DrawSys` @0x1001c70c
CDrawSys& DrawSys() {
    static CDrawSys sys;
    return sys;
}

} // namespace moses
