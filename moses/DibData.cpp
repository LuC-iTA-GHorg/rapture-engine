// moses.dll port - CDibObj / CDibData / CDibList / CDibSect (moses.md sec. 2). Package 1.
#include "moses/DibData.h"

namespace moses {

// ---------------------------------------------------------------------------------------------
// CDibObj
// moses.dll 0x10003610 center_bounds
void CDibObj::center_bounds(int w, int h) {
    int r = w / 2 + origin_.x;
    bounds_.right = r;
    bounds_.left = r - w;
    int b = h / 2 + origin_.y;
    bounds_.bottom = b;
    bounds_.top = b - h;
}

// moses.dll 0x10001060 GetBounds / 0x10003650 GetFlippedBounds
void CDibObj::GetBounds(RECT& r, u32 flip) const {
    if (flip == 0) {
        r = bounds_;
        return;
    }
    // GetFlippedBounds tests (flip & 0xff) / (flip & 0xff00) on the full u32.
    if ((flip & 0xff) == 0) { r.left = bounds_.left; r.right = bounds_.right; }
    else { r.left = -bounds_.right; r.right = -bounds_.left; }
    if ((flip & 0xff00) == 0) { r.top = bounds_.top; r.bottom = bounds_.bottom; }
    else { r.top = -bounds_.bottom; r.bottom = -bounds_.top; }
}

// moses.dll 0x10003700 OffsetPos
void CDibObj::OffsetPos(int dx, int dy) {
    origin_.x += dx;
    origin_.y += dy;
    bounds_.left += dx;
    bounds_.right += dx;
    bounds_.top += dy;
    bounds_.bottom += dy;
}

// ---------------------------------------------------------------------------------------------
// CDibData
// moses.dll 0x100039d0
CDibData::CDibData(u32 w, u32 h, u32 bpp, u8* bits, int own) {
    (void)bpp; (void)bits; (void)own; // port: always a zeroed (transparent) 32 bit surface
    surf_ = std::make_shared<::Surface>(int(w), int(h));
    center_bounds(int(w), int(h));
}

void CDibData::init_from_dib(const ::Dib& d) {
    dib_ = d;
    surf_.reset();
    origin_ = POINT{};
    center_bounds(d.Width(), d.Height());
    // moses.dll 0x10003a20: OffsetPos(DIBREC.origin - current origin), then the flags
    OffsetPos(d.OriginX() - origin_.x, d.OriginY() - origin_.y);
    flag21_ = ((d.Flags() >> 3) & 1) != 0; // bit3: DibBlt draws nothing
    solid_ = (d.Flags() & 1) != 0;         // bit0: opaque bitmap
}

// moses.dll 0x10003a20
CDibData::CDibData(const ::Dib& dib) { init_from_dib(dib); }

// moses.dll 0x10003b00 (shares the pixels)
CDibData::CDibData(const CDibData& other) : CDibObj(other), dib_(other.dib_), surf_(other.surf_) {}

int CDibData::Width() const { return surf_ ? surf_->Width() : dib_.Width(); }
int CDibData::Height() const { return surf_ ? surf_->Height() : dib_.Height(); }
bool CDibData::Empty() const { return !surf_ && dib_.Empty(); }

namespace {
// Pixel (x,y) in the bitmap's own storage (top-down); the caller guarantees the range.
inline uint32_t PixelAt(const ::Dib& d, const ::Surface* s, int x, int y) {
    return s ? s->Row(y)[x] : d.Row(y)[x];
}
} // namespace

// moses.dll 0x10003b50 CDibData::DibBlt
// (x,y) = where the top-left of `src` (local, hot-spot relative coords) lands in dst's centred coords.
// NOTE: not BlitHot. The original delegates to CDibBits::DibBlt with bottom-up rows; the port reads and
// writes top-down pixels directly (same result).
void CDibData::DibBlt(CDibData* dst, int x, int y, RECT& src, u16 flip, DROP rop) {
    if (flag21_) return;               // +0x21: placeholder bitmap draws nothing
    if (!dst || !dst->surf_) return;   // read-only destination: nothing
    if (Empty()) return;
    RECT fb;
    GetBounds(fb, flip);
    int w = src.right - src.left, h = src.bottom - src.top;
    if (w <= 0 || h <= 0) return;
    int sx0 = src.left - fb.left, sy0 = src.top - fb.top;
    if (solid_ && rop == DROP_TRANSPARENT) rop = DROP_OPAQUE; // "this[0x20] && rop == 0 -> rop = 1"
    bool fx = FlipX(flip), fy = FlipY(flip);
    const int dw = Width(), dh = Height();
    ::Surface& S = *dst->surf_;
    const ::Surface* ssrc = surf_.get();
    int dbx = dst->bounds_.left, dby = dst->bounds_.top;
    for (int oy = 0; oy < h; ++oy) {
        int ty = y + oy - dby;
        if (ty < 0 || ty >= S.Height()) continue;
        int sy = fy ? dh - 1 - (sy0 + oy) : sy0 + oy;
        if (sy < 0 || sy >= dh) continue;
        uint32_t* drow = S.Row(ty);
        for (int ox = 0; ox < w; ++ox) {
            int tx = x + ox - dbx;
            if (tx < 0 || tx >= S.Width()) continue;
            int sx = fx ? dw - 1 - (sx0 + ox) : sx0 + ox;
            if (sx < 0 || sx >= dw) continue;
            uint32_t p = PixelAt(dib_, ssrc, sx, sy);
            switch (rop) {
            case DROP_OPAQUE: drow[tx] = p; break;
            case DROP_ON_DEST: // blt_mask_dest 0x10004730: src != 0 && dst != 0
                if ((p >> 24) != 0 && (drow[tx] >> 24) != 0) drow[tx] = p;
                break;
            default:
                if ((p >> 24) != 0) drow[tx] = p;
                break;
            }
        }
    }
}

// moses.dll 0x10003e50 CDibData::GetPixel (0/1 only)
int CDibData::GetPixel(int x, int y, u32 flip) const {
    RECT fb;
    GetBounds(fb, flip);
    if (!(x >= fb.left && x < fb.right && y >= fb.top && y < fb.bottom)) return 0;
    if (solid_) return 1;
    if (flag21_) return 0;
    if (Empty()) return 0;
    int px = x - fb.left, py = y - fb.top;
    if (flip & 0xff) px = Width() - px - 1;
    if ((flip >> 8) & 0xff) py = Height() - py - 1; // original stores rows bottom-up, port top-down
    if (px < 0 || px >= Width() || py < 0 || py >= Height()) return 0;
    return (PixelAt(dib_, surf_.get(), px, py) >> 24) != 0 ? 1 : 0;
}

// moses.dll 0x10003ce0 DibSet (x,y,w,h in SURFACE pixel coordinates, 0-based)
void CDibData::DibSet(u32 x, u32 y, u32 w, u32 h, u32 color) {
    if (!surf_) return;
    uint32_t argb = Rgb555ToArgb(uint16_t(color)) | 0xff000000u; // opaque; 0 = black
    ::Rect r;
    r.left = int(x); r.top = int(y); r.right = int(x + w); r.bottom = int(y + h);
    surf_->FillRect(r, argb);
}

// moses.dll 0x10003d10 (NULL = whole bitmap)
void CDibData::DibSet(const RECT* r, u32 color) {
    if (r) DibSet(u32(r->left), u32(r->top), u32(r->right - r->left), u32(r->bottom - r->top), color);
    else DibSet(0, 0, u32(Width()), u32(Height()), color);
}

// ---------------------------------------------------------------------------------------------
// CDibList
CDibList::CDibList(u32 capacityHint) { items_.reserve(capacityHint); }

// moses.dll 0x100033b0
CDibList::CDibList(const CDibList& other) : CDibObj(other), items_(other.items_) {}

// moses.dll 0x10003440 (bounds = union; the first item sets the bounds)
int CDibList::AddDib(CDibObj* dib) {
    if (items_.empty()) bounds_ = dib->Bounds();
    else { RECT t; UnionRect(&t, &bounds_, &dib->Bounds()); bounds_ = t; }
    items_.push_back(dib);
    return 1;
}

// moses.dll 0x100034b0
void CDibList::DibBlt(CDibData* dst, int x, int y, RECT& src, u16 flip, DROP rop) {
    for (CDibObj* it : items_) {
        RECT b;
        it->GetBounds(b, flip);
        RECT c;
        if (IntersectRect(&c, &b, &src))
            it->DibBlt(dst, (c.left - src.left) + x, (c.top - src.top) + y, c, flip, rop);
    }
}

// moses.dll 0x10003580 (OR of the items)
int CDibList::GetPixel(int x, int y, u32 flip) const {
    for (CDibObj* it : items_)
        if (it->GetPixel(x, y, flip)) return 1;
    return 0;
}

// ---------------------------------------------------------------------------------------------
// CDibTile
// moses.dll 0x10014370 (zeroed fields)
CDibTile::CDibTile() {}

// moses.dll 0x100143a0
CDibTile::~CDibTile() { free_cells(); }

// moses.dll FUN_10014320: deletes every cell copy and the array
void CDibTile::free_cells() {
    for (CDibData* c : cells_) delete c;
    cells_.clear();
}

// moses.dll 0x100145c0
int CDibTile::Init(const HDR& h) {
    free_cells();
    tileW_ = h.tileW;
    tileH_ = h.tileH;
    cols_ = h.cols;
    rows_ = h.rows;
    cells_.assign(size_t(cols_) * rows_, nullptr); // CMem::Alloc(count*4, zeroed)
    center_bounds(int(cols_ * tileW_), int(tileH_ * rows_));
    OffsetPos(int(h.x) - origin_.x, int(h.y) - origin_.y);
    return 1;
}

// moses.dll 0x10014660
int CDibTile::SetDib(const CDibData* dib, u32 index) {
    // original test is `index <= count` (off-by-one: index == count writes past the array); the port refuses it
    if (index >= cells_.size() || dib == nullptr) return 0;
    CDibData* c = new CDibData(*dib);
    delete cells_[index];
    cells_[index] = c;
    c->OffsetPos(int((index % cols_) * tileW_ + (tileW_ >> 1)) + bounds_.left,
                 int((index / cols_) * tileH_ + (tileH_ >> 1)) + bounds_.top);
    return 1;
}

// moses.dll 0x100143c0: every cell whose (flipped) bounds meet `src` is drawn clipped to it.
void CDibTile::DibBlt(CDibData* dst, int x, int y, RECT& src, u16 flip, DROP rop) {
    for (CDibData* c : cells_) {
        if (!c) continue;
        RECT b;
        c->GetBounds(b, flip); // flip == 0 -> plain bounds (GetFlippedBounds 0x10003650 otherwise)
        RECT i;
        if (IntersectRect(&i, &b, &src))
            c->DibBlt(dst, (i.left - src.left) + x, (i.top - src.top) + y, i, flip, rop);
    }
}

// moses.dll 0x10014490
int CDibTile::GetPixel(int x, int y, u32 flip) const {
    RECT b;
    GetBounds(b, flip);
    if (x < b.left || x >= b.right || y < b.top || y >= b.bottom) return 0;
    int fx = x, fy = y;
    if ((flip & 0xff) != 0) fx = (b.right - x) + b.left;
    if (((flip >> 8) & 0xff) != 0) fy = (b.bottom - y) + b.top;
    u32 col = u32(fx - b.left) / tileW_;
    u32 row = u32(fy - b.top) / tileH_;
    size_t idx = size_t(cols_) * row + col;
    if (idx >= cells_.size() || !cells_[idx]) return 0;
    const CDibData* c = cells_[idx];
    // the cell is asked in its own frame (original uses cell+0x14/+0x18 = origin, subtracted from the tile cell centre)
    return c->GetPixel(x - (((c->Origin().x - int(tileW_ * col)) - int(tileW_ >> 1)) - b.left),
                       y - (((c->Origin().y - int(tileH_ * row)) - int(tileH_ >> 1)) - b.top), flip);
}

// ---------------------------------------------------------------------------------------------
// CDibSect
// moses.dll 0x100045f0
int CDibSect::Init(void* hdc, u32 w, u32 h, u32 bpp) {
    (void)hdc;
    width_ = w; height_ = h; bpp_ = bpp;
    surf_ = std::make_shared<::Surface>(int(w), int(h));
    org_ = POINT{-int(w >> 1), -int(h >> 1)};
    bounds_ = RECT{org_.x, org_.y, org_.x + int(w), org_.y + int(h)};
    return 1;
}

// moses.dll 0x10004660 (same size: nothing; otherwise the contents are lost)
int CDibSect::Resize(u32 w, u32 h) {
    if (w == width_ && h == height_ && surf_) return 1;
    return Init(nullptr, w, h, bpp_);
}

// moses.dll 0x10004510
int CDibSect::ChangeColorDepth(u32 bpp) { bpp_ = bpp; return 1; }

} // namespace moses
