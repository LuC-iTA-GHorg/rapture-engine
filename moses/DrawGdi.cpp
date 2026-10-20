// moses.dll port - CDrawGdi / CDrawRect / CDrawPoly (moses.md sec. 4 "CDrawGdi"). Package 1.
// CDrawText lives in DrawText.cpp. GDI -> GdiRaster.
#include "moses/DrawGdi.h"
#include "moses/DrawView.h"
#include "moses/GdiRaster.h"
#include "moses/ObjCache.h"

namespace moses {

// ---------------------------------------------------------------------------------------------
// CDrawGdi
CDrawGdi::CDrawGdi(CDrawView* view, int level) : CDrawObj(view, level) {} // 0x10008bc0

// moses.dll 0x10008b60: rect widened by edge_width/2 (if > 1)
void CDrawGdi::get_bounds(RECT& r, u32 flip) {
    (void)flip;
    r = rect_;
    int w = edge_width_;
    if (w > 1) {
        int h = w / 2;
        r.right += h;
        r.left -= (w - h);
        r.bottom += h;
        r.top -= (w - h);
    }
}

// moses.dll 0x10008bf0 Compose. Order of the virtual calls: select_pen (+0x24), select_brush (+0x20),
// select_font (+0x28), display (+0x1c). The original clips to r with right/bottom +1 (IntersectClipRect is
// exclusive on the right); the port clips to r exactly: the extra row/column was outside the region being
// recomposed and therefore never visible.
void CDrawGdi::Compose(CDibSect* view, RECT& r) {
    GdiCtx dc;
    dc.surf = &view->Pixels();
    dc.viewportOrg = POINT{-view->Bounds().left, -view->Bounds().top};
    dc.clip = r;
    dc.bkColor = bk_;
    dc.bkOpaque = opaque_ != 0;
    // GDI defaults of a fresh DC: BLACK_PEN, WHITE_BRUSH
    dc.penStyle = 0; dc.penWidth = 1; dc.penColor = 0; dc.penNull = false;
    dc.brushStyle = 0; dc.brushColor = Rgb(255, 255, 255);
    void* oldPen = select_pen(&dc);
    void* oldBrush = select_brush(&dc);
    void* oldFont = select_font(&dc);
    display(view, &dc, r);
    // the original re-selects the objects returned by the three select_* calls before RestoreDC
    if (oldFont) gdi::SelectObject(&dc, static_cast<GdiObj*>(oldFont));
    if (oldBrush) gdi::SelectObject(&dc, static_cast<GdiObj*>(oldBrush));
    if (oldPen) gdi::SelectObject(&dc, static_cast<GdiObj*>(oldPen));
}

// ---------------------------------------------------------------------------------------------
// CDrawRect
CDrawRect::CDrawRect(CDrawView* view, int level) : CDrawGdi(view, level) {} // 0x10008e80

CDrawObj* CDrawRect::Copy() const { return new CDrawRect(*this); }

// moses.dll 0x10008ee0
int CDrawRect::Init(const RECT* rc, int ellipse) {
    rect_ = *rc;
    ellipse_ = ellipse;
    recalc_bounds();
    return CDrawObj::Init();
}

// moses.dll 0x10008f60
void CDrawRect::SetRect(const RECT* rc) {
    rect_ = *rc;
    recalc_bounds();
}

// moses.dll 0x10008f20: writes edge_width and LOGPEN.width.x; bounds recomputed, oldbounds = bounds (no redraw)
void CDrawRect::SetEdgeWidth(int w) {
    pen_.width.x = w;
    edge_width_ = w;
    recalc_bounds();
    p_.oldbounds = p_.bounds;
}

void* CDrawRect::select_brush(GdiCtx* dc) { // 0x10008e40
    if (brush_cache_) return brush_cache_->Select(dc, &brush_); // original: CGdiCache::Select(+0x3c, hdc, &LOGBRUSH)
    // no cache (port): apply the values directly
    dc->brushStyle = brush_.style;
    dc->brushColor = brush_.color;
    dc->brushHatch = brush_.hatch;
    return nullptr;
}

void* CDrawRect::select_pen(GdiCtx* dc) { // 0x10008e60
    if (pen_cache_) return pen_cache_->Select(dc, &pen_); // original: CGdiCache::Select(+0x28, hdc, &LOGPEN)
    dc->penStyle = pen_.style;
    dc->penWidth = u32(pen_.width.x);
    dc->penColor = pen_.color;
    dc->penNull = pen_.style == 5; // PS_NULL
    return nullptr;
}

// moses.dll 0x10008d20 Rectangle / RoundRect / Ellipse, with the optional iris hole (ExcludeClipRect)
void CDrawRect::display(CDibSect* view, GdiCtx* dc, RECT& r) {
    (void)view; (void)r;
    int left = p_.bounds.left, top = p_.bounds.top, right = p_.bounds.right, bottom = p_.bounds.bottom;
    if (pen_.style == 5) { // NULL pen: GDI leaves out the right/bottom edge
        right += 1;
        bottom += 1;
    }
    int w = edge_width_;
    if (w > 1) {
        int h = w / 2;
        right -= h;
        bottom -= h;
        left += (w - h);
        top += (w - h);
    }
    if (has_hole_) {
        dc->exclude = RECT{hole_.left + p_.pos.x, hole_.top + p_.pos.y, hole_.right + 1 + p_.pos.x, hole_.bottom + 1 + p_.pos.y};
        dc->hasExclude = true;
    }
    if (ellipse_ == 0) {
        if (rw_ == 0 || rh_ == 0) gdi::Rectangle(*dc, left, top, right, bottom);
        else gdi::RoundRect(*dc, left, top, right, bottom, rw_, rh_);
    } else {
        gdi::Ellipse(*dc, left, top, right, bottom);
    }
    dc->hasExclude = false;
}

// ---------------------------------------------------------------------------------------------
// CDrawPoly
CDrawPoly::CDrawPoly(CDrawView* view, int level) : CDrawGdi(view, level) {} // 0x10008990

// moses.dll 0x10008a90
CDrawPoly::~CDrawPoly() { delete[] pts_; }

// Deep copy (the original object was copied with a shallow memcpy of the pointer - CAnimPoly::Copy 0x10001600).
CDrawObj* CDrawPoly::Copy() const {
    CDrawPoly* c = new CDrawPoly(p_.view, p_.level);
    CDrawObj* hi = c->p_.prev; CDrawObj* lo = c->p_.next; // keep the list position given by the constructor
    c->p_ = p_;
    c->p_.prev = hi; c->p_.next = lo;
    c->bk_ = bk_; c->opaque_ = opaque_; c->edge_width_ = edge_width_; c->rect_ = rect_;
    c->pen_ = pen_; c->pen_cache_ = pen_cache_; c->closed_ = closed_;
    c->AddPoints(pts_, npts_);
    c->rect_ = rect_;
    return c;
}

// moses.dll 0x10008a50
int CDrawPoly::Init(POINT* pts, u32 n, int closed) {
    closed_ = closed;
    if (CDrawObj::Init() && AddPoints(pts, n)) return 1;
    return 0;
}

// moses.dll 0x100089f0 (a line)
int CDrawPoly::Init(POINT* p0, POINT* p1) {
    POINT pts[2] = {*p0, *p1};
    if (CDrawObj::Init() && AddPoints(pts, 2)) return 1;
    return 0;
}

// moses.dll 0x10008ac0 AddPoints: rect_ = bounding box of the points ([min, max+1))
int CDrawPoly::AddPoints(POINT* pts, u32 n) {
    if (!pts || n == 0) return 1;
    POINT* np = new POINT[npts_ + n];
    for (u32 i = 0; i < npts_; ++i) np[i] = pts_[i];
    for (u32 i = 0; i < n; ++i) {
        const POINT& p = pts[i];
        if (npts_ + i == 0) {
            rect_.left = p.x; rect_.right = p.x + 1;
            rect_.top = p.y; rect_.bottom = p.y + 1;
        } else {
            if (p.x < rect_.left) rect_.left = p.x; else if (rect_.right <= p.x) rect_.right = p.x + 1;
            if (p.y < rect_.top) rect_.top = p.y; else if (rect_.bottom <= p.y) rect_.bottom = p.y + 1;
        }
        np[npts_ + i] = p;
    }
    delete[] pts_;
    pts_ = np;
    npts_ += n;
    // TODO(pkg1): the original does not recalc the bounds here (nothing in the game uses CDrawPoly);
    // the port does so that the object is drawable.
    recalc_bounds();
    return 1;
}

void* CDrawPoly::select_pen(GdiCtx* dc) { // moses.dll 0x10001980 (CGdiCache::Select(+0x28, hdc, +0x2c))
    if (pen_cache_) return pen_cache_->Select(dc, &pen_);
    dc->penStyle = pen_.style;
    dc->penWidth = u32(pen_.width.x);
    dc->penColor = pen_.color;
    dc->penNull = pen_.style == 5;
    return nullptr;
}

// moses.dll 0x10008920: OffsetViewportOrg(pos); Polyline (n < 2 or open) / Polygon
void CDrawPoly::display(CDibSect* view, GdiCtx* dc, RECT& r) {
    (void)view; (void)r;
    std::vector<POINT> tmp(npts_);
    for (u32 i = 0; i < npts_; ++i) tmp[i] = POINT{pts_[i].x + p_.pos.x, pts_[i].y + p_.pos.y};
    if (npts_ < 2 || closed_ == 0) gdi::Polyline(*dc, tmp.data(), int(npts_));
    else gdi::Polygon(*dc, tmp.data(), int(npts_));
}

} // namespace moses
