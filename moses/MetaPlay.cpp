// EMF interpreter behind CDrawMeta::display (replaces PlayEnhMetaFile, moses.dll 0x10009250).  See DrawMeta.h for the coverage.
// Record layouts: [MS-EMF] "Enhanced Metafile Format". All values little endian; the host is assumed little endian.
#include "moses/DrawMeta.h"
#include "moses/GdiRaster.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

namespace moses {
namespace emf {

namespace {

constexpr double kPi = 3.14159265358979323846;

inline u32 U32(const u8* p) { u32 v; std::memcpy(&v, p, 4); return v; }
inline i32 I32(const u8* p) { return i32(U32(p)); }
inline u16 U16(const u8* p) { u16 v; std::memcpy(&v, p, 2); return v; }
inline i16 I16(const u8* p) { return i16(U16(p)); }
inline float F32(const u8* p) { float v; std::memcpy(&v, p, 4); return v; }

// Record types (wingdi.h EMR_*).
enum RecType : u32 {
    R_HEADER = 1, R_POLYBEZIER = 2, R_POLYGON = 3, R_POLYLINE = 4, R_POLYBEZIERTO = 5, R_POLYLINETO = 6,
    R_POLYPOLYLINE = 7, R_POLYPOLYGON = 8, R_SETWINDOWEXTEX = 9, R_SETWINDOWORGEX = 10, R_SETVIEWPORTEXTEX = 11,
    R_SETVIEWPORTORGEX = 12, R_SETBRUSHORGEX = 13, R_EOF = 14, R_SETPIXELV = 15, R_SETMAPPERFLAGS = 16, R_SETMAPMODE = 17,
    R_SETBKMODE = 18, R_SETPOLYFILLMODE = 19, R_SETROP2 = 20, R_SETSTRETCHBLTMODE = 21, R_SETTEXTALIGN = 22,
    R_SETCOLORADJUSTMENT = 23, R_SETTEXTCOLOR = 24, R_SETBKCOLOR = 25, R_OFFSETCLIPRGN = 26, R_MOVETOEX = 27,
    R_SETMETARGN = 28, R_EXCLUDECLIPRECT = 29, R_INTERSECTCLIPRECT = 30, R_SCALEVIEWPORTEXTEX = 31,
    R_SCALEWINDOWEXTEX = 32, R_SAVEDC = 33, R_RESTOREDC = 34, R_SETWORLDTRANSFORM = 35, R_MODIFYWORLDTRANSFORM = 36,
    R_SELECTOBJECT = 37, R_CREATEPEN = 38, R_CREATEBRUSHINDIRECT = 39, R_DELETEOBJECT = 40, R_ANGLEARC = 41,
    R_ELLIPSE = 42, R_RECTANGLE = 43, R_ROUNDRECT = 44, R_ARC = 45, R_CHORD = 46, R_PIE = 47, R_SELECTPALETTE = 48,
    R_CREATEPALETTE = 49, R_SETPALETTEENTRIES = 50, R_RESIZEPALETTE = 51, R_REALIZEPALETTE = 52, R_EXTFLOODFILL = 53,
    R_LINETO = 54, R_ARCTO = 55, R_POLYDRAW = 56, R_SETARCDIRECTION = 57, R_SETMITERLIMIT = 58, R_BEGINPATH = 59,
    R_ENDPATH = 60, R_CLOSEFIGURE = 61, R_FILLPATH = 62, R_STROKEANDFILLPATH = 63, R_STROKEPATH = 64,
    R_FLATTENPATH = 65, R_WIDENPATH = 66, R_SELECTCLIPPATH = 67, R_ABORTPATH = 68, R_GDICOMMENT = 70, R_FILLRGN = 71,
    R_FRAMERGN = 72, R_INVERTRGN = 73, R_PAINTRGN = 74, R_EXTSELECTCLIPRGN = 75, R_BITBLT = 76, R_STRETCHBLT = 77,
    R_MASKBLT = 78, R_PLGBLT = 79, R_SETDIBITSTODEVICE = 80, R_STRETCHDIBITS = 81, R_EXTCREATEFONTINDIRECTW = 82,
    R_EXTTEXTOUTA = 83, R_EXTTEXTOUTW = 84, R_POLYBEZIER16 = 85, R_POLYGON16 = 86, R_POLYLINE16 = 87,
    R_POLYBEZIERTO16 = 88, R_POLYLINETO16 = 89, R_POLYPOLYLINE16 = 90, R_POLYPOLYGON16 = 91, R_POLYDRAW16 = 92,
    R_CREATEMONOBRUSH = 93, R_CREATEDIBPATTERNBRUSHPT = 94, R_EXTCREATEPEN = 95, R_POLYTEXTOUTA = 96,
    R_POLYTEXTOUTW = 97, R_SETICMMODE = 98, R_CREATECOLORSPACE = 99, R_SETCOLORSPACE = 100, R_DELETECOLORSPACE = 101,
    R_GLSRECORD = 102, R_GLSBOUNDEDRECORD = 103, R_PIXELFORMAT = 104, R_DRAWESCAPE = 105, R_EXTESCAPE = 106,
    R_SMALLTEXTOUT = 108, R_FORCEUFIMAPPING = 109, R_NAMEDESCAPE = 110, R_COLORCORRECTPALETTE = 111,
    R_SETICMPROFILEA = 112, R_SETICMPROFILEW = 113, R_ALPHABLEND = 114, R_SETLAYOUT = 115, R_TRANSPARENTBLT = 116,
    R_GRADIENTFILL = 118, R_SETLINKEDUFIS = 119, R_SETTEXTJUSTIFICATION = 120, R_COLORMATCHTOTARGETW = 121,
    R_CREATECOLORSPACEW = 122,
};

struct Pt { double x = 0, y = 0; };

using Mask = std::vector<u8>;
using MaskPtr = std::shared_ptr<const Mask>;

struct Bitmap { // decoded DIB, top-down ARGB
    int w = 0, h = 0;
    std::vector<u32> px;
};

struct Obj { // EMF object table entry
    enum Kind { NONE, PEN, BRUSH, FONT, PALETTE } kind = NONE;
    LOGPEN pen;
    LOGBRUSH brush;
    LOGFONT_LITE font;
    bool nullObj = false;                  // NULL_PEN / NULL_BRUSH
    int escapement = 0;                    // font: lfEscapement, tenths of a degree
    std::vector<int> dashes;               // pen PS_USERSTYLE entries (logical units)
    std::shared_ptr<std::vector<u32>> pat; // pattern brush tile (top-down ARGB); for mono brushes the bits 0/1
    int patW = 0, patH = 0;
    bool mono = false;                     // pattern from a 1 bit bitmap: 0 -> text colour, 1 -> background colour
};

struct State {
    double xf[6] = {1, 0, 0, 1, 0, 0}; // eM11 eM12 eM21 eM22 eDx eDy  (x' = x*eM11 + y*eM21 + eDx ; y' = x*eM12 + y*eM22 + eDy)
    int mapMode = 1;
    double winOrgX = 0, winOrgY = 0, winExtX = 1, winExtY = 1, vpOrgX = 0, vpOrgY = 0, vpExtX = 1, vpExtY = 1;
    COLORREF bk = Rgb(255, 255, 255), text = 0;
    int bkMode = 2; // OPAQUE
    u32 textAlign = 0;
    int polyFill = 1; // ALTERNATE (2 = WINDING)
    int rop2 = 13;    // R2_COPYPEN
    int arcDir = 1;   // AD_COUNTERCLOCKWISE (2 = AD_CLOCKWISE)
    Pt cur;
    int selPen = -1, selBrush = -1, selFont = -1; // object index; negative = stock (see StockObj)
    int stockPen = 7, stockBrush = 0, stockFont = 13; // stock index selected via 0x80000000|n
    RECT clip;
    bool hasClip = false;
    RECT excl;
    bool hasExcl = false;
    MaskPtr clipMask; // clip region (EXTSELECTCLIPRGN / SELECTCLIPPATH), combined with the rectangles above
    MaskPtr metaMask; // meta region (SETMETARGN): what an empty region selects
};

// ROP3 (index = P<<2 | S<<1 | D), evaluated bitwise on the 24 colour bits.
inline u32 Rop3(u32 rop, u32 P, u32 S, u32 D) {
    u32 res = 0;
    for (u32 idx = 0; idx < 8; ++idx) {
        if (!(rop >> idx & 1)) continue;
        res |= ((idx & 4) ? P : ~P) & ((idx & 2) ? S : ~S) & ((idx & 1) ? D : ~D);
    }
    return 0xff000000u | (res & 0x00ffffffu);
}
inline bool RopUsesSource(u32 rop) {
    for (u32 idx = 0; idx < 8; ++idx)
        if ((rop >> idx & 1) != (rop >> (idx ^ 2) & 1)) return true;
    return false;
}

struct Player {
    const std::vector<u8>& d;
    const Header& h;
    GdiCtx dc;                      // working context (copy of the caller's)
    RECT dest;
    std::vector<Obj> objs;
    State st;
    std::vector<State> stack;
    std::map<u32, u32>& skipped;
    u32& emfPlus;
    double fx0 = 0, fy0 = 0, fw = 1, fh = 1; // frame in reference pixels
    RECT mb{0, 0, 0, 0};                     // bounds of the clip masks (the caller's clip, inside the surface)
    int mw = 0, mh = 0;
    // path recording
    struct Fig { std::vector<Pt> pts; bool closed = false; };
    bool inPath = false;
    std::vector<Fig> path;
    bool pathReady = false;
    Mask pathMask;          // pixels added to the path by text / WidenPath
    bool hasPathMask = false;
    mutable std::vector<u32> monoTmp;
    mutable std::vector<int> dashTmp;

