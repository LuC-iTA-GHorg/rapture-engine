// EMF interpreter behind CDrawMeta::display (replaces PlayEnhMetaFile, moses.dll 0x10009250).  See DrawMeta.h for the coverage.
// Record layouts: [MS-EMF] "Enhanced Metafile Format". All values little endian; the host is assumed little endian.
#include "moses/DrawMeta.h"
#include "moses/GdiRaster.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace moses {
namespace emf {

namespace {

constexpr double kPi = 3.14159265358979323846;

inline u32 U32(const u8* p) { u32 v; std::memcpy(&v, p, 4); return v; }
inline i32 I32(const u8* p) { return i32(U32(p)); }
inline u16 U16(const u8* p) { u16 v; std::memcpy(&v, p, 2); return v; }
inline i16 I16(const u8* p) { return i16(U16(p)); }
inline float F32(const u8* p) { float v; std::memcpy(&v, p, 4); return v; }

enum RecType : u32 {
    R_HEADER = 1, R_POLYBEZIER = 2, R_POLYGON = 3, R_POLYLINE = 4, R_POLYBEZIERTO = 5, R_POLYLINETO = 6,
    R_POLYPOLYLINE = 7, R_POLYPOLYGON = 8, R_SETWINDOWEXTEX = 9, R_SETWINDOWORGEX = 10, R_SETVIEWPORTEXTEX = 11,
    R_SETVIEWPORTORGEX = 12, R_SETBRUSHORGEX = 13, R_EOF = 14, R_SETPIXELV = 15, R_SETMAPMODE = 17, R_SETBKMODE = 18,
    R_SETPOLYFILLMODE = 19, R_SETROP2 = 20, R_SETSTRETCHBLTMODE = 21, R_SETTEXTALIGN = 22, R_SETTEXTCOLOR = 24,
    R_SETBKCOLOR = 25, R_MOVETOEX = 27, R_EXCLUDECLIPRECT = 29, R_INTERSECTCLIPRECT = 30, R_SCALEVIEWPORTEXTEX = 31,
    R_SCALEWINDOWEXTEX = 32, R_SAVEDC = 33, R_RESTOREDC = 34, R_SETWORLDTRANSFORM = 35, R_MODIFYWORLDTRANSFORM = 36,
    R_SELECTOBJECT = 37, R_CREATEPEN = 38, R_CREATEBRUSHINDIRECT = 39, R_DELETEOBJECT = 40, R_ANGLEARC = 41,
    R_ELLIPSE = 42, R_RECTANGLE = 43, R_ROUNDRECT = 44, R_ARC = 45, R_CHORD = 46, R_PIE = 47, R_LINETO = 54,
    R_ARCTO = 55, R_BEGINPATH = 59, R_ENDPATH = 60, R_CLOSEFIGURE = 61, R_FILLPATH = 62, R_STROKEANDFILLPATH = 63,
    R_STROKEPATH = 64, R_SELECTCLIPPATH = 67, R_ABORTPATH = 68, R_GDICOMMENT = 70, R_BITBLT = 76, R_STRETCHBLT = 77,
    R_SETDIBITSTODEVICE = 80, R_STRETCHDIBITS = 81, R_EXTCREATEFONTINDIRECTW = 82, R_EXTTEXTOUTA = 83,
    R_EXTTEXTOUTW = 84, R_POLYBEZIER16 = 85, R_POLYGON16 = 86, R_POLYLINE16 = 87, R_POLYBEZIERTO16 = 88,
    R_POLYLINETO16 = 89, R_POLYPOLYLINE16 = 90, R_POLYPOLYGON16 = 91, R_EXTCREATEPEN = 95, R_SETLAYOUT = 115,
};

struct Pt { double x = 0, y = 0; };

struct Obj { // EMF object table entry
    enum Kind { NONE, PEN, BRUSH, FONT } kind = NONE;
    LOGPEN pen;
    LOGBRUSH brush;
    LOGFONT_LITE font;
    bool nullObj = false; // NULL_PEN / NULL_BRUSH
};

struct State {
    double xf[6] = {1, 0, 0, 1, 0, 0}; // eM11 eM12 eM21 eM22 eDx eDy  (x' = x*eM11 + y*eM21 + eDx ; y' = x*eM12 + y*eM22 + eDy)
    int mapMode = 1;
    double winOrgX = 0, winOrgY = 0, winExtX = 1, winExtY = 1, vpOrgX = 0, vpOrgY = 0, vpExtX = 1, vpExtY = 1;
    COLORREF bk = Rgb(255, 255, 255), text = 0;
    int bkMode = 2; // OPAQUE
    u32 textAlign = 0;
    int polyFill = 1; // ALTERNATE
    Pt cur;
    int selPen = -1, selBrush = -1, selFont = -1; // object index; negative = stock (see StockObj)
    int stockPen = 7, stockBrush = 0, stockFont = 13; // stock index selected via 0x80000000|n
    RECT clip;
    bool hasClip = false;
    RECT excl;
    bool hasExcl = false;
};

struct Player {
    const std::vector<u8>& d;
    const Header& h;
    GdiCtx dc;                      // working context (copy of the caller's)
    RECT dest;
    std::vector<Obj> objs;
    State st;
    std::vector<State> stack;
    std::map<u32, u32>& skipped;
    double fx0 = 0, fy0 = 0, fw = 1, fh = 1; // frame in reference pixels
    // path recording
    struct Fig { std::vector<Pt> pts; bool closed = false; };
    bool inPath = false;
    std::vector<Fig> path;
    bool pathReady = false;

