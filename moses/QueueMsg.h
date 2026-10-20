// QUEUEMSG: one instruction of a CAnimObj message queue (moses.md sec. 5).
//
// A queue is an array of QUEUEMSG* ("QUEUEMSG**" + count); CAnimView::QStart(obj, list, n).
// The original messages are packed dword structs of different lengths; the port uses ONE flat
// struct (so the static tables of Shoot.* can be written as arrays of QUEUEMSG) with this rule:
//
//     original +0x00 -> op
//     original +0x04 -> a   (or ptr when that field is a pointer: CALL/SUBLIST/GOTO)
//     original +0x08 -> b
//     original +0x0c -> c
//     original +0x10 -> s0  (i16)         [ANIM+CB: the callback pointer goes in ptr]
//     original +0x12 -> s1  (i16)
//     ANIMLIST ids  -> ids[0..3] (original +0x0c..+0x1b)
//
// a/b/c are intptr_t because CALL / NOTIFY / POST carry pointers (`{5, fn, this, 0}`), which do
// not fit 32 bit in a 64-bit build. The in-memory layout is therefore NOT the 32-bit one:
// tables must be re-typed (the factory functions below make that mechanical), never memcpy'd.
#pragma once

#include "moses/Types.h"

namespace moses {

class CAnimObj;
class CAnimDib;

// Opcodes. 1..15 are handled by CAnimObj itself; >= 100 are class specific (process_message_hook).
enum QMSG_OP : u32 {
    QM_LOOP = 1,     // a = count (0 = infinite)
    QM_DELAY = 2,    // a = ms
    QM_LOOPEND = 3,
    QM_FLIP = 4,     // a = fx, b = fy (FlipArg); executes the NEXT message immediately (batches the redraw)
    QM_CALL = 5,     // ptr = QMSG_CALLBACK, b = arg a, c = arg b      (via CAnimView::QCall)
    QM_SUBLIST = 6,  // ptr = QUEUEMSG**, b = count                    (push a sub list)
    QM_GOTO = 7,     // ptr = QUEUEMSG**, b = count                    (restart with another list)
    QM_LINETO = 8,   // a = x, b = y, c = d, s0 = relative, s1 = mode  (see moses.md sec. 5)
    QM_NOTIFY = 9,   // a, b, c -> CAnimView::OnNotify(obj, a, b, c)
    QM_MOVE = 10,    // a = dx, b = dy, c = relative(u8) | flipaware(u8)<<8; executes the next message at once
    QM_POST = 0xb,   // a = msg, b = wParam, c = lParam   -> PostMessage(view hwnd) (port: Scene message queue)
    QM_SEND = 0xc,   // same, SendMessage (port: immediate dispatch)
    QM_SYNC = 0xd,   // a = 0/1 ("no drift" timing)
    QM_SHOW = 0xe,   // a != 0 Show, else Hide
    QM_SETSPEED = 0xf, // a = speed (ms per LINETO step), b = motion (ms per frame, 0 = leave)

    QM_CLASS_BASE = 100,

    // CAnimDib (0x1000a9ec / 0x1000aa73)
    QM_ANIM = 100,      // a = delay ms (-1 = keep), b = group<<16 | first_index, c = last_index
    QM_ANIM_CB = 101,   // same as QM_ANIM + ptr = ANIM_CALLBACK (called after every frame)
    QM_ANIMLIST = 102,  // a = delay ms, b = count (<= 4), ids[] (shows the composition of several ids once)
    QM_SETRANGE = 103,  // a = group, b = start, c = lo, s0 = hi   (no animation, continues at once)

