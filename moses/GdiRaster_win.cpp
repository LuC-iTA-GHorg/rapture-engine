// Text rendering through the Windows GDI (WORKPLAN pkg1: allowed only in a dedicated *_win.cpp).
// Font = whatever GDI resolves for LOGFONT_LITE (face/height/weight), NONANTIALIASED like the 16-bit original.
#include "platform/Config.h"
#ifdef RAPTURE_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef Rectangle
#undef Ellipse
#undef Polyline
#undef Polygon
#undef RoundRect
#undef PostMessage
#undef LoadString
#undef PlaySound
#undef GetObject
#undef DrawText
#undef CreateWindow

#include "moses/GdiRaster.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace moses {
namespace gdi {

namespace {
struct TextDC {
    HDC dc = nullptr;
    HDC mem() {
        if (!dc) dc = CreateCompatibleDC(nullptr);
        return dc;
    }
};
TextDC g_dc;

// WP-5: RAPTURE_TEXT_BACKEND=ttf selects the portable stb_truetype backend on Windows too (testing); GDI is the default.
bool UseTtf() {
    static int v = -1;
    if (v < 0) {
        const char* e = std::getenv("RAPTURE_TEXT_BACKEND");
        v = (e && std::strcmp(e, "ttf") == 0) ? 1 : 0;
    }
    return v == 1;
}

HFONT MakeFont(const LOGFONT_LITE& f) {
    LOGFONTA lf;
    std::memset(&lf, 0, sizeof lf);
    lf.lfHeight = f.height;
    lf.lfWeight = f.weight;
    lf.lfItalic = f.italic;
    lf.lfUnderline = f.underline;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = NONANTIALIASED_QUALITY;
    std::strncpy(lf.lfFaceName, f.face, sizeof lf.lfFaceName - 1);
    return CreateFontIndirectA(&lf);
}
} // namespace

int TextRect(const LOGFONT_LITE& f, const char* s, int len, RECT& rc, u32 flags) {
    if (UseTtf()) return ttf::TextRect(f, s, len, rc, flags);
    HDC dc = g_dc.mem();
    HFONT font = MakeFont(f);
    HGDIOBJ old = SelectObject(dc, font);
    ::RECT r{rc.left, rc.top, rc.right, rc.bottom};
    int h = DrawTextA(dc, s, len, &r, flags | DT_CALCRECT);
    rc.left = r.left; rc.top = r.top; rc.right = r.right; rc.bottom = r.bottom;
    SelectObject(dc, old);
    DeleteObject(font);
    return h;
}

void TextDraw(GdiCtx& ctx, const LOGFONT_LITE& f, const char* s, int len, const RECT& rc, u32 flags) {
    if (UseTtf()) { ttf::TextDraw(ctx, f, s, len, rc, flags); return; }
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return;
    HDC dc = g_dc.mem();
    BITMAPINFO bi;
    std::memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { if (bmp) DeleteObject(bmp); return; }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    std::memset(bits, 0, size_t(w) * size_t(h) * 4);
    HFONT font = MakeFont(f);
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, RGB(255, 255, 255));
    ::RECT r{0, 0, w, h};
    DrawTextA(dc, s, len, &r, flags & ~u32(DT_CALCRECT));
    GdiFlush();
    const uint32_t* px = static_cast<const uint32_t*>(bits);
    uint32_t col = ToArgb(ctx.textColor);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (px[size_t(y) * w + x] & 0x00ffffff) PutPixelCopy(ctx, rc.left + x, rc.top + y, col);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
}


// WP-5: tmAscent of the font (used by the metafile player to place baseline-aligned text)
int TextAscent(const LOGFONT_LITE& f) {
    if (UseTtf()) return ttf::TextAscent(f);
    HDC dc = g_dc.mem();
    HFONT font = MakeFont(f);
    HGDIOBJ old = SelectObject(dc, font);
    TEXTMETRICA tm;
    std::memset(&tm, 0, sizeof tm);
    GetTextMetricsA(dc, &tm);
    SelectObject(dc, old);
    DeleteObject(font);
    return int(tm.tmAscent);
}
} // namespace gdi
} // namespace moses
#endif