    Player(const std::vector<u8>& data, const Header& hdr, const GdiCtx& ctx, const RECT& rc, std::map<u32, u32>& sk)
        : d(data), h(hdr), dc(ctx), dest(rc), skipped(sk) {
        objs.resize(size_t(h.nHandles) + 1);
        double pxX = h.mmCx ? double(h.devCx) / (double(h.mmCx) * 100.0) : 0.0; // device px per 0.01 mm
        double pxY = h.mmCy ? double(h.devCy) / (double(h.mmCy) * 100.0) : 0.0;
        fx0 = h.frame.left * pxX;
        fy0 = h.frame.top * pxY;
        fw = (h.frame.right - h.frame.left) * pxX;
        fh = (h.frame.bottom - h.frame.top) * pxY;
        if (fw < 1 || fh < 1) { // no usable frame: use the bounds
            fx0 = h.bounds.left; fy0 = h.bounds.top;
            fw = h.bounds.right - h.bounds.left + 1; fh = h.bounds.bottom - h.bounds.top + 1;
            if (fw < 1) fw = 1;
            if (fh < 1) fh = 1;
        }
        st.clip = dc.clip;
    }

    // ---- coordinate pipeline ------------------------------------------------------------------------------
    void mapScale(double& sx, double& sy) const {
        double mx = h.mmCx ? double(h.devCx) / h.mmCx : 1.0, my = h.mmCy ? double(h.devCy) / h.mmCy : 1.0; // px per mm
        switch (st.mapMode) {
        case 2: sx = mx * 0.1; sy = -my * 0.1; break;                 // MM_LOMETRIC
        case 3: sx = mx * 0.01; sy = -my * 0.01; break;               // MM_HIMETRIC
        case 4: sx = mx * 0.254; sy = -my * 0.254; break;             // MM_LOENGLISH (0.01 in)
        case 5: sx = mx * 0.0254; sy = -my * 0.0254; break;           // MM_HIENGLISH
        case 6: sx = mx * 25.4 / 1440.0; sy = -my * 25.4 / 1440.0; break; // MM_TWIPS
        case 7: case 8:                                               // ISOTROPIC (as ANISOTROPIC) / ANISOTROPIC
            sx = st.winExtX != 0 ? st.vpExtX / st.winExtX : 1.0;
            sy = st.winExtY != 0 ? st.vpExtY / st.winExtY : 1.0;
            break;
        default: sx = sy = 1.0; break;                                // MM_TEXT
        }
    }
    // logical -> destination (view) coordinates
    Pt map(double x, double y) const {
        double wx = x * st.xf[0] + y * st.xf[2] + st.xf[4];
        double wy = x * st.xf[1] + y * st.xf[3] + st.xf[5];
        double sx, sy;
        mapScale(sx, sy);
        double ox = st.mapMode == 1 || st.mapMode >= 7 ? st.winOrgX : 0.0, oy = st.mapMode == 1 || st.mapMode >= 7 ? st.winOrgY : 0.0;
        if (st.mapMode >= 2 && st.mapMode <= 6) { ox = st.winOrgX; oy = st.winOrgY; }
        double dx = (wx - ox) * sx + st.vpOrgX, dy = (wy - oy) * sy + st.vpOrgY;
        Pt r;
        r.x = dest.left + (dx - fx0) * (dest.right - dest.left) / fw;
        r.y = dest.top + (dy - fy0) * (dest.bottom - dest.top) / fh;
        return r;
    }
    double totalScale() const { // mean linear scale logical -> destination
        Pt o = map(0, 0), a = map(1, 0), b = map(0, 1);
        double sa = std::hypot(a.x - o.x, a.y - o.y), sb = std::hypot(b.x - o.x, b.y - o.y);
        return std::sqrt(std::max(sa * sb, 0.0));
    }
    bool axisAligned() const {
        Pt o = map(0, 0), a = map(1, 0), b = map(0, 1);
        return std::fabs(a.y - o.y) < 1e-6 && std::fabs(b.x - o.x) < 1e-6;
    }
    static int Rnd(double v) { return int(std::floor(v + 0.5)); }

    // ---- GDI objects -----------------------------------------------------------------------------------------
    const Obj* curObj(Obj::Kind k, Obj& tmp) const {
        int idx = k == Obj::PEN ? st.selPen : k == Obj::BRUSH ? st.selBrush : st.selFont;
        if (idx >= 0 && size_t(idx) < objs.size() && objs[size_t(idx)].kind == k) return &objs[size_t(idx)];
        tmp = Obj();
        tmp.kind = k;
        if (k == Obj::PEN) { // stock pens: WHITE_PEN 6, BLACK_PEN 7, NULL_PEN 8, DC_PEN 19
            tmp.pen.style = st.stockPen == 8 ? 5 : 0;
            tmp.pen.width = POINT{0, 0};
            tmp.pen.color = st.stockPen == 6 ? Rgb(255, 255, 255) : 0;
            tmp.nullObj = st.stockPen == 8;
        } else if (k == Obj::BRUSH) { // WHITE 0, LTGRAY 1, GRAY 2, DKGRAY 3, BLACK 4, NULL 5
            static const COLORREF c[] = {Rgb(255, 255, 255), Rgb(192, 192, 192), Rgb(128, 128, 128), Rgb(64, 64, 64), 0};
            tmp.brush.style = st.stockBrush == 5 ? 1 : 0;
            tmp.brush.color = st.stockBrush >= 0 && st.stockBrush <= 4 ? c[st.stockBrush] : 0;
            tmp.nullObj = st.stockBrush == 5;
        } else {
            tmp.font.height = -12;
            std::strcpy(tmp.font.face, "Arial");
        }
        return &tmp;
    }
    void setupPen(GdiCtx& g) const {
        Obj t;
        const Obj* o = curObj(Obj::PEN, t);
        g.penNull = o->nullObj || o->pen.style == 5;
        g.penStyle = o->pen.style;
        g.penColor = o->pen.color;
        double w = o->pen.width.x * totalScale();
        g.penWidth = u32(std::max(1, Rnd(w)));
    }
    void setupBrush(GdiCtx& g) const {
        Obj t;
        const Obj* o = curObj(Obj::BRUSH, t);
        g.brushStyle = o->nullObj ? 1 : o->brush.style == 1 ? 1 : 0; // hatch/pattern -> solid (see DrawMeta.h)
        g.brushColor = o->brush.color;
    }

