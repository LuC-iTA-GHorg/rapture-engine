// Timer widget (Aaron.dll 0x10006a70 .. 0x10006de3). Package 4.
#include "aaron/Scene.h"
#include "aaron/Sprites.h"

namespace aaron {

// Aaron 0x10006a70 (ord 29): Node(parent, cmdId, style), zero the 6 state words, Start(period, userData).
Timer::Timer(WidgetParent parent, u32 cmdId, u32 periodMs, u32 style, u32 userData)
    : Widget(parent, kNoRes, 0, 0, 0, cmdId, style) {
    Start(periodMs, userData);
}

Timer::~Timer() { Stop(); } // Aaron 0x10006ec0: ~Subscriber removes the listener

// Aaron 0x10006de3 (ord 600). period == 0 -> Stop.
void Timer::Start(u32 periodMs, u32 userData) {
    if (periodMs == 0) {
        Stop();
        return;
    }
    if (!scene_) return;
    TimerMgr& mgr = scene_->Timers();
    count_ = 0;
    user_ = userData;
    period_ = periodMs;
    running_ = true;
    done_ = false;
    acc_ = mgr.TotalMs() + periodMs;
    mgr.AddListener(this);
}

// Aaron ord 525 0x10006d83 (vtable slot 23 of the Timer): posts AMSG_NOTIFY(cmdId, this) unconditionally (Widget::PostNotify skips code 0).
void Timer::PostNotify(u32 code) {
    (void)code;
    if (scene_) scene_->PostMessage(AMSG_NOTIFY, cmdId_, reinterpret_cast<intptr_t>(this));
}

// Aaron 0x10006b94 (ord 425)
void Timer::Stop() {
    if (running_ && scene_) scene_->Timers().RemoveListener(this);
    running_ = false;
}

void Timer::OnAdvise(int code, intptr_t value, Broadcaster* src) {
    (void)src;
    if (code == static_cast<int>(AMSG_TICK)) OnTick(static_cast<u32>(value), 0);
}

// Aaron 0x10006bf6 (ord 452). `total` = TimerMgr accumulated ms.
void Timer::OnTick(u32 total, u32 delta) {
    (void)delta;
    if (acc_ <= total && hide_ == 0) {
        ++count_;
        if (user_ == 0xffffffffu || count_ < user_) {
            if ((style_ & 0x20000) == 0) acc_ = total + period_;  // drifting
            else acc_ += period_;                                 // no drift
        } else if ((style_ & 0x10000) == 0) {
            Stop();
        } else {
            done_ = true;                                         // self-delete after notifying
        }
        // FUN_10001043 (AddRef) ... FUN_1000105b (Release): the tree owns one reference.
        if (disable_ == 0) {
            // slot 23 of the Timer vtable = FUN_10006d83 (ord 525): PostMessage(0xc06, cmdId, this)
            PostNotify(0);
        }
        if (done_) delete this; // second Release -> destroys the timer. NOTE: the AMSG_NOTIFY just posted points to it.
    }
}

// Aaron 0x10006d07: pause (shift forward by the total) / resume (shift back).
void Timer::Pause(bool on) {
    if (running_) {
        if (on) acc_ += scene_->Timers().TotalMs();
        else acc_ -= scene_->Timers().TotalMs();
    }
}

// Aaron 0x10006b27
u32 Timer::Remaining() const {
    if (!running_) return 0;
    return hide_ == 0 ? acc_ - scene_->Timers().TotalMs() : acc_;
}

// Aaron 0x10006db7
void Timer::Restart() {
    if (period_ != 0) Start(period_, user_);
}

} // namespace aaron