    // CAnimRect "iris" (0x1000bb00): a = delay ms, b = steps
    QM_IRIS_CLOSE_XY = 100, QM_IRIS_OPEN_XY = 101,
    QM_IRIS_CLOSE_X = 102, QM_IRIS_OPEN_X = 103,
    QM_IRIS_CLOSE_Y = 104, QM_IRIS_OPEN_Y = 105,
};

// Original callbacks are __stdcall; irrelevant in the port (no ABI to match).
using QMSG_CALLBACK = void (*)(CAnimObj* obj, uintptr_t a, uintptr_t b); // QM_CALL
using ANIM_CALLBACK = void (*)(CAnimDib* obj);                          // QM_ANIM_CB

struct QUEUEMSG {
    u32 op = 0;
    intptr_t a = 0, b = 0, c = 0;
    i16 s0 = 0, s1 = 0;
    void* ptr = nullptr;
    u32 ids[4] = {0, 0, 0, 0};
};

// Factory functions: `static QUEUEMSG tab[] = { qm::Sync(1), qm::Anim(50, 0x8b0220, 0, 7), ... };`
namespace qm {
constexpr QUEUEMSG Make(u32 op, intptr_t a = 0, intptr_t b = 0, intptr_t c = 0, i16 s0 = 0, i16 s1 = 0, void* ptr = nullptr) {
    QUEUEMSG m; m.op = op; m.a = a; m.b = b; m.c = c; m.s0 = s0; m.s1 = s1; m.ptr = ptr; return m;
}
constexpr QUEUEMSG Loop(int count) { return Make(QM_LOOP, count); }
constexpr QUEUEMSG Delay(int ms) { return Make(QM_DELAY, ms); }
constexpr QUEUEMSG LoopEnd() { return Make(QM_LOOPEND); }
constexpr QUEUEMSG Flip(unsigned fx, unsigned fy) { return Make(QM_FLIP, fx, fy); }
inline QUEUEMSG Call(QMSG_CALLBACK fn, uintptr_t a = 0, uintptr_t b = 0) { return Make(QM_CALL, 0, intptr_t(a), intptr_t(b), 0, 0, reinterpret_cast<void*>(fn)); }
inline QUEUEMSG SubList(QUEUEMSG** list, unsigned n) { return Make(QM_SUBLIST, 0, n, 0, 0, 0, list); }
inline QUEUEMSG Goto(QUEUEMSG** list, unsigned n) { return Make(QM_GOTO, 0, n, 0, 0, 0, list); }
constexpr QUEUEMSG LineTo(int x, int y, int d, int relative = 0, int mode = 0) { return Make(QM_LINETO, x, y, d, i16(relative), i16(mode)); }
constexpr QUEUEMSG Notify(intptr_t a, intptr_t b = 0, intptr_t c = 0) { return Make(QM_NOTIFY, a, b, c); }
constexpr QUEUEMSG Move(int dx, int dy, int relative = 1, int flipaware = 0) { return Make(QM_MOVE, dx, dy, (relative & 0xff) | ((flipaware & 0xff) << 8)); }
constexpr QUEUEMSG Post(unsigned msg, uintptr_t w = 0, intptr_t l = 0) { return Make(QM_POST, msg, intptr_t(w), l); }
constexpr QUEUEMSG Send(unsigned msg, uintptr_t w = 0, intptr_t l = 0) { return Make(QM_SEND, msg, intptr_t(w), l); }
constexpr QUEUEMSG Sync(int on) { return Make(QM_SYNC, on); }
constexpr QUEUEMSG Show(int show) { return Make(QM_SHOW, show); }
constexpr QUEUEMSG SetSpeed(int speed, int motion = 0) { return Make(QM_SETSPEED, speed, motion); }
constexpr QUEUEMSG Anim(int delayMs, unsigned group, unsigned first, unsigned last) { return Make(QM_ANIM, delayMs, intptr_t((group << 16) | first), last); }
inline QUEUEMSG AnimCb(int delayMs, unsigned group, unsigned first, unsigned last, ANIM_CALLBACK cb) {
    return Make(QM_ANIM_CB, delayMs, intptr_t((group << 16) | first), last, 0, 0, reinterpret_cast<void*>(cb));
}
constexpr QUEUEMSG SetRange(unsigned group, unsigned start, unsigned lo, unsigned hi) { return Make(QM_SETRANGE, group, start, lo, i16(hi)); }
constexpr QUEUEMSG Iris(u32 op, int delayMs, int steps) { return Make(op, delayMs, steps); }
inline QUEUEMSG AnimList(int delayMs, unsigned n, const u32* ids) {
    QUEUEMSG m = Make(QM_ANIMLIST, delayMs, n);
    for (unsigned i = 0; i < n && i < 4; ++i) m.ids[i] = ids[i];
    return m;
}
} // namespace qm

} // namespace moses