    // ---- shape sinks (draw, or record into a path) -----------------------------------------------------------------
    void poly(const std::vector<Pt>& p, bool closed, bool fill) {
        if (inPath) {
            Fig f;
            f.pts = p;
            f.closed = closed;
            path.push_back(f);
            return;
        }
        std::vector<POINT> pts;
        pts.reserve(p.size());
        for (const Pt& q : p) pts.push_back(POINT{Rnd(q.x), Rnd(q.y)});
        GdiCtx g = clipped();
        setupPen(g);
        setupBrush(g);
        if (pts.empty()) return;
        if (closed && fill) gdi::Polygon(g, pts.data(), int(pts.size()));
        else if (closed) { pts.push_back(pts[0]); gdi::Polyline(g, pts.data(), int(pts.size())); }
        else gdi::Polyline(g, pts.data(), int(pts.size()));
    }
    GdiCtx clipped() const {
        GdiCtx g = dc;
        if (st.hasClip) {
            g.clip.left = std::max(g.clip.left, st.clip.left);
            g.clip.top = std::max(g.clip.top, st.clip.top);
            g.clip.right = std::min(g.clip.right, st.clip.right);
            g.clip.bottom = std::min(g.clip.bottom, st.clip.bottom);
        }
        if (st.hasExcl) { g.exclude = st.excl; g.hasExclude = true; }
        return g;
    }

    void box(const u8* rec, int kind, int cornerW = 0, int cornerH = 0) { // 0 rect, 1 ellipse, 2 roundrect
        double l = I32(rec + 24), t = I32(rec + 28), r = I32(rec + 32), b = I32(rec + 36);
        Pt p0 = map(l, t), p1 = map(r, b);
        if (!inPath && axisAligned()) {
            GdiCtx g = clipped();
            setupPen(g);
            setupBrush(g);
            int L = Rnd(std::min(p0.x, p1.x)), R = Rnd(std::max(p0.x, p1.x));
            int T = Rnd(std::min(p0.y, p1.y)), B = Rnd(std::max(p0.y, p1.y));
            if (kind == 0) gdi::Rectangle(g, L, T, R, B);
            else if (kind == 1) gdi::Ellipse(g, L, T, R, B);
            else {
                Pt c0 = map(0, 0), c1 = map(cornerW, cornerH);
                gdi::RoundRect(g, L, T, R, B, std::abs(Rnd(c1.x - c0.x)), std::abs(Rnd(c1.y - c0.y)));
            }
            return;
        }
        std::vector<Pt> pts;
        if (kind == 1 || kind == 2) { // ellipse (a rounded rectangle is approximated by its ellipse when rotated / in a path)
            double cx = (l + r) / 2, cy = (t + b) / 2, rx = (r - l) / 2, ry = (b - t) / 2;
            for (int i = 0; i < 64; ++i) {
                double a = 2 * kPi * i / 64;
                pts.push_back(map(cx + rx * std::cos(a), cy + ry * std::sin(a)));
            }
        } else {
            pts = {map(l, t), map(r, t), map(r, b), map(l, b)};
        }
        poly(pts, true, true);
    }

    void arc(const u8* rec, int kind) { // 0 arc, 1 chord, 2 pie ; rec: box at 24, start at 40, end at 48
        double l = I32(rec + 24), t = I32(rec + 28), r = I32(rec + 32), b = I32(rec + 36);
        double cx = (l + r) / 2, cy = (t + b) / 2, rx = (r - l) / 2, ry = (b - t) / 2;
        if (rx == 0 || ry == 0) return;
        auto ang = [&](double px, double py) { return std::atan2(-(py - cy) / ry, (px - cx) / rx); };
        double a0 = ang(I32(rec + 40), I32(rec + 44)), a1 = ang(I32(rec + 48), I32(rec + 52));
        arcPoints(cx, cy, rx, ry, a0, a1, kind);
    }
    void arcPoints(double cx, double cy, double rx, double ry, double a0, double a1, int kind) {
        double sweep = a1 - a0;
        while (sweep <= 0) sweep += 2 * kPi; // counterclockwise
        int n = std::max(8, int(sweep / (2 * kPi) * 64));
        std::vector<Pt> pts;
        if (kind == 2) pts.push_back(map(cx, cy));
        for (int i = 0; i <= n; ++i) {
            double a = a0 + sweep * i / n;
            pts.push_back(map(cx + rx * std::cos(a), cy - ry * std::sin(a)));
        }
        poly(pts, kind != 0, true);
    }

    void bezier(std::vector<Pt>& out, const std::vector<Pt>& c, size_t at) { // appends a flattened cubic starting at out.back()
        Pt p0 = out.back(), p1 = c[at], p2 = c[at + 1], p3 = c[at + 2];
        for (int i = 1; i <= 16; ++i) {
            double t = i / 16.0, u = 1 - t;
            Pt q;
            q.x = u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x;
            q.y = u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y;
            out.push_back(q);
        }
    }

