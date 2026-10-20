// Cursor objects (WP-4). See Cursors.h.
#include "aaron/Cursors.h"

#include "aaron/Scene.h"

namespace aaron {

Cursor::Cursor(Scene* scene, u32 id) : scene_(scene), id_(id) { // ord 6 0x10008350
    if (scene_) scene_->Cursor().AddCursor(this);              // FUN_10013332
}

Cursor::~Cursor() { // ord 63 0x100083d2
    if (scene_) scene_->Cursor().RemoveCursor(this);           // FUN_10013373
}

ResCursor::ResCursor(Scene* scene, void* hInstance, u32 resId, u32 id) : Cursor(scene, id) { // ord 10 0x10008441
    if (hInstance != nullptr || resId != 0) handle_ = (hInstance ? 0x10000u : 0u) | (resId & 0xffff);
}

DibCursor::DibCursor(Scene* view, u32 resId, int id, int hotX, int hotY) // ord 8 0x100084bc
    : Cursor(view, u32(id)), dib_(view, 0x7fffffff), resId_(resId) {
    view->LoadResource(resId);
    dib_.SetResId(resId);
    dib_.SetPickMode(1);
    if (id != 0) { // QUIRK: the hot spot is only taken when the cursor has an id
        hotX_ = hotX;
        hotY_ = hotY;
    }
}

DibCursor::~DibCursor() { // ord 65 0x1000859b: FreeResource first, then the sprite goes
    if (scene_) scene_->FreeResource(resId_);
}

int DibCursor::Activate(bool on) { // ord 133 0x1000865e
    if (!on) dib_.Hide();
    else dib_.Animate(resId_, nullptr);
    return int(id_);
}

uintptr_t DibCursor::Handle(int x, int y, int number) { // ord 597 0x1000862d
    (void)number;
    dib_.SetPosition(x - hotX_, y - hotY_, 0);
    return 0;
}

AnimCursor::AnimCursor(Scene* view, u32 resId, moses::QUEUEMSG** msgs, u32 count, int id, int hotX, int hotY) // ord 7 0x100086c4
    : Cursor(view, u32(id)), view_(view), anim_(view, 0x7fffffff), resId_(resId), msgs_(msgs), count_(count) {
    view->LoadResource(resId);
    anim_.SetResId(resId);
    anim_.SetPickMode(1);
    if (id != 0) {
        hotX_ = hotX;
        hotY_ = hotY;
    }
}

AnimCursor::~AnimCursor() { // ord 64 0x100087e0
    if (scene_) scene_->FreeResource(resId_);
}

int AnimCursor::Activate(bool on) { // ord 132 0x100088e8
    if (!on) {
        view_->QStop(&anim_);
        anim_.Hide();
    } else {
        view_->QStart(&anim_, msgs_, count_);
    }
    return int(id_);
}

uintptr_t AnimCursor::Handle(int x, int y, int number) { // ord 596 0x100088ae
    (void)number;
    anim_.SetPosition(x - hotX_, y + hotY_, 0); // sic: + hotY
    return 0;
}

CursorPair::CursorPair(Scene* scene, Cursor* a, Cursor* b) : Cursor(scene, 0) { // ord 9 0x1000896c
    c_[0] = a;
    c_[1] = b ? b : a;
}

CursorPair::~CursorPair() {} // ord 66 0x100089cb (the member cursors are not deleted)

int CursorPair::Activate(bool on) { // ord 134 0x10008a83
    return c_[sel_] ? c_[sel_]->Activate(on) : 0;
}

uintptr_t CursorPair::Handle(int x, int y, int number) { // ord 598 0x100089e7
    int sel = number != 0 ? 1 : 0;
    uintptr_t r = c_[sel] ? c_[sel]->Handle(x, y, number) : 0;
    if (sel != sel_) {
        if (c_[sel_]) c_[sel_]->Activate(false);
        sel_ = sel;
        if (c_[sel_]) c_[sel_]->Activate(true);
    }
    return r;
}

} // namespace aaron
