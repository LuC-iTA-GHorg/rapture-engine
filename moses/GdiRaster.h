// Private software rasteriser standing in for the GDI calls of moses.dll (Rectangle/RoundRect/Ellipse/
// Polyline/Polygon/DrawText). Package 1 internal: not included by public headers.
//
// All coordinates are LOGICAL view coordinates (centre origin); GdiCtx::viewportOrg maps them to surface pixels.
// Clipping: ctx.clip (IntersectClipRect, half-open here) minus ctx.exclude (ExcludeClipRect).
//
// FONT CHOICE: text is rendered by the Windows GDI inside GdiRaster_win.cpp (the only file that includes
// <windows.h>, allowed by WORKPLAN) with NONANTIALIASED quality, like the original 16-bit GDI output. On other
// platforms GdiRaster.cpp forwards to the portable backend GdiTextTtf.cpp (stb_truetype, third_party/stb).
#pragma once

#include "moses/DrawGdi.h"

namespace moses {
namespace gdi {

void Rectangle(GdiCtx& dc, int l, int t, int r, int b);
void RoundRect(GdiCtx& dc, int l, int t, int r, int b, int rw, int rh);
void Ellipse(GdiCtx& dc, int l, int t, int r, int b);
void Polyline(GdiCtx& dc, const POINT* pts, int n);
void Polygon(GdiCtx& dc, const POINT* pts, int n);

// Plot one opaque pixel (logical coordinates) honouring clip/exclude.
void PutPixel(GdiCtx& dc, int x, int y, uint32_t argb);
inline uint32_t ToArgb(COLORREF c) { return 0xff000000u | ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff); }

// Text. `flags` are Win32 DT_* values. With DT_CALCRECT (0x400) the rectangle is only measured (rc.left/top are
// kept, right/bottom computed). Returns the text height. Implemented in GdiRaster_win.cpp / GdiRaster.cpp.
int TextRect(const LOGFONT_LITE& f, const char* s, int len, RECT& rc, u32 flags);
// Draws the text with the colour dc.textColor into rc (logical coordinates), clipped by dc.clip/exclude.
void TextDraw(GdiCtx& dc, const LOGFONT_LITE& f, const char* s, int len, const RECT& rc, u32 flags);
// Ascent in pixels of the font (GetTextMetrics tmAscent): distance from the top of the cell to the baseline.
int TextAscent(const LOGFONT_LITE& f);

// Portable text backend (GdiTextTtf.cpp, stb_truetype + a built-in 5x7 bitmap font as last resort).  It is what TextRect /
// TextDraw / TextAscent call on non-Windows platforms; on Windows the GDI is used (fidelity), the functions below stay
// callable (tests, tools).
namespace ttf {
int TextRect(const LOGFONT_LITE& f, const char* s, int len, RECT& rc, u32 flags);
void TextDraw(GdiCtx& dc, const LOGFONT_LITE& f, const char* s, int len, const RECT& rc, u32 flags);
int TextAscent(const LOGFONT_LITE& f);
// true when a TrueType file was found for the face (otherwise the bitmap font is used)
bool HasOutlineFont(const LOGFONT_LITE& f);
// Registers a font file for a face name ("" or "*" = default for every face). Bold/italic variants: pass "<face>:bold", "<face>:italic", "<face>:bolditalic".
bool AddFontFile(const char* face, const char* path);
// Extra directory searched for <face>.ttf, fonts named after the face (case-insensitive) and "rapture.ttf" (the default).
void AddFontDir(const char* dir);
void ResetFonts(); // forget registered files / dirs and cached fonts
} // namespace ttf

} // namespace gdi
} // namespace moses