    // ---- bitmaps ---------------------------------------------------------------------------------------------------
    bool decodeDib(const u8* bmi, size_t bmiLen, const u8* bits, size_t bitsLen, int& w, int& h2, std::vector<u32>& argb) const {
        if (bmiLen < 40) return false;
        u32 hs = U32(bmi);
        if (hs < 40 || hs > bmiLen) return false;
        w = I32(bmi + 4);
        int hh = I32(bmi + 8);
        u16 bpp = U16(bmi + 14);
        u32 comp = U32(bmi + 16), used = U32(bmi + 32);
        if (w <= 0 || hh == 0 || w > 8192 || std::abs(hh) > 8192 || comp != 0) return false; // BI_RGB only
        h2 = std::abs(hh);
        u32 ncol = bpp <= 8 ? (used ? used : (1u << bpp)) : 0;
        const u8* pal = bmi + hs;
        if (hs + size_t(ncol) * 4 > bmiLen) return false;
        size_t stride = ((size_t(w) * bpp + 31) / 32) * 4;
        if (bitsLen < stride * size_t(h2)) return false;
        argb.assign(size_t(w) * size_t(h2), 0xff000000u);
        for (int y = 0; y < h2; ++y) {
            const u8* row = bits + stride * size_t(hh < 0 ? y : h2 - 1 - y);
            for (int x = 0; x < w; ++x) {
                u32 c = 0;
                auto quad = [&](u32 i) { const u8* q = pal + 4 * i; return u32(q[2]) << 16 | u32(q[1]) << 8 | q[0]; };
                switch (bpp) {
                case 1: c = quad((row[x >> 3] >> (7 - (x & 7))) & 1); break;
                case 4: c = quad((x & 1) ? (row[x >> 1] & 15) : (row[x >> 1] >> 4)); break;
                case 8: c = quad(row[x]); break;
                case 16: { u16 v = U16(row + 2 * x); // RGB555
                    c = u32((v >> 10 & 31) * 255 / 31) << 16 | u32((v >> 5 & 31) * 255 / 31) << 8 | u32((v & 31) * 255 / 31); break; }
                case 24: c = u32(row[3 * x + 2]) << 16 | u32(row[3 * x + 1]) << 8 | row[3 * x]; break;
                case 32: c = u32(row[4 * x + 2]) << 16 | u32(row[4 * x + 1]) << 8 | row[4 * x]; break;
                default: return false;
                }
                argb[size_t(y) * size_t(w) + size_t(x)] = 0xff000000u | (c & 0xffffffu);
            }
        }
        return true;
    }
    // draws src rect (sx,sy,sw,sh) of the bitmap into the destination box (logical dx,dy,dw,dh) with nearest sampling
    void blit(const std::vector<u32>& argb, int bw, int bh, int sx, int sy, int sw, int sh, double dx, double dy, double dw, double dh) {
        if (sw <= 0 || sh <= 0) return;
        if (sx < 0 || sy < 0 || sx + sw > bw || sy + sh > bh) { sx = std::max(0, sx); sy = std::max(0, sy); sw = std::min(sw, bw - sx); sh = std::min(sh, bh - sy); if (sw <= 0 || sh <= 0) return; }
        Pt p0 = map(dx, dy), p1 = map(dx + dw, dy + dh);
        int L = Rnd(std::min(p0.x, p1.x)), R = Rnd(std::max(p0.x, p1.x));
        int T = Rnd(std::min(p0.y, p1.y)), B = Rnd(std::max(p0.y, p1.y));
        bool flipX = p1.x < p0.x, flipY = p1.y < p0.y;
        if (R == L) R = L + 1;
        if (B == T) B = T + 1;
        GdiCtx g = clipped();
        for (int y = T; y < B; ++y)
            for (int x = L; x < R; ++x) {
                int u = int(int64_t(x - L) * sw / (R - L)), v = int(int64_t(y - T) * sh / (B - T));
                if (flipX) u = sw - 1 - u;
                if (flipY) v = sh - 1 - v;
                gdi::PutPixel(g, x, y, argb[size_t(sy + v) * size_t(bw) + size_t(sx + u)]);
            }
    }
    void fillRect(double x, double y, double w, double h2, uint32_t argb) {
        Pt p0 = map(x, y), p1 = map(x + w, y + h2);
        int L = Rnd(std::min(p0.x, p1.x)), R = Rnd(std::max(p0.x, p1.x));
        int T = Rnd(std::min(p0.y, p1.y)), B = Rnd(std::max(p0.y, p1.y));
        GdiCtx g = clipped();
        for (int yy = T; yy < B; ++yy)
            for (int xx = L; xx < R; ++xx) gdi::PutPixel(g, xx, yy, argb);
    }

    // ---- text -------------------------------------------------------------------------------------------------------
    void text(const u8* rec, size_t recLen, bool wide) {
        if (recLen < 76) return;
        i32 rx = I32(rec + 36), ry = I32(rec + 40);
        u32 n = U32(rec + 44), off = U32(rec + 48), opts = U32(rec + 52);
        if (n == 0 || n > 4096 || off + size_t(n) * (wide ? 2 : 1) > recLen) return;
        std::string s;
        for (u32 i = 0; i < n; ++i) {
            u32 c = wide ? U16(rec + off + 2 * i) : rec[off + i];
            s.push_back(c < 256 ? char(c) : '?');
        }
        Obj t;
        const Obj* fo = curObj(Obj::FONT, t);
        LOGFONT_LITE f = fo->font;
        double sc = totalScale();
        int hpx = Rnd(std::fabs(double(f.height)) * sc);
        f.height = f.height < 0 ? -std::max(1, hpx) : std::max(1, hpx);
        RECT rc{0, 0, 0, 0};
        int th = gdi::TextRect(f, s.c_str(), int(s.size()), rc, DT_SINGLELINE_ | 0x800 /*DT_NOPREFIX*/ | 0x400 /*DT_CALCRECT*/);
        int tw = rc.right - rc.left;
        if (th <= 0) th = std::max(1, hpx);
        Pt p = map(rx, ry);
        int x = Rnd(p.x), y = Rnd(p.y);
        u32 ta = st.textAlign;
        int h2 = ta & 6; // TA_LEFT 0, TA_RIGHT 2, TA_CENTER 6
        if (h2 == 6) x -= tw / 2;
        else if (h2 == 2) x -= tw;
        int v = ta & 24; // TA_TOP 0, TA_BOTTOM 8, TA_BASELINE 24
        if (v == 8) y -= th;
        else if (v == 24) y -= gdi::TextAscent(f);
        RECT out{x, y, x + tw, y + th};
        GdiCtx g = clipped();
        g.textColor = st.text;
        if (opts & 2 /*ETO_OPAQUE*/ || st.bkMode == 2) { // background rectangle (OPAQUE mode paints behind the glyph cell)
            for (int yy = out.top; yy < out.bottom; ++yy)
                for (int xx = out.left; xx < out.right; ++xx) gdi::PutPixel(g, xx, yy, gdi::ToArgb(st.bk));
        }
        gdi::TextDraw(g, f, s.c_str(), int(s.size()), out, DT_SINGLELINE_ | 0x800);
    }

