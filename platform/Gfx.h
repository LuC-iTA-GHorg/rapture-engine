// Basic 32-bit software surface used by every platform piece.
//
// Pixel format is 0xAARRGGBB (SDL_PIXELFORMAT_ARGB8888 / XRGB8888 compatible).
// Why 32 bit instead of the original RGB555: the display texture is 32 bit, so
// the framebuffer needs no per-frame conversion, and DIB decoding is done once
// at load time. Transparency is carried in the alpha byte (0x00 = transparent,
// 0xFF = opaque; DIB data never has partial alpha), so the 16-bit "pixel 0 is
// transparent" rule is preserved exactly with no separate mask plane.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0; // right/bottom exclusive
    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
    bool IsEmpty() const { return right <= left || bottom <= top; }
};

class Surface {
public:
    Surface() = default;
    Surface(int w, int h) { Create(w, h); }
    void Create(int w, int h) {
        w_ = w;
        h_ = h;
        px_.assign(size_t(w) * size_t(h), 0);
    }
    int Width() const { return w_; }
    int Height() const { return h_; }
    int Pitch() const { return w_; } // in pixels
    uint32_t* Pixels() { return px_.data(); }
    const uint32_t* Pixels() const { return px_.data(); }
    uint32_t* Row(int y) { return px_.data() + size_t(y) * size_t(w_); }
    const uint32_t* Row(int y) const { return px_.data() + size_t(y) * size_t(w_); }

    void Clear(uint32_t argb) { std::fill(px_.begin(), px_.end(), argb); }
    void FillRect(const Rect& r, uint32_t argb) {
        int x0 = std::max(r.left, 0), x1 = std::min(r.right, w_);
        int y0 = std::max(r.top, 0), y1 = std::min(r.bottom, h_);
        if (x1 <= x0) return;
        for (int y = y0; y < y1; ++y) std::fill(Row(y) + x0, Row(y) + x1, argb);
    }

private:
    int w_ = 0, h_ = 0;
    std::vector<uint32_t> px_;
};
