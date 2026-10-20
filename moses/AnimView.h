// CAnimView / CAnimObj: message queues that animate CDrawObj (moses.md sec. 5).
//
// There is NO thread and NO timer callback: everything advances inside CAnimView::Service(),
// which the main loop (aaron::Application) calls once per iteration. Time is in milliseconds from
// CAnimView::Now() (original: timeGetTime(); port: ::Window::Ticks() / SDL_GetTicks()).
#pragma once

#include "moses/DrawView.h"
#include "moses/QueueMsg.h"

namespace moses {

class CAnimView;
class CAnimObj;

// OnNotify callback (SetNotifyProc). Original __stdcall.
using ANIMNOTIFYPROC = void (*)(void* data, CAnimObj* obj, intptr_t a, intptr_t b, intptr_t c);

// QM_POST / QM_SEND destination (original: PostMessageA/SendMessageA on the view's HWND). Port: aaron::Scene installs
// a proc that calls Scene::PostMessage (send == false) or dispatches immediately (send == true).
using ANIMPOSTPROC = void (*)(void* data, u32 msg, uintptr_t wParam, intptr_t lParam, bool send);

struct FRAME { // moses.md sec. 5: 0x10 bytes
    QUEUEMSG** list = nullptr;
    u32 index = 0;
    u32 count = 0;
    u32 loop = 0;
};

// ---------------------------------------------------------------------------------------------
// CAnimObj (0x9c). Not a CDrawObj: it is paired with one (`target`) by the derived class
// (CAnimDib = CAnimObj + CDrawDib ...). Shoot/Aaron override IsRange / next_message_hook /
// process_message_hook / queue_stop.
class CAnimObj {
public:
    explicit CAnimObj(CAnimView* view);                // moses.dll CAnimObj::CAnimObj(CAnimView*)
    CAnimObj(const CAnimObj& other);                   // moses.dll CAnimObj::CAnimObj(CAnimObj*)
    virtual ~CAnimObj();                               // moses.dll 0x1000ba50
    // moses.dll 0x10001510: the original copies the 0x26 dwords after the vptr (also the list links and the frame pointer,
    // which corrupts the view list). Port: copies the same state as the copy constructor and keeps this object's own links.
    CAnimObj& operator=(const CAnimObj& o);

    // ---- virtual interface, order of the original vtable (0x1001666c) -------------------------
    virtual int IsRange();                             // slot 1  base: 0 (CAnimDib: is_range)
    virtual void next_message_hook();                  // slot 2  op >= 100 when the message is loaded
    virtual int process_message_hook();                // slot 3  op >= 100 while it runs; returns 1 when complete; base returns 1
    virtual void queue_stop();                         // slot 4  moses.dll 0x1000b7cd: clears frame,msg,depth,speed,t_*,motion_delay,brk and target->need_index

    // ---- exported API ----------------------------------------------------------------------------
    int Start(QUEUEMSG** list, u32 n);                 // moses.dll CAnimObj::Start = view->QStart(this, list, n)
    int Stop();                                        // moses.dll CAnimObj::Stop  = view->QStop(this)
    uintptr_t GetId();                                 // moses.dll 0x100013f0 (= target->GetId())
    void SetId(uintptr_t id);                          // moses.dll 0x10001410
    int InQueue() const { return frame_ != nullptr; }  // moses.dll 0x10001430
    int IsPaused() const { return paused_; }           // moses.dll 0x10001450
    void SetBreak(int b) { brk_ = u8(b); }             // moses.dll 0x10001470 leave the current loop at the next LOOPEND
    void Pause(int on);                                // moses.dll 0x1000b8f9 (on resume, shifts t_motion/t_speed by the pause time)
    void SetMotion(u32 ms);                            // moses.dll 0x1000b989 ms between two frames
    void SetSpeed(u32 ms);                             // moses.dll 0x1000b9c5 ms between two LINETO steps
    void SetSync(int on);                              // moses.dll 0x1000ba01

    CAnimView* GetAnimView() const { return view_; }
    CDrawObj* Target() const { return target_; }

    // ---- engine internals (used by CAnimView) ---------------------------------------------------
    void queue_start(QUEUEMSG** list, u32 n);          // moses.dll 0x1000b76f
    int next_message();                                // moses.dll 0x1000b0ee  0 = queue finished
    int process_message();                             // moses.dll 0x1000b3c7  1 = message complete
    int process_delay();                               // moses.dll CAnimObj::process_delay
    int process_lineto();                              // moses.dll 0x1000af13
    void next_lineto();                                // moses.dll CAnimObj::next_lineto

    bool Ready() const { return ready_ != 0; }
    bool HasMessage() const { return msg_ != nullptr; }

protected:
    void SetTarget(CDrawObj* t) { target_ = t; }       // derived classes: SetTarget(static_cast<CDrawObj*>(this))