    // ---- main loop ----------------------------------------------------------------------------------------------------
    static void Mul(const double a[6], const double b[6], double out[6]) { // row vectors: result = a * b (apply a, then b)
        out[0] = a[0] * b[0] + a[1] * b[2];
        out[1] = a[0] * b[1] + a[1] * b[3];
        out[2] = a[2] * b[0] + a[3] * b[2];
        out[3] = a[2] * b[1] + a[3] * b[3];
        out[4] = a[4] * b[0] + a[5] * b[2] + b[4];
        out[5] = a[4] * b[1] + a[5] * b[3] + b[5];
    }

    void points(const u8* rec, bool is16, bool bez, int kind) { // kind 0 polyline, 1 polygon, 2 polylineto
        u32 n = U32(rec + 24);
        std::vector<Pt> raw;
        for (u32 i = 0; i < n; ++i) {
            if (is16) raw.push_back(map(I16(rec + 28 + 4 * i), I16(rec + 30 + 4 * i)));
            else raw.push_back(map(I32(rec + 28 + 8 * i), I32(rec + 32 + 8 * i)));
        }
        if (kind == 2) { // POLYLINETO / POLYBEZIERTO continue from the current position
            std::vector<Pt> all;
            all.push_back(map(st.cur.x, st.cur.y));
            if (bez) { for (size_t i = 0; i + 2 < raw.size(); i += 3) bezier(all, raw, i); }
            else all.insert(all.end(), raw.begin(), raw.end());
            poly(all, false, false);
            if (n) {
                if (is16) { st.cur.x = I16(rec + 28 + 4 * (n - 1)); st.cur.y = I16(rec + 30 + 4 * (n - 1)); }
                else { st.cur.x = I32(rec + 28 + 8 * (n - 1)); st.cur.y = I32(rec + 32 + 8 * (n - 1)); }
            }
            return;
        }
        if (bez) {
            if (raw.empty()) return;
            std::vector<Pt> all{raw[0]};
            for (size_t i = 1; i + 2 < raw.size() + 0; i += 3) bezier(all, raw, i);
            poly(all, false, false);
            return;
        }
        poly(raw, kind == 1, true);
    }
    void polypoly(const u8* rec, bool is16, bool closed) {
        u32 np = U32(rec + 24), total = U32(rec + 28);
        const u8* counts = rec + 32;
        const u8* pts = counts + 4 * np;
        u32 at = 0;
        for (u32 k = 0; k < np; ++k) {
            u32 c = U32(counts + 4 * k);
            std::vector<Pt> p;
            for (u32 i = 0; i < c && at + i < total; ++i) {
                u32 j = at + i;
                if (is16) p.push_back(map(I16(pts + 4 * j), I16(pts + 4 * j + 2)));
                else p.push_back(map(I32(pts + 8 * j), I32(pts + 8 * j + 4)));
            }
            at += c;
            poly(p, closed, true);
        }
    }

    void selectObject(u32 ih) {
        if (ih & 0x80000000u) { // stock object
            u32 n = ih & 0x7fffffffu;
            if (n <= 5) { st.selBrush = -1; st.stockBrush = int(n); }
            else if (n == 6 || n == 7 || n == 8 || n == 19) { st.selPen = -1; st.stockPen = int(n); }
            else st.selFont = -1;
            return;
        }
        if (ih == 0 || ih >= objs.size()) return;
        const Obj& o = objs[ih];
        if (o.kind == Obj::PEN) st.selPen = int(ih);
        else if (o.kind == Obj::BRUSH) st.selBrush = int(ih);
        else if (o.kind == Obj::FONT) st.selFont = int(ih);
    }

    void endPathDraw(bool fill, bool stroke) {
        std::vector<POINT> tmp;
        GdiCtx g = clipped();
        setupPen(g);
        setupBrush(g);
        for (const Fig& f : path) {
            tmp.clear();
            for (const Pt& q : f.pts) tmp.push_back(POINT{Rnd(q.x), Rnd(q.y)});
            if (tmp.empty()) continue;
            if (fill && tmp.size() >= 3) {
                GdiCtx fg = g;
                fg.penNull = true; // fill only
                gdi::Polygon(fg, tmp.data(), int(tmp.size()));
            }
            if (stroke) {
                if (f.closed) tmp.push_back(tmp[0]);
                gdi::Polyline(g, tmp.data(), int(tmp.size()));
            }
        }
    }

