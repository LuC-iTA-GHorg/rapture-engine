// Software replacement of the GDI primitives used by moses.dll (see GdiRaster.h). Package 1.
#include "moses/GdiRaster.h"
#include "platform/Config.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace moses {
namespace gdi {

namespace {

// Clip rectangle, ExcludeClipRect rectangle and the optional clip mask (EMF clip regions).
inline bool Visible(const GdiCtx& dc, int x, int y) {
    if (x < dc.clip.left || x >= dc.clip.right || y < dc.clip.top || y >= dc.clip.bottom) return false;
    if (dc.hasExclude && x >= dc.exclude.left && x < dc.exclude.right && y >= dc.exclude.top && y < dc.exclude.bottom) return false;
    if (dc.clipMask) {
        if (x < dc.maskRect.left || x >= dc.maskRect.right || y < dc.maskRect.top || y >= dc.maskRect.bottom) return false;
        size_t i = size_t(y - dc.maskRect.top) * size_t(dc.maskRect.right - dc.maskRect.left) + size_t(x - dc.maskRect.left);
        if (!dc.clipMask[i]) return false;
    }
    return true;
}

// Binary raster operations R2_* (P = pen/brush colour, D = destination), on the 24 colour bits.
inline uint32_t Rop2(int rop, uint32_t P, uint32_t D) {
    uint32_t r;
    switch (rop) {
    case 1: r = 0; break;                  // R2_BLACK
    case 2: r = ~(P | D); break;           // R2_NOTMERGEPEN
    case 3: r = ~P & D; break;             // R2_MASKNOTPEN
    case 4: r = ~P; break;                 // R2_NOTCOPYPEN
    case 5: r = P & ~D; break;             // R2_MASKPENNOT
    case 6: r = ~D; break;                 // R2_NOT
    case 7: r = P ^ D; break;              // R2_XORPEN
    case 8: r = ~(P & D); break;           // R2_NOTMASKPEN
    case 9: r = P & D; break;              // R2_MASKPEN
    case 10: r = ~(P ^ D); break;          // R2_NOTXORPEN
    case 11: r = D; break;                 // R2_NOP
    case 12: r = ~P | D; break;            // R2_MERGENOTPEN
    case 14: r = P | ~D; break;            // R2_MERGEPENNOT
    case 15: r = P | D; break;             // R2_MERGEPEN
    case 16: r = 0xffffffffu; break;       // R2_WHITE
    default: r = P; break;                 // R2_COPYPEN (13)
    }
    return 0xff000000u | (r & 0x00ffffffu);
}

// HS_* hatch pattern of an 8x8 cell, tested on surface pixel coordinates (like GDI, the pattern is aligned to the device).
inline bool HatchOn(int style, int x, int y) {
    int mx = x & 7, my = y & 7;
    switch (style) {
    case 0: return my == 4;                          // HS_HORIZONTAL
    case 1: return mx == 4;                          // HS_VERTICAL
    case 2: return mx == my;                         // HS_FDIAGONAL  '\'
    case 3: return ((mx + my) & 7) == 7;             // HS_BDIAGONAL  '/'
    case 4: return mx == 4 || my == 4;               // HS_CROSS
    default: return mx == my || ((mx + my) & 7) == 7; // HS_DIAGCROSS
    }
}

} // namespace

void PutPixelCopy(GdiCtx& dc, int x, int y, uint32_t argb) {
    if (!dc.surf) return;
    if (!Visible(dc, x, y)) return;
    int sx = x + dc.viewportOrg.x, sy = y + dc.viewportOrg.y;
    if (sx < 0 || sy < 0 || sx >= dc.surf->Width() || sy >= dc.surf->Height()) return;
    dc.surf->Row(sy)[sx] = argb;
}

bool GetPixel(const GdiCtx& dc, int x, int y, uint32_t& argb) {
    if (!dc.surf) return false;
    int sx = x + dc.viewportOrg.x, sy = y + dc.viewportOrg.y;
    if (sx < 0 || sy < 0 || sx >= dc.surf->Width() || sy >= dc.surf->Height()) return false;
    argb = dc.surf->Row(sy)[sx];
    return true;
}

void PutPixel(GdiCtx& dc, int x, int y, uint32_t argb) {
    if (dc.rop2 == 13 || !dc.surf) {
        PutPixelCopy(dc, x, y, argb);
        return;
    }
    if (!Visible(dc, x, y)) return;
    int sx = x + dc.viewportOrg.x, sy = y + dc.viewportOrg.y;
    if (sx < 0 || sy < 0 || sx >= dc.surf->Width() || sy >= dc.surf->Height()) return;
    uint32_t& d = dc.surf->Row(sy)[sx];
    d = Rop2(dc.rop2, argb, d);
}

namespace {

inline int PenW(const GdiCtx& dc) { return dc.penWidth < 1 ? 1 : int(dc.penWidth); }

// One interior pixel of a filled shape: solid, or hatched (bkOpaque paints the gaps with the background colour).
inline void FillPix(GdiCtx& dc, int x, int y, uint32_t brush) {
    if (dc.brushStyle == 2) {
        if (HatchOn(dc.brushHatch, x + dc.viewportOrg.x, y + dc.viewportOrg.y)) PutPixel(dc, x, y, brush);
        else if (dc.bkOpaque) PutPixel(dc, x, y, ToArgb(dc.bkColor));
    } else if (dc.brushStyle == 3 && dc.brushPat && dc.brushPatW > 0 && dc.brushPatH > 0) {
        int px = (x + dc.viewportOrg.x) % dc.brushPatW, py = (y + dc.viewportOrg.y) % dc.brushPatH;
        if (px < 0) px += dc.brushPatW;
        if (py < 0) py += dc.brushPatH;
        PutPixel(dc, x, y, dc.brushPat[size_t(py) * size_t(dc.brushPatW) + size_t(px)]);
    } else {
        PutPixel(dc, x, y, brush);
    }
}

// Generic closed shape: `outer(x,y)` / `inner(x,y)` predicates in logical coordinates.
template <class OUT, class IN>
void PaintShape(GdiCtx& dc, const RECT& bb, OUT outer, IN inner, bool hasPen) {
    uint32_t pen = ToArgb(dc.penColor), brush = ToArgb(dc.brushColor);
    bool hasBrush = dc.brushStyle != 1; // BS_NULL = hollow
    for (int y = bb.top; y < bb.bottom; ++y)
        for (int x = bb.left; x < bb.right; ++x) {
            if (!outer(x, y)) continue;
            if (inner(x, y)) {
                if (hasBrush) FillPix(dc, x, y, brush);
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

// Dash patterns of a cosmetic (1 pixel) pen on screen: PS_DASH, PS_DOT, PS_DASHDOT, PS_DASHDOTDOT (on, off, on, off ... lengths).
struct Dash {
    const int* pat = nullptr;
    int n = 0, idx = 0, left = 0;
    explicit Dash(const GdiCtx& dc) {
        static const int dash[] = {18, 6}, dot[] = {3, 3}, dashdot[] = {9, 6, 3, 6}, dashdotdot[] = {9, 3, 3, 3, 3, 3};
        if ((dc.penStyle & 0xf) == 7) { // PS_USERSTYLE: any width
            if (dc.dashPat && dc.dashN > 0) { pat = dc.dashPat; n = dc.dashN; left = pat[0]; }
            return;
        }
        if (PenW(dc) != 1) return; // wide pens with the built-in styles are solid here
        switch (dc.penStyle & 0xf) {
        case 1: pat = dash; n = 2; break;
        case 2: pat = dot; n = 2; break;
        case 3: pat = dashdot; n = 4; break;
        case 4: pat = dashdotdot; n = 6; break;
        default: break;
        }
        if (pat) left = pat[0];
    }
    bool Active() const { return pat != nullptr; }
    bool Step() { // returns true when the next pixel is in an "on" run
        bool on = (idx & 1) == 0;
        if (--left <= 0) { idx = (idx + 1) % n; left = pat[idx]; }
        return on;
    }
};

void StampLine(GdiCtx& dc, int x0, int y0, int x1, int y1, int w, uint32_t argb, Dash* dash = nullptr) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        bool on = dash && dash->Active() ? dash->Step() : true;
        if (on) {
            for (int j = 0; j < w; ++j)
                for (int i = 0; i < w; ++i) PutPixel(dc, x0 - w / 2 + i, y0 - w / 2 + j, argb);
        } else if (dc.bkOpaque) {
            PutPixel(dc, x0, y0, ToArgb(dc.bkColor)); // OPAQUE background mode: the gaps take the background colour
        }
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
    Dash dash(dc); // the dash phase continues from one segment to the next
    for (int i = 0; i + 1 < n; ++i) StampLine(dc, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, w, pen, &dash);
}

void Polygon(GdiCtx& dc, const POINT* pts, int n) {
    if (n < 2) return;
    if (dc.brushStyle != 1 && n >= 3) { // ALTERNATE (or WINDING with dc.polyWinding) fill, sampled at pixel centres
        int ymin = pts[0].y, ymax = pts[0].y;
        for (int i = 1; i < n; ++i) { ymin = std::min(ymin, int(pts[i].y)); ymax = std::max(ymax, int(pts[i].y)); }
        uint32_t brush = ToArgb(dc.brushColor);
        std::vector<std::pair<double, int>> xs; // crossing x, edge direction (+1 downwards, -1 upwards)
        for (int y = ymin; y < ymax; ++y) {
            xs.clear();
            double cy = y + 0.5;
            for (int i = 0; i < n; ++i) {
                const POINT& a = pts[i];
                const POINT& b = pts[(i + 1) % n];
                if ((a.y <= cy && b.y > cy) || (b.y <= cy && a.y > cy))
                    xs.push_back({a.x + (cy - a.y) * (double(b.x) - a.x) / (double(b.y) - a.y), b.y > a.y ? 1 : -1});
            }
            std::sort(xs.begin(), xs.end());
            if (!dc.polyWinding) {
                for (size_t k = 0; k + 1 < xs.size(); k += 2)
                    for (int x = int(std::ceil(xs[k].first - 0.5)); x < int(std::ceil(xs[k + 1].first - 0.5)); ++x) FillPix(dc, x, y, brush);
            } else {
                int wind = 0;
                for (size_t k = 0; k + 1 < xs.size(); ++k) {
                    wind += xs[k].second;
                    if (wind != 0)
                        for (int x = int(std::ceil(xs[k].first - 0.5)); x < int(std::ceil(xs[k + 1].first - 0.5)); ++x) FillPix(dc, x, y, brush);
                }
            }
        }
    }
    if (!dc.penNull) {
        uint32_t pen = ToArgb(dc.penColor);
        int w = PenW(dc);
        Dash dash(dc);
        for (int i = 0; i < n; ++i) {
            const POINT& a = pts[i];
            const POINT& b = pts[(i + 1) % n];
            StampLine(dc, a.x, a.y, b.x, b.y, w, pen, &dash);
        }
    }
}

// One interior pixel with the current brush (solid, hatched or pattern), clipped; used by the EMF player for path / region fills.
void FillPixel(GdiCtx& dc, int x, int y) { FillPix(dc, x, y, ToArgb(dc.brushColor)); }

#ifndef RAPTURE_WIN32
// Portable text: the stb_truetype backend (GdiTextTtf.cpp), used when RAPTURE_WIN32 is not defined (Linux/macOS, or RAPTURE_FORCE_PORTABLE).
// (The Windows build uses the real GDI, GdiRaster_win.cpp.)
int TextRect(const LOGFONT_LITE& f, const char* s, int len, RECT& rc, u32 flags) { return ttf::TextRect(f, s, len, rc, flags); }
void TextDraw(GdiCtx& dc, const LOGFONT_LITE& f, const char* s, int len, const RECT& rc, u32 flags) { ttf::TextDraw(dc, f, s, len, rc, flags); }
int TextAscent(const LOGFONT_LITE& f) { return ttf::TextAscent(f); }
#endif

} // namespace gdi
} // namespace moses
