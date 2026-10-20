// moses.dll port - CAnimView (moses.md sec. 5). Package 1.
#include "moses/AnimView.h"
#include "platform/Window.h"

namespace moses {

namespace {
u32 (*g_timeSource)() = nullptr;
}

// timeGetTime(): one clock for the whole engine.
u32 CAnimView::Now() { return g_timeSource ? g_timeSource() : ::Window::Ticks(); }
void CAnimView::SetTimeSource(u32 (*fn)()) { g_timeSource = fn; }

CAnimView::CAnimView() {} // moses.dll 0x1000a24c

// moses.dll 0x100012c0. Port: objects still attached are detached so that they do not keep a dangling view.
CAnimView::~CAnimView() {
    for (CAnimObj* o = head_; o;) {
        CAnimObj* n = o->next_;
        o->next_ = o->prev_ = nullptr;
        o->frame_ = nullptr;
        o->msg_ = nullptr;
        o->view_ = nullptr;
        o = n;
    }
    head_ = tail_ = iter_ = nullptr;
    count_ = 0;
}

// moses.dll 0x1000a0a0 (append at the tail)
void CAnimView::attach_object(CAnimObj* o) {
    ++count_;
    o->next_ = o->prev_ = nullptr;
    if (!head_) {
        head_ = tail_ = o;
    } else {
        o->prev_ = tail_;
        tail_->next_ = o;
        tail_ = o;
    }
}

// moses.dll 0x1000a1ad. If the object is the Service cursor the cursor is reset (the loop then ends).
void CAnimView::detach_object(CAnimObj* o) {
    --count_;
    CAnimObj* n = o->next_;
    CAnimObj* p = o->prev_;
    o->next_ = o->prev_ = nullptr;
    if (!p) head_ = n; else p->next_ = n;
    if (!n) tail_ = p; else n->prev_ = p;
    if (o == iter_) iter_ = nullptr;
}

// moses.dll 0x1000a27f
void CAnimView::Pause(int on) {
    for (CAnimObj* o = head_; o; o = o->next_) o->Pause(on);
}

// moses.dll 0x1000a2b9 QCall: runs a QM_CALL / QM_NOTIFY callback. v_.cur is the re-entrancy guard; if the
// object is destroyed / unlinked during the callback (cur cleared) it returns 0 without touching the object.
int CAnimView::QCall(CAnimObj* obj, QUEUEMSG* msg) {
    if (v_.cur != nullptr) return 0;
    CDrawObj* t = obj->target_;
    v_.cur = t;
    obj->in_call_ = 1;
    obj->ready_ = 0;
    if (msg->op == QM_NOTIFY) {
        OnNotify(obj, msg->a, msg->b, msg->c);
    } else if (msg->op == QM_CALL) {
        reinterpret_cast<QMSG_CALLBACK>(msg->ptr)(obj, uintptr_t(msg->b), uintptr_t(msg->c));
    }
    if (v_.cur == t) {
        obj->ready_ = 1;
        obj->in_call_ = 0;
        v_.cur = nullptr;
        return 1;
    }
    return 0;
}

// moses.dll 0x1000a396
int CAnimView::QStart(CAnimObj* obj, QUEUEMSG** list, u32 n) {
    if (!obj || !list || n == 0) return 0;
    if (obj->frame_ == nullptr) attach_object(obj);
    else obj->queue_stop();
    obj->queue_start(list, n);
    return 1;
}

// moses.dll 0x1000a3f7
int CAnimView::QStop(CAnimObj* obj) {
    if (obj->frame_ != nullptr) {
        obj->queue_stop();
        detach_object(obj);
    }
    return 0;
}

// moses.dll 0x1000a42d Service (not re-entrant: iter_ != null means a Service is already running)
void CAnimView::Service() {
    if (iter_ != nullptr) return;
    CAnimObj* o = head_;
    while (o) {
        iter_ = o->next_;
        if (o->msg_ != nullptr && o->ready_ != 0 && o->paused_ == 0 && o->process_message() != 0) o->next_message();
        o = iter_;
    }
    iter_ = nullptr;
}

void CAnimView::SetNotifyProc(ANIMNOTIFYPROC proc, void* data) { notify_ = proc; notify_data_ = data; } // 0x1000a4c3

// moses.dll 0x1000a4f0
void CAnimView::OnNotify(CAnimObj* obj, intptr_t a, intptr_t b, intptr_t c) {
    if (notify_) notify_(notify_data_, obj, a, b, c);
}

} // namespace moses
