// Mouse cursor objects of Aaron (WP-4): Cursor (base, class id 0x20108), ResCursor (a system / resource cursor), DibCursor (a sprite that follows
// the pointer), AnimCursor (an animated sprite), CursorPair (two cursors, one selected by the pointer state). aaron.md sec. 2.8.
//
// Original: every cursor registers itself with the scene's CursorMgr (Scene+0x670, array at +0x54) when it is created and leaves it when it
// dies; CursorMgr holds the "current" cursor object and drives it with two virtual calls:
//   slot 2  Activate(on) -> int   (0x10008bf0 base: returns the cursor id; the picker is enabled when the active cursor needs it)
//   slot 3  Handle(x, y, number) -> HCURSOR   (0x10008c10 ResCursor: the loaded cursor; the sprite cursors move their sprite and return 0, which
//           hides the OS cursor)
// A Cursor is an RObject. Port: HCURSOR is an opaque token (no Win32 cursor resources): ResCursor keeps (hInstance, resId); platform code may map
// the system ids (0x7f00 IDC_ARROW, 0x7f02 IDC_WAIT, 0x7f88 IDC_NO ...) to SDL system cursors.
#pragma once

#include "aaron/RObject.h"
#include "moses/AnimObjs.h"
#include "moses/AnimView.h"

namespace aaron {

class Scene;
class CursorMgr;

class Cursor : public RObject {
public:
    // Aaron ord 6 0x10008350: Cursor(scene, id): registers with scene->Cursor() when `scene` != null (the five global cursor sets pass null).
    Cursor(Scene* scene, u32 id);
    ~Cursor() override;                                                // Aaron ord 63 0x100083d2 (leaves the CursorMgr)
    u32 GetObjClassId() const override { return CID_CURSOR; }          // Aaron ord 367 0x10008be0 (0x20108)
    // slot 2: Aaron ord 131 0x10008bf0 (returns the id; Dib/Anim/Pair cursors override: ord 133 / 132 / 134)
    virtual int Activate(bool on) { (void)on; return int(id_); }
    // slot 3: the HCURSOR for the pointer at (x, y) with cursor `number`; base class: pure virtual (0x100153e3 _purecall) - the port returns 0.
    virtual uintptr_t Handle(int x, int y, int number) { (void)x; (void)y; (void)number; return 0; }
    u32 Id() const { return id_; }
    Scene* GetScene() const { return scene_; }

protected:
    Scene* scene_;  // +8
    u32 id_;        // +0xc
};

// ord 10: ResCursor(scene, hInstance, resId, id): LoadCursor(hInstance, MAKEINTRESOURCE(resId & 0xffff)) when either argument is nonzero.
class ResCursor : public Cursor {
public:
    ResCursor(Scene* scene, void* hInstance, u32 resId, u32 id);       // Aaron ord 10 0x10008441
    // Aaron ord 599 0x10008c10 (slot 3): the handle loaded by the constructor (0 = none / hidden)
    uintptr_t Handle(int x, int y, int number) override { (void)x; (void)y; (void)number; return handle_; }
    uintptr_t NativeHandle() const { return handle_; }                 // the +0x10 field
private:
    uintptr_t handle_ = 0;  // +0x10; port: (hInstance != 0) << 16 | resId & 0xffff (a token)
};

// ord 8: DibCursor(view, resId, id, hotX, hotY): a CDrawDib (+0x10) at level 0x7fffffff, pick mode 1, shown at pointer - hot spot.
class DibCursor : public Cursor {
public:
    DibCursor(Scene* view, u32 resId, int id, int hotX, int hotY);     // Aaron ord 8 0x100084bc (hot spot kept only when id != 0 - quirk)
    ~DibCursor() override;                                             // Aaron ord 65 0x1000859b (FreeResource + ~CDrawDib)
    // Aaron ord 133 0x1000865e (slot 2): off -> Hide(); on -> Animate(resId); returns id
    int Activate(bool on) override;
    // Aaron ord 597 0x1000862d (slot 3): SetPosition(x - hotX, y - hotY); returns 0 (the OS cursor is hidden)
    uintptr_t Handle(int x, int y, int number) override;
    moses::CDrawDib& Dib() { return dib_; }
    u32 ResId() const { return resId_; }
private:
    moses::CDrawDib dib_;   // +0x10
    u32 resId_;             // +0x34
    int hotX_ = 0, hotY_ = 0; // +0x38 / +0x3c
};

// ord 7: AnimCursor(view, resId, msgs, count, id, hotX, hotY): a CAnimDib (+0x10) driven by a QUEUEMSG list.
class AnimCursor : public Cursor {
public:
    AnimCursor(Scene* view, u32 resId, moses::QUEUEMSG** msgs, u32 count, int id, int hotX, int hotY); // Aaron ord 7 0x100086c4
    ~AnimCursor() override;                                            // Aaron ord 64 0x100087e0
    // Aaron ord 132 0x100088e8 (slot 2): off -> QStop + Hide; on -> QStart(msgs, count); returns id
    int Activate(bool on) override;
    // Aaron ord 596 0x100088ae (slot 3): SetPosition(x - hotX, y + hotY) - QUIRK: PLUS hotY, DibCursor subtracts it; returns 0
    uintptr_t Handle(int x, int y, int number) override;
    moses::CAnimDib& Anim() { return anim_; }
private:
    Scene* view_;
    moses::CAnimDib anim_;  // +0x10
    u32 resId_;             // +0xe0
    moses::QUEUEMSG** msgs_;
    u32 count_;
    int hotX_ = 0, hotY_ = 0; // +0xec / +0xf0
};

// ord 9: CursorPair(scene, a, b): two cursors; Handle(x, y, n) selects cursors[n != 0] (b defaults to a).
class CursorPair : public Cursor {
public:
    CursorPair(Scene* scene, Cursor* a, Cursor* b);                    // Aaron ord 9 0x1000896c (id = 0)
    ~CursorPair() override;                                            // Aaron ord 66 0x100089cb
    // Aaron ord 134 0x10008a83 (slot 2): forwards Activate(on) to the selected cursor
    int Activate(bool on) override;
    // Aaron ord 598 0x100089e7 (slot 3): r = cursors[n != 0]->Handle(x, y, n); when the selection changed: old->Activate(0), new->Activate(1)
    uintptr_t Handle(int x, int y, int number) override;
    Cursor* Selected() const { return c_[sel_]; }
private:
    Cursor* c_[2];          // +0x10 / +0x14
    int sel_ = 0;           // +0x18
};

} // namespace aaron
