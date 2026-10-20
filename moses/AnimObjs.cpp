// moses.dll port - CAnimDib / CAnimRect (iris) / CAnimText / CAnimPoly (moses.md sec. 5). Package 1.
#include "moses/AnimObjs.h"

namespace moses {

// ---------------------------------------------------------------------------------------------
// CAnimDib
// moses.dll 0x1000ab20 (the extra state is zeroed; forward_ too, as the dword at +0xcc)
CAnimDib::CAnimDib(CAnimView* view, int level) : CAnimObj(view), CDrawDib(view, level) {
    forward_ = 0; flag_ = 0; is_range_ = 0; list_done_ = 0;
    SetTarget(static_cast<CDrawObj*>(this));
}

// moses.dll 0x1000aba3
CAnimDib::CAnimDib(const CAnimDib& o) : CAnimObj(o), CDrawDib(o) {
    forward_ = 0; flag_ = 0; is_range_ = 0; list_done_ = 0;
    SetTarget(static_cast<CDrawObj*>(this));
}

CAnimDib::~CAnimDib() { Stop(); } // detach from the view while the CDrawDib half is still alive

CDrawObj* CAnimDib::Copy() const { return static_cast<CDrawObj*>(new CAnimDib(*this)); } // 0x10001540

int CAnimDib::IsRange() { return is_range_; } // 0x100015a0

// moses.dll 0x1000a530 set_range(group, first_shown, lo, hi): range [lo..hi] inclusive, ascending if hi >= lo
void CAnimDib::set_range(u16 group, u16 first, u16 lo, u16 hi) {
    group_ = group;
    lo_ = lo;
    hi_ = hi;
    remaining_ = i16(i16(hi_) - i16(lo_));
    is_range_ = remaining_ != 0;
    flag_ = 0;
    p_.need_index = 1;
    forward_ = remaining_ >= 0;
    if (!forward_) {
        // quirk preserved: out of range start -> hi (not lo)
        cur_ = (first < hi || lo < first) ? hi : first;
        remaining_ = i16(-remaining_);
    } else {
        cur_ = (first < lo || hi < first) ? lo : first;
    }
    remaining_ = i16(remaining_ + 1);
}

// moses.dll 0x1000a6b0 process_animate: one frame every `delay` ms; callback after each frame (op 101)
int CAnimDib::process_animate() {
    u32 now = CAnimView::Now();
    if (now < t_motion_) return 0;
    if (remaining_ == 0) return 1;
    if (msg_->a != -1) motion_delay_ = u32(msg_->a);
    if (sync_ == 0 || t_motion_ == 0) t_motion_ = now;
    t_motion_ += motion_delay_;
    p_.need_index = 1;
    p_.visible = 1;
    update_impl();
    if (msg_->op == QM_ANIM_CB) reinterpret_cast<ANIM_CALLBACK>(msg_->ptr)(this);
    remaining_ = i16(remaining_ - 1);
    return (remaining_ == 0 && motion_delay_ == 0) ? 1 : 0;
}

// moses.dll 0x1000a7c4 process_animatelist: shows the composition of up to 4 ids once, then waits `delay`
int CAnimDib::process_animatelist() {
    u32 now = CAnimView::Now();
    if (now < t_motion_) return 0;
    if (list_done_ != 0) return 1;
    QUEUEMSG* m = msg_;
    if (m->b != 0 && load_ids(m->ids, u32(m->b))) {
        motion_delay_ = u32(m->a);
        if (sync_ == 0 || t_motion_ == 0) t_motion_ = now;
        t_motion_ += motion_delay_;
        p_.visible = 1;
        list_done_ = 1;
        p_.need_index = 0;
        update_impl();
        return motion_delay_ == 0 ? 1 : 0;
    }
    return 1;
}

// moses.dll 0x1000a8c1 update_index: advance cur (wrap lo..hi) and load the frame that WAS current.
// Returns 0 when the picture is unchanged and already drawn (no redraw needed).
int CAnimDib::update_index() {
    u16 old = cur_;
    if (forward_ == 0) {
        cur_ = u16(cur_ - 1);
        if (cur_ < hi_) cur_ = lo_;
    } else {
        cur_ = u16(cur_ + 1);
        if (hi_ < cur_) cur_ = lo_;
    }
    p_.need_index = 0;
    u32 id = (u32(group_) << 16) | old;
    CDibObj* before = dib_;
    if (!load_ids(&id, 1)) return 0;
    shown_idx_ = old;
    bool same = flag_ != 0 && p_.visible != 0 && dib_ == before;
    flag_ = 1;
    return same ? 0 : 1;
}

// moses.dll 0x1000a9ec next_message_hook
void CAnimDib::next_message_hook() {
    u32 op = msg_->op;
    if (op >= 100) {
        if (op < 0x66) {
            set_range(u16(u32(msg_->b) >> 16), u16(msg_->b), u16(msg_->b), u16(msg_->c));
            p_.dirty_batch = 0;
        } else if (op == QM_ANIMLIST) {
            list_done_ = 0;
            flag_ = 0;
        }
    }
}

// moses.dll 0x1000aa73 process_message_hook
int CAnimDib::process_message_hook() {
    int done = 1;
    switch (msg_->op) {
    case QM_ANIM:
    case QM_ANIM_CB:
        done = process_animate();
        break;
    case QM_ANIMLIST:
        done = process_animatelist();
        break;
    case QM_SETRANGE:
        set_range(u16(msg_->a), u16(msg_->b), u16(msg_->c), u16(msg_->s0));
        if (next_message()) done = process_message();
        break;
    }
    return done;
}

// ---------------------------------------------------------------------------------------------
// CAnimRect (iris)
CAnimRect::CAnimRect(CAnimView* view, int level) : CAnimObj(view), CDrawRect(view, level) { // 0x1000be3d
    SetTarget(static_cast<CDrawObj*>(this));
}

CAnimRect::CAnimRect(const CAnimRect& o) : CAnimObj(o), CDrawRect(o) {
    step_ = steps_ = 0;
    SetTarget(static_cast<CDrawObj*>(this));
}

CAnimRect::~CAnimRect() { Stop(); }

CDrawObj* CAnimRect::Copy() const { return static_cast<CDrawObj*>(new CAnimRect(*this)); } // 0x10001a30

// moses.dll 0x1000bd17: ops 100..105
void CAnimRect::next_message_hook() {
    if (msg_->op >= 100 && msg_->op < 0x6a) {
        has_hole_ = 0;
        step_ = 0;
        steps_ = int(msg_->b);
    }
}

// moses.dll 0x1000bd75: 100/101 X+Y, 102/103 X, 104/105 Y; even op = close
int CAnimRect::process_message_hook() {
    int done = 1;
    u32 op = msg_->op;
    switch (op) {
    case QM_IRIS_CLOSE_XY: case QM_IRIS_OPEN_XY: done = process_box(op == QM_IRIS_CLOSE_XY, 1, 1); break;
    case QM_IRIS_CLOSE_X: case QM_IRIS_OPEN_X: done = process_box(op == QM_IRIS_CLOSE_X, 1, 0); break;
    case QM_IRIS_CLOSE_Y: case QM_IRIS_OPEN_Y: done = process_box(op == QM_IRIS_CLOSE_Y, 0, 1); break;
    }
    return done;
}

// moses.dll 0x1000be1d
void CAnimRect::queue_stop() {
    has_hole_ = 0;
    CAnimObj::queue_stop();
}

// moses.dll 0x1000bb00 process_box. NOTE: the parameters of the header declaration are, in the original,
// (closing, useX, useY) - the names axes/closing/stepsDone of the header are misleading.
// Step i: hole.left = i*w/(2*steps-1) (unsigned maths), hole.right = w-left, same for Y; hole then offset by
// the object's bounds origin. At the end the object is Shown (closed) or Hidden (open).
int CAnimRect::process_box(int closing, int useX, int useY) {
    u32 now = CAnimView::Now();
    if (now < t_motion_) return 0;
    motion_delay_ = u32(msg_->a);
    if (sync_ == 0 || t_motion_ == 0) t_motion_ = now;
    t_motion_ += motion_delay_;
    ++step_;
    if (step_ < steps_) {
        int i = closing == 0 ? steps_ - step_ : step_;
        const RECT& b = p_.bounds;
        u32 den = u32(steps_) * 2u - 1u;
        int w = b.right - b.left, h = b.bottom - b.top;
        hole_.left = useX ? int(u32(i * w) / den) : 0;
        hole_.right = w - hole_.left;
        hole_.top = useY ? int(u32(i * h) / den) : 0;
        hole_.bottom = h - hole_.top;
        OffsetRect(&hole_, b.left, b.top);
        p_.visible = 1;
        has_hole_ = 1;
        update_impl();
        return 0;
    }
    has_hole_ = 0;
    if (closing == 0) Hide(); else Show();
    return 1;
}

// ---------------------------------------------------------------------------------------------
// vftables of the multiple-inheritance sub-objects (C++ ABI artefacts, nothing to port; addresses listed so that the
// coverage tool finds them): moses.dll 0x10016658 CAnimDib vftable{for CAnimObj}, 0x1001663c CAnimDib vftable{for CDrawDib},
// 0x10016340 CAnimRect vftable{for CDrawGdi}, 0x1001633c CAnimRect vftable{for CGdiPen}.
// ---------------------------------------------------------------------------------------------
// CAnimText / CAnimPoly: only the constructors and Copy have code of their own
CAnimText::CAnimText(CAnimView* view, int level) : CAnimObj(view), CDrawText(view, level) { // 0x1000bfc0
    SetTarget(static_cast<CDrawObj*>(this));
}
CAnimText::~CAnimText() { Stop(); }
CDrawObj* CAnimText::Copy() const { // CAnimText::Copy 0x10001f70: CAnimObj copy + CDrawText copy
    CAnimText* c = new CAnimText(view_, p_.level);
    CDrawObj* hi = c->p_.prev; CDrawObj* lo = c->p_.next;
    c->p_ = p_;
    c->p_.prev = hi; c->p_.next = lo;
    c->bk_ = bk_; c->opaque_ = opaque_; c->edge_width_ = edge_width_; c->rect_ = rect_;
    return static_cast<CDrawObj*>(c);
}

CAnimPoly::CAnimPoly(CAnimView* view, int level) : CAnimObj(view), CDrawPoly(view, level) { // 0x1000bed0
    SetTarget(static_cast<CDrawObj*>(this));
}
CAnimPoly::~CAnimPoly() { Stop(); }
CDrawObj* CAnimPoly::Copy() const { // CAnimPoly::Copy 0x10001600 (deep copy of the points)
    CAnimPoly* c = new CAnimPoly(view_, p_.level);
    CDrawObj* hi = c->p_.prev; CDrawObj* lo = c->p_.next;
    c->p_ = p_;
    c->p_.prev = hi; c->p_.next = lo;
    c->bk_ = bk_; c->opaque_ = opaque_; c->edge_width_ = edge_width_;
    c->pen_ = pen_; c->pen_cache_ = pen_cache_; c->closed_ = closed_;
    c->AddPoints(pts_, npts_);
    c->rect_ = rect_;
    return static_cast<CDrawObj*>(c);
}

} // namespace moses
