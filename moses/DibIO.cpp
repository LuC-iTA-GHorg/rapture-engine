// moses.dll port (WP-5): CDibPal, CDibData::Read/Write/Print (BMP files, printing), CDibSect palette, CDrawView::Profile.
#include "moses/DibData.h"
#include "moses/DrawView.h"
#include "moses/FileIO.h"

#include <SDL.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

namespace moses {

// ---------------------------------------------------------------------------------------------
// destructors (the pixel storage is shared_ptr: nothing to free by hand)
// moses.dll 0x10003ac0 CDibData::~CDibData
CDibData::~CDibData() {}
// moses.dll 0x10003380 CDibList::~CDibList
CDibList::~CDibList() {}

// CDibPal
// moses.dll 0x10006d40
CDibPal::CDibPal(const u32* rgbquad, u32 n) {
    if (n > 0x100) n = 0x100;
    count_ = u16(n);
    if (rgbquad && count_)
        for (u32 i = 0; i < count_; ++i) std::memcpy(e_[i], &rgbquad[i], 4);
}

// moses.dll 0x10006e20
void CDibPal::SetEntry(u32 i, u32 rgb) {
    if (i >= 256) return;
    e_[i][0] = u8(rgb);
    e_[i][1] = u8(rgb >> 8);
    e_[i][2] = u8(rgb >> 16);
}

u32 CDibPal::Entry(u32 i) const {
    if (i >= 256) return 0;
    u32 v;
    std::memcpy(&v, e_[i], 4);
    return v;
}

// moses.dll 0x10006de0
u16 CDibPal::ToRgbQuad(u8* out) const {
    for (u32 i = 0; i < count_; ++i) {
        out[4 * i + 2] = e_[i][0];
        out[4 * i + 1] = e_[i][1];
        out[4 * i + 0] = e_[i][2];
    }
    return count_;
}

u32 CDibPal::ToArgb(u32 i) const {
    if (i >= 256) return 0xff000000u;
    return 0xff000000u | u32(e_[i][2]) << 16 | u32(e_[i][1]) << 8 | u32(e_[i][0]); // RGBQUAD: B,G,R
}

// ---------------------------------------------------------------------------------------------
// CDibSect palette
// moses.dll 0x10004690 (the surface stays 32 bit; see DibData.h)
void CDibSect::SetPalette(const CDibPal* pal) {
    if (pal_) DeletePalette(); // delete_palette 0x10004480
    if (pal) pal_ = std::make_shared<CDibPal>(*pal);
}

void CDibSect::ColorTable(u8* rgbquad1024) const {
    std::memset(rgbquad1024, 0, 1024);
    if (pal_) pal_->ToRgbQuad(rgbquad1024);
}

// ---------------------------------------------------------------------------------------------
// BMP structures (little endian, packed on disk)
namespace {
struct BmpFileHeader { u16 type; u32 size; u16 r1, r2; u32 offBits; }; // 14 bytes on disk
inline u32 Le32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
inline u16 Le16(const u8* p) { return u16(p[0] | p[1] << 8); }
inline void Put16(u8* p, u32 v) { p[0] = u8(v); p[1] = u8(v >> 8); }
inline void Put32(u8* p, u32 v) { p[0] = u8(v); p[1] = u8(v >> 8); p[2] = u8(v >> 16); p[3] = u8(v >> 24); }
} // namespace

// moses.dll 0x10004070
int CDibData::Read(CFileIO* io) {
    if (!io) return 0;
    u8 fh[14], ih[40];
    if (!io->Read(fh, 14) || !io->Read(ih, 40)) return 0;
    u32 offBits = Le32(fh + 10);
    u32 biSize = Le32(ih), w = Le32(ih + 4);
    i32 hRaw = i32(Le32(ih + 8));
    u16 planes = Le16(ih + 12), bpp = Le16(ih + 14);
    u32 comp = Le32(ih + 16), clrUsed = Le32(ih + 32);
    // original checks: file seek to offBits != 0, 'BM', planes == 1, compression == 0, bpp 8 or 4
    if (Le16(fh) != 0x4d42 || offBits == 0 || planes != 1 || comp != 0 || (bpp != 8 && bpp != 4)) return 0;
    if (w == 0 || w > 16384 || hRaw == 0) return 0;
    u32 h = u32(hRaw < 0 ? -hRaw : hRaw);
    if (h > 16384) return 0;
    // palette (the original skips it and keeps the raw indexes)
    u32 ncol = clrUsed ? clrUsed : (1u << bpp);
    if (ncol > (1u << bpp)) ncol = 1u << bpp;
    std::vector<u32> quad(ncol, 0);
    if (io->Seek(long(14 + biSize), 0) == 0xFFFFFFFFu) return 0;
    if (!io->Read(quad.data(), ncol * 4)) return 0;
    CDibPal pal(quad.data(), ncol);
    if (io->Seek(long(offBits), 0) == 0xFFFFFFFFu) return 0;
    // original: size = ((bpp*w)/8 + 3 & ~3) * h; for odd 4 bit widths that is one byte short per row: use the correct stride
    u32 stride = ((bpp * w + 31) / 32) * 4;
    std::vector<u8> bits(size_t(stride) * h);
    if (!io->Read(bits.data(), u32(bits.size()))) return 0;
    CDibData tmp(w, h, 32);
    ::Surface& S = *tmp.surf_;
    for (u32 y = 0; y < h; ++y) {
        const u8* row = bits.data() + size_t(stride) * (hRaw < 0 ? y : h - 1 - y); // bottom-up unless the height is negative
        uint32_t* dst = S.Row(int(y));
        for (u32 x = 0; x < w; ++x) {
            u32 idx = bpp == 8 ? row[x] : ((x & 1) ? (row[x >> 1] & 0xf) : (row[x >> 1] >> 4));
            dst[x] = pal.ToArgb(idx);
        }
    }
    *this = tmp;
    return 1;
}

// moses.dll 0x100041a0
int CDibData::Write(CFileIO* io) {
    if (!io || Empty()) return 0;
    const int w = Width(), h = Height();
    const ::Surface* S = surf_.get();
    auto px = [&](int x, int y) -> u32 { return S ? S->Row(y)[x] : dib_.Row(y)[x]; };
    auto rgb = [](u32 argb) -> u32 { return (argb & 0xff000000u) ? (argb & 0xffffffu) : 0u; }; // transparent -> black
    std::map<u32, u8> index;
    std::vector<u32> table;
    bool paletted = true;
    for (int y = 0; y < h && paletted; ++y)
        for (int x = 0; x < w; ++x) {
            u32 c = rgb(px(x, y));
            if (!index.count(c)) {
                if (table.size() == 256) { paletted = false; break; }
                index[c] = u8(table.size());
                table.push_back(c);
            }
        }
    u32 bpp = paletted ? 8 : 24;
    u32 stride = ((u32(w) * bpp + 31) / 32) * 4;
    u32 palBytes = paletted ? u32(table.size()) * 4 : 0;
    u32 off = 14 + 40 + palBytes;
    u32 dataSize = stride * u32(h);
    u8 fh[14] = {'B', 'M'}, ih[40] = {};
    Put32(fh + 2, off + dataSize);
    Put32(fh + 10, off);
    Put32(ih, 40);
    Put32(ih + 4, u32(w));
    Put32(ih + 8, u32(h));
    Put16(ih + 12, 1);
    Put16(ih + 14, bpp);
    Put32(ih + 20, dataSize);
    Put32(ih + 32, u32(table.size()));
    if (!io->Write(fh, 14) || !io->Write(ih, 40)) return 0;
    if (paletted) {
        std::vector<u8> pq(palBytes);
        for (size_t i = 0; i < table.size(); ++i) { // RGBQUAD: B,G,R,0
            pq[4 * i] = u8(table[i]);
            pq[4 * i + 1] = u8(table[i] >> 8);
            pq[4 * i + 2] = u8(table[i] >> 16);
        }
        if (!io->Write(pq.data(), palBytes)) return 0;
    }
    std::vector<u8> row(stride);
    for (int y = h - 1; y >= 0; --y) { // bottom-up
        std::fill(row.begin(), row.end(), u8(0));
        for (int x = 0; x < w; ++x) {
            u32 c = rgb(px(x, y));
            if (paletted) row[size_t(x)] = index[c];
            else { row[size_t(x) * 3] = u8(c); row[size_t(x) * 3 + 1] = u8(c >> 8); row[size_t(x) * 3 + 2] = u8(c >> 16); }
        }
        if (!io->Write(row.data(), stride)) return 0;
    }
    return 1;
}

// moses.dll 0x10003f30
int CDibData::Print(::Surface* dst, const RECT* page) {
    if (!dst || Empty() || !Expand()) return 0;
    const int bw = Width(), bh = Height();
    RECT pg = page ? *page : RECT{0, 0, dst->Width(), dst->Height()};
    int pw = pg.Width(), ph = pg.Height();
    if (pw <= 0 || ph <= 0 || bw <= 0 || bh <= 0) return 0;
    // same arithmetic as the original: fit width or height, keep the aspect ratio, centre
    int a = pw * bh, b = ph * bw;
    int dw = pw, dh = ph;
    if (b < a) dw = b / bh;
    if (a < b) dh = a / bw;
    int x0 = (pw - dw) / 2 + pg.left, y0 = (ph - dh) / 2 + pg.top;
    const ::Surface* S = surf_.get();
    for (int y = 0; y < dh; ++y) {
        int ty = y0 + y;
        if (ty < 0 || ty >= dst->Height()) continue;
        int sy = int(int64_t(y) * bh / dh);
        for (int x = 0; x < dw; ++x) {
            int tx = x0 + x;
            if (tx < 0 || tx >= dst->Width()) continue;
            int sx = int(int64_t(x) * bw / dw);
            dst->Row(ty)[tx] = S ? S->Row(sy)[sx] : dib_.Row(sy)[sx]; // SRCCOPY
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------
// moses.dll 0x10007500: a benchmark of the compositor
u32 CDrawView::Profile(u32 ms) {
    u32 n = 0;
    u32 start = SDL_GetTicks();
    do {
        RECT r;
        GetDisplayBox(r);
        Display(r);
        ++n;
    } while (SDL_GetTicks() < start + ms);
    return n;
}

} // namespace moses
