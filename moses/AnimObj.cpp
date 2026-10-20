// moses.dll port - CAnimObj: the QUEUEMSG state machine (moses.md sec. 5). Package 1.
#include "moses/AnimView.h"

#include <cstdlib>

namespace moses {

// moses.dll 0x1000b835
CAnimObj::CAnimObj(CAnimView* view) : view_(view) {}

// moses.dll 0x1000b868: copies the state; if it was queued the copy is attached to the view too
CAnimObj::CAnimObj(const CAnimObj& o)
    : frame_(o.frame_), msg_(o.msg_), target_(o.target_), view_(o.view_), motion_delay_(o.motion_delay_),
      t_motion_(o.t_motion_), t_speed_(o.t_speed_), t_pause_(o.t_pause_), speed_(o.speed_), start_(o.start_),
      end_(o.end_), cur_(o.cur_), steps_(o.steps_), step_(o.step_), depth_(o.depth_), brk_(o.brk_), in_call_(o.in_call_),
      ready_(o.ready_), delay_started_(o.delay_started_), paused_(o.paused_), sync_(o.sync_) {
    for (int i = 0; i < 4; ++i) stack_[i] = o.stack_[i];
    if (frame_ != nullptr && view_ != nullptr) {
        view_->attach_object(this);
        frame_ = &stack_[depth_];
    } else {
        frame_ = nullptr;
    }
}

// moses.dll 0x1000b8cd
CAnimObj::~CAnimObj() {
    if (view_) view_->QStop(this);
}

// moses.dll 0x10001510
CAnimObj& CAnimObj::operator=(const CAnimObj& o) {
    if (this == &o) return *this;
    msg_ = o.msg_; target_ = o.target_; view_ = o.view_; motion_delay_ = o.motion_delay_; t_motion_ = o.t_motion_;
    t_speed_ = o.t_speed_; t_pause_ = o.t_pause_; speed_ = o.speed_; start_ = o.start_; end_ = o.end_; cur_ = o.cur_;
    steps_ = o.steps_; step_ = o.step_; depth_ = o.depth_; brk_ = o.brk_; in_call_ = o.in_call_; ready_ = o.ready_;
    delay_started_ = o.delay_started_; paused_ = o.paused_; sync_ = o.sync_;
    for (int i = 0; i < 4; ++i) stack_[i] = o.stack_[i];
    frame_ = o.frame_ ? &stack_[depth_] : nullptr;
    return *this;
}

int CAnimObj::IsRange() { return 0; }              // 0x100014e0
void CAnimObj::next_message_hook() {}              // 0x100014f0
int CAnimObj::process_message_hook() { return 1; } // 0x10001500

// moses.dll 0x1000b7cd
void CAnimObj::queue_stop() {
    msg_ = nullptr;
    frame_ = nullptr;
    depth_ = 0;
    speed_ = 0;
    t_speed_ = 0;
    t_motion_ = 0;
    motion_delay_ = 0;
    if (target_) target_->p_.need_index = 0;
    brk_ = 0;
}

int CAnimObj::Start(QUEUEMSG** list, u32 n) { return view_ ? view_->QStart(this, list, n) : 0; } // 0x10001490
int CAnimObj::Stop() { return view_ ? view_->QStop(this) : 0; }                                  // 0x100014c0
uintptr_t CAnimObj::GetId() { return target_->GetId(); }                                         // 0x100013f0
void CAnimObj::SetId(uintptr_t id) { target_->SetId(id); }                                       // 0x10001410

// moses.dll 0x1000b76f
void CAnimObj::queue_start(QUEUEMSG** list, u32 n) {
    frame_ = &stack_[0];
    frame_->list = list;
    frame_->index = 0;
    frame_->count = n;
    if (!in_call_) next_message();
    ready_ = 1;
}

// moses.dll 0x1000b8f9: pausing records the time; resuming shifts the pending timers by the pause length
void CAnimObj::Pause(int on) {
    u8 p = on != 0;
    if (paused_ != p) {
        u32 now = CAnimView::Now();
        if (p == 0) {
            if (t_motion_ != 0) t_motion_ += now - t_pause_;
            if (t_speed_ != 0) t_speed_ += now - t_pause_;
        } else {
            t_pause_ = now;
        }
        paused_ = p;
    }
}

// moses.dll 0x1000b989
void CAnimObj::SetMotion(u32 ms) {
    motion_delay_ = ms;
    u32 now = CAnimView::Now();
    t_speed_ = now + speed_;
    t_motion_ = now + ms;
}

// moses.dll 0x1000b9c5
void CAnimObj::SetSpeed(u32 ms) {
    speed_ = ms;
    u32 now = CAnimView::Now();
    t_speed_ = now + ms;
    t_motion_ = now + motion_delay_;
}

// moses.dll 0x1000ba01
void CAnimObj::SetSync(int on) {
    sync_ = u8(on);
    t_speed_ = 0;
    t_motion_ = on == 0 ? 0 : CAnimView::Now();
}

// moses.dll 0x1000b0ee next_message: loads the next message. Returns 0 when the queue is finished.
int CAnimObj::next_message() {
    if (!frame_) return 0;
    if (frame_->index >= frame_->count) {
        if (depth_ == 0) return view_->QStop(this);
        --depth_;
        --frame_;
    }
    msg_ = frame_->list[frame_->index];
    ++frame_->index;
    switch (msg_->op) {
    case QM_LOOP: { // push a copy of the frame (body = what follows) with loop = count (0 = infinite)
        if (depth_ > 3) return view_->QStop(this);
        ++depth_;
        FRAME* old = frame_;
        ++frame_;
        frame_->list = old->list;
        frame_->count = old->count;
        frame_->index = old->index;
        frame_->loop = u32(msg_->a);
        return next_message();
    }
    case QM_DELAY:
        delay_started_ = 0;
        break;
    case QM_LOOPEND: {
        if (depth_ == 0) return view_->QStop(this);
        FRAME* c = frame_;
        if (brk_ == 0 && c->loop != 1) {
            if (c->loop != 0) --c->loop;
            c = c - 1; // parent frame: its index is the first instruction of the body
        } else {
            --depth_;
            --frame_;
            frame_->index = c->index; // leave the loop: continue after LOOPEND
        }
        frame_->index = c->index;
        return next_message();
    }
    default:
        if (msg_->op >= QM_CLASS_BASE) next_message_hook();
        break;
    case QM_SUBLIST:
        if (depth_ > 3) return view_->QStop(this);
        ++depth_;
        ++frame_;
        frame_->list = static_cast<QUEUEMSG**>(msg_->ptr);
        frame_->count = u32(msg_->b);
        frame_->index = 0;
        frame_->loop = 0;
        return next_message();
    case QM_LINETO:
        next_lineto();
        break;
    }
    return 1;
}

// moses.dll 0x1000ae9e process_delay: the first call arms the timer, later calls test it
int CAnimObj::process_delay() {
    u32 now = CAnimView::Now();
    if (delay_started_ == 0) {
        delay_started_ = 1;
        if (sync_ == 0 || t_motion_ == 0) t_motion_ = now; // "no drift": add to the previous instant
        t_motion_ += u32(msg_->a);
        return 0;
    }
    return t_motion_ <= now ? 1 : 0;
}

// moses.dll 0x1000ad30 next_lineto
void CAnimObj::next_lineto() {
    t_speed_ = sync_ == 0 ? 0 : t_motion_;
    cur_ = target_->p_.pos;
    start_ = cur_;
    end_.x = int32_t(msg_->a);
    end_.y = int32_t(msg_->b);
    if (msg_->s0 != 0) { end_.x += start_.x; end_.y += start_.y; } // relative
    step_ = 0;
    steps_ = u32(msg_->c);
    if (msg_->s1 != 0) { // speed mode: c = pixels per step
        u32 dx = u32(std::abs(end_.x - start_.x)), dy = u32(std::abs(end_.y - start_.y));
        u32 m = dy < dx ? dx : dy;
        u32 d = steps_ == 0 ? 1 : steps_;
        steps_ = m / d + (m % d != 0 ? 1 : 0);
    }
}

// moses.dll 0x1000af13 process_lineto: one position step every `speed_` ms; frame animation of a ranged
// CAnimDib every `motion_delay_` ms during the move. Returns 1 when the last step has been done.
int CAnimObj::process_lineto() {
    int range = IsRange();
    u32 now = CAnimView::Now();
    if (now < t_speed_) {
        if (range && t_motion_ <= now) {
            target_->p_.need_index = 1;
            if (sync_ == 0 || t_motion_ == 0) t_motion_ = now;
            t_motion_ += motion_delay_;
            target_->p_.overlap_old = 1;
            target_->update_impl();
        }
        return 0;
    }
    if (sync_ == 0 || t_speed_ == 0) t_speed_ = now;
    t_speed_ += speed_;
    if (range && t_motion_ <= now) {
        target_->p_.need_index = 1;
        if (sync_ == 0 || t_motion_ == 0) t_motion_ = now;
        t_motion_ += motion_delay_;
    }
    ++step_;
    if (step_ <= steps_) {
        int x = ((end_.x - start_.x) * int(step_)) / int(steps_) + start_.x; // integer division as the original
        int y = ((end_.y - start_.y) * int(step_)) / int(steps_) + start_.y;
        target_->set_pos_impl(x - cur_.x, y - cur_.y, 1, 1);
        cur_.x = x;
        cur_.y = y;
    }
    return steps_ <= step_ ? 1 : 0;
}

// moses.dll 0x1000b3c7 process_message: runs the current message; returns 1 when it is complete
int CAnimObj::process_message() {
    int done = 1;
    switch (msg_->op) {
    case QM_DELAY:
        done = process_delay();
        break;
    default:
        done = process_message_hook();
        break;
    case QM_FLIP:
    case QM_MOVE: {
        if (msg_->op == QM_FLIP) {
            target_->set_flip_impl(u8(msg_->a), u8(msg_->b), 0);
        } else {
            int dx = int(msg_->a), dy = int(msg_->b);
            if ((msg_->c >> 8) & 0xff) { // flip aware: mirror the offset when the object is flipped
                u16 f = target_->GetFlip();
                if (f & 0xff) dx = -dx;
                if (f >> 8) dy = -dy;
            }
            target_->set_pos_impl(dx, dy, int(msg_->c & 0xff), 0);
        }
        target_->p_.dirty_batch = 1;
        int n = next_message();
        done = n == 0 ? 1 : 0;
        if (target_->p_.dirty_batch) target_->update_impl();
        if (msg_ != nullptr) { // the next message runs at once
            t_motion_ = 0;
            done = process_message();
        }
        break;
    }
    case QM_CALL:
    case QM_NOTIFY:
        if (view_) done = view_->QCall(this, msg_);
        break;
    case QM_GOTO:
        view_->QStart(this, static_cast<QUEUEMSG**>(msg_->ptr), u32(msg_->b));
        done = 0;
        break;
    case QM_LINETO:
        done = process_lineto();
        break;
    case QM_POST:
    case QM_SEND:
        if (view_ && view_->post_) view_->post_(view_->post_data_, u32(msg_->a), uintptr_t(msg_->b), intptr_t(msg_->c), msg_->op == QM_SEND);
        break;
    case QM_SYNC:
        sync_ = u8(msg_->a);
        t_speed_ = 0;
        t_motion_ = 0;
        if (next_message()) done = process_message();
        break;
    case QM_SHOW:
        if (msg_->a == 0) target_->Hide(); else target_->Show();
        break;
    case QM_SETSPEED:
        SetSpeed(u32(msg_->a));
        if (msg_->b != 0) SetMotion(u32(msg_->b));
        if (next_message()) done = process_message();
        break;
    }
    return done;
}

} // namespace moses
