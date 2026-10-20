#include "platform/Dib.h"

#include <cstring>

namespace {

uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

struct Lut5 {
    uint8_t v[32];
    Lut5() {
        for (int i = 0; i < 32; ++i) v[i] = uint8_t((i * 255 + 15) / 31);
    }
};
const Lut5 kLut5;

} // namespace

uint32_t Rgb555ToArgb(uint16_t v) {
    return 0xff000000u | uint32_t(kLut5.v[(v >> 10) & 31]) << 16 | uint32_t(kLut5.v[(v >> 5) & 31]) << 8 |
           kLut5.v[v & 31];
}

// CDibData ctor 0x10003a20 + compressed 16-bit blit worker 0x100049c0.
bool DecodeDib(const uint8_t* data, size_t size, Dib& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    if (size < 10) return fail("DIB too short");
    int w = Le16(data), h = Le16(data + 2);
    int ox = int16_t(Le16(data + 4)), oy = int16_t(Le16(data + 6));
    int bpp = data[8], fl = data[9];
    if (bpp != 16) return fail("unsupported bpp");
    if (!(fl & 2)) return fail("uncompressed DIB not supported");
    const uint8_t* d = data + 10;
    size_t dsize = size - 10;
    size_t entry = (fl & 4) ? 4 : 2; // line table entries are dwords when flag bit2
    if (size_t(h) * entry > dsize) return fail("line table out of range");

    auto pix = std::make_shared<std::vector<uint32_t>>(size_t(w) * size_t(h), 0u);
    for (int y = 0; y < h; ++y) {
        size_t p = (fl & 4) ? Le32(d + 4 * y) : Le16(d + 2 * y);
        uint32_t* row = pix->data() + size_t(h - 1 - y) * w; // file row 0 is the bottom row
        int x = 0;
        for (;;) {
            if (p >= dsize) return fail("row runs past data");
            uint8_t c = d[p++];
            if (c == 0) break; // end of row; remaining pixels are transparent
            if (c & 0x80) {
                int n = c & 0x7f;
                if (p + 2 * size_t(n) > dsize || x + n > w) return fail("literal run out of range");
                for (int i = 0; i < n; ++i, p += 2) {
                    uint16_t v = Le16(d + p);
                    row[x++] = v ? Rgb555ToArgb(v) : 0u; // pixel 0 = transparent
                }
            } else {
                if (p + 2 > dsize || x + c > w) return fail("repeat run out of range");
                uint16_t v = Le16(d + p);
                p += 2;
                uint32_t px = v ? Rgb555ToArgb(v) : 0u;
                for (int i = 0; i < c; ++i) row[x++] = px;
            }
        }
    }
    out.w_ = w;
    out.h_ = h;
    out.ox_ = ox;
    out.oy_ = oy;
    out.flags_ = fl;
    out.pix_ = std::move(pix);
    return true;
}

void Blit(Surface& dst, const Dib& src, int x, int y, int flags) {
    if (src.Empty()) return;
    const int sw = src.Width(), sh = src.Height();
    // Destination range clipped to the surface.
    int dx0 = std::max(x, 0), dx1 = std::min(x + sw, dst.Width());
    int dy0 = std::max(y, 0), dy1 = std::min(y + sh, dst.Height());
    if (dx0 >= dx1 || dy0 >= dy1) return;
    const bool fh = flags & kFlipH, fv = flags & kFlipV;
    for (int dy = dy0; dy < dy1; ++dy) {
        int sy = fv ? sh - 1 - (dy - y) : dy - y;
        const uint32_t* s = src.Row(sy);
        uint32_t* d = dst.Row(dy);
        if (!fh) {
            const uint32_t* sp = s + (dx0 - x);
            for (int dx = dx0; dx < dx1; ++dx, ++sp)
                if (*sp >> 24) d[dx] = *sp;
        } else {
            const uint32_t* sp = s + (sw - 1 - (dx0 - x));
            for (int dx = dx0; dx < dx1; ++dx, --sp)
                if (*sp >> 24) d[dx] = *sp;
        }
    }
}

void BlitHot(Surface& dst, const Dib& src, int x, int y, int flags) {
    int ox = (flags & kFlipH) ? src.Width() - 1 - src.OriginX() : src.OriginX();
    int oy = (flags & kFlipV) ? src.Height() - 1 - src.OriginY() : src.OriginY();
    Blit(dst, src, x - ox, y - oy, flags);
}

void CopySurface(Surface& dst, const Surface& src, int x, int y) {
    int dx0 = std::max(x, 0), dx1 = std::min(x + src.Width(), dst.Width());
    int dy0 = std::max(y, 0), dy1 = std::min(y + src.Height(), dst.Height());
    if (dx0 >= dx1 || dy0 >= dy1) return;
    for (int dy = dy0; dy < dy1; ++dy)
        std::memcpy(dst.Row(dy) + dx0, src.Row(dy - y) + (dx0 - x), size_t(dx1 - dx0) * 4);
}