    u32 run() {
        u32 executed = 0;
        size_t pos = 0, size = d.size();
        while (pos + 8 <= size) {
            u32 type = U32(&d[pos]), len = U32(&d[pos + 4]);
            if (len < 8 || len % 4 || pos + len > size) break;
            const u8* r = &d[pos];
            size_t L = len;
            ++executed;
            switch (type) {
            case R_HEADER: break;
            case R_EOF: return executed;
            case R_SETWINDOWEXTEX: st.winExtX = I32(r + 8); st.winExtY = I32(r + 12); break;
            case R_SETWINDOWORGEX: st.winOrgX = I32(r + 8); st.winOrgY = I32(r + 12); break;
            case R_SETVIEWPORTEXTEX: st.vpExtX = I32(r + 8); st.vpExtY = I32(r + 12); break;
            case R_SETVIEWPORTORGEX: st.vpOrgX = I32(r + 8); st.vpOrgY = I32(r + 12); break;
            case R_SCALEVIEWPORTEXTEX:
                if (I32(r + 12) && I32(r + 20)) { st.vpExtX = st.vpExtX * I32(r + 8) / I32(r + 12); st.vpExtY = st.vpExtY * I32(r + 16) / I32(r + 20); }
                break;
            case R_SCALEWINDOWEXTEX:
                if (I32(r + 12) && I32(r + 20)) { st.winExtX = st.winExtX * I32(r + 8) / I32(r + 12); st.winExtY = st.winExtY * I32(r + 16) / I32(r + 20); }
                break;
            case R_SETMAPMODE: st.mapMode = I32(r + 8); if (st.mapMode == 1) { st.winExtX = st.winExtY = st.vpExtX = st.vpExtY = 1; } break;
            case R_SETBKMODE: st.bkMode = I32(r + 8); break;
            case R_SETPOLYFILLMODE: st.polyFill = I32(r + 8); break;
            case R_SETTEXTALIGN: st.textAlign = U32(r + 8); break;
            case R_SETTEXTCOLOR: st.text = U32(r + 8) & 0xffffff; break;
            case R_SETBKCOLOR: st.bk = U32(r + 8) & 0xffffff; break;
            case R_SETROP2: case R_SETSTRETCHBLTMODE: case R_SETBRUSHORGEX: case R_SETLAYOUT: case R_GDICOMMENT: break;
            case R_MOVETOEX: st.cur = Pt{double(I32(r + 8)), double(I32(r + 12))}; if (inPath) path.push_back(Fig{{map(st.cur.x, st.cur.y)}, false}); break;
            case R_LINETO: {
                Pt a = map(st.cur.x, st.cur.y), b = map(I32(r + 8), I32(r + 12));
                st.cur = Pt{double(I32(r + 8)), double(I32(r + 12))};
                if (inPath && !path.empty() && !path.back().closed) path.back().pts.push_back(b);
                else poly({a, b}, false, false);
                break;
            }
            case R_SETPIXELV: { Pt p = map(I32(r + 8), I32(r + 12)); GdiCtx g = clipped(); gdi::PutPixel(g, Rnd(p.x), Rnd(p.y), gdi::ToArgb(U32(r + 16))); break; }
            case R_SAVEDC: stack.push_back(st); break;
            case R_RESTOREDC: {
                int n = I32(r + 8);
                int idx = n < 0 ? int(stack.size()) + n : n - 1;
                if (idx >= 0 && size_t(idx) < stack.size()) {
                    State s2 = stack[size_t(idx)];
                    stack.resize(size_t(idx));
                    st = s2;
                }
                break;
            }
            case R_SETWORLDTRANSFORM: for (int i = 0; i < 6; ++i) st.xf[i] = F32(r + 8 + 4 * i); break;
            case R_MODIFYWORLDTRANSFORM: {
                double x[6], o[6];
                for (int i = 0; i < 6; ++i) x[i] = F32(r + 8 + 4 * i);
                u32 mode = U32(r + 32);
                if (mode == 1) { double id[6] = {1, 0, 0, 1, 0, 0}; std::memcpy(st.xf, id, sizeof id); }
                else if (mode == 2) { Mul(x, st.xf, o); std::memcpy(st.xf, o, sizeof o); }
                else if (mode == 3) { Mul(st.xf, x, o); std::memcpy(st.xf, o, sizeof o); }
                else if (mode == 4) std::memcpy(st.xf, x, sizeof x);
                break;
            }
            case R_INTERSECTCLIPRECT: {
                Pt p0 = map(I32(r + 8), I32(r + 12)), p1 = map(I32(r + 16), I32(r + 20));
                RECT c{Rnd(std::min(p0.x, p1.x)), Rnd(std::min(p0.y, p1.y)), Rnd(std::max(p0.x, p1.x)), Rnd(std::max(p0.y, p1.y))};
                if (st.hasClip) IntersectRect(&st.clip, &st.clip, &c); else { st.clip = c; st.hasClip = true; }
                break;
            }
            case R_EXCLUDECLIPRECT: {
                Pt p0 = map(I32(r + 8), I32(r + 12)), p1 = map(I32(r + 16), I32(r + 20));
                st.excl = RECT{Rnd(std::min(p0.x, p1.x)), Rnd(std::min(p0.y, p1.y)), Rnd(std::max(p0.x, p1.x)), Rnd(std::max(p0.y, p1.y))};
                st.hasExcl = true;
                break;
            }
            case R_CREATEPEN: { // ihPen, lopnStyle, lopnWidth(POINTL), lopnColor
                u32 ih = U32(r + 8);
                if (ih && ih < objs.size()) { Obj o; o.kind = Obj::PEN; o.pen.style = U32(r + 12) & 0xff; o.pen.width = POINT{I32(r + 16), I32(r + 20)}; o.pen.color = U32(r + 24) & 0xffffff; o.nullObj = o.pen.style == 5; objs[ih] = o; }
                break;
            }
            case R_EXTCREATEPEN: { // ihPen, offBmi, cbBmi, offBits, cbBits, elp{penStyle, width, brushStyle, color, hatch, nEntries}
                u32 ih = U32(r + 8);
                if (ih && ih < objs.size() && L >= 56) { Obj o; o.kind = Obj::PEN; o.pen.style = U32(r + 28) & 0xff; o.pen.width = POINT{I32(r + 32), 0}; o.pen.color = U32(r + 40) & 0xffffff; o.nullObj = o.pen.style == 5; objs[ih] = o; }
                break;
            }
            case R_CREATEBRUSHINDIRECT: {
                u32 ih = U32(r + 8);
                if (ih && ih < objs.size()) { Obj o; o.kind = Obj::BRUSH; o.brush.style = U32(r + 12); o.brush.color = U32(r + 16) & 0xffffff; o.brush.hatch = I32(r + 20); o.nullObj = o.brush.style == 1; objs[ih] = o; }
                break;
            }
            case R_EXTCREATEFONTINDIRECTW: {
                u32 ih = U32(r + 8);
                if (ih && ih < objs.size() && L >= 8 + 4 + 28 + 64) {
                    Obj o; o.kind = Obj::FONT;
                    const u8* lf = r + 12;
                    o.font.height = I32(lf);
                    o.font.weight = I32(lf + 16);
                    o.font.italic = lf[20] != 0;
                    o.font.underline = lf[21] != 0;
                    for (int i = 0; i < 31; ++i) { u16 c = U16(lf + 28 + 2 * i); if (!c) break; o.font.face[i] = c < 256 ? char(c) : '?'; }
                    objs[ih] = o;
                }
                break;
            }
            case R_SELECTOBJECT: selectObject(U32(r + 8)); break;
            case R_DELETEOBJECT: { u32 ih = U32(r + 8); if (ih && ih < objs.size()) { objs[ih] = Obj(); if (st.selPen == int(ih)) st.selPen = -1; if (st.selBrush == int(ih)) st.selBrush = -1; if (st.selFont == int(ih)) st.selFont = -1; } break; }
            case R_RECTANGLE: box(r, 0); break;
            case R_ELLIPSE: box(r, 1); break;
            case R_ROUNDRECT: box(r, 2, I32(r + 40), I32(r + 44)); break;
            case R_ARC: arc(r, 0); break;
            case R_CHORD: arc(r, 1); break;
            case R_PIE: arc(r, 2); break;
            case R_ANGLEARC: {
                double cx = I32(r + 8), cy = I32(r + 12), rad = U32(r + 16);
                double a0 = double(F32(r + 20)) * kPi / 180.0, sw = double(F32(r + 24)) * kPi / 180.0;
                std::vector<Pt> pts{map(st.cur.x, st.cur.y)};
                int n = std::max(4, int(std::fabs(sw) / (2 * kPi) * 64));
                for (int i = 0; i <= n; ++i) { double a = a0 + sw * i / n; pts.push_back(map(cx + rad * std::cos(a), cy - rad * std::sin(a))); }
                st.cur = Pt{cx + rad * std::cos(a0 + sw), cy - rad * std::sin(a0 + sw)};
                poly(pts, false, false);
                break;
            }
            case R_POLYGON: points(r, false, false, 1); break;
            case R_POLYGON16: points(r, true, false, 1); break;
            case R_POLYLINE: points(r, false, false, 0); break;
            case R_POLYLINE16: points(r, true, false, 0); break;
            case R_POLYBEZIER: points(r, false, true, 0); break;
            case R_POLYBEZIER16: points(r, true, true, 0); break;
            case R_POLYLINETO: points(r, false, false, 2); break;
            case R_POLYLINETO16: points(r, true, false, 2); break;
            case R_POLYBEZIERTO: points(r, false, true, 2); break;
            case R_POLYBEZIERTO16: points(r, true, true, 2); break;
            case R_POLYPOLYGON: polypoly(r, false, true); break;
            case R_POLYPOLYGON16: polypoly(r, true, true); break;
            case R_POLYPOLYLINE: polypoly(r, false, false); break;
            case R_POLYPOLYLINE16: polypoly(r, true, false); break;
            case R_EXTTEXTOUTA: text(r, L, false); break;
            case R_EXTTEXTOUTW: text(r, L, true); break;
            case R_BEGINPATH: inPath = true; path.clear(); pathReady = false; break;
            case R_ENDPATH: inPath = false; pathReady = true; break;
            case R_ABORTPATH: inPath = false; path.clear(); pathReady = false; break;
            case R_CLOSEFIGURE: if (!path.empty()) path.back().closed = true; break;
            case R_FILLPATH: endPathDraw(true, false); path.clear(); pathReady = false; break;
            case R_STROKEPATH: endPathDraw(false, true); path.clear(); pathReady = false; break;
            case R_STROKEANDFILLPATH: endPathDraw(true, true); path.clear(); pathReady = false; break;
            case R_SELECTCLIPPATH: { // bounding box of the path
                if (!path.empty()) {
                    RECT c{INT_MAX, INT_MAX, INT_MIN, INT_MIN};
                    for (const Fig& f : path)
                        for (const Pt& q : f.pts) { c.left = std::min(c.left, Rnd(q.x)); c.top = std::min(c.top, Rnd(q.y)); c.right = std::max(c.right, Rnd(q.x) + 1); c.bottom = std::max(c.bottom, Rnd(q.y) + 1); }
                    if (st.hasClip) IntersectRect(&st.clip, &st.clip, &c); else { st.clip = c; st.hasClip = true; }
                }
                path.clear(); pathReady = false;
                break;
            }
            case R_STRETCHDIBITS: {
                if (L < 80) break;
                i32 xd = I32(r + 24), yd = I32(r + 28), xs = I32(r + 32), ys = I32(r + 36), cxs = I32(r + 40), cys = I32(r + 44);
                u32 offBmi = U32(r + 48), cbBmi = U32(r + 52), offBits = U32(r + 56), cbBits = U32(r + 60);
                i32 cxd = I32(r + 72), cyd = I32(r + 76);
                int bw, bh;
                std::vector<u32> px;
                if (offBmi + cbBmi <= L && offBits + cbBits <= L && decodeDib(r + offBmi, cbBmi, r + offBits, cbBits, bw, bh, px)) {
                    // source rows: ySrc counts from the bottom of a bottom-up DIB
                    int ysTop = bh - (ys + cys);
                    blit(px, bw, bh, xs, ysTop, cxs, cys, xd, yd, cxd, cyd);
                } else ++skipped[type];
                break;
            }
            case R_SETDIBITSTODEVICE: {
                if (L < 76) break;
                i32 xd = I32(r + 24), yd = I32(r + 28), xs = I32(r + 32), ys = I32(r + 36);
                u32 cxs = U32(r + 40), cys = U32(r + 44), offBmi = U32(r + 48), cbBmi = U32(r + 52), offBits = U32(r + 56), cbBits = U32(r + 60);
                int bw, bh;
                std::vector<u32> px;
                if (offBmi + cbBmi <= L && offBits + cbBits <= L && decodeDib(r + offBmi, cbBmi, r + offBits, cbBits, bw, bh, px))
                    blit(px, bw, bh, xs, bh - (ys + int(cys)), int(cxs), int(cys), xd, yd, double(cxs), double(cys));
                else ++skipped[type];
                break;
            }
            case R_BITBLT: case R_STRETCHBLT: {
                if (L < 100) break;
                i32 xd = I32(r + 24), yd = I32(r + 28), cxd = I32(r + 32), cyd = I32(r + 36);
                u32 rop = U32(r + 40);
                i32 xs = I32(r + 44), ys = I32(r + 48);
                u32 offBmi = U32(r + 84), cbBmi = U32(r + 88), offBits = U32(r + 92), cbBits = U32(r + 96);
                i32 cxs = type == R_STRETCHBLT && L >= 108 ? I32(r + 100) : cxd, cys = type == R_STRETCHBLT && L >= 108 ? I32(r + 104) : cyd;
                int bw, bh;
                std::vector<u32> px;
                if (cbBmi && offBmi + cbBmi <= L && offBits + cbBits <= L && decodeDib(r + offBmi, cbBmi, r + offBits, cbBits, bw, bh, px)) {
                    blit(px, bw, bh, xs, ys, cxs, cys, xd, yd, cxd, cyd);
                } else if (rop == 0x00F00021u) { // PATCOPY with the current brush
                    Obj t; const Obj* b = curObj(Obj::BRUSH, t);
                    if (!b->nullObj) fillRect(xd, yd, cxd, cyd, gdi::ToArgb(b->brush.color));
                } else if (rop == 0x00000042u) fillRect(xd, yd, cxd, cyd, 0xff000000u);          // BLACKNESS
                else if (rop == 0x00FF0062u) fillRect(xd, yd, cxd, cyd, 0xffffffffu);            // WHITENESS
                else ++skipped[type];
                break;
            }
            default: ++skipped[type]; break;
            }
            pos += len;
        }
        return executed;
    }
};

} // namespace

// ---------------------------------------------------------------------------------------------
bool Metafile::Load(const u8* data, size_t size) {
    data_.clear();
    skipped_.clear();
    hdr_ = Header();
    recCount_ = 0;
    if (!data || size < 88) return false;
    // EMR_HEADER: type 1, size >= 88, signature at +40 = " EMF"
    if (U32(data) != 1 || U32(data + 4) < 88 || U32(data + 40) != 0x464d4520u) return false;
    hdr_.bounds = RECT{I32(data + 8), I32(data + 12), I32(data + 16), I32(data + 20)};
    hdr_.frame = RECT{I32(data + 24), I32(data + 28), I32(data + 32), I32(data + 36)};
    hdr_.nBytes = U32(data + 48);
    hdr_.nRecords = U32(data + 52);
    hdr_.nHandles = U16(data + 56);
    hdr_.devCx = I32(data + 72);
    hdr_.devCy = I32(data + 76);
    hdr_.mmCx = I32(data + 80);
    hdr_.mmCy = I32(data + 84);
    if (hdr_.nHandles > 4096) return false;
    // validate the record chain up to EMR_EOF
    size_t pos = 0;
    u32 n = 0;
    bool eof = false;
    while (pos + 8 <= size) {
        u32 type = U32(data + pos), len = U32(data + pos + 4);
        if (len < 8 || (len & 3) || pos + len > size) return false;
        ++n;
        pos += len;
        if (type == R_EOF) { eof = true; break; }
    }
    if (!eof) return false;
    data_.assign(data, data + pos);
    recCount_ = n;
    return true;
}

bool Metafile::LoadFile(const char* path) {
    std::FILE* f = std::fopen(path ? path : "", "rb");
    if (!f) return false;
    std::vector<u8> buf;
    u8 tmp[4096];
    size_t n;
    while ((n = std::fread(tmp, 1, sizeof tmp, f)) > 0) buf.insert(buf.end(), tmp, tmp + n);
    std::fclose(f);
    return Load(buf.data(), buf.size());
}

u32 Metafile::Play(GdiCtx& dc, const RECT& dest) const {
    skipped_.clear();
    if (!Valid()) return 0;
    Player p(data_, hdr_, dc, dest, skipped_);
    return p.run();
}

} // namespace emf

