// Animated drawables: CAnimDib / CAnimRect / CAnimText / CAnimPoly (moses.md sec. 1, 5).
// Original: CAnimObj at +0 and the CDrawXxx at +0x9c (multiple inheritance). Same here:
//   class CAnimDib : public CAnimObj, public CDrawDib
// The `CDrawObj*` of a CAnimDib therefore differs from its `CAnimObj*` / `CAnimDib*`
// (as in the original: this+0x9c); use static_cast, never reinterpret_cast.
// CAnimMeta (EMF metafile) lives in DrawMeta.h (included here).
#pragma once

#include "moses/AnimView.h"
#include "moses/DrawGdi.h"
#include "moses/DrawMeta.h"

namespace moses {

// ---------------------------------------------------------------------------------------------
class CAnimDib : public CAnimObj, public CDrawDib {
public:
    CAnimDib(CAnimView* view, int level);              // moses.dll 0x1000ab20 CAnimDib(CAnimView*,int)
    CAnimDib(const CAnimDib& other);                   // moses.dll 0x1000aba3 CAnimDib(CAnimDib*)
    ~CAnimDib() override;                              // moses.dll 0x100015c0

    CDrawObj* Copy() const override;                   // moses.dll 0x10001540
    // CAnimObj and CDrawObj both have GetId/SetId (same meaning: the target's id). Disambiguated here.
    uintptr_t GetId() const { return CDrawObj::GetId(); }
    void SetId(uintptr_t id) { CDrawObj::SetId(id); }

    // overrides used by Shoot.dll (it imports exactly these four)
    int IsRange() override;                            // moses.dll 0x100015a0  -> is_range (hi != lo)
    void next_message_hook() override;                 // moses.dll 0x1000a9ec  100/101 -> set_range(group, first, first, last)
    int process_message_hook() override;               // moses.dll 0x1000aa73  100/101 -> process_animate, 102 -> process_animatelist, 103 -> next
    int update_index() override;                       // moses.dll 0x1000a8c1  advance cur (wrap lo..hi), reload id (group<<16)|idx; 0 = no redraw needed

    // set_range(group, first_shown, lo, hi) moses.dll 0x1000a530
    void set_range(u16 group, u16 first, u16 lo, u16 hi);
    int process_animate();                             // moses.dll 0x1000a6b0
    int process_animatelist();                         // moses.dll 0x1000a7c4

    // Added state (offsets inside the original object)
    u16 group_ = 0;        // +c0
    u16 shown_idx_ = 0;    // +c2
    u16 cur_ = 0;          // +c4
    u16 lo_ = 0, hi_ = 0;  // +c6 +c8
    i16 remaining_ = 0;    // +ca
    u8 forward_ = 1;       // +cc
    u8 flag_ = 0;          // +cd
    u8 is_range_ = 0;      // +ce
    u8 list_done_ = 0;     // +cf
    // (The callback of QM_ANIM_CB lives in the current QUEUEMSG::ptr.)
};

// ---------------------------------------------------------------------------------------------
// CAnimRect: iris effects (QM_IRIS_*): rectangle with a growing/shrinking hole.
class CAnimRect : public CAnimObj, public CDrawRect {
public:
    CAnimRect(CAnimView* view, int level);             // moses.dll 0x10001ce0 / 0x1000be3d
    CAnimRect(const CAnimRect& other);                 // moses.dll CAnimRect::CAnimRect(CAnimRect*)
    ~CAnimRect() override;                             // moses.dll 0x10001e40
    CDrawObj* Copy() const override;                   // moses.dll 0x10001a30
    uintptr_t GetId() const { return CDrawObj::GetId(); }
    void SetId(uintptr_t id) { CDrawObj::SetId(id); }

    void next_message_hook() override;                 // moses.dll 0x1000bd17
    int process_message_hook() override;               // moses.dll 0x1000bd75
    void queue_stop() override;                        // moses.dll 0x1000be1d  (clears the hole)
    // step i: hole.left = i*w/(2*steps-1) (same for top), hole.right = w-left; at the end Show (closed) or Hide (open).
    int process_box(int axes, int closing, int stepsDone);   // moses.dll 0x1000bb00 (axes: 1=X, 2=Y, 3=XY)

    int step_ = 0;     // +108
    int steps_ = 0;    // +10c
};

// ---------------------------------------------------------------------------------------------
class CAnimText : public CAnimObj, public CDrawText { // 0x104 bytes
public:
    CAnimText(CAnimView* view, int level);             // moses.dll CAnimText::CAnimText(CAnimView*,int)
    ~CAnimText() override;
    CDrawObj* Copy() const override;                   // moses.dll CAnimText::Copy
    uintptr_t GetId() const { return CDrawObj::GetId(); }
    void SetId(uintptr_t id) { CDrawObj::SetId(id); }
};

class CAnimPoly : public CAnimObj, public CDrawPoly { // 0x104 bytes
public:
    CAnimPoly(CAnimView* view, int level);             // moses.dll CAnimPoly::CAnimPoly(CAnimView*,int)
    ~CAnimPoly() override;
    CDrawObj* Copy() const override;                   // moses.dll CAnimPoly::Copy
    uintptr_t GetId() const { return CDrawObj::GetId(); }
    void SetId(uintptr_t id) { CDrawObj::SetId(id); }
};

} // namespace moses