    Player(const std::vector<u8>& data, const Header& hdr, const GdiCtx& ctx, const RECT& rc, std::map<u32, u32>& sk, u32& plus)
        : d(data), h(hdr), dc(ctx), dest(rc), skipped(sk), emfPlus(plus) {
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
        // mask bounds: the clip rectangle of the caller, limited to the surface
        mb = dc.clip;
        if (dc.surf) {
            RECT sr{-dc.viewportOrg.x, -dc.viewportOrg.y, dc.surf->Width() - dc.viewportOrg.x, dc.surf->Height() - dc.viewportOrg.y};
            IntersectRect(&mb, &mb, &sr);
        }
        if (mb.right <= mb.left || mb.bottom <= mb.top) mb = RECT{0, 0, 0, 0};
        mw = mb.right - mb.left;
        mh = mb.bottom - mb.top;
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
        case 7: case 8:                                               // ISOTROPIC (extents fitted by fixIsotropic) / ANISOTROPIC
            sx = st.winExtX != 0 ? st.vpExtX / st.winExtX : 1.0;
            sy = st.winExtY != 0 ? st.vpExtY / st.winExtY : 1.0;
            break;
        default: sx = sy = 1.0; break;                                // MM_TEXT
        }
    }
    // MM_ISOTROPIC: GDI keeps one logical unit the same physical length on both axes by shrinking one viewport extent.
    void fixIsotropic() {
        if (st.mapMode != 7) return;
        if (st.winExtX == 0 || st.winExtY == 0 || !h.devCx || !h.devCy || !h.mmCx || !h.mmCy) return;
        double xdim = std::fabs(st.vpExtX * h.mmCx / (double(h.devCx) * st.winExtX));
        double ydim = std::fabs(st.vpExtY * h.mmCy / (double(h.devCy) * st.winExtY));
        if (xdim <= 0 || ydim <= 0) return;
        if (xdim > ydim) {
            st.vpExtX = st.vpExtX * ydim / xdim;
            if (std::fabs(st.vpExtX) < 1) st.vpExtX = st.vpExtX < 0 ? -1 : 1;
        } else {
            st.vpExtY = st.vpExtY * xdim / ydim;
            if (std::fabs(st.vpExtY) < 1) st.vpExtY = st.vpExtY < 0 ? -1 : 1;
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
        return mapDev(dx, dy);
    }
    // reference-device pixels (what regions are recorded in) -> destination
    Pt mapDev(double dx, double dy) const {
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
        double sc = totalScale();
        double w = o->pen.width.x * sc;
        g.penWidth = u32(std::max(1, Rnd(w)));
        if ((o->pen.style & 0xf) == 7 && !o->dashes.empty()) { // PS_USERSTYLE
            dashTmp.clear();
            for (int e : o->dashes) dashTmp.push_back(std::max(1, Rnd(double(e) * sc)));
            g.dashPat = dashTmp.data();
            g.dashN = int(dashTmp.size());
        }
    }
    void brushInto(GdiCtx& g, const Obj* o) const {
        g.brushColor = o->brush.color;
        g.brushStyle = 0;
        g.brushPat = nullptr;
        if (o->nullObj) g.brushStyle = 1;
        else if (o->brush.style == 2) { g.brushStyle = 2; g.brushHatch = o->brush.hatch; } // BS_HATCHED
        else if (o->pat && o->patW > 0 && o->patH > 0) {
            if (o->mono) {
                monoTmp.assign(o->pat->size(), 0);
                for (size_t i = 0; i < monoTmp.size(); ++i) monoTmp[i] = gdi::ToArgb((*o->pat)[i] ? st.bk : st.text);
                g.brushPat = monoTmp.data();
            } else {
                g.brushPat = o->pat->data();
            }
            g.brushPatW = o->patW;
            g.brushPatH = o->patH;
            g.brushStyle = 3;
        }
    }
    void setupBrush(GdiCtx& g) const {
        Obj t;
        brushInto(g, curObj(Obj::BRUSH, t));
    }
    u32 patternAt(int x, int y) const { // the "P" of a ROP: the current brush at a destination pixel
        Obj t;
        const Obj* o = curObj(Obj::BRUSH, t);
        if (o->pat && o->patW > 0 && o->patH > 0) {
            int px = ((x + dc.viewportOrg.x) % o->patW + o->patW) % o->patW, py = ((y + dc.viewportOrg.y) % o->patH + o->patH) % o->patH;
            u32 v = (*o->pat)[size_t(py) * size_t(o->patW) + size_t(px)];
            return o->mono ? gdi::ToArgb(v ? st.bk : st.text) : v;
        }
        return gdi::ToArgb(o->brush.color);
    }

    // ---- clip --------------------------------------------------------------------------------------------------
    GdiCtx clipped() const { // clip rectangle, excluded rectangle and clip region; plain copy raster op
        GdiCtx g = dc;
        g.rop2 = 13;
        if (st.hasClip) {
            g.clip.left = std::max(g.clip.left, st.clip.left);
            g.clip.top = std::max(g.clip.top, st.clip.top);
            g.clip.right = std::min(g.clip.right, st.clip.right);
            g.clip.bottom = std::min(g.clip.bottom, st.clip.bottom);
        }
        if (st.hasExcl) { g.exclude = st.excl; g.hasExclude = true; }
        if (st.clipMask) { g.clipMask = st.clipMask->data(); g.maskRect = mb; }
        return g;
    }
    GdiCtx shapeCtx() const { // + pen, brush, raster op, fill mode, background (hatch gaps, dash gaps)
        GdiCtx g = clipped();
        g.rop2 = st.rop2;
        g.polyWinding = st.polyFill == 2;
        g.bkOpaque = st.bkMode == 2;
        g.bkColor = st.bk;
        setupPen(g);
        setupBrush(g);
        return g;
    }
    Mask zeroMask() const { return Mask(size_t(mw) * size_t(mh), 0); }
    Mask materializeClip() const { // the current clip as a mask over mb
        Mask m(size_t(mw) * size_t(mh), 1);
        for (int y = 0; y < mh; ++y)
            for (int x = 0; x < mw; ++x) {
                int X = mb.left + x, Y = mb.top + y;
                bool v = true;
                if (st.hasClip && (X < st.clip.left || X >= st.clip.right || Y < st.clip.top || Y >= st.clip.bottom)) v = false;
                if (v && st.hasExcl && X >= st.excl.left && X < st.excl.right && Y >= st.excl.top && Y < st.excl.bottom) v = false;
                if (v && st.clipMask && !(*st.clipMask)[size_t(y) * size_t(mw) + size_t(x)]) v = false;
                m[size_t(y) * size_t(mw) + size_t(x)] = v ? 1 : 0;
            }
        return m;
    }
    // ExtSelectClipRgn modes: RGN_AND 1, RGN_OR 2, RGN_XOR 3, RGN_DIFF 4, RGN_COPY 5.  region == nullptr is the empty / NULL region.
    void applyRegion(u32 mode, const Mask* region) {
        if (mode == 5) {
            st.hasClip = false;
            st.hasExcl = false;
            if (!region) { st.clipMask = st.metaMask; return; }
            Mask m = *region;
            if (st.metaMask)
                for (size_t i = 0; i < m.size(); ++i) m[i] = m[i] && (*st.metaMask)[i];
            st.clipMask = std::make_shared<const Mask>(std::move(m));
            return;
        }
        Mask cur = materializeClip();
        for (size_t i = 0; i < cur.size(); ++i) {
            bool a = cur[i] != 0, b = region && (*region)[i] != 0;
            switch (mode) {
            case 1: a = a && b; break;
            case 2: a = a || b; break;
            case 3: a = a != b; break;
            case 4: a = a && !b; break;
            default: break;
            }
            cur[i] = a ? 1 : 0;
        }
        st.hasClip = false;
        st.hasExcl = false;
        st.clipMask = std::make_shared<const Mask>(std::move(cur));
    }
    Mask renderMask(const std::function<void(GdiCtx&)>& draw) const { // draws into a scratch surface, non-zero pixels = mask
        Mask m = zeroMask();
        if (!mw || !mh) return m;
        Surface s(mw, mh);
        GdiCtx t;
        t.surf = &s;
        t.viewportOrg = POINT{-mb.left, -mb.top};
        t.clip = mb;
        draw(t);
        const uint32_t* p = s.Pixels();
        for (size_t i = 0; i < m.size(); ++i) m[i] = p[i] != 0;
        return m;
    }
    void fillRectMask(Mask& m, int L, int T, int R, int B) const {
        L = std::max(L, mb.left); T = std::max(T, mb.top); R = std::min(R, mb.right); B = std::min(B, mb.bottom);
        for (int y = T; y < B; ++y)
            for (int x = L; x < R; ++x) m[size_t(y - mb.top) * size_t(mw) + size_t(x - mb.left)] = 1;
    }
    // RGNDATA (header 32 bytes, RDH_RECTANGLES, RECT list) in reference device pixels -> mask
    bool regionMask(const u8* data, size_t cb, Mask& out) const {
        if (cb < 32 || U32(data + 4) != 1) return false;
        u32 n = U32(data + 8);
        if (n > 200000 || 32 + size_t(n) * 16 > cb) return false;
        out = zeroMask();
        for (u32 i = 0; i < n; ++i) {
            const u8* r = data + 32 + 16 * i;
            Pt a = mapDev(I32(r), I32(r + 4)), b = mapDev(I32(r + 8), I32(r + 12));
            fillRectMask(out, Rnd(std::min(a.x, b.x)), Rnd(std::min(a.y, b.y)), Rnd(std::max(a.x, b.x)), Rnd(std::max(a.y, b.y)));
        }
        return true;
    }
    template <class F> void forMask(const Mask& m, F put) const { // put(x, y) in view coordinates
        for (int y = 0; y < mh; ++y)
            for (int x = 0; x < mw; ++x)
                if (m[size_t(y) * size_t(mw) + size_t(x)]) put(mb.left + x, mb.top + y);
    }

    // ---- scan conversion of several figures at once (holes through even-odd / winding) --------------------------
    template <class F> void scanFill(const std::vector<std::vector<Pt>>& figs, bool winding, F put) const {
        std::vector<std::vector<POINT>> ps;
        int ymin = INT_MAX, ymax = INT_MIN;
        for (const auto& f : figs) {
            std::vector<POINT> q;
            for (const Pt& p : f) {
                q.push_back(POINT{Rnd(p.x), Rnd(p.y)});
                ymin = std::min(ymin, q.back().y);
                ymax = std::max(ymax, q.back().y);
            }
            if (q.size() >= 2) ps.push_back(std::move(q));
        }
        std::vector<std::pair<double, int>> xs;
        for (int y = ymin; y < ymax; ++y) {
            xs.clear();
            double cy = y + 0.5;
            for (const auto& q : ps)
                for (size_t i = 0; i < q.size(); ++i) {
                    const POINT& a = q[i];
                    const POINT& b = q[(i + 1) % q.size()];
                    if ((a.y <= cy && b.y > cy) || (b.y <= cy && a.y > cy))
                        xs.push_back({a.x + (cy - a.y) * (double(b.x) - a.x) / (double(b.y) - a.y), b.y > a.y ? 1 : -1});
                }
            std::sort(xs.begin(), xs.end());
            if (!winding) {
                for (size_t k = 0; k + 1 < xs.size(); k += 2)
                    for (int x = int(std::ceil(xs[k].first - 0.5)); x < int(std::ceil(xs[k + 1].first - 0.5)); ++x) put(x, y);
            } else {
                int wind = 0;
                for (size_t k = 0; k + 1 < xs.size(); ++k) {
                    wind += xs[k].second;
                    if (wind != 0)
                        for (int x = int(std::ceil(xs[k].first - 0.5)); x < int(std::ceil(xs[k + 1].first - 0.5)); ++x) put(x, y);
                }
            }
        }
    }

    // ---- shape sinks (draw, or record into a path) -----------------------------------------------------------------
    static bool dashed(const GdiCtx& g) {
        u32 s = g.penStyle & 0xf;
        return !g.penNull && ((s >= 1 && s <= 4 && g.penWidth == 1) || (s == 7 && g.dashN > 0));
    }
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
        GdiCtx g = shapeCtx();
        if (pts.empty()) return;
        if (closed && fill) gdi::Polygon(g, pts.data(), int(pts.size()));
        else if (closed) { pts.push_back(pts[0]); gdi::Polyline(g, pts.data(), int(pts.size())); }
        else gdi::Polyline(g, pts.data(), int(pts.size()));
    }