    FRAME* frame_ = nullptr;        // +04 current frame of the stack (null = not queued)
    QUEUEMSG* msg_ = nullptr;       // +08 current message
    CDrawObj* target_ = nullptr;    // +0c
    CAnimView* view_ = nullptr;     // +10
    CAnimObj* next_ = nullptr;      // +14 list of the view (attach_object appends at the tail)
    CAnimObj* prev_ = nullptr;      // +18
    u32 motion_delay_ = 0;          // +1c
    u32 t_motion_ = 0;              // +20 next instant (ms) for the next frame / end of DELAY
    u32 t_speed_ = 0;               // +24 next instant for the next LINETO step
    u32 t_pause_ = 0;               // +28
    u32 speed_ = 0;                 // +2c
    POINT start_, end_, cur_;       // +30 +38 +40
    u32 steps_ = 0, step_ = 0;      // +48 +4c
    u32 depth_ = 0;                 // +50 stack depth 0..3
    FRAME stack_[4];                // +54
    u8 brk_ = 0;                    // +94
    u8 in_call_ = 0;                // +95 running a callback
    u8 ready_ = 0;                  // +96 queue active (Service processes it)
    u8 delay_started_ = 0;          // +97
    u8 paused_ = 0;                 // +98
    u8 sync_ = 0;                   // +99 "no drift" timing: DELAY adds to the previous t instead of now

    friend class CAnimView;
    friend class CAnimDib;
    friend class CAnimRect;
};

// ---------------------------------------------------------------------------------------------
// CAnimView (0x49c) : CDrawView. aaron::Scene derives from it.
class CAnimView : public CDrawView {
public:
    CAnimView();                                       // moses.dll 0x1000a24c
    CAnimView(const CAnimView&) = delete;              // (original has a copy ctor 0x10001240, never used)
    ~CAnimView() override;                             // moses.dll 0x100012c0

    // Called in EVERY main-loop iteration: for each attached object with msg && ready && !paused:
    // `if (process_message(o)) next_message(o)`. Not re-entrant (iter guard). moses.dll 0x1000a42d
    void Service();
    int QStart(CAnimObj* obj, QUEUEMSG** list, u32 n);        // moses.dll 0x1000a396  (stops it first if already queued)
    int QStop(CAnimObj* obj);                                 // moses.dll 0x1000a3f7  returns 0
    int QCall(CAnimObj* obj, QUEUEMSG* msg);                  // moses.dll 0x1000a2b9  runs QM_CALL / QM_NOTIFY callbacks safely
    void Pause(int on);                                       // moses.dll 0x1000a27f  CAnimObj::Pause on all
    void SetNotifyProc(ANIMNOTIFYPROC proc, void* data);      // moses.dll 0x1000a4c3
    // Virtual (slot 3 of the vtable 0x10016270): QM_NOTIFY lands here. aaron::Scene overrides it
    // (Scene::OnObjectEvent). Base: calls the notify proc.
    virtual void OnNotify(CAnimObj* obj, intptr_t a, intptr_t b, intptr_t c); // moses.dll 0x1000a4f0
    void SetPostProc(ANIMPOSTPROC proc, void* data) { post_ = proc; post_data_ = data; } // port addition
    ANIMPOSTPROC PostProc() const { return post_; }
    void* PostProcData() const { return post_data_; }
    void* GetHWnd() const { return hwnd_; }                   // moses.dll 0x10001200 (opaque; unused by the port)
    void SetHWnd(void* h) { hwnd_ = h; }                      // moses.dll 0x10001220
    void attach_object(CAnimObj* o);                          // moses.dll 0x1000a0a0 (append at the tail)
    void detach_object(CAnimObj* o);                          // moses.dll 0x1000a1ad (keeps iter valid)
    int AnimObjectCount() const { return count_; }

    // timeGetTime(): the one clock of the engine, in ms. Tests may install their own.
    static u32 Now();
    static void SetTimeSource(u32 (*fn)());

protected:
    CAnimObj* head_ = nullptr;      // +47c
    CAnimObj* tail_ = nullptr;      // +480
    CAnimObj* iter_ = nullptr;      // +488
    void* hwnd_ = nullptr;          // +48c  target of QM_POST/QM_SEND in the original; the port routes them to the Scene queue
    int count_ = 0;                 // +490
    ANIMNOTIFYPROC notify_ = nullptr;   // +494
    void* notify_data_ = nullptr;       // +498
    ANIMPOSTPROC post_ = nullptr;       // port: QM_POST / QM_SEND
    void* post_data_ = nullptr;

    friend class CAnimObj;
};

} // namespace moses
