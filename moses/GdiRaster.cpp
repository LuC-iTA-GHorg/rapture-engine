// Software replacement of the GDI primitives used by moses.dll (see GdiRaster.h). Package 1.
#include "moses/GdiRaster.h"

#include <cmath>
#include <vector>

namespace moses {
namespace gdi {

void PutPixel(GdiCtx& dc, int x, int y, uint32_t argb) {
    if (!dc.surf) return;
    if (x < dc.clip.left || x >= dc.clip.right || y < dc.clip.top || y >= dc.clip.bottom) return;
    if (dc.hasExclude && x >= dc.exclude.left && x < dc.exclude.right && y >= dc.exclude.top && y < dc.exclude.bottom)
        return;
    int sx = x + dc.viewportOrg.x, sy = y + dc.viewportOrg.y;
    if (sx < 0 || sy < 0 || sx >= dc.surf->Width() || sy >= dc.surf->Height()) return;
    dc.surf->Row(sy)[sx] = argb;
}

namespace {

inline int PenW(const GdiCtx& dc) { return dc.penWidth < 1 ? 1 : int(dc.penWidth); }

// Generic closed shape: `outer(x,y)` / `inner(x,y)` predicates in logical coordinates.
template <class OUT, class IN>
void PaintShape(GdiCtx& dc, const RECT& bb, OUT outer, IN inner, bool hasPen) {
    uint32_t pen = ToArgb(dc.penColor), brush = ToArgb(dc.brushColor);
    bool hasBrush = dc.brushStyle != 1; // BS_NULL = hollow
    for (int y = bb.top; y < bb.bottom; ++y)
        for (int x = bb.left; x < bb.right; ++x) {
            if (!outer(x, y)) continue;
            if (inner(x, y)) {
                if (hasBrush) PutPixel(dc, x, y, brush);
            } else if (hasPen) {
                PutPixel(dc, x, y, pen);
            }
        }
}

// Outer box for a shape drawn with Rectangle(l,t,r,b): see the comments in CDrawRect::display (0x10008d20).
RECT OuterBox(const GdiCtx& dc, int l, int t, int r, int b, bool& hasPen) {
    int w = PenW(dc);
    hasPen = !dc.penNull;
    RECT o{l, t, r, b};
    if (dc.penNull) { o.right = r - 1; o.bottom = b - 1; } // NULL pen: right/bottom edge not painted
    else if (w > 1) { o.left = l - (w - w / 2); o.top = t - (w - w / 2); o.right = r + w / 2; o.bottom = b + w / 2; }
    return o;
}

inline RECT Shrink(const RECT& o, int n) { return RECT{o.left + n, o.top + n, o.right - n, o.bottom - n}; }

inline bool InRect(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

// Pixel centre inside ellipse inscribed in rect e (half-open).
inline bool InEllipse(const RECT& e, int x, int y) {
    double a = (e.right - e.left) / 2.0, b = (e.bottom - e.top) / 2.0;
    if (a <= 0 || b <= 0) return false;
    double dx = (x + 0.5 - (e.left + a)) / a, dy = (y + 0.5 - (e.top + b)) / b;
    return dx * dx + dy * dy <= 1.0;
}

inline bool InRound(const RECT& e, int rw, int rh, int x, int y) {
    if (!InRect(e, x, y)) return false;
    double a = rw / 2.0, b = rh / 2.0;
    if (a <= 0 || b <= 0) return true;
    double cx, cy;
    if (x + 0.5 < e.left + a) cx = e.left + a; else if (x + 0.5 > e.right - a) cx = e.right - a; else return true;
    if (y + 0.5 < e.top + b) cy = e.top + b; else if (y + 0.5 > e.bottom - b) cy = e.bottom - b; else return true;
    double dx = (x + 0.5 - cx) / a, dy = (y + 0.5 - cy) / b;
    return dx * dx + dy * dy <= 1.0;
}

void StampLine(GdiCtx& dc, int x0, int y0, int x1, int y1, int w, uint32_t argb) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        for (int j = 0; j < w; ++j)
            for (int i = 0; i < w; ++i) PutPixel(dc, x0 - w / 2 + i, y0 - w / 2 + j, argb);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

} // namespace

void Rectangle(GdiCtx& dc, int l, int t, int r, int b) {
    bool hasPen;
    RECT o = OuterBox(dc, l, t, r, b, hasPen);
    RECT in = hasPen ? Shrink(o, PenW(dc)) : o;
    PaintShape(dc, o, [&](int, int) { return true; }, [&](int x, int y) { return InRect(in, x, y); }, hasPen);
}

void RoundRect(GdiCtx& dc, int l, int t, int r, int b, int rw, int rh) {
    bool hasPen;
    RECT o = OuterBox(dc, l, t, r, b, hasPen);
    int w = hasPen ? PenW(dc) : 0;
    RECT in = Shrink(o, w);
    PaintShape(dc, o, [&](int x, int y) { return InRound(o, rw, rh, x, y); },
               [&](int x, int y) { return InRound(in, rw > 2 * w ? rw - 2 * w : 0, rh > 2 * w ? rh - 2 * w : 0, x, y); }, hasPen);
}

void Ellipse(GdiCtx& dc, int l, int t, int r, int b) {
    bool hasPen;
    RECT o = OuterBox(dc, l, t, r, b, hasPen);
    RECT in = hasPen ? Shrink(o, PenW(dc)) : o;
    PaintShape(dc, o, [&](int x, int y) { return InEllipse(o, x, y); }, [&](int x, int y) { return InEllipse(in, x, y); }, hasPen);
}

void Polyline(GdiCtx& dc, const POINT* pts, int n) {
    if (dc.penNull || n < 1) return;
    uint32_t pen = ToArgb(dc.penColor);
    int w = PenW(dc);
    if (n == 1) { PutPixel(dc, pts[0].x, pts[0].y, pen); return; }
    for (int i = 0; i + 1 < n; ++i) StampLine(dc, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, w, pen);
}

void Polygon(GdiCtx& dc, const POINT* pts, int n) {
    if (n < 2) return;
    if (dc.brushStyle != 1 && n >= 3) { // ALTERNATE fill, sampled at pixel centres
        int ymin = pts[0].y, ymax = pts[0].y;
        for (int i = 1; i < n; ++i) { ymin = std::min(ymin, int(pts[i].y)); ymax = std::max(ymax, int(pts[i].y)); }
        uint32_t brush = ToArgb(dc.brushColor);
        std::vector<double> xs;
        for (int y = ymin; y < ymax; ++y) {
            xs.clear();
            double cy = y + 0.5;
            for (int i = 0; i < n; ++i) {
                const POINT& a = pts[i];
                const POINT& b = pts[(i + 1) % n];
                if ((a.y <= cy && b.y > cy) || (b.y <= cy && a.y > cy))
                    xs.push_back(a.x + (cy - a.y) * (double(b.x) - a.x) / (double(b.y) - a.y));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2)
                for (int x = int(std::ceil(xs[k] - 0.5)); x < int(std::ceil(xs[k + 1] - 0.5)); ++x) PutPixel(dc, x, y, brush);
        }
    }
    if (!dc.penNull) {
        uint32_t pen = ToArgb(dc.penColor);
        int w = PenW(dc);
        for (int i = 0; i < n; ++i) {
            const POINT& a = pts[i];
            const POINT& b = pts[(i + 1) % n];
            StampLine(dc, a.x, a.y, b.x, b.y, w, pen);
        }
    }
}

#ifndef _WIN32
// Non-Windows text: the portable stb_truetype backend (GdiTextTtf.cpp).  (Windows uses the real GDI, GdiRaster_win.cpp.)
int TextRect(const LOGFONT_LITE& f, const char* s, int len, RECT& rc, u32 flags) { return ttf::TextRect(f, s, len, rc, flags); }
void TextDraw(GdiCtx& dc, const LOGFONT_LITE& f, const char* s, int len, const RECT& rc, u32 flags) { ttf::TextDraw(dc, f, s, len, rc, flags); }
int TextAscent(const LOGFONT_LITE& f) { return ttf::TextAscent(f); }
#endif

} // namespace gdi
} // namespace moses