    void box(const u8* rec, int kind, int cornerW = 0, int cornerH = 0) { // 0 rect, 1 ellipse, 2 roundrect
        double l = I32(rec + 24), t = I32(rec + 28), r = I32(rec + 32), b = I32(rec + 36);
        Pt p0 = map(l, t), p1 = map(r, b);
        if (!inPath && axisAligned()) {
            GdiCtx g = shapeCtx();
            int L = Rnd(std::min(p0.x, p1.x)), R = Rnd(std::max(p0.x, p1.x));
            int T = Rnd(std::min(p0.y, p1.y)), B = Rnd(std::max(p0.y, p1.y));
            Pt c0 = map(0, 0), c1 = map(cornerW, cornerH);
            int rw = std::abs(Rnd(c1.x - c0.x)), rh = std::abs(Rnd(c1.y - c0.y));
            if (dashed(g)) { // dashed outline: fill without pen, then walk the outline as a dashed polyline
                GdiCtx f = g;
                f.penNull = true;
                if (kind == 0) gdi::Rectangle(f, L, T, R, B);
                else if (kind == 1) gdi::Ellipse(f, L, T, R, B);
                else gdi::RoundRect(f, L, T, R, B, rw, rh);
                std::vector<POINT> o;
                if (kind == 0) o = {{L, T}, {R - 1, T}, {R - 1, B - 1}, {L, B - 1}, {L, T}};
                else {
                    double cx = (L + R - 1) / 2.0, cy = (T + B - 1) / 2.0, ax = (R - 1 - L) / 2.0, ay = (B - 1 - T) / 2.0;
                    for (int i = 0; i <= 64; ++i) {
                        double a = 2 * kPi * i / 64;
                        o.push_back(POINT{Rnd(cx + ax * std::cos(a)), Rnd(cy + ay * std::sin(a))});
                    }
                }
                gdi::Polyline(g, o.data(), int(o.size()));
                return;
            }
            if (kind == 0) gdi::Rectangle(g, L, T, R, B);
            else if (kind == 1) gdi::Ellipse(g, L, T, R, B);
            else gdi::RoundRect(g, L, T, R, B, rw, rh);
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

    void arc(const u8* rec, int kind) { // 0 arc, 1 chord, 2 pie, 3 arcto ; rec: box at 24, start at 40, end at 48
        double l = I32(rec + 24), t = I32(rec + 28), r = I32(rec + 32), b = I32(rec + 36);
        double cx = (l + r) / 2, cy = (t + b) / 2, rx = (r - l) / 2, ry = (b - t) / 2;
        if (rx == 0 || ry == 0) return;
        auto ang = [&](double px, double py) { return std::atan2(-(py - cy) / ry, (px - cx) / rx); };
        double a0 = ang(I32(rec + 40), I32(rec + 44)), a1 = ang(I32(rec + 48), I32(rec + 52));
        arcPoints(cx, cy, rx, ry, a0, a1, kind);
    }
    void arcPoints(double cx, double cy, double rx, double ry, double a0, double a1, int kind) {
        double sweep = a1 - a0;
        if (st.arcDir == 2) { while (sweep >= 0) sweep -= 2 * kPi; } // AD_CLOCKWISE
        else { while (sweep <= 0) sweep += 2 * kPi; }                 // AD_COUNTERCLOCKWISE
        int n = std::max(8, int(std::fabs(sweep) / (2 * kPi) * 64));
        std::vector<Pt> pts;
        if (kind == 2) pts.push_back(map(cx, cy));
        if (kind == 3) pts.push_back(map(st.cur.x, st.cur.y)); // ARCTO: a line from the current position to the start of the arc
        for (int i = 0; i <= n; ++i) {
            double a = a0 + sweep * i / n;
            pts.push_back(map(cx + rx * std::cos(a), cy - ry * std::sin(a)));
        }
        if (kind == 3) st.cur = Pt{cx + rx * std::cos(a1), cy - ry * std::sin(a1)}; // the end of the arc becomes the current position
        poly(pts, kind == 1 || kind == 2, true);
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
    static u32 Chan(u32 v, u32 mask, int bits8) { // extracts the field of `mask` from v and scales it to 8 bits
        if (!mask) return 0;
        int sh = 0;
        while (!((mask >> sh) & 1)) ++sh;
        u32 m = mask >> sh;
        u32 f = (v & mask) >> sh;
        (void)bits8;
        return m ? u32((uint64_t(f) * 255 + m / 2) / m) : 0;
    }
    bool decodeDib(const u8* bmi, size_t bmiLen, const u8* bits, size_t bitsLen, Bitmap& bm, bool keepAlpha = false) const {
        if (bmiLen < 40) return false;
        u32 hs = U32(bmi);
        if (hs < 40 || hs > bmiLen) return false;
        int w = I32(bmi + 4);
        int hh = I32(bmi + 8);
        u16 bpp = U16(bmi + 14);
        u32 comp = U32(bmi + 16), used = U32(bmi + 32);
        if (w <= 0 || hh == 0 || w > 8192 || std::abs(hh) > 8192 || (comp != 0 && comp != 3)) return false; // BI_RGB, BI_BITFIELDS
        if (comp == 3 && bpp != 16 && bpp != 32) return false;
        int h2 = std::abs(hh);
        u32 rm = 0, gm = 0, bm2 = 0, am = 0;
        if (comp == 3) {
            if (hs >= 52) { rm = U32(bmi + 40); gm = U32(bmi + 44); bm2 = U32(bmi + 48); if (hs >= 56) am = U32(bmi + 52); }
            else if (bmiLen >= 52) { rm = U32(bmi + 40); gm = U32(bmi + 44); bm2 = U32(bmi + 48); }
            else return false;
        }
        u32 ncol = bpp <= 8 ? (used ? used : (1u << bpp)) : 0;
        const u8* pal = bmi + hs;
        if (hs + size_t(ncol) * 4 > bmiLen) return false;
        size_t stride = ((size_t(w) * bpp + 31) / 32) * 4;
        if (bitsLen < stride * size_t(h2)) return false;
        bm.w = w;
        bm.h = h2;
        bm.px.assign(size_t(w) * size_t(h2), 0xff000000u);
        for (int y = 0; y < h2; ++y) {
            const u8* row = bits + stride * size_t(hh < 0 ? y : h2 - 1 - y);
            for (int x = 0; x < w; ++x) {
                u32 c = 0, a = 0xff;
                auto quad = [&](u32 i) { const u8* q = pal + 4 * (i < ncol ? i : 0); return u32(q[2]) << 16 | u32(q[1]) << 8 | q[0]; };
                switch (bpp) {
                case 1: c = quad((row[x >> 3] >> (7 - (x & 7))) & 1); break;
                case 4: c = quad((x & 1) ? (row[x >> 1] & 15) : (row[x >> 1] >> 4)); break;
                case 8: c = quad(row[x]); break;
                case 16: {
                    u16 v = U16(row + 2 * x);
                    if (comp == 3) c = Chan(v, rm, 8) << 16 | Chan(v, gm, 8) << 8 | Chan(v, bm2, 8);
                    else c = u32((v >> 10 & 31) * 255 / 31) << 16 | u32((v >> 5 & 31) * 255 / 31) << 8 | u32((v & 31) * 255 / 31); // RGB555
                    break;
                }
                case 24: c = u32(row[3 * x + 2]) << 16 | u32(row[3 * x + 1]) << 8 | row[3 * x]; break;
                case 32:
                    if (comp == 3) {
                        u32 v = U32(row + 4 * x);
                        c = Chan(v, rm, 8) << 16 | Chan(v, gm, 8) << 8 | Chan(v, bm2, 8);
                        if (keepAlpha && am) a = Chan(v, am, 8);
                    } else {
                        c = u32(row[4 * x + 2]) << 16 | u32(row[4 * x + 1]) << 8 | row[4 * x];
                        if (keepAlpha) a = row[4 * x + 3];
                    }
                    break;
                default: return false;
                }
                bm.px[size_t(y) * size_t(w) + size_t(x)] = a << 24 | (c & 0xffffffu);
            }
        }
        return true;
    }
    // 1 bit bitmap -> the bit values (1 = set), top-down; used for masks and monochrome brushes
    bool decodeMono(const u8* bmi, size_t bmiLen, const u8* bits, size_t bitsLen, Bitmap& bm) const {
        if (bmiLen < 40 || U32(bmi) < 40 || U32(bmi + 16) != 0 || U16(bmi + 14) != 1) return false;
        int w = I32(bmi + 4), hh = I32(bmi + 8);
        if (w <= 0 || hh == 0 || w > 8192 || std::abs(hh) > 8192) return false;
        int h2 = std::abs(hh);
        size_t stride = ((size_t(w) + 31) / 32) * 4;
        if (bitsLen < stride * size_t(h2)) return false;
        bm.w = w;
        bm.h = h2;
        bm.px.assign(size_t(w) * size_t(h2), 0);
        for (int y = 0; y < h2; ++y) {
            const u8* row = bits + stride * size_t(hh < 0 ? y : h2 - 1 - y);
            for (int x = 0; x < w; ++x) bm.px[size_t(y) * size_t(w) + size_t(x)] = (row[x >> 3] >> (7 - (x & 7))) & 1;
        }
        return true;
    }
    // Draws the source rectangle (sx,sy,sw,sh) of the bitmap into the destination box (logical dx,dy,dw,dh) with nearest sampling;
    // fn(x, y, u, v, src, dst, out) decides the resulting pixel (u, v = source pixel inside the rectangle), false = leave as is.
    template <class F>
    void blitF(const Bitmap& bm, int sx, int sy, int sw, int sh, double dx, double dy, double dw, double dh, F fn) {
        if (sw <= 0 || sh <= 0 || bm.px.empty()) return;
        if (sx < 0 || sy < 0 || sx + sw > bm.w || sy + sh > bm.h) {
            sx = std::max(0, sx); sy = std::max(0, sy);
            sw = std::min(sw, bm.w - sx); sh = std::min(sh, bm.h - sy);
            if (sw <= 0 || sh <= 0) return;
        }
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
                u32 s = bm.px[size_t(sy + v) * size_t(bm.w) + size_t(sx + u)], dd = 0, out = 0;
                gdi::GetPixel(g, x, y, dd);
                if (fn(x, y, u, v, s, dd, out)) gdi::PutPixelCopy(g, x, y, out);
            }
    }
    // ternary raster operation blit; rop = index (0..255)
    void blitRop(const Bitmap& bm, int sx, int sy, int sw, int sh, double dx, double dy, double dw, double dh, u32 rop) {
        if (rop == 0xCC) {
            blitF(bm, sx, sy, sw, sh, dx, dy, dw, dh, [](int, int, int, int, u32 s, u32, u32& o) { o = 0xff000000u | (s & 0xffffffu); return true; });
            return;
        }
        blitF(bm, sx, sy, sw, sh, dx, dy, dw, dh, [&](int x, int y, int, int, u32 s, u32 dd, u32& o) {
            o = Rop3(rop, patternAt(x, y) & 0xffffffu, s & 0xffffffu, dd & 0xffffffu);
            return true;
        });
    }
    // no source bitmap: pattern / constant ROPs on a destination rectangle
    void fillRop(double x, double y, double w, double h2, u32 rop) {
        Pt p0 = map(x, y), p1 = map(x + w, y + h2);
        int L = Rnd(std::min(p0.x, p1.x)), R = Rnd(std::max(p0.x, p1.x));
        int T = Rnd(std::min(p0.y, p1.y)), B = Rnd(std::max(p0.y, p1.y));
        GdiCtx g = clipped();
        for (int yy = T; yy < B; ++yy)
            for (int xx = L; xx < R; ++xx) {
                u32 dd = 0;
                gdi::GetPixel(g, xx, yy, dd);
                gdi::PutPixelCopy(g, xx, yy, Rop3(rop, patternAt(xx, yy) & 0xffffffu, 0, dd & 0xffffffu));
            }
    }

    // ---- text -------------------------------------------------------------------------------------------------------
    // One text run (EXTTEXTOUT, POLYTEXTOUT, SMALLTEXTOUT).  rx, ry: reference point (logical); rcl: ETO rectangle (logical) or null;
    // dx: per-character advances (logical) or null.
    void drawText(const std::string& s, double rx, double ry, u32 opts, const RECT* rcl, const std::vector<i32>* dx) {
        if (s.empty()) return;
        Obj t;
        const Obj* fo = curObj(Obj::FONT, t);
        LOGFONT_LITE f = fo->font;
        double sc = totalScale();
        int hpx = Rnd(std::fabs(double(f.height)) * sc);
        f.height = f.height < 0 ? -std::max(1, hpx) : std::max(1, hpx);
        u32 ta = st.textAlign;
        if (ta & 1) { rx = st.cur.x; ry = st.cur.y; } // TA_UPDATECP: the reference point is the current position
        RECT rc{0, 0, 0, 0};
        int th = gdi::TextRect(f, s.c_str(), int(s.size()), rc, DT_SINGLELINE_ | 0x800 /*DT_NOPREFIX*/ | 0x400 /*DT_CALCRECT*/);
        int tw = rc.right - rc.left;
        if (th <= 0) th = std::max(1, hpx);
        if (dx && dx->size() >= s.size()) { // explicit advances: the run is as wide as their sum
            double sum = 0;
            for (size_t i = 0; i < s.size(); ++i) sum += (*dx)[i];
            Pt a = map(0, 0), b = map(sum, 0);
            tw = std::max(tw, Rnd(std::hypot(b.x - a.x, b.y - a.y)));
        }
        Pt p = map(rx, ry);
        int x = Rnd(p.x), y = Rnd(p.y);
        int ox = 0, oy = 0; // reference point inside the unrotated text cell
        int h2 = ta & 6; // TA_LEFT 0, TA_RIGHT 2, TA_CENTER 6
        if (h2 == 6) ox = tw / 2;
        else if (h2 == 2) ox = tw;
        int v = ta & 24; // TA_TOP 0, TA_BOTTOM 8, TA_BASELINE 24
        if (v == 8) oy = th;
        else if (v == 24) oy = gdi::TextAscent(f);
        GdiCtx g = clipped();
        g.textColor = st.text;
        if ((opts & 4 /*ETO_CLIPPED*/) && rcl) {
            Pt a = map(rcl->left, rcl->top), b = map(rcl->right, rcl->bottom);
            RECT c{Rnd(std::min(a.x, b.x)), Rnd(std::min(a.y, b.y)), Rnd(std::max(a.x, b.x)), Rnd(std::max(a.y, b.y))};
            IntersectRect(&g.clip, &g.clip, &c);
        }
        auto drawAt = [&](GdiCtx& gc, const RECT& out) {
            if (dx && dx->size() >= s.size()) { // one character at a time
                double cum = 0;
                for (size_t i = 0; i < s.size(); ++i) {
                    Pt a = map(rx + cum, ry), o = map(rx, ry);
                    RECT cell{out.left + Rnd(a.x - o.x), out.top, out.left + Rnd(a.x - o.x) + tw, out.bottom};
                    cell.top += Rnd(a.y - o.y);
                    cell.bottom += Rnd(a.y - o.y);
                    gdi::TextDraw(gc, f, s.c_str() + i, 1, cell, DT_SINGLELINE_ | 0x800);
                    cum += (*dx)[i];
                }
            } else {
                gdi::TextDraw(gc, f, s.c_str(), int(s.size()), out, DT_SINGLELINE_ | 0x800);
            }
        };
        if (inPath) { // text in a path: its glyph pixels join the path (fill / stroke / clip)
            if (fo->escapement == 0) {
                RECT out{x - ox, y - oy, x - ox + tw, y - oy + th};
                Mask m = renderMask([&](GdiCtx& tc) { tc.textColor = 0xffffff; drawAt(tc, out); });
                if (!hasPathMask) { pathMask = zeroMask(); hasPathMask = true; }
                for (size_t i = 0; i < m.size(); ++i) pathMask[i] |= m[i];
            }
            return;
        }
        if (fo->escapement == 0) {
            RECT out{x - ox, y - oy, x - ox + tw, y - oy + th};
            if ((opts & 2 /*ETO_OPAQUE*/) && rcl) { // the rectangle is painted with the background colour
                Pt a = map(rcl->left, rcl->top), b = map(rcl->right, rcl->bottom);
                int L = Rnd(std::min(a.x, b.x)), R = Rnd(std::max(a.x, b.x)), T = Rnd(std::min(a.y, b.y)), B = Rnd(std::max(a.y, b.y));
                for (int yy = T; yy < B; ++yy)
                    for (int xx = L; xx < R; ++xx) gdi::PutPixelCopy(g, xx, yy, gdi::ToArgb(st.bk));
            } else if (st.bkMode == 2) { // OPAQUE mode paints behind the glyph cell
                for (int yy = out.top; yy < out.bottom; ++yy)
                    for (int xx = out.left; xx < out.right; ++xx) gdi::PutPixelCopy(g, xx, yy, gdi::ToArgb(st.bk));
            }
            drawAt(g, out);
        } else { // escapement: draw the unrotated cell into a scratch surface and rotate it around the reference point
            Surface tmp(std::max(1, tw), std::max(1, th));
            GdiCtx tc;
            tc.surf = &tmp;
            tc.viewportOrg = POINT{0, 0};
            tc.clip = RECT{0, 0, tw, th};
            tc.textColor = st.text;
            gdi::TextDraw(tc, f, s.c_str(), int(s.size()), RECT{0, 0, tw, th}, DT_SINGLELINE_ | 0x800);
            double ang = fo->escapement / 10.0 * kPi / 180.0, ca = std::cos(ang), sa = std::sin(ang);
            int r = tw + th + 2;
            for (int yy = y - r; yy <= y + r; ++yy)
                for (int xx = x - r; xx <= x + r; ++xx) {
                    double ddx = xx + 0.5 - x, ddy = yy + 0.5 - y;
                    double relx = ddx * ca - ddy * sa, rely = ddx * sa + ddy * ca;
                    int i = int(std::floor(relx + ox)), j = int(std::floor(rely + oy));
                    if (i < 0 || j < 0 || i >= tw || j >= th) continue;
                    u32 px = tmp.Row(j)[i];
                    if (px) gdi::PutPixelCopy(g, xx, yy, px);
                }
        }
        if (ta & 1) st.cur.x += tw / (sc > 0 ? sc : 1.0); // TA_UPDATECP: advance the current position
    }
    static std::string Chars(const u8* p, u32 n, bool wide) {
        std::string s;
        for (u32 i = 0; i < n; ++i) {
            u32 c = wide ? U16(p + 2 * i) : p[i];
            s.push_back(c < 256 ? char(c) : '?');
        }
        return s;
    }
    // EMRTEXT at rec + off: ptlReference, nChars, offString, fOptions, rcl, offDx (offsets relative to the record start)
    void emrText(const u8* rec, size_t recLen, size_t off, bool wide) {
        if (off + 40 > recLen) return;
        i32 rx = I32(rec + off), ry = I32(rec + off + 4);
        u32 n = U32(rec + off + 8), offStr = U32(rec + off + 12), opts = U32(rec + off + 16), offDx = U32(rec + off + 36);
        if (n == 0 || n > 4096 || size_t(offStr) + size_t(n) * (wide ? 2 : 1) > recLen) return;
        RECT rcl{I32(rec + off + 20), I32(rec + off + 24), I32(rec + off + 28), I32(rec + off + 32)};
        std::vector<i32> dx;
        if (offDx && size_t(offDx) + size_t(n) * 4 <= recLen) {
            u32 step = (opts & 0x2000 /*ETO_PDY*/) ? 2 : 1;
            if (size_t(offDx) + size_t(n) * 4 * step <= recLen)
                for (u32 i = 0; i < n; ++i) dx.push_back(I32(rec + offDx + 4 * i * step));
        }
        drawText(Chars(rec + offStr, n, wide), rx, ry, opts, (opts & 6) ? &rcl : nullptr, dx.empty() ? nullptr : &dx);
    }
    void smallText(const u8* rec, size_t recLen) { // EMR_SMALLTEXTOUT
        if (recLen < 36) return;
        i32 rx = I32(rec + 8), ry = I32(rec + 12);
        u32 n = U32(rec + 16), opts = U32(rec + 20);
        size_t off = 36;
        RECT rcl{0, 0, 0, 0};
        bool hasRect = !(opts & 0x100 /*ETO_NO_RECT*/);
        if (hasRect) {
            if (recLen < 52) return;
            rcl = RECT{I32(rec + 36), I32(rec + 40), I32(rec + 44), I32(rec + 48)};
            off = 52;
        }
        bool wide = !(opts & 0x200 /*ETO_SMALL_CHARS*/);
        if (n == 0 || n > 4096 || off + size_t(n) * (wide ? 2 : 1) > recLen) return;
        drawText(Chars(rec + off, n, wide), rx, ry, opts, hasRect ? &rcl : nullptr, nullptr);
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
        std::vector<std::vector<Pt>> figs;
        for (u32 k = 0; k < np; ++k) {
            u32 c = U32(counts + 4 * k);
            std::vector<Pt> p;
            for (u32 i = 0; i < c && at + i < total; ++i) {
                u32 j = at + i;
                if (is16) p.push_back(map(I16(pts + 4 * j), I16(pts + 4 * j + 2)));
                else p.push_back(map(I32(pts + 8 * j), I32(pts + 8 * j + 4)));
            }
            at += c;
            figs.push_back(p);
        }
        if (inPath) {
            for (auto& f : figs) { Fig fg; fg.pts = f; fg.closed = closed; path.push_back(fg); }
            return;
        }
        GdiCtx g = shapeCtx();
        if (closed && g.brushStyle != 1) { // all polygons are filled together: overlaps and nested figures follow the fill mode
            GdiCtx f = g;
            scanFill(figs, g.polyWinding, [&](int x, int y) { gdi::FillPixel(f, x, y); });
        }
        for (auto& f : figs) {
            std::vector<POINT> pp;
            for (const Pt& q : f) pp.push_back(POINT{Rnd(q.x), Rnd(q.y)});
            if (pp.empty()) continue;
            if (closed) pp.push_back(pp[0]);
            gdi::Polyline(g, pp.data(), int(pp.size()));
        }
    }
    // EMR_POLYDRAW / POLYDRAW16: points from the current position; types PT_MOVETO 6, PT_LINETO 2, PT_BEZIERTO 4, + PT_CLOSEFIGURE 1
    void polyDraw(const u8* rec, size_t recLen, bool is16) {
        u32 n = U32(rec + 24);
        size_t ptSize = is16 ? 4 : 8;
        if (n > 100000 || 28 + size_t(n) * ptSize + n > recLen) return;
        const u8* types = rec + 28 + size_t(n) * ptSize;
        std::vector<Pt> logical(n), raw(n);
        for (u32 i = 0; i < n; ++i) {
            double x = is16 ? I16(rec + 28 + 4 * i) : I32(rec + 28 + 8 * i), y = is16 ? I16(rec + 30 + 4 * i) : I32(rec + 32 + 8 * i);
            logical[i] = Pt{x, y};
            raw[i] = map(x, y);
        }
        std::vector<Pt> cur;
        auto flush = [&](bool closed) {
            if (cur.size() >= 2 || (closed && !cur.empty())) poly(cur, closed, false);
            cur.clear();
        };
        cur.push_back(map(st.cur.x, st.cur.y));
        for (u32 i = 0; i < n; ++i) {
            u8 ty = types[i] & ~1u;
            bool close = types[i] & 1;
            if (ty == 6) { flush(false); cur.push_back(raw[i]); st.cur = logical[i]; }
            else if (ty == 2) { cur.push_back(raw[i]); st.cur = logical[i]; }
            else if (ty == 4 && i + 2 < n) {
                bezier(cur, raw, i);
                st.cur = logical[i + 2];
                i += 2;
                close = types[i] & 1;
            }
            if (close) flush(true);
        }
        flush(false);
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

    // ---- paths ---------------------------------------------------------------------------------------------------------
    Mask pathToMask() const { // interior of the path (figures through the fill mode) plus the text / widened pixels
        Mask m = hasPathMask ? pathMask : zeroMask();
        std::vector<std::vector<Pt>> figs;
        for (const Fig& f : path) if (f.pts.size() >= 3) figs.push_back(f.pts);
        scanFill(figs, st.polyFill == 2, [&](int x, int y) {
            if (x >= mb.left && x < mb.right && y >= mb.top && y < mb.bottom) m[size_t(y - mb.top) * size_t(mw) + size_t(x - mb.left)] = 1;
        });
        return m;
    }
    void endPathDraw(bool fill, bool stroke) {
        GdiCtx g = shapeCtx();
        if (fill) {
            std::vector<std::vector<Pt>> figs;
            for (const Fig& f : path) if (f.pts.size() >= 3) figs.push_back(f.pts);
            scanFill(figs, g.polyWinding, [&](int x, int y) { gdi::FillPixel(g, x, y); });
            if (hasPathMask) forMask(pathMask, [&](int x, int y) { gdi::FillPixel(g, x, y); });
        }
        if (stroke) {
            std::vector<POINT> tmp;
            for (const Fig& f : path) {
                tmp.clear();
                for (const Pt& q : f.pts) tmp.push_back(POINT{Rnd(q.x), Rnd(q.y)});
                if (tmp.empty()) continue;
                if (f.closed) tmp.push_back(tmp[0]);
                gdi::Polyline(g, tmp.data(), int(tmp.size()));
            }
            if (hasPathMask) { // outline of the text: the pixels that touch the outside
                forMask(pathMask, [&](int x, int y) {
                    auto at = [&](int xx, int yy) {
                        if (xx < mb.left || xx >= mb.right || yy < mb.top || yy >= mb.bottom) return false;
                        return pathMask[size_t(yy - mb.top) * size_t(mw) + size_t(xx - mb.left)] != 0;
                    };
                    if (!at(x - 1, y) || !at(x + 1, y) || !at(x, y - 1) || !at(x, y + 1)) gdi::PutPixel(g, x, y, gdi::ToArgb(g.penColor));
                });
            }
        }
    }
    void clearPath() { path.clear(); pathReady = false; pathMask.clear(); hasPathMask = false; }

    // ---- regions -------------------------------------------------------------------------------------------------------
    const Obj* brushObj(u32 ih) const { return ih < objs.size() && objs[ih].kind == Obj::BRUSH ? &objs[ih] : nullptr; }
    void paintMask(const Mask& m, const Obj* brush) {
        GdiCtx g = clipped();
        g.bkOpaque = st.bkMode == 2;
        g.bkColor = st.bk;
        brushInto(g, brush);
        forMask(m, [&](int x, int y) { gdi::FillPixel(g, x, y); });
    }
    void frameMask(const Mask& m, const Obj* brush, int fw2, int fh2) {
        auto at = [&](int x, int y) {
            if (x < 0 || x >= mw || y < 0 || y >= mh) return false;
            return m[size_t(y) * size_t(mw) + size_t(x)] != 0;
        };
        Mask frame = zeroMask();
        for (int y = 0; y < mh; ++y)
            for (int x = 0; x < mw; ++x)
                if (at(x, y) && !(at(x - fw2, y) && at(x + fw2, y) && at(x, y - fh2) && at(x, y + fh2))) frame[size_t(y) * size_t(mw) + size_t(x)] = 1;
        paintMask(frame, brush);
    }

    // ---- gradient fill ----------------------------------------------------------------------------------------------------
    void gradientFill(const u8* r, size_t L) {
        if (L < 36) return;
        u32 nVer = U32(r + 24), nTri = U32(r + 28), mode = U32(r + 32);
        if (nVer > 100000 || nTri > 100000) return;
        size_t verEnd = 36 + size_t(nVer) * 16;
        size_t idxSize = mode == 2 ? 12 : 8;
        if (verEnd + size_t(nTri) * idxSize > L) return;
        struct V { double x, y; double c[3]; };
        std::vector<V> v(nVer);
        for (u32 i = 0; i < nVer; ++i) {
            const u8* p = r + 36 + 16 * i;
            v[i].x = I32(p);
            v[i].y = I32(p + 4);
            v[i].c[0] = U16(p + 8) / 257.0;   // 16 bit COLOR16 -> 0..255
            v[i].c[1] = U16(p + 10) / 257.0;
            v[i].c[2] = U16(p + 12) / 257.0;
        }
        GdiCtx g = clipped();
        auto rgb = [](double a, double b, double c) { return 0xff000000u | u32(std::clamp(int(a + 0.5), 0, 255)) << 16 | u32(std::clamp(int(b + 0.5), 0, 255)) << 8 | u32(std::clamp(int(c + 0.5), 0, 255)); };
        for (u32 k = 0; k < nTri; ++k) {
            const u8* ix = r + verEnd + k * idxSize;
            if (mode == 2) { // GRADIENT_FILL_TRIANGLE: barycentric interpolation
                u32 a = U32(ix), b = U32(ix + 4), c = U32(ix + 8);
                if (a >= nVer || b >= nVer || c >= nVer) continue;
                Pt pa = map(v[a].x, v[a].y), pb = map(v[b].x, v[b].y), pc = map(v[c].x, v[c].y);
                double den = (pb.y - pc.y) * (pa.x - pc.x) + (pc.x - pb.x) * (pa.y - pc.y);
                if (std::fabs(den) < 1e-9) continue;
                int L2 = Rnd(std::min({pa.x, pb.x, pc.x}) - 1), R2 = Rnd(std::max({pa.x, pb.x, pc.x}) + 1);
                int T2 = Rnd(std::min({pa.y, pb.y, pc.y}) - 1), B2 = Rnd(std::max({pa.y, pb.y, pc.y}) + 1);
                for (int y = T2; y < B2; ++y)
                    for (int x = L2; x < R2; ++x) {
                        double px = x + 0.5, py = y + 0.5;
                        double w0 = ((pb.y - pc.y) * (px - pc.x) + (pc.x - pb.x) * (py - pc.y)) / den;
                        double w1 = ((pc.y - pa.y) * (px - pc.x) + (pa.x - pc.x) * (py - pc.y)) / den;
                        double w2 = 1 - w0 - w1;
                        if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
                        gdi::PutPixelCopy(g, x, y, rgb(w0 * v[a].c[0] + w1 * v[b].c[0] + w2 * v[c].c[0], w0 * v[a].c[1] + w1 * v[b].c[1] + w2 * v[c].c[1],
                                                     w0 * v[a].c[2] + w1 * v[b].c[2] + w2 * v[c].c[2]));
                    }
            } else { // rectangles: horizontal (0) or vertical (1) ramp between the upper-left and lower-right vertex colours
                u32 a = U32(ix), b = U32(ix + 4);
                if (a >= nVer || b >= nVer) continue;
                Pt pa = map(v[a].x, v[a].y), pb = map(v[b].x, v[b].y);
                int L2 = Rnd(std::min(pa.x, pb.x)), R2 = Rnd(std::max(pa.x, pb.x)), T2 = Rnd(std::min(pa.y, pb.y)), B2 = Rnd(std::max(pa.y, pb.y));
                for (int y = T2; y < B2; ++y)
                    for (int x = L2; x < R2; ++x) {
                        double t = mode == 0 ? (R2 - L2 > 1 ? double(x - L2) / (R2 - L2 - 1) : 0.0) : (B2 - T2 > 1 ? double(y - T2) / (B2 - T2 - 1) : 0.0);
                        gdi::PutPixelCopy(g, x, y, rgb(v[a].c[0] + (v[b].c[0] - v[a].c[0]) * t, v[a].c[1] + (v[b].c[1] - v[a].c[1]) * t, v[a].c[2] + (v[b].c[2] - v[a].c[2]) * t));
                    }
            }
        }
    }

    void floodFill(const u8* r) { // EMR_EXTFLOODFILL
        Pt s = map(I32(r + 8), I32(r + 12));
        u32 key = gdi::ToArgb(U32(r + 16) & 0xffffff) & 0xffffffu;
        u32 mode = U32(r + 20);
        int sx = Rnd(s.x), sy = Rnd(s.y);
        GdiCtx g = shapeCtx();
        RECT bnd = mb;
        if (sx < bnd.left || sx >= bnd.right || sy < bnd.top || sy >= bnd.bottom) return;
        std::vector<u8> seen(size_t(mw) * size_t(mh), 0);
        auto fillable = [&](int x, int y) {
            u32 px = 0;
            if (!gdi::GetPixel(g, x, y, px)) return false;
            px &= 0xffffffu;
            return mode == 1 ? px == key : px != key; // FLOODFILLSURFACE : FLOODFILLBORDER
        };
        if (!fillable(sx, sy)) return;
        std::vector<std::pair<int, int>> todo{{sx, sy}};
        seen[size_t(sy - bnd.top) * size_t(mw) + size_t(sx - bnd.left)] = 1;
        std::vector<std::pair<int, int>> hit;
        while (!todo.empty()) {
            auto [x, y] = todo.back();
            todo.pop_back();
            hit.push_back({x, y});
            const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < bnd.left || nx[k] >= bnd.right || ny[k] < bnd.top || ny[k] >= bnd.bottom) continue;
                size_t i = size_t(ny[k] - bnd.top) * size_t(mw) + size_t(nx[k] - bnd.left);
                if (seen[i] || !fillable(nx[k], ny[k])) continue;
                seen[i] = 1;
                todo.push_back({nx[k], ny[k]});
            }
        }
        for (auto& p : hit) gdi::FillPixel(g, p.first, p.second); // painted after the search: the brush colour must not change the result
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
            case R_SETWINDOWEXTEX: st.winExtX = I32(r + 8); st.winExtY = I32(r + 12); fixIsotropic(); break;
            case R_SETWINDOWORGEX: st.winOrgX = I32(r + 8); st.winOrgY = I32(r + 12); break;
            case R_SETVIEWPORTEXTEX: st.vpExtX = I32(r + 8); st.vpExtY = I32(r + 12); fixIsotropic(); break;
            case R_SETVIEWPORTORGEX: st.vpOrgX = I32(r + 8); st.vpOrgY = I32(r + 12); break;
            case R_SCALEVIEWPORTEXTEX:
                if (I32(r + 12) && I32(r + 20)) { st.vpExtX = st.vpExtX * I32(r + 8) / I32(r + 12); st.vpExtY = st.vpExtY * I32(r + 16) / I32(r + 20); fixIsotropic(); }
                break;
            case R_SCALEWINDOWEXTEX:
                if (I32(r + 12) && I32(r + 20)) { st.winExtX = st.winExtX * I32(r + 8) / I32(r + 12); st.winExtY = st.winExtY * I32(r + 16) / I32(r + 20); fixIsotropic(); }
                break;
            case R_SETMAPMODE:
                st.mapMode = I32(r + 8);
                if (st.mapMode == 1) { st.winExtX = st.winExtY = st.vpExtX = st.vpExtY = 1; }
                fixIsotropic();
                break;
            case R_SETBKMODE: st.bkMode = I32(r + 8); break;
            case R_SETPOLYFILLMODE: st.polyFill = I32(r + 8); break;
            case R_SETROP2: st.rop2 = I32(r + 8); break;
            case R_SETARCDIRECTION: st.arcDir = I32(r + 8); break;
            case R_SETTEXTALIGN: st.textAlign = U32(r + 8); break;
            case R_SETTEXTCOLOR: st.text = U32(r + 8) & 0xffffff; break;
            case R_SETBKCOLOR: st.bk = U32(r + 8) & 0xffffff; break;
            // state that has no effect on a true-colour software surface or on the clip/geometry of the port: accepted and ignored
            case R_SETSTRETCHBLTMODE: case R_SETBRUSHORGEX: case R_SETLAYOUT: case R_SETMAPPERFLAGS: case R_SETCOLORADJUSTMENT:
            case R_SETMITERLIMIT: case R_SETTEXTJUSTIFICATION: case R_SELECTPALETTE: case R_SETPALETTEENTRIES: case R_RESIZEPALETTE:
            case R_REALIZEPALETTE: case R_FLATTENPATH: case R_SETICMMODE: case R_CREATECOLORSPACE: case R_SETCOLORSPACE:
            case R_DELETECOLORSPACE: case R_CREATECOLORSPACEW: case R_COLORCORRECTPALETTE: case R_SETICMPROFILEA:
            case R_SETICMPROFILEW: case R_COLORMATCHTOTARGETW: case R_SETLINKEDUFIS: case R_FORCEUFIMAPPING: case R_GLSRECORD:
            case R_GLSBOUNDEDRECORD: case R_PIXELFORMAT: case R_DRAWESCAPE: case R_EXTESCAPE: case R_NAMEDESCAPE:
                break;
            case R_GDICOMMENT:
                if (L >= 16 && U32(r + 12) == 0x2B464D45u /* "EMF+" */) ++emfPlus; // EMF+ records are not interpreted (dual files draw their EMF part)
                break;
            case R_CREATEPALETTE: { u32 ih = U32(r + 8); if (ih && ih < objs.size()) { objs[ih] = Obj(); objs[ih].kind = Obj::PALETTE; } break; }
            case R_MOVETOEX: st.cur = Pt{double(I32(r + 8)), double(I32(r + 12))}; if (inPath) path.push_back(Fig{{map(st.cur.x, st.cur.y)}, false}); break;
            case R_LINETO: {
                Pt a = map(st.cur.x, st.cur.y), b = map(I32(r + 8), I32(r + 12));
                st.cur = Pt{double(I32(r + 8)), double(I32(r + 12))};
                if (inPath && !path.empty() && !path.back().closed) path.back().pts.push_back(b);
                else poly({a, b}, false, false);
                break;
            }
            case R_SETPIXELV: { Pt p = map(I32(r + 8), I32(r + 12)); GdiCtx g = clipped(); gdi::PutPixelCopy(g, Rnd(p.x), Rnd(p.y), gdi::ToArgb(U32(r + 16))); break; }
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
                RECT e{Rnd(std::min(p0.x, p1.x)), Rnd(std::min(p0.y, p1.y)), Rnd(std::max(p0.x, p1.x)), Rnd(std::max(p0.y, p1.y))};
                if (st.hasExcl) { // a second excluded rectangle: fold both into the clip region
                    Mask m = zeroMask();
                    fillRectMask(m, st.excl.left, st.excl.top, st.excl.right, st.excl.bottom);
                    fillRectMask(m, e.left, e.top, e.right, e.bottom);
                    applyRegion(4, &m); // RGN_DIFF with the union of both rectangles (applyRegion clears hasExcl after materializing)
                } else { st.excl = e; st.hasExcl = true; }
                break;
            }
            case R_OFFSETCLIPRGN: { // ptlOffset in logical units
                Pt o = map(0, 0), q = map(I32(r + 8), I32(r + 12));
                int dx = Rnd(q.x - o.x), dy = Rnd(q.y - o.y);
                if (st.hasClip) OffsetRect(&st.clip, dx, dy);
                if (st.hasExcl) OffsetRect(&st.excl, dx, dy);
                if (st.clipMask) {
                    Mask m = zeroMask();
                    for (int y = 0; y < mh; ++y)
                        for (int x = 0; x < mw; ++x) {
                            int sx = x - dx, sy = y - dy;
                            if (sx >= 0 && sx < mw && sy >= 0 && sy < mh) m[size_t(y) * size_t(mw) + size_t(x)] = (*st.clipMask)[size_t(sy) * size_t(mw) + size_t(sx)];
                        }
                    st.clipMask = std::make_shared<const Mask>(std::move(m));
                }
                break;
            }
            case R_SETMETARGN: { // the current clip becomes the meta region, the clip itself is reset to it
                st.metaMask = std::make_shared<const Mask>(materializeClip());
                st.clipMask = st.metaMask;
                st.hasClip = false;
                st.hasExcl = false;
                break;
            }
            case R_EXTSELECTCLIPRGN: {
                if (L < 16) break;
                u32 cb = U32(r + 8), mode = U32(r + 12);
                if (cb == 0) { applyRegion(mode, nullptr); break; }
                Mask m;
                if (16 + size_t(cb) <= L && regionMask(r + 16, cb, m)) applyRegion(mode, &m);
                else ++skipped[type];
                break;
            }
            case R_FILLRGN: case R_FRAMERGN: case R_INVERTRGN: case R_PAINTRGN: {
                size_t base = type == R_FRAMERGN ? 40 : type == R_FILLRGN ? 32 : 28;
                if (L < base) break;
                u32 cb = U32(r + 24);
                Mask m;
                if (base + size_t(cb) > L || !regionMask(r + base, cb, m)) { ++skipped[type]; break; }
                if (type == R_INVERTRGN) {
                    GdiCtx g = clipped();
                    forMask(m, [&](int x, int y) {
                        u32 px = 0;
                        if (gdi::GetPixel(g, x, y, px)) gdi::PutPixelCopy(g, x, y, 0xff000000u | (~px & 0xffffffu));
                    });
                } else if (type == R_PAINTRGN) {
                    Obj t;
                    paintMask(m, curObj(Obj::BRUSH, t));
                } else {
                    const Obj* b = brushObj(U32(r + 28));
                    if (!b) break;
                    if (type == R_FILLRGN) paintMask(m, b);
                    else {
                        Pt o = map(0, 0), q = map(I32(r + 32), I32(r + 36));
                        frameMask(m, b, std::max(1, std::abs(Rnd(q.x - o.x))), std::max(1, std::abs(Rnd(q.y - o.y))));
                    }
                }
                break;
            }
            case R_CREATEPEN: { // ihPen, lopnStyle, lopnWidth(POINTL), lopnColor
                u32 ih = U32(r + 8);
                if (ih && ih < objs.size()) { Obj o; o.kind = Obj::PEN; o.pen.style = U32(r + 12) & 0xff; o.pen.width = POINT{I32(r + 16), I32(r + 20)}; o.pen.color = U32(r + 24) & 0xffffff; o.nullObj = o.pen.style == 5; objs[ih] = o; }
                break;
            }
            case R_EXTCREATEPEN: { // ihPen, offBmi, cbBmi, offBits, cbBits, elp{penStyle, width, brushStyle, color, hatch, nEntries, entries[]}
                u32 ih = U32(r + 8);
                if (ih && ih < objs.size() && L >= 56) {
                    Obj o; o.kind = Obj::PEN; o.pen.style = U32(r + 28) & 0xff; o.pen.width = POINT{I32(r + 32), 0}; o.pen.color = U32(r + 40) & 0xffffff; o.nullObj = (o.pen.style & 0xf) == 5;
                    u32 ne = U32(r + 48);
                    if ((o.pen.style & 0xf) == 7 && ne <= 64 && 52 + size_t(ne) * 4 <= L)
                        for (u32 i = 0; i < ne; ++i) o.dashes.push_back(int(U32(r + 52 + 4 * i)));
                    objs[ih] = o;
                }
                break;
            }
            case R_CREATEBRUSHINDIRECT: {
                u32 ih = U32(r + 8);
                if (ih && ih < objs.size()) { Obj o; o.kind = Obj::BRUSH; o.brush.style = U32(r + 12); o.brush.color = U32(r + 16) & 0xffffff; o.brush.hatch = I32(r + 20); o.nullObj = o.brush.style == 1; objs[ih] = o; }
                break;
            }
            case R_CREATEMONOBRUSH: case R_CREATEDIBPATTERNBRUSHPT: { // ihBrush, iUsage, offBmi, cbBmi, offBits, cbBits
                u32 ih = U32(r + 8);
                if (!ih || ih >= objs.size() || L < 32) break;
                u32 offBmi = U32(r + 16), cbBmi = U32(r + 20), offBits = U32(r + 24), cbBits = U32(r + 28);
                Bitmap bm;
                bool ok = size_t(offBmi) + cbBmi <= L && size_t(offBits) + cbBits <= L;
                if (ok) ok = type == R_CREATEMONOBRUSH ? decodeMono(r + offBmi, cbBmi, r + offBits, cbBits, bm) : decodeDib(r + offBmi, cbBmi, r + offBits, cbBits, bm);
                if (!ok) { ++skipped[type]; break; }
                Obj o; o.kind = Obj::BRUSH;
                o.pat = std::make_shared<std::vector<u32>>(bm.px);
                o.patW = bm.w; o.patH = bm.h;
                o.mono = type == R_CREATEMONOBRUSH;
                objs[ih] = o;
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
                    o.escapement = I32(lf + 8);
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
            case R_ARCTO: arc(r, 3); break;
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
            case R_POLYDRAW: polyDraw(r, L, false); break;
            case R_POLYDRAW16: polyDraw(r, L, true); break;
            case R_EXTTEXTOUTA: emrText(r, L, 36, false); break;
            case R_EXTTEXTOUTW: emrText(r, L, 36, true); break;
            case R_POLYTEXTOUTA: case R_POLYTEXTOUTW: { // rclBounds, iGraphicsMode, exScale, eyScale, cStrings, aemrtext[]
                if (L < 40) break;
                u32 cs = U32(r + 36);
                for (u32 i = 0; i < cs && i < 4096; ++i) emrText(r, L, 40 + size_t(i) * 40, type == R_POLYTEXTOUTW);
                break;
            }
            case R_SMALLTEXTOUT: smallText(r, L); break;
            case R_EXTFLOODFILL: if (L >= 24) floodFill(r); break;
            case R_GRADIENTFILL: gradientFill(r, L); break;
            case R_BEGINPATH: inPath = true; clearPath(); break;
            case R_ENDPATH: inPath = false; pathReady = true; break;
            case R_ABORTPATH: inPath = false; clearPath(); break;
            case R_CLOSEFIGURE: if (!path.empty()) path.back().closed = true; break;
            case R_FILLPATH: endPathDraw(true, false); clearPath(); break;
            case R_STROKEPATH: endPathDraw(false, true); clearPath(); break;
            case R_STROKEANDFILLPATH: endPathDraw(true, true); clearPath(); break;
            case R_WIDENPATH: { // the path becomes the outline swept by the current pen
                GdiCtx pg = shapeCtx();
                Mask m = renderMask([&](GdiCtx& t) {
                    t.penColor = 0xffffff;
                    t.penWidth = pg.penWidth;
                    t.penStyle = 0;
                    t.penNull = false;
                    std::vector<POINT> tmp;
                    for (const Fig& f : path) {
                        tmp.clear();
                        for (const Pt& q : f.pts) tmp.push_back(POINT{Rnd(q.x), Rnd(q.y)});
                        if (tmp.empty()) continue;
                        if (f.closed) tmp.push_back(tmp[0]);
                        gdi::Polyline(t, tmp.data(), int(tmp.size()));
                    }
                    if (hasPathMask) forMask(pathMask, [&](int x, int y) { gdi::PutPixelCopy(t, x, y, 0xffffffffu); });
                });
                path.clear();
                pathMask = std::move(m);
                hasPathMask = true;
                break;
            }
            case R_SELECTCLIPPATH: {
                Mask m = pathToMask();
                applyRegion(L >= 12 ? U32(r + 8) : 5, &m);
                clearPath();
                break;
            }
            case R_STRETCHDIBITS: {
                if (L < 80) break;
                i32 xd = I32(r + 24), yd = I32(r + 28), xs = I32(r + 32), ys = I32(r + 36), cxs = I32(r + 40), cys = I32(r + 44);
                u32 offBmi = U32(r + 48), cbBmi = U32(r + 52), offBits = U32(r + 56), cbBits = U32(r + 60);
                u32 rop = (U32(r + 68) >> 16) & 0xff;
                i32 cxd = I32(r + 72), cyd = I32(r + 76);
                Bitmap bm;
                if (offBmi + cbBmi <= L && offBits + cbBits <= L && decodeDib(r + offBmi, cbBmi, r + offBits, cbBits, bm)) {
                    // source rows: ySrc counts from the bottom of a bottom-up DIB
                    int ysTop = bm.h - (ys + cys);
                    blitRop(bm, xs, ysTop, cxs, cys, xd, yd, cxd, cyd, rop);
                } else ++skipped[type];
                break;
            }
            case R_SETDIBITSTODEVICE: {
                if (L < 76) break;
                i32 xd = I32(r + 24), yd = I32(r + 28), xs = I32(r + 32), ys = I32(r + 36);
                u32 cxs = U32(r + 40), cys = U32(r + 44), offBmi = U32(r + 48), cbBmi = U32(r + 52), offBits = U32(r + 56), cbBits = U32(r + 60);
                Bitmap bm;
                if (offBmi + cbBmi <= L && offBits + cbBits <= L && decodeDib(r + offBmi, cbBmi, r + offBits, cbBits, bm))
                    blitRop(bm, xs, bm.h - (ys + int(cys)), int(cxs), int(cys), xd, yd, double(cxs), double(cys), 0xCC);
                else ++skipped[type];
                break;
            }
            case R_BITBLT: case R_STRETCHBLT: {
                if (L < 100) break;
                i32 xd = I32(r + 24), yd = I32(r + 28), cxd = I32(r + 32), cyd = I32(r + 36);
                u32 rop = (U32(r + 40) >> 16) & 0xff;
                i32 xs = I32(r + 44), ys = I32(r + 48);
                u32 offBmi = U32(r + 84), cbBmi = U32(r + 88), offBits = U32(r + 92), cbBits = U32(r + 96);
                i32 cxs = type == R_STRETCHBLT && L >= 108 ? I32(r + 100) : cxd, cys = type == R_STRETCHBLT && L >= 108 ? I32(r + 104) : cyd;
                Bitmap bm;
                if (cbBmi && offBmi + cbBmi <= L && offBits + cbBits <= L && decodeDib(r + offBmi, cbBmi, r + offBits, cbBits, bm)) {
                    blitRop(bm, xs, ys, cxs, cys, xd, yd, cxd, cyd, rop);
                } else if (!RopUsesSource(rop)) { // PATCOPY, PATINVERT, DSTINVERT, BLACKNESS, WHITENESS ...
                    fillRop(xd, yd, cxd, cyd, rop);
                } else ++skipped[type]; // needs a source that the record does not carry
                break;
            }
            case R_ALPHABLEND: case R_TRANSPARENTBLT: {
                if (L < 108) break;
                i32 xd = I32(r + 24), yd = I32(r + 28), cxd = I32(r + 32), cyd = I32(r + 36);
                u32 bf = U32(r + 40);
                i32 xs = I32(r + 44), ys = I32(r + 48);
                u32 key = U32(r + 76) & 0xffffff;
                u32 offBmi = U32(r + 84), cbBmi = U32(r + 88), offBits = U32(r + 92), cbBits = U32(r + 96);
                i32 cxs = I32(r + 100), cys = I32(r + 104);
                Bitmap bm;
                if (!(cbBmi && offBmi + cbBmi <= L && offBits + cbBits <= L &&
                      decodeDib(r + offBmi, cbBmi, r + offBits, cbBits, bm, type == R_ALPHABLEND && (bf >> 24) == 1))) { ++skipped[type]; break; }
                if (type == R_TRANSPARENTBLT) {
                    u32 k = gdi::ToArgb(key) & 0xffffffu;
                    blitF(bm, xs, ys, cxs, cys, xd, yd, cxd, cyd, [k](int, int, int, int, u32 s, u32, u32& o) {
                        if ((s & 0xffffffu) == k) return false; // the colour key is not drawn
                        o = 0xff000000u | (s & 0xffffffu);
                        return true;
                    });
                } else { // BLENDFUNCTION: byte 0 op (AC_SRC_OVER), byte 2 SourceConstantAlpha, byte 3 AlphaFormat (1 = per-pixel, premultiplied)
                    u32 sca = (bf >> 16) & 0xff;
                    bool perPixel = (bf >> 24) == 1;
                    blitF(bm, xs, ys, cxs, cys, xd, yd, cxd, cyd, [sca, perPixel](int, int, int, int, u32 s, u32 dd, u32& o) {
                        u32 sa = perPixel ? (s >> 24) : 255;
                        u32 res = 0xff000000u;
                        for (int sh = 0; sh <= 16; sh += 8) {
                            u32 sc = (s >> sh) & 0xff, dc2 = (dd >> sh) & 0xff, v;
                            if (perPixel) { // premultiplied source: scale by the constant alpha, destination by the remaining coverage
                                u32 scp = sc * sca / 255, sap = sa * sca / 255;
                                v = scp + dc2 * (255 - sap) / 255;
                            } else {
                                v = (sc * sca + dc2 * (255 - sca)) / 255;
                            }
                            res |= std::min(255u, v) << sh;
                        }
                        o = res;
                        return true;
                    });
                }
                break;
            }
            case R_MASKBLT: {
                if (L < 128) break;
                i32 xd = I32(r + 24), yd = I32(r + 28), cxd = I32(r + 32), cyd = I32(r + 36);
                u32 rop4 = U32(r + 40);
                u32 fore = (rop4 >> 16) & 0xff, back = (rop4 >> 24) & 0xff;
                i32 xs = I32(r + 44), ys = I32(r + 48), xm = I32(r + 100), ym = I32(r + 104);
                u32 offBmi = U32(r + 84), cbBmi = U32(r + 88), offBits = U32(r + 92), cbBits = U32(r + 96);
                u32 mOffBmi = U32(r + 112), mCbBmi = U32(r + 116), mOffBits = U32(r + 120), mCbBits = U32(r + 124);
                Bitmap bm, mk;
                bool haveSrc = cbBmi && offBmi + cbBmi <= L && offBits + cbBits <= L && decodeDib(r + offBmi, cbBmi, r + offBits, cbBits, bm);
                bool haveMask = mCbBmi && mOffBmi + mCbBmi <= L && mOffBits + mCbBits <= L && decodeMono(r + mOffBmi, mCbBmi, r + mOffBits, mCbBits, mk);
                if (!haveSrc && RopUsesSource(fore)) { ++skipped[type]; break; }
                if (!haveSrc) { bm.w = bm.h = 1; bm.px.assign(1, 0); }
                blitF(bm, haveSrc ? xs : 0, haveSrc ? ys : 0, haveSrc ? cxd : 1, haveSrc ? cyd : 1, xd, yd, cxd, cyd,
                      [&](int x, int y, int u, int v, u32 s, u32 dd, u32& o) {
                          bool on = true;
                          if (haveMask) {
                              int mx = xm + u, my = ym + v;
                              on = mx >= 0 && my >= 0 && mx < mk.w && my < mk.h && mk.px[size_t(my) * size_t(mk.w) + size_t(mx)] != 0;
                          }
                          o = Rop3(on ? fore : back, patternAt(x, y) & 0xffffffu, s & 0xffffffu, dd & 0xffffffu);
                          return true;
                      });
                break;
            }
            case R_PLGBLT: { // parallelogram: aptlDest[0] upper-left, [1] upper-right, [2] lower-left of the source rectangle
                if (L < 140) break;
                double ax = I32(r + 24), ay = I32(r + 28), bx = I32(r + 32), by = I32(r + 36), cx = I32(r + 40), cy = I32(r + 44);
                i32 xs = I32(r + 48), ys = I32(r + 52), cxs = I32(r + 56), cys = I32(r + 60);
                u32 offBmi = U32(r + 96), cbBmi = U32(r + 100), offBits = U32(r + 104), cbBits = U32(r + 108);
                i32 xm = I32(r + 112), ym = I32(r + 116);
                u32 mOffBmi = U32(r + 124), mCbBmi = U32(r + 128), mOffBits = U32(r + 132), mCbBits = U32(r + 136);
                Bitmap bm, mk;
                if (!(cbBmi && offBmi + cbBmi <= L && offBits + cbBits <= L && decodeDib(r + offBmi, cbBmi, r + offBits, cbBits, bm)) || cxs <= 0 || cys <= 0) { ++skipped[type]; break; }
                bool haveMask = mCbBmi && mOffBmi + mCbBmi <= L && mOffBits + mCbBits <= L && decodeMono(r + mOffBmi, mCbBmi, r + mOffBits, mCbBits, mk);
                Pt p0 = map(ax, ay), p1 = map(bx, by), p2 = map(cx, cy);
                double ux = p1.x - p0.x, uy = p1.y - p0.y, vx = p2.x - p0.x, vy = p2.y - p0.y;
                double det = ux * vy - uy * vx;
                if (std::fabs(det) < 1e-9) break;
                Pt p3{p1.x + p2.x - p0.x, p1.y + p2.y - p0.y};
                int L2 = Rnd(std::min({p0.x, p1.x, p2.x, p3.x}) - 1), R2 = Rnd(std::max({p0.x, p1.x, p2.x, p3.x}) + 1);
                int T2 = Rnd(std::min({p0.y, p1.y, p2.y, p3.y}) - 1), B2 = Rnd(std::max({p0.y, p1.y, p2.y, p3.y}) + 1);
                GdiCtx g = clipped();
                for (int y = T2; y < B2; ++y)
                    for (int x = L2; x < R2; ++x) {
                        double dx2 = x + 0.5 - p0.x, dy2 = y + 0.5 - p0.y;
                        double a = (dx2 * vy - dy2 * vx) / det, b = (ux * dy2 - uy * dx2) / det;
                        if (a < 0 || a >= 1 || b < 0 || b >= 1) continue;
                        int su = xs + int(a * cxs), sv = ys + int(b * cys);
                        if (su < 0 || sv < 0 || su >= bm.w || sv >= bm.h) continue;
                        if (haveMask) {
                            int mx = xm + int(a * cxs), my = ym + int(b * cys);
                            if (mx < 0 || my < 0 || mx >= mk.w || my >= mk.h || !mk.px[size_t(my) * size_t(mk.w) + size_t(mx)]) continue;
                        }
                        gdi::PutPixelCopy(g, x, y, 0xff000000u | (bm.px[size_t(sv) * size_t(bm.w) + size_t(su)] & 0xffffffu));
                    }
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
    emfPlus_ = 0;
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
    emfPlus_ = 0;
    if (!Valid()) return 0;
    Player p(data_, hdr_, dc, dest, skipped_, emfPlus_);
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
