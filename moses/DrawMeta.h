// CDrawMeta / CAnimMeta: a drawable that plays an ENHANCED METAFILE (.emf) into a rectangle (moses.dll 0x10009250..0x100092d0,
// 0x1000bf50, 0x100020d0).  Unused by the games (no archive refers to one), kept for completeness / a future editor.
//
// ORIGINAL: CDrawMeta::Init(RECT*, path) = GetEnhMetaFileA(path) (EMF only: old 16 bit WMF / placeable WMF files are
// REFUSED by GetEnhMetaFile, so the port refuses them too), display = PlayEnhMetaFile(hdc, hemf, &rect).
//
// PORT: there is no GDI, so the file is played by a small EMF interpreter (MetaPlay.cpp) on top of the software GDI of
// GdiRaster (the same primitives CDrawRect / CDrawPoly use).  The picture frame of the metafile is mapped onto the
// destination rectangle exactly like PlayEnhMetaFile does (reference device -> frame -> rect).
//
// COVERAGE of the interpreter (record types of the EMF specification, MS-EMF):
//   supported : HEADER, EOF, SAVEDC/RESTOREDC, SETWINDOW/VIEWPORT ORG/EXT, SETMAPMODE (all modes; ISOTROPIC treated as
//               ANISOTROPIC), SETWORLDTRANSFORM / MODIFYWORLDTRANSFORM, SETBKMODE/BKCOLOR/TEXTCOLOR/TEXTALIGN/POLYFILLMODE,
//               CREATEPEN / EXTCREATEPEN / CREATEBRUSHINDIRECT / EXTCREATEFONTINDIRECTW / SELECTOBJECT (+stock objects) /
//               DELETEOBJECT, MOVETOEX/LINETO, RECTANGLE/ROUNDRECT/ELLIPSE, POLYGON/POLYLINE/POLYLINETO (+16 bit forms),
//               POLYPOLYGON/POLYPOLYLINE (+16), POLYBEZIER* (flattened), ARC/PIE/CHORD/ANGLEARC (as ellipse arcs),
//               SETPIXELV, EXTTEXTOUTA/EXTTEXTOUTW (single line, ETO_OPAQUE background), STRETCHDIBITS / SETDIBITSTODEVICE /
//               BITBLT / STRETCHBLT (bitmaps 1/4/8/16/24/32 bit uncompressed, SRCCOPY; PATCOPY/BLACKNESS/WHITENESS fills),
//               INTERSECTCLIPRECT / EXCLUDECLIPRECT (one excluded rectangle), paths (BEGINPATH..FILLPATH/STROKEPATH/
//               STROKEANDFILLPATH/SELECTCLIPPATH for polygonal figures).
//   approximated: dashed pens are solid; pattern/hatch brushes are solid; WINDING fill is even-odd; text is one line,
//               baseline = top + ascent of the selected font; escapement/orientation ignored.
//   skipped (counted in Metafile::skipped()): regions (FILLRGN, FRAMERGN, PAINTRGN, EXTSELECTCLIPRGN, EXTCREATEREGION),
//               ALPHABLEND / TRANSPARENTBLT / GRADIENTFILL, MASKBLT / PLGBLT / SETDIBCOLORTABLE, palettes, GDICOMMENT
//               (incl. EMF+ dual files: only the EMF part is drawn), flood fills, SMALLTEXTOUT, text paths.
#pragma once

#include "moses/AnimView.h"
#include "moses/DrawGdi.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace moses {

namespace emf {

struct Header {
    RECT bounds;               // rclBounds (inclusive, device units)
    RECT frame;                // rclFrame (inclusive, 0.01 mm)
    u32 nBytes = 0, nRecords = 0;
    u16 nHandles = 0;
    int devCx = 0, devCy = 0;  // szlDevice (reference device, pixels)
    int mmCx = 0, mmCy = 0;    // szlMillimeters
};

class Metafile {
public:
    // Parses an EMF image (EMR_HEADER with the " EMF" signature, records up to EMR_EOF).  Returns false for anything
    // else (also for WMF files, like GetEnhMetaFile).
    bool Load(const u8* data, size_t size);
    bool LoadFile(const char* path);
    bool Valid() const { return !data_.empty(); }
    const Header& header() const { return hdr_; }
    u32 RecordCount() const { return recCount_; }

    // PlayEnhMetaFile(hdc, hemf, &dest): draws into the software DC (`dc` coordinates = view coordinates) with the picture
    // frame stretched onto `dest`.  Returns the number of records executed; the types that are not supported are counted
    // in skipped().
    u32 Play(GdiCtx& dc, const RECT& dest) const;
    const std::map<u32, u32>& skipped() const { return skipped_; } // record type -> times skipped (last Play)

private:
    std::vector<u8> data_;
    Header hdr_;
    u32 recCount_ = 0;
    mutable std::map<u32, u32> skipped_;
};

} // namespace emf

// ---------------------------------------------------------------------------------------------
// CDrawMeta (0x38 bytes): CDrawGdi + HENHMETAFILE at +0x24 (+ 4 zeroed dwords).
class CDrawMeta : public CDrawGdi {
public:
    CDrawMeta(CDrawView* view, int level);              // moses.dll 0x10009270
    CDrawMeta(const CDrawMeta&) = default;
    ~CDrawMeta() override;                              // moses.dll 0x100092b0 (DeleteEnhMetaFile)
    CDrawObj* Copy() const override;

    // moses.dll 0x100092d0: rect_ = *rc (object coordinates), CDrawObj::Init(), GetEnhMetaFileA(path).  Returns 1 only when
    // the file was loaded.
    int Init(const RECT* rc, const char* path);
    // port: attach an already parsed metafile (an editor / tests building one in memory)
    int InitFromMemory(const RECT* rc, const u8* data, size_t size);
    // moses.dll 0x10009250: PlayEnhMetaFile(hdc, hemf, &rect_).
    // ORIGINAL QUIRK: the rectangle is the object-space rect_ and the DC origin is NOT offset by the object position, so
    // the metafile does not follow SetPosition() (bounds do).  Kept.
    void display(CDibSect* view, GdiCtx* dc, RECT& r) override;
    const emf::Metafile* Metafile() const { return meta_.get(); }

protected:
    std::shared_ptr<emf::Metafile> meta_;               // +24 HENHMETAFILE (shared on Copy: the original shares the handle)
};

// CAnimMeta (0xd4 bytes): CAnimObj + CDrawMeta at +0x9c.  Has no queue opcodes of its own (moves/shows like CAnimDib).
class CAnimMeta : public CAnimObj, public CDrawMeta {
public:
    CAnimMeta(CAnimView* view, int level);              // moses.dll 0x1000bf50
    ~CAnimMeta() override;
    CDrawObj* Copy() const override;                    // moses.dll 0x100020d0
    uintptr_t GetId() const { return CDrawObj::GetId(); }
    void SetId(uintptr_t id) { CDrawObj::SetId(id); }
};

} // namespace moses
