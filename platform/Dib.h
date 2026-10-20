// 16-bit RLE DIB (RGB555, pixel 0 = transparent) decoded to a 32-bit surface.
//
// Format: docs/rrs_format.md sec. 3 (moses.dll CDibData 0x10003a20, compressed
// blit workers 0x100049c0 / 0x10004870 / 0x10004730).
//
// Design choice: decode ONCE to ARGB8888 (alpha 0x00 for the transparent pixel,
// 0xFF otherwise) instead of keeping RGB555 + mask. The framebuffer/texture is 32
// bit so blits are straight copies, and the pixel-0 rule maps exactly onto alpha.
// Memory cost (4 bytes/px vs 2) is irrelevant on modern hardware (largest sheet
// set is a few hundred 640x480-or-smaller sprites). Rows are stored TOP-DOWN
// (the file stores them bottom-up; decode flips them).
//
// Pixels are shared (shared_ptr) so DIBREF copies and Dib copies are cheap.
#pragma once

#include "platform/Gfx.h"

#include <cstdint>
#include <memory>
#include <string>

enum BlitFlags : int {
    kFlipH = 1, // mirror horizontally
    kFlipV = 2, // mirror vertically
};

class Dib {
public:
    bool Empty() const { return !pix_; }
    int Width() const { return w_; }
    int Height() const { return h_; }
    int OriginX() const { return ox_; } // hot-spot (DIBREC +4/+6)
    int OriginY() const { return oy_; }
    int Flags() const { return flags_; } // raw DIBREC flags byte
    const uint32_t* Pixels() const { return pix_ ? pix_->data() : nullptr; } // top-down, w*h
    const uint32_t* Row(int y) const { return pix_->data() + size_t(y) * w_; }

    // Same bitmap with another hot-spot (DIBREF: CDibData copy + OffsetPos, moses.dll 0x1000c8e0).
    Dib WithOrigin(int x, int y) const {
        Dib d = *this;
        d.ox_ = x;
        d.oy_ = y;
        return d;
    }

private:
    friend bool DecodeDib(const uint8_t*, size_t, Dib&, std::string*);
    int w_ = 0, h_ = 0, ox_ = 0, oy_ = 0, flags_ = 0;
    std::shared_ptr<std::vector<uint32_t>> pix_;
};

// Decodes a type-2 record (DIBREC + line table + RLE). Returns false and fills *err on bad data.
bool DecodeDib(const uint8_t* data, size_t size, Dib& out, std::string* err = nullptr);

// Converts one RGB555 value (bit 15 ignored) with the exact 5->8 expansion used by engine/tools/rrs_extract.py.
uint32_t Rgb555ToArgb(uint16_t v);

// Blits `src` with its top-left corner at (x, y), skipping transparent pixels, clipped to `dst`.
void Blit(Surface& dst, const Dib& src, int x, int y, int flags = 0);

// Same, but (x, y) is where the hot-spot lands (dst top-left = (x - originX, y - originY)).
// With kFlipH/kFlipV the hot-spot is mirrored too (ASSUMPTION: origin' = size - 1 - origin).
void BlitHot(Surface& dst, const Dib& src, int x, int y, int flags = 0);

// Opaque copy of a whole surface (movie frames, backgrounds), clipped.
void CopySurface(Surface& dst, const Surface& src, int x = 0, int y = 0);
