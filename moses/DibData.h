// Bitmaps of moses.dll: CDibObj / CDibData / CDibList / CDibSect (moses.md sec. 2).
//
// PORT DESIGN
//  * Pixel storage is the platform one: decoded ARGB8888, top-down, pixel 0x0000 -> alpha 0
//    (platform/Dib.h, platform/Gfx.h). The original 16-bit RGB555 DIB section / CDibBits class
//    hierarchy (1/4/8/16 bit, RLE) is NOT reproduced: DecodeDib() already expanded the RLE, so
//    CDibData::Expand() is a no-op and `bpp` arguments are accepted and ignored.
//  * A CDibData is either a read-only picture (wraps a ::Dib, shared pixels, cheap copies - this is
//    what CResMgr hands out) or a writable surface (CDibSect, or a CDibData created with (w,h,bpp)).
//    DibBlt() / DibSet() on a destination need a writable surface; on a read-only one they do nothing.
//  * NOTE: platform ::BlitHot() does NOT implement moses' hot-spot rule. Moses centres the picture
//    on the origin (see CDibObj::center_bounds below); DibBlt must use the plain ::Blit()
//    with the top-left computed from `bounds`, never BlitHot.
#pragma once

#include "moses/Types.h"
#include "platform/Dib.h"
#include "platform/Gfx.h"

#include <memory>
#include <vector>

namespace moses {

class CDibData;
class CFileIO;

// ---------------------------------------------------------------------------------------------
// CDibPal (0x404 bytes, moses.dll CDibPal::CDibPal 0x10006d40): a LOGPALETTE { u16 version = 0x300; u16 count;
// PALETTEENTRY[256] }.  The constructor copies RGBQUADs (B,G,R,0) as raw dwords into the entries; ToRgbQuad (0x10006de0)
// swaps the first and third byte again (so a palette loaded from a file keeps its RGBQUAD order after the round trip).
class CDibPal {
public:
    CDibPal(const u32* rgbquad = nullptr, u32 n = 0);   // moses.dll 0x10006d40 (n clamped to 256)
    u16 Version() const { return 0x300; }
    u16 Count() const { return count_; }
    void SetEntry(u32 i, u32 rgb);                      // moses.dll 0x10006e20 (low 3 bytes of rgb stored as bytes 0..2)
    u32 Entry(u32 i) const;                             // raw dword of entry i (flags byte included)
    u16 ToRgbQuad(u8* out) const;                       // moses.dll 0x10006de0: out[4i] = e[2], out[4i+1] = e[1], out[4i+2] = e[0]; returns count
    u32 ToArgb(u32 i) const;                            // port: opaque 0xAARRGGBB of entry i read as RGBQUAD (B,G,R)
private:
    u16 count_ = 0;
    u8 e_[256][4] = {};
};

// ---------------------------------------------------------------------------------------------
// CDibObj (0x24 bytes in the original): common base, bounds relative to the hot-spot.
class CDibObj {
public:
    CDibObj() = default;
    virtual ~CDibObj() = default;

    // slot 1 / 2 of the original vtable (CDibData 0x100165a0, CDibList 0x1001647c)
    // moses.dll CDibData::DibBlt 0x10003b50 / CDibList::DibBlt 0x100034b0
    //   (x, y) = where the LEFT/TOP corner of `src` lands in the (centred) coordinates of `dst`;
    //   `src` is in the bitmap's own local coordinates (relative to the hot-spot, like bounds).
    virtual void DibBlt(CDibData* dst, int x, int y, RECT& src, u16 flip, DROP rop) = 0;
    // moses.dll CDibData::GetPixel 0x10003e50: 1 if the pixel is opaque, 0 if transparent/outside
    // (returns only 0/1, never the colour).
    virtual int GetPixel(int x, int y, u32 flip) const = 0;

    // moses.dll CDibObj::GetBounds 0x10001060: flip == 0 -> bounds; else mirrored around the hot-spot
    // (GetFlippedBounds 0x10003650): flipX (low byte) -> [-right,-left], flipY (high byte) -> [-bottom,-top].
    void GetBounds(RECT& r, u32 flip) const;
    // moses.dll CDibObj::OffsetPos 0x10003700: adds (dx,dy) to origin and to the 4 sides.
    void OffsetPos(int dx, int dy);

    const RECT& Bounds() const { return bounds_; }
    const POINT& Origin() const { return origin_; }

    // Base for the "transparent / invisible" flags read from DIBREC.flags (rrs_format.md sec. 3):
    bool IsSolid() const { return solid_; }        // flags bit0: opaque bitmap, GetPixel == 1 everywhere
    bool IsPlaceholder() const { return flag21_; } // flags bit3: DibBlt draws nothing (1x1 hot-spot markers)

protected:
    // moses.dll 0x10003610 center_bounds: right = w/2 + origin.x; left = right - w;
    //                                     bottom = h/2 + origin.y; top = bottom - h   (integer /2)
    void center_bounds(int w, int h);

