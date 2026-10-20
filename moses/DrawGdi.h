// CDrawGdi family: rectangles, polygons and text (moses.md sec. 4 "CDrawGdi", 4.1 "rich text").
//
// The original draws these with GDI into the view's memory DC. The port has no GDI: every
// `HDC` parameter of the original virtual functions becomes a `GdiCtx*`, a tiny software device
// context (target surface + viewport origin + clip + background settings). The class layout,
// the virtual interface (display / select_brush / select_pen) and the bounds maths are kept so the
// overrides of Shoot.exe (CListHit = CDrawRect derived) transcribe 1:1.
//
// Rendering primitives needed (package 1): filled/outlined rectangle, round rect, ellipse (with the
// optional "hole" used by the iris effect of CAnimRect), polyline/polygon, and text (DrawText with
// DT_* alignment + the rich mini-language of CTextHelper).
// CDrawMeta / CAnimMeta (metafiles): DrawMeta.h.  GDI object caches (CGdiPenCache ...): ObjCache.h.
#pragma once

#include "moses/DrawObj.h"

namespace moses {

class CTextHelper; // rich text parser/renderer, private to DrawText.cpp
struct LOGFONT_LITE;
struct GdiObj;     // software HGDIOBJ (ObjCache.h)
class CGdiPenCache;
class CGdiBrushCache;
class CGdiFontCache;

// Replacement for HDC in display()/select_*(). Filled by CDrawGdi::Compose.
struct GdiCtx {
    ::Surface* surf = nullptr;   // the view surface (ARGB)
    POINT viewportOrg;           // surface pixel of the view centre (SetViewportOrgEx): x_surf = x + org.x
    RECT clip;                   // IntersectClipRect(r), in view coordinates
    COLORREF bkColor = 0;        // SetBkColor
    bool bkOpaque = false;       // SetBkMode(OPAQUE) vs TRANSPARENT
    // Current pen/brush as selected by select_pen()/select_brush():
    u32 penStyle = 0, penWidth = 1;
    COLORREF penColor = 0;
    bool penNull = false;
    u32 brushStyle = 0;          // BS_SOLID=0, BS_NULL(hollow)=1, BS_HATCHED=2
    COLORREF brushColor = 0;
    int brushHatch = 0;
    // pkg1 additions (additive): ExcludeClipRect (iris hole), SetTextColor and the selected font.
    RECT exclude;                // valid when hasExclude (view coordinates, half-open)
    bool hasExclude = false;
    COLORREF textColor = 0;
    const LOGFONT_LITE* font = nullptr;
    // WP-5 additions (additive): the objects currently selected through gdi::SelectObject (nullptr = stock object).
    GdiObj* selPen = nullptr;
    GdiObj* selBrush = nullptr;
    GdiObj* selFont = nullptr;
    // limits branch additions (additive, used by the EMF player and available to every GdiCtx user; defaults = old behaviour):
    int rop2 = 13;               // SetROP2 value, 13 = R2_COPYPEN. Applies to gdi:: shape/line drawing through PutPixel (not to PutPixelCopy)
    bool polyWinding = false;    // SetPolyFillMode(WINDING): non-zero winding fill in gdi::Polygon instead of alternate (even-odd)
    const uint8_t* clipMask = nullptr; // extra clip region: a pixel is visible only inside maskRect with a non-zero byte (row-major,
    RECT maskRect{0, 0, 0, 0};         // stride = maskRect width); used for EMF clip regions (EXTSELECTCLIPRGN)
    const int* dashPat = nullptr; // PS_USERSTYLE (7): alternating on/off lengths in pixels
    int dashN = 0;
    const uint32_t* brushPat = nullptr; // brushStyle 3 (BS_PATTERN): ARGB tile, repeated and aligned to the surface pixel grid
    int brushPatW = 0, brushPatH = 0;
};

// GDI structures reduced to what the game sets.
struct LOGPEN { u32 style = 0; POINT width; COLORREF color = 0; }; // style: PS_SOLID=0, PS_NULL=5
struct LOGBRUSH { u32 style = 0; COLORREF color = 0; int hatch = 0; };
struct LOGFONT_LITE {
    int height = 0;      // lfHeight (negative = character height, as GDI)
    int weight = 400;
    bool italic = false, underline = false;
    char face[32] = {0};
};

// DrawText alignment flags used by SetTextAlign (Win32 values)
enum : u32 { DT_LEFT_ = 0x0, DT_CENTER_ = 0x1, DT_RIGHT_ = 0x2, DT_WORDBREAK_ = 0x10, DT_SINGLELINE_ = 0x20 };

// ---------------------------------------------------------------------------------------------
class CDrawGdi : public CDrawObj {
public:
    CDrawGdi(CDrawView* view, int level);
    ~CDrawGdi() override = default;

    // CDrawGdi overrides (moses.dll exports CDrawGdi::get_bounds 0x10008b60 and CDrawGdi::Compose 0x10008bf0)
    void get_bounds(RECT& r, u32 flip) override;    // rect widened by edge_width/2 (if > 1)
    // Compose: SaveDC; SetViewportOrg(centre); IntersectClipRect(r); SetBkColor/SetBkMode;
    // select pen/brush/font; display(view, ctx, r) - virtual calls in this order: +0x24,+0x20,+0x28,+0x1c.
    void Compose(CDibSect* view, RECT& r) override;