// ---------------------------------------------------------------------------------------------
// CDrawMeta
CDrawMeta::CDrawMeta(CDrawView* view, int level) : CDrawGdi(view, level) {} // moses.dll 0x10009270

CDrawMeta::~CDrawMeta() {} // moses.dll 0x100092b0 (DeleteEnhMetaFile: the shared_ptr releases the data)

CDrawObj* CDrawMeta::Copy() const { return new CDrawMeta(*this); }

// moses.dll 0x100092d0
int CDrawMeta::Init(const RECT* rc, const char* path) {
    rect_ = *rc;
    if (!CDrawObj::Init()) return 0;
    recalc_bounds(); // PORT: the original never recalculates the bounds here, so the object stayed empty (invisible) until moved
    auto m = std::make_shared<emf::Metafile>();
    if (!m->LoadFile(path)) return 0;
    meta_ = m;
    return 1;
}

int CDrawMeta::InitFromMemory(const RECT* rc, const u8* data, size_t size) {
    rect_ = *rc;
    if (!CDrawObj::Init()) return 0;
    recalc_bounds(); // PORT: the original never recalculates the bounds here, so the object stayed empty (invisible) until moved
    auto m = std::make_shared<emf::Metafile>();
    if (!m->Load(data, size)) return 0;
    meta_ = m;
    return 1;
}

// moses.dll 0x10009250
void CDrawMeta::display(CDibSect* view, GdiCtx* dc, RECT& r) {
    (void)view; (void)r;
    if (meta_) meta_->Play(*dc, rect_);
}

// ---------------------------------------------------------------------------------------------
// CAnimMeta
CAnimMeta::CAnimMeta(CAnimView* view, int level) : CAnimObj(view), CDrawMeta(view, level) { // moses.dll 0x1000bf50
    SetTarget(static_cast<CDrawObj*>(this));
}
CAnimMeta::~CAnimMeta() { Stop(); }

// moses.dll 0x100020d0: CAnimObj copy + CDrawObj copy + the 5 dwords of the CDrawMeta body (HENHMETAFILE shared)
CDrawObj* CAnimMeta::Copy() const {
    CAnimMeta* c = new CAnimMeta(view_, p_.level);
    CDrawObj* hi = c->p_.prev; CDrawObj* lo = c->p_.next;
    c->p_ = p_;
    c->p_.prev = hi; c->p_.next = lo;
    c->bk_ = bk_; c->opaque_ = opaque_; c->edge_width_ = edge_width_; c->rect_ = rect_;
    c->meta_ = meta_;
    return static_cast<CDrawObj*>(c);
}

} // namespace moses