    RECT bounds_;     // +04  relative to the hot-spot
    POINT origin_;    // +14  DIBREC origin (OffsetPos accumulates here)
    bool solid_ = false;
    bool flag21_ = false;
};

// ---------------------------------------------------------------------------------------------
// CDibData (0x24): one picture.
class CDibData : public CDibObj {
public:
    CDibData() = default;
    // moses.dll 0x100039d0 CDibData(w,h,bpp,bits*,own). Port: allocates a writable, zeroed (transparent)
    // surface w x h; `bits`/`own` are ignored (must be nullptr), bpp ignored (always 32 bit inside).
    CDibData(u32 w, u32 h, u32 bpp, u8* bits = nullptr, int own = 0);
    // moses.dll 0x10003a20 CDibData(DIBREC&, bits*, own). Port: wraps an already decoded picture
    // (rrs: DecodeDib); origin and flags come from the Dib (OriginX/OriginY/Flags()).
    explicit CDibData(const ::Dib& dib);
    // moses.dll 0x10003b00 CDibData(CDibData*): shares the pixels.
    CDibData(const CDibData& other);
    ~CDibData() override;                                // moses.dll 0x10003ac0

    void DibBlt(CDibData* dst, int x, int y, RECT& src, u16 flip, DROP rop) override; // moses.dll 0x10003b50
    int GetPixel(int x, int y, u32 flip) const override;                               // moses.dll 0x10003e50

    // moses.dll 0x10003ce0 / 0x10003d10. Fill a rectangle (local coords) with a colour; NULL rect = all.
    // `color` is the ORIGINAL raw value (RGB555 for 16 bit); the port converts it with Rgb555ToArgb()
    // and writes it opaque (alpha 0xFF) - 0 means black, not transparent. Needs a writable surface.
    void DibSet(u32 x, u32 y, u32 w, u32 h, u32 color);
    void DibSet(const RECT* r, u32 color);
    // moses.dll 0x10003d60: decompress RLE. Port: nothing to do, returns 1.
    int Expand() { return 1; }
    // moses.dll 0x10004070 Read: BMP file (BITMAPFILEHEADER + BITMAPINFOHEADER, 4 or 8 bit, BI_RGB, bottom-up or top-down).
    // The original keeps the palette indexes without a palette (the colours are lost); the port reads the palette that
    // follows the info header and expands to opaque 32 bit pixels.  Replaces the bitmap; returns 1 on success.
    int Read(CFileIO* io);
    // moses.dll 0x100041a0 Write: BMP file.  The original writes the bits as they are (only valid for 4/8 bit bitmaps; for the
    // 16 bit bitmaps of the game the size computation is wrong).  Port: 8 bit paletted BMP when the bitmap has <= 256
    // colours (transparent pixels written as black), otherwise 24 bit.
    int Write(CFileIO* io);
    // moses.dll 0x10003f30 Print: StretchDIBits to a printer DC, scaled to fit inside the printable area keeping the aspect
    // ratio and centred.  Port: the "printer" is a surface; `page` = printable rectangle in pixels (nullptr = whole surface).
    int Print(::Surface* dst, const RECT* page = nullptr);

    int Width() const;
    int Height() const;
    bool Empty() const;
    // Read side: the picture (valid when !IsWritable()) ...
    const ::Dib& Source() const { return dib_; }
    // ... write side: the surface (null for read-only pictures).
    ::Surface* Target() { return surf_.get(); }
    const ::Surface* Target() const { return surf_.get(); }
    bool IsWritable() const { return surf_ != nullptr; }

protected:
    void init_from_dib(const ::Dib& d);