    // slots 7..10 of the CDrawGdi vtable (0x10016310)
    virtual void display(CDibSect* view, GdiCtx* dc, RECT& r) = 0;       // slot 7 (pure)
    virtual void* select_brush(GdiCtx* dc) { (void)dc; return nullptr; } // slot 8
    virtual void* select_pen(GdiCtx* dc) { (void)dc; return nullptr; }   // slot 9
    virtual void* select_font(GdiCtx* dc) { (void)dc; return nullptr; }  // slot 10 (0x10001810 returns 0; text 0x10002020)

    COLORREF bk_ = 0;      // +08
    int opaque_ = 0;       // +0c
    int edge_width_ = 0;   // +10
    RECT rect_;            // +14 in object coordinates
};

// ---------------------------------------------------------------------------------------------
class CDrawRect : public CDrawGdi {
public:
    CDrawRect(CDrawView* view, int level);                 // moses.dll CDrawRect::CDrawRect(CDrawView*,int)
    CDrawRect(const CDrawRect&) = default;
    ~CDrawRect() override = default;

    CDrawObj* Copy() const override;
    int Init(const RECT* rc, int ellipse);                 // moses.dll 0x10008ee0
    void SetRect(const RECT* rc);                          // moses.dll 0x10008f60
    virtual void SetEdgeWidth(int w);                      // moses.dll 0x10008f20 (virtual: UAEXH) writes edge_width and LOGPEN.width.x, recalcs bounds
    void display(CDibSect* view, GdiCtx* dc, RECT& r) override;   // moses.dll 0x10008d20 Rectangle / RoundRect / Ellipse (+ hole)
    void* select_brush(GdiCtx* dc) override;               // moses.dll CDrawRect::select_brush
    void* select_pen(GdiCtx* dc) override;                 // moses.dll CDrawRect::select_pen

    // fields +0x28.. of the original (pen/brush caches are not ported - plain values)
    LOGPEN pen_;           // +2c
    LOGBRUSH brush_;       // +40
    int rw_ = 0, rh_ = 0;  // +4c +50 rounded corners (RoundRect)
    int ellipse_ = 0;      // +54
    CGdiPenCache* pen_cache_ = nullptr;     // +28 (assigned by the owner, e.g. Aaron; nullptr = pen_ applied directly)
    CGdiBrushCache* brush_cache_ = nullptr; // +3c
    int has_hole_ = 0;     // +58 (CAnimRect iris)
    RECT hole_;            // +5c
};

// ---------------------------------------------------------------------------------------------
class CDrawPoly : public CDrawGdi {
public:
    CDrawPoly(CDrawView* view, int level);
    ~CDrawPoly() override;
    CDrawObj* Copy() const override;
    int Init(POINT* pts, u32 n, int closed);               // moses.dll CDrawPoly::Init(POINT*,uint,int)
    int Init(POINT* p0, POINT* p1);                        // moses.dll CDrawPoly::Init(POINT*,POINT*)  (a line)
    int AddPoints(POINT* pts, u32 n);                      // moses.dll CDrawPoly::AddPoints
    void display(CDibSect* view, GdiCtx* dc, RECT& r) override; // Polyline / Polygon, origin = pos
    void* select_pen(GdiCtx* dc) override;

    LOGPEN pen_;
    CGdiPenCache* pen_cache_ = nullptr; // +28 (see CDrawRect)
    int closed_ = 0;
    POINT* pts_ = nullptr;
    u32 npts_ = 0;
};

// ---------------------------------------------------------------------------------------------
// CDrawText: Shoot.exe only uses SetString (credits scroll, rich text).
class CDrawText : public CDrawGdi {
public:
    CDrawText(CDrawView* view, int level);                 // moses.dll CDrawText::CDrawText(CDrawView*,int)
    CDrawText(const CDrawText& other);                     // moses.dll CDrawText::CDrawText(CDrawText*)
    ~CDrawText() override;                                 // moses.dll CDrawText::~CDrawText
    CDrawObj* Copy() const override;                       // 0x10009230

    int Init(const char* str);                             // moses.dll CDrawText::Init(char*)
    void SetFontInfo(const char* face, int height);        // 0x100091a0 (31 chars + height, recalc_bounds)
    // rich == 0: plain DrawText of `str`. rich == 1: mini-language of moses.md sec. 4.1:
    //   "text" runs with escapes \t \r \n \c, and /FACENAME:"x" /HEIGHT:n /COLOR:n|0xhex /BOLD /NORMAL
    //   /ITALIC:n /UNDERLINE:n /TABS:n /LEFT /CENTER /RIGHT ; terminated by NUL or Ctrl-Z. 0x100091d0.
    int SetString(const char* str, int rich = 0);
    void SetTextAlign(u32 dtFlags);                        // moses.dll CDrawText::SetTextAlign
    void SetTextColor(COLORREF c);                         // moses.dll CDrawText::SetTextColor
    void get_bounds(RECT& r, u32 flip) override;           // 0x10008fe0 measures the text
    void display(CDibSect* view, GdiCtx* dc, RECT& r) override; // 0x10009?? DrawTextA / CTextHelper
    void* select_font(GdiCtx* dc) override;                // 0x10002020

    CGdiFontCache* font_cache_ = nullptr; // +24 (nullptr = the port selects font_ directly)
    LOGFONT_LITE font_;    // +28
    u32 align_ = 0;
    COLORREF color_ = 0;
    CTextHelper* helper_ = nullptr; // +64 (0x2c bytes): parsed rich text; owned
};

} // namespace moses
