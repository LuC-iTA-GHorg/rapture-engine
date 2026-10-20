// moses.dll port - CDrawObj / CDrawDib (moses.md sec. 4). Package 1.
#include "moses/DrawObj.h"
#include "moses/DrawView.h"

namespace moses {

// ---------------------------------------------------------------------------------------------
// CDrawObj
// moses.dll 0x10007f30 (pickmode = 1, level, linked into the view; NOT visible until Show/Animate)
CDrawObj::CDrawObj(CDrawView* view, int level) {
    p_.pickmode = 1;
    p_.level = level;
    p_.view = view;
    if (view) view->link_object(this);
}

// moses.dll 0x10007f90: copies the private block, links into the same view at the same level
CDrawObj::CDrawObj(const CDrawObj& other) : p_(other.p_) {
    p_.prev = p_.next = nullptr;
    if (p_.view) p_.view->link_object(this);
}

// moses.dll 0x10007fd0: Hide(), unlink
CDrawObj::~CDrawObj() {
    Hide();
    if (p_.view) p_.view->unlink_object(this);
}

CDrawObj* CDrawObj::Copy() const { return new CDrawObj(*this); }   // 0x10001780
void CDrawObj::get_bounds(RECT& r, u32 flip) { (void)flip; r = RECT{}; } // 0x100010b0 (empty)
int CDrawObj::update_index() { return 1; }                          // 0x10001500
void CDrawObj::Compose(CDibSect* view, RECT& r) { (void)view; (void)r; } // 0x10008420
u32 CDrawObj::GetPixel(int x, int y) const { (void)x; (void)y; return 0; } // 0x100017c0
int CDrawObj::InObject(int x, int y) const { (void)x; (void)y; return 0; } // 0x100017d0
void CDrawObj::display_hook() {}                                    // 0x100014f0

int CDrawObj::Init() { return p_.view != nullptr ? 1 : 0; }         // 0x100080f0

void CDrawObj::GetBoundsMid(POINT& pt) const { // 0x10008010
    pt.x = (p_.bounds.right + p_.bounds.left) / 2;
    pt.y = (p_.bounds.bottom + p_.bounds.top) / 2;
}

void CDrawObj::SetLevel(int level) { // 0x10008340
    if (p_.view) p_.view->SetLevel(this, level);
}

void CDrawObj::SetClipBox(int l, int t, int r, int b) { // 0x10008280
    p_.clip = RECT{l, t, r, b};
    p_.has_clip = 1;
}

void CDrawObj::SetClipBox(const RECT* r) { // 0x100082c0
    p_.has_clip = r != nullptr;
    if (p_.has_clip) p_.clip = *r;
}

void CDrawObj::SetPosition(int x, int y, int relative) { set_pos_impl(x, y, relative, 1); } // 0x10008370

void CDrawObj::SetUpdatePos(int x, int y, int relative) { // 0x100083b0
    if (relative) { x += p_.pos.x; y += p_.pos.y; }
    p_.pending.x = x;
    p_.pending.y = y;
    p_.has_pending = 1;
}

void CDrawObj::SetFlip(u32 fx, u32 fy) { set_flip_impl(fx, fy, 1); } // 0x10008310
void CDrawObj::Show() { p_.visible = 1; update_impl(); }              // 0x10008400
void CDrawObj::Update() { update_impl(); }                            // 0x10008410 (== 0x10007b90)

// moses.dll 0x100080c0
void CDrawObj::Hide() {
    if (p_.visible) erase_old_area(0);
    p_.need_index = 0;
    p_.visible = 0;
    p_.drawn = 0;
}

// moses.dll 0x10007af0: old = bounds; get_bounds(bounds, flip); bounds += pos; overlap_old = old & bounds
void CDrawObj::recalc_bounds() {
    RECT old = p_.bounds;
    get_bounds(p_.bounds, u32(p_.flipx | (p_.flipy << 8)));
    OffsetRect(&p_.bounds, p_.pos.x, p_.pos.y);
    RECT tmp;
    p_.overlap_old = IntersectRect(&tmp, &old, &p_.bounds) ? 1 : 0;
}

// moses.dll 0x10007dd0
void CDrawObj::set_pos_impl(int x, int y, int relative, int update) {
    p_.has_pending = 0;
    if (relative) { x += p_.pos.x; y += p_.pos.y; }
    int dy = y - p_.pos.y;
    int dx = x - p_.pos.x;
    if (dy != 0 || dx != 0) {
        RECT old = p_.bounds;
        OffsetRect(&p_.bounds, dx, dy);
        p_.pos.x = x;
        p_.pos.y = y;
        RECT tmp;
        p_.overlap_old = IntersectRect(&tmp, &old, &p_.bounds) ? 1 : 0; // quirk: vs the bounds before THIS move
        if (update) update_impl();
    }
}

// moses.dll 0x10007d40: each argument 0/1 = set, 2 = toggle, 3 = keep
void CDrawObj::set_flip_impl(u32 fx, u32 fy, int update) {
    u8 nx = fx == 2 ? (p_.flipx == 0) : fx == 3 ? p_.flipx : (fx != 0);
    u8 ny = fy == 2 ? (p_.flipy == 0) : fy == 3 ? p_.flipy : (fy != 0);
    if (nx != p_.flipx || ny != p_.flipy) {
        p_.flipx = nx;
        p_.flipy = ny;
        recalc_bounds();
        if (update) update_impl();
    }
}

// moses.dll 0x10007cc0: redisplay the old area with the object hidden.
// (The original also ExcludeClipRect()s the new bounds when excludeNew; the new area is recomposed right
// after by update_impl, so the final pixels are identical and the port does not need the clip exclusion.)
void CDrawObj::erase_old_area(int excludeNew) {
    (void)excludeNew;
    u8 v = p_.visible;
    p_.visible = 0;
    if (p_.view) p_.view->Display(p_.oldbounds);
    p_.visible = v;
}

// moses.dll 0x10007b90 (see moses.md sec. 4)
void CDrawObj::update_impl() {
    p_.dirty_batch = 0;
    if (p_.has_pending) set_pos_impl(p_.pending.x, p_.pending.y, 0, 0);
    if (!p_.visible) return;
    if (p_.need_index && update_index() == 0) goto done; // nothing to redraw
    {
        RECT r;
        bool display = true;
        if (!p_.drawn || !p_.overlap_old) {
            if (p_.drawn) erase_old_area(1); // old and new areas are disjoint
            if (p_.has_clip) {
                if (!IntersectRect(&r, &p_.bounds, &p_.clip)) display = false;
            } else {
                r = p_.bounds;
            }
        } else {
            UnionRect(&r, &p_.bounds, &p_.oldbounds);
            if (p_.has_clip && !IntersectRect(&r, &r, &p_.clip)) display = false;
        }
        if (display && p_.view) p_.view->Display(r);
    }
done:
    p_.oldbounds = p_.bounds;
    p_.overlap_old = 1;
    p_.drawn = 1;
    if (p_.display_hook) display_hook();
}

// moses.dll 0x100081c0
int CDrawObj::IsPicked(int x, int y) const {
    u8 m = p_.pickmode;
    if ((m & 1) == 0 && (p_.visible || (m & 4) != 0) && p_.bounds.left <= x && x < p_.bounds.right &&
        p_.bounds.top <= y && y < p_.bounds.bottom) {
        if (m & 2) return 1;
        return InObject(x, y);
    }
    return 0;
}

// moses.dll 0x10008240
int CDrawObj::PtInObject(int x, int y) const {
    if (p_.bounds.left <= x && x < p_.bounds.right && p_.bounds.top <= y && y < p_.bounds.bottom)
        if (InObject(x, y)) return 1;
    return 0;
}

// moses.dll 0x10008110: pixel scan of the bounds intersection (both visible)
int CDrawObj::IsContacting(const CDrawObj* other) const {
    if (!p_.visible || !other || !other->IsVisible()) return 0;
    RECT r;
    if (!IntersectRect(&r, &p_.bounds, other->GetBounds())) return 0;
    for (int y = r.top; y < r.bottom; ++y)
        for (int x = r.left; x < r.right; ++x)
            if (GetPixel(x, y) && other->GetPixel(x, y)) return 1;
    return 0;
}

// ---------------------------------------------------------------------------------------------
// CDrawDib
CDrawDib::CDrawDib(CDrawView* view, int level) : CDrawObj(view, level) {} // 0x10008700

// moses.dll 0x10008730: base copy, then re-Animate with the same ids
CDrawDib::CDrawDib(const CDrawDib& other) : CDrawObj(other) {
    u32 ids[4];
    u32 n = other.GetIds(ids);
    if (n) Animate(ids, n, nullptr);
}

// moses.dll 0x10008780
CDrawDib::~CDrawDib() {
    if (nids_) {
        for (u32 i = 0; i < nids_; ++i) {
            u32 id = ids_[i];
            if (p_.view) p_.view->CallResProc(this, 0, &id);
        }
        nids_ = 0;
    }
    delete list_;
    list_ = nullptr;
    dib_ = nullptr;
}

CDrawObj* CDrawDib::Copy() const { return new CDrawDib(*this); } // 0x100088c0

// moses.dll 0x100085b0
void CDrawDib::get_bounds(RECT& r, u32 flip) {
    if (!dib_) { r = RECT{}; return; }
    dib_->GetBounds(r, flip);
}

// moses.dll 0x10008610: dib->DibBlt(view, r.left, r.top, r - pos, flip, DROP_TRANSPARENT)
void CDrawDib::Compose(CDibSect* view, RECT& r) {
    if (!dib_) return;
    RECT l{r.left - p_.pos.x, r.top - p_.pos.y, r.right - p_.pos.x, r.bottom - p_.pos.y};
    dib_->DibBlt(view, r.left, r.top, l, GetFlip(), DROP_TRANSPARENT);
}

// moses.dll 0x10008680
u32 CDrawDib::GetPixel(int x, int y) const {
    if (!dib_) return 0;
    return u32(dib_->GetPixel(x - p_.pos.x, y - p_.pos.y, GetFlip()));
}

// moses.dll 0x100086c0
int CDrawDib::InObject(int x, int y) const {
    if (!dib_) return 0;
    return dib_->GetPixel(x - p_.pos.x, y - p_.pos.y, GetFlip());
}

// moses.dll 0x10008430 free_ids: ResProc mode 0 for every id (the id is passed by pointer, may be rewritten)
static void FreeIds(CDrawDib* self, CDrawView* view, u32* ids, u32 n) {
    for (u32 i = 0; i < n; ++i)
        if (view) view->CallResProc(self, 0, &ids[i]);
}

// moses.dll 0x10008540 build_list: the list is (re)filled from the loaded dibs
void CDrawDib::build_list() {}

// moses.dll 0x10008470 load_ids (max 4). New ids are loaded BEFORE the old ones are released.
int CDrawDib::load_ids(u32* ids, u32 n) {
    if (n == 0 || n > 4) return 0;
    CDibObj* loaded[4] = {nullptr, nullptr, nullptr, nullptr};
    u32 local[4];
    for (u32 i = 0; i < n; ++i) {
        local[i] = ids[i];
        loaded[i] = static_cast<CDibObj*>(p_.view ? p_.view->CallResProc(this, 1, &local[i]) : nullptr);
        if (!loaded[i]) {
            FreeIds(this, p_.view, local, i + 1); // quirk: also "frees" the id that failed to load
            return 0;
        }
    }
    if (n == 1) {
        dib_ = loaded[0];
    } else {
        delete list_;
        list_ = new CDibList(n);
        for (u32 i = 0; i < n; ++i) list_->AddDib(loaded[i]);
        dib_ = list_;
    }
    if (nids_) {
        u32 old[4];
        for (u32 i = 0; i < nids_; ++i) old[i] = ids_[i];
        FreeIds(this, p_.view, old, nids_);
    }
    for (u32 i = 0; i < n; ++i) ids_[i] = local[i];
    nids_ = n;
    recalc_bounds();
    return 1;
}

// moses.dll 0x100087b0
void CDrawDib::Animate(u32* ids, u32 n, POINT* pos) {
    if (!ids || n == 0) return;
    if (pos) set_pos_impl(pos->x, pos->y, 0, 0);
    if (load_ids(ids, n)) {
        p_.visible = 1;
        p_.need_index = 0;
        update_impl();
    }
}

void CDrawDib::Animate(u32 id, POINT* pos) { Animate(&id, 1, pos); } // 0x10008810

// moses.dll 0x10008830
void CDrawDib::AnimateDib(CDibObj* dib) {
    p_.visible = 1;
    if (nids_) {
        u32 old[4];
        for (u32 i = 0; i < nids_; ++i) old[i] = ids_[i];
        FreeIds(this, p_.view, old, nids_);
        nids_ = 0;
    }
    dib_ = dib;
    recalc_bounds();
    p_.visible = 1;
    p_.need_index = 0;
    update_impl();
}

// moses.dll 0x10008880
u32 CDrawDib::GetIds(u32* ids) const {
    for (u32 i = 0; i < nids_; ++i) ids[i] = ids_[i];
    return nids_;
}

} // namespace moses
