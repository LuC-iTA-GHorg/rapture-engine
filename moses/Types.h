// moses.dll port - basic types.
//
// Everything in the moses layer lives in `namespace moses` (the original is a flat C++ DLL;
// the namespace only exists so that `::Rect` / `::Window` / `::Surface` of source/platform and
// the names of this layer never collide - game code writes `using namespace moses;`).
//
// COORDINATE SYSTEM (moses.md sec. 0/3): y grows DOWNWARDS, the origin is the CENTRE of the view
// surface (640x480: x in [-320,320), y in [-240,240)). RECT is half-open: right/bottom excluded.
#pragma once

#include "platform/Gfx.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>


// <windows.h> HAZARD: windows.h #defines PostMessage / LoadString / PlaySound / GetObject ... to their ...A/...W
// variants. Our classes have methods with those names (Scene::PostMessage, Scene::LoadString, Scene::PlaySound,
// CResMgr::GetObj ...); if a translation unit saw the macro and another did not, the link would break.
// Rule: engine and game .cpp files do not include <windows.h> (platform/*_win.cpp do); this block makes a stray
// include harmless by removing the macros.
#ifdef PostMessage
#undef PostMessage
#endif
#ifdef LoadString
#undef LoadString
#endif
#ifdef PlaySound
#undef PlaySound
#endif
#ifdef GetObject
#undef GetObject
#endif
#ifdef DrawText
#undef DrawText
#endif
#ifdef CreateWindow
#undef CreateWindow
#endif

namespace moses {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using i16 = int16_t;
using i32 = int32_t;

// Win32 tagPOINT / tagRECT (32-bit LONG members, same field order as the original).
struct POINT {
    int32_t x = 0, y = 0;
};

struct RECT {
    int32_t left = 0, top = 0, right = 0, bottom = 0;
    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
};

using COLORREF = uint32_t; // 0x00BBGGRR (GDI objects: CDrawRect/CDrawText colours)

constexpr COLORREF Rgb(unsigned r, unsigned g, unsigned b) { return r | (g << 8) | (b << 16); }

// ---- Win32 RECT helpers used by the game (USER32: OffsetRect, PtInRect ... see imports.txt).
// Same semantics as USER32 (half-open rects; Intersect/Union return false and zero the result when empty).
inline void SetRect(RECT* r, int l, int t, int rr, int b) { r->left = l; r->top = t; r->right = rr; r->bottom = b; }
inline bool IsRectEmpty(const RECT* r) { return r->right <= r->left || r->bottom <= r->top; }
inline bool EqualRect(const RECT* a, const RECT* b) {
    return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}
inline void OffsetRect(RECT* r, int dx, int dy) { r->left += dx; r->right += dx; r->top += dy; r->bottom += dy; }
inline void InflateRect(RECT* r, int dx, int dy) { r->left -= dx; r->right += dx; r->top -= dy; r->bottom += dy; }
inline bool PtInRect(const RECT* r, int x, int y) { return x >= r->left && x < r->right && y >= r->top && y < r->bottom; }
inline bool IntersectRect(RECT* out, const RECT* a, const RECT* b) { // out may alias a or b
    RECT t;
    t.left = std::max(a->left, b->left);
    t.top = std::max(a->top, b->top);
    t.right = std::min(a->right, b->right);
    t.bottom = std::min(a->bottom, b->bottom);
    if (t.right <= t.left || t.bottom <= t.top) { *out = RECT{}; return false; }
    *out = t;
    return true;
}
inline bool UnionRect(RECT* out, const RECT* a, const RECT* b) { // empty operands are ignored (USER32 behaviour)
    bool ea = IsRectEmpty(a), eb = IsRectEmpty(b);
    if (ea && eb) { *out = RECT{}; return false; }
    if (ea) { *out = *b; return true; }
    if (eb) { *out = *a; return true; }
    RECT t;
    t.left = std::min(a->left, b->left);
    t.top = std::min(a->top, b->top);
    t.right = std::max(a->right, b->right);
    t.bottom = std::max(a->bottom, b->bottom);
    *out = t;
    return true;
}

// Conversions to/from the platform Rect (identical layout and semantics).
inline ::Rect ToPlatform(const RECT& r) { ::Rect p; p.left = r.left; p.top = r.top; p.right = r.right; p.bottom = r.bottom; return p; }
inline RECT FromPlatform(const ::Rect& p) { return RECT{p.left, p.top, p.right, p.bottom}; }

// ---- DROP: raster operation of CDibData::DibBlt (moses.md sec. 2).
// The original is `enum DROP`; the numeric values are what the game passes.
enum DROP : int {
    DROP_TRANSPARENT = 0, // copy, source pixel 0x0000 (port: alpha 0) is skipped
    DROP_OPAQUE = 1,      // copy everything, also the "transparent" pixels
    DROP_ON_DEST = 2,     // write the source pixel only where the destination is already non-zero
                          // (port: destination alpha != 0, since every composed pixel is opaque)
};

// ---- flip: u16, low byte = horizontal flip, high byte = vertical flip (moses.md sec. 2).
constexpr u16 MakeFlip(unsigned fx, unsigned fy) { return u16((fx & 0xff) | ((fy & 0xff) << 8)); }
constexpr bool FlipX(u16 f) { return (f & 0xff) != 0; }
constexpr bool FlipY(u16 f) { return (f >> 8) != 0; }
// SetFlip(fx, fy) arguments: 0/1 = set, 2 = toggle, 3 = leave unchanged.
enum FlipArg : unsigned { FLIP_OFF = 0, FLIP_ON = 1, FLIP_TOGGLE = 2, FLIP_KEEP = 3 };

// ---- z-order ("level"): higher level = drawn later = on top. Typical values in the game:
// 0x26fc (dialogs/buttons), 0x7fffffff (always on top), 10000, 0x2706.
constexpr int LEVEL_MIN = INT_MIN; // CDrawView default min_compose_level (0x80000000): everything is composed
constexpr int LEVEL_MAX = INT_MAX;

// CDrawObj::GetType() (moses.dll 0x100080a0) always returns this constant.
constexpr unsigned long DRAWOBJ_TYPE = 0x10002;

// Resource ids: only the low word is the index inside a .rrs; see rrs_format.md and aaron/Types.h.
constexpr u32 ResIndex(u32 id) { return id & 0xffff; }

} // namespace moses