    ::Dib dib_;                          // read-only picture (shared pixels)
    std::shared_ptr<::Surface> surf_;    // writable pixels (CDibSect, CDibData(w,h,bpp)); shared on copy
};

// ---------------------------------------------------------------------------------------------
// CDibList (0x24): z-ordered list of CDibObj, bounds = union (moses.md sec. 2). Used for RRS type 8
// (DIBLIST) and for CDrawDib::Animate with several ids. Does NOT own the items (CResMgr does).
class CDibList : public CDibObj {
public:
    explicit CDibList(u32 capacityHint = 0);              // moses.dll CDibList::CDibList(uint)
    CDibList(const CDibList& other);                      // moses.dll CDibList::CDibList(CDibList*)
    ~CDibList() override;                                 // moses.dll 0x10003380 CDibList::~CDibList
    int AddDib(CDibObj* dib);                             // moses.dll 0x10003440 (bounds = union)
    void DibBlt(CDibData* dst, int x, int y, RECT& src, u16 flip, DROP rop) override; // 0x100034b0
    int GetPixel(int x, int y, u32 flip) const override;                               // 0x10003580 (OR)
    u32 Count() const { return u32(items_.size()); }
    CDibObj* Item(u32 i) const { return items_[i]; }

private:
    std::vector<CDibObj*> items_;
};

// CDibTile (0x34): mosaic of dibs (RRS type 9, load_dibtile 0x1000c970). cols x rows cells of tileW x tileH
// pixels; every cell holds a private CDibData copy (shared pixels, 0x24 bytes) placed with OffsetPos.
// Layout in the original: +1c CDibData** cells, +20 count, +24 cols, +28 rows, +2c tileW, +30 tileH.
class CDibTile : public CDibObj {
public:
    CDibTile();                                           // moses.dll CDibTile::CDibTile 0x10014370
    ~CDibTile() override;                                 // moses.dll CDibTile::~CDibTile 0x100143a0 (frees cells: 0x10014320)
    CDibTile(const CDibTile&) = delete;
    CDibTile& operator=(const CDibTile&) = delete;
    // moses.dll CDibTile::Init 0x100145c0. DIBTILEHDR (12 byte, pool +4): u16 cols, u16 rows, u16 tileW,
    // u16 tileH, i16 x, i16 y (hot-spot position). Returns 1 (0 on allocation failure).
    struct HDR { u16 cols = 0, rows = 0, tileW = 0, tileH = 0; i16 x = 0, y = 0; };
    int Init(const HDR& hdr);
    // moses.dll CDibTile::SetDib 0x10014660: stores a COPY of `dib` in cell `index` and moves it to the cell.
    // NOTE: the original accepts index == count (off-by-one, writes past the array); the port refuses it.
    int SetDib(const CDibData* dib, u32 index);
    void DibBlt(CDibData* dst, int x, int y, RECT& src, u16 flip, DROP rop) override; // moses.dll 0x143c0
    int GetPixel(int x, int y, u32 flip) const override;                               // moses.dll 0x14490
    u32 Count() const { return u32(cells_.size()); }
    u32 Cols() const { return cols_; }
    u32 Rows() const { return rows_; }

private:
    void free_cells();                                    // moses.dll FUN_10014320
    std::vector<CDibData*> cells_;                        // +1c / +20
    u32 cols_ = 0, rows_ = 0, tileW_ = 0, tileH_ = 0;     // +24 +28 +2c +30
};

// ---------------------------------------------------------------------------------------------
// CDibSect (0x478): the drawing surface of a view. Original = 16-bit DIB section + memory DC.
// Port = a writable 32-bit ::Surface; the bounds are always centred:
//   bounds = [-(w>>1), -(h>>1), -(w>>1)+w, -(h>>1)+h]
class CDibSect : public CDibData {
public:
    CDibSect() = default;
    ~CDibSect() override = default;

    // moses.dll CDibSect::Init 0x100045f0 (HDC, w, h, bpp). hdc is ignored (nullptr). Returns 1.
    int Init(void* hdc, u32 w, u32 h, u32 bpp);
    // moses.dll CDibSect::Resize 0x10004660: recreates the surface (contents lost).
    int Resize(u32 w, u32 h);
    // moses.dll CDibSect::ChangeColorDepth 0x10004510. The game always calls (16). Port: always stays 32 bit
    // internally, returns 1 (kept so that `view->ChangeColorDepth(0x10)` transcribes 1:1).
    int ChangeColorDepth(u32 bpp);
    // moses.dll CDibSect::SetPalette 0x10004690: the original builds a logical palette (when the display is palettised) and
    // loads the 256 entry colour table of the DIB section.  Port: the surface is always 32 bit, so the palette is only
    // stored (for 8 bit consumers such as Read/Write) and exposed by Palette()/ColorTable().
    void SetPalette(const CDibPal* pal);
    const CDibPal* Palette() const { return pal_.get(); }
    // moses.dll CDibSect::delete_palette 0x10004480 (stock palette selected again, logical palette deleted)
    void DeletePalette() { pal_.reset(); }
    // The 256 entry RGBQUAD table passed to SetDIBColorTable (1024 bytes; unused entries zero).
    void ColorTable(u8* rgbquad1024) const;

    u32 SurfaceWidth() const { return width_; }
    u32 SurfaceHeight() const { return height_; }
    u32 Bpp() const { return bpp_; } // value last passed to Init/ChangeColorDepth (informative)
    const POINT& SurfaceOrg() const { return org_; } // (-(w>>1), -(h>>1))
    ::Surface& Pixels() { return *surf_; }
    const ::Surface& Pixels() const { return *surf_; }

protected:
    POINT org_;
    u32 width_ = 0, height_ = 0, bpp_ = 16;
    std::shared_ptr<CDibPal> pal_;   // +4c (HPALETTE in the original)
};

} // namespace moses
