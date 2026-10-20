// G1 - event / sequence system.  See Events.h.  Shoot.dll addresses in comments.
#include "arcade/Events.h"

#include "aaron/Scene.h"
#include "aaron/Sprites.h"
#include "arcade/GameBase.h"

#include <algorithm>
#include <cstring>

namespace rapture {

using moses::QUEUEMSG;


// ---------------------------------------------------------------------------------------------
// CQueueMsgArray
CQueueMsgArray::CQueueMsgArray(int capacity, QUEUEMSG* const* src, u32 count) { // 0x100134b0
    v_.reserve(size_t(capacity > 0 ? capacity : 0));
    if (src && count) v_.assign(src, src + count);
}

void CQueueMsgArray::SetParam100(intptr_t param) { // 0x100135f8: the delay of QM_ANIM (op 100)
    for (QUEUEMSG* m : v_)
        if (m->op == 100) m->a = param;
}

int CQueueMsgArray::Remove(u32 from, u32 to) { // 0x1001365a
    if (from >= v_.size()) return 0;
    if (to == 0xffffffffu) to = from;
    if (to < from) return 0;
    if (to >= v_.size()) to = u32(v_.size()) - 1; // (the original would copy a negative size)
    v_.erase(v_.begin() + long(from), v_.begin() + long(to) + 1);
    return 1;
}

int CQueueMsgArray::Insert(QUEUEMSG* m, u32 index) { // 0x100136e7
    u32 pos = index < v_.size() ? index : u32(v_.size());
    v_.insert(v_.begin() + long(pos), m);
    return int(pos);
}

int CQueueMsgArray::InsertN(QUEUEMSG* const* src, u32 n, u32 index) { // 0x100137da
    u32 pos = index < v_.size() ? index : u32(v_.size());
    v_.insert(v_.begin() + long(pos), src, src + n);
    return int(pos);
}

// ---------------------------------------------------------------------------------------------
// CEventList
CEventList::CEventList(int growBy, CEventList* root) : aaron::PtrArray(growBy) { // 0x1001bca0
    root_ = root ? root : this;
}

CEventList::~CEventList() { CEventList::RemoveAll(); } // 0x1001bd2b (non virtual call, as the original)

// 0x1001bdf3: cur = 0; every event: refcount-- and delete when < 1; count = 0.
void CEventList::RemoveAll() {
    cur_ = 0;
    int n = Count();
    for (int i = 0; i < n; ++i) {
        IEvent* e = static_cast<IEvent*>(At(i));
        if (e && --e->refcount < 1) delete e;
    }
    Clear();
}

// 0x1001bdb7
int CEventList::Add(IEvent* e) {
    e->refcount += 1;
    e->owner = this;
    return aaron::PtrArray::Insert(e, -1);
}

// 0x1001c086
int CEventList::Insert(IEvent* e, int index) {
    e->refcount += 1;
    e->owner = this;
    return aaron::PtrArray::Insert(e, index);
}

// 0x1001c0c4
bool CEventList::RemoveAt(int index) {
    IEvent* e = GetAt(index);
    if (e && --e->refcount < 1) delete e;
    return aaron::PtrArray::RemoveAt(index) != nullptr;
}

// 0x1001c137.  QUIRK: the refcount is decremented (and the event possibly deleted) even when `e` is not in the list.
bool CEventList::Remove(IEvent* e) {
    int idx = IndexOf(e);
    e->refcount -= 1;
    if (e->refcount < 1) delete e;
    if (idx == -1) return false;
    return aaron::PtrArray::RemoveAt(idx) != nullptr;
}

// 0x1001be90
int CEventList::Prev(int wrap) {
    if (cur_ == 0) {
        if (wrap == 0) return -1;
        cur_ = Count() - 1;
    } else {
        cur_ -= 1;
    }
    return cur_;
}

// 0x1001bee1
IEvent* CEventList::GetAt(int index) {
    if (index >= 0 && index < Count()) return static_cast<IEvent*>(At(index));
    return nullptr;
}

// 0x1001bf22
int CEventList::IndexOf(const IEvent* e) {
    for (int i = 0; i < Count(); ++i)
        if (At(i) == e) return i;
    return -1;
}

// 0x1001c032
int CEventList::NextIndex(int wrap) {
    if (cur_ == Count() - 1) {
        if (wrap == 0) return -1;
        cur_ = 0;
    } else {
        cur_ += 1;
    }
    return cur_;
}

// 0x1001bf75
IEvent* CEventList::GetNextEvent(int wrap) {
    int i = NextIndex(wrap);
    return i == -1 ? nullptr : static_cast<IEvent*>(At(cur_));
}

// 0x1001bfee
IEvent* CEventList::GetPrevEvent(int wrap) {
    int i = Prev(wrap);
    return i == -1 ? nullptr : static_cast<IEvent*>(At(cur_));
}

// 0x1001bfb9: the list's owner (IEvent::owner = +0x1c) is its parent; without parent the stored root is returned.
CEventList* CEventList::GetRoot() { return owner ? owner->GetRoot() : root_; }

// 0x1001c1b9
int CEventList::SetIndex(int index) {
    if (index >= 0 && index < Count()) {
        int old = cur_;
        cur_ = index;
        return old;
    }
    return -1;
}

// 0x1001c1f3
int CEventList::StartAt(int index) {
    IEvent* e = GetAt(index);
    if (!e) return 0;
    SetIndex(index);
    return e->Start() ? 1 : 0;
}

// 0x1001c232 (returns the index or -1)
int CEventList::StartNext(int wrap) {
    int i = NextIndex(wrap);
    if (i != -1) StartAt(i);
    return i;
}

// 0x1001c265
int CEventList::StartPrev(int wrap) {
    int i = Prev(wrap);
    if (i != -1) StartAt(i);
    return i;
}

// ---------------------------------------------------------------------------------------------
// CEventGroupList
CEventGroupList::CEventGroupList(int growBy, CEventList* root) : CEventList(growBy, root) {} // 0x100177e0

// 0x10017855
void CEventGroupList::OnChildDone() {
    done_ += 1;
    if (done_ == Count() && owner) owner->OnChildDone();
}

// 0x1001789f
bool CEventGroupList::Start() {
    int n = Count();
    done_ = 0;
    if (n == 0) {
        if (owner) owner->OnChildDone();
    } else {
        for (int i = 0; i < n; ++i) StartAt(i);
    }
    return true;
}

// 0x10017911
bool CEventGroupList::End() {
    int n = Count();
    for (int i = 0; i < n; ++i) {
        IEvent* e = static_cast<IEvent*>(At(i));
        if (e) e->End();
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// CEventSequencer
CEventSequencer::CEventSequencer(int growBy, int mode, CEventList* root) : CEventList(growBy, root) { // 0x100179d0
    mode_ = mode;
}

void CEventSequencer::RemoveAll() { if (!playing_) CEventList::RemoveAll(); }                          // 0x10017aba
bool CEventSequencer::RemoveAt(int index) { return playing_ ? false : CEventList::RemoveAt(index); }   // 0x10017c28
int CEventSequencer::Add(IEvent* e) { return playing_ ? -1 : CEventList::Add(e); }                      // 0x10017a87
int CEventSequencer::Insert(IEvent* e, int index) { return playing_ ? -1 : CEventList::Insert(e, index); } // 0x10017b94
bool CEventSequencer::Remove(IEvent* e) { return playing_ ? false : CEventList::Remove(e); }            // 0x10017c5b
void CEventSequencer::ResetIndex() { if (!playing_) cur_ = 0; }                                         // 0x10017c8e

// 0x10017a2c: stop a running sequence: end the current event, then EndSequence(notify).
bool CEventSequencer::Stop(int notify) {
    if (!playing_ || ending_) return false;
    ending_ = 1;
    if (curEvent_) curEvent_->End();
    EndSequence(notify);
    return true;
}

// 0x10017ad6 ("CEventSequencer::End")
void CEventSequencer::EndSequence(int notify) {
    if (!playing_) return;
    playing_ = 0;
    if (mode_ == 1) RemoveAll();
    OnEnd();
    if (cb_ && notify) cb_(this, 2, userData1_, userData2_);
    if (!owner) {
        if (mode_ == 2) delete this;
    } else {
        owner->OnChildDone();
    }
}

// 0x10017cac.  `busy_`/`pendingAdvance_` make a synchronous completion of the event being started (OnChildDone called
// from inside Start) loop instead of recurse.  QUIRK: if the current event's End() returns false the sequence stalls.
IEvent* CEventSequencer::Advance(int wrap) {
    bool proceed = true;
    if (ending_) return nullptr;
    if (busy_) {
        pendingAdvance_ = 1;
        return nullptr;
    }
    if (curEvent_) proceed = curEvent_->End();
    if (proceed) {
        busy_ = 1;
        int idx = StartNext(wrap);
        curEvent_ = idx == -1 ? nullptr : GetAt(idx);
        busy_ = 0;
        if (!curEvent_) {
            EndSequence(1);
        } else if (pendingAdvance_) {
            pendingAdvance_ = 0;
            Advance(wrap);
        }
    }
    return curEvent_;
}

// 0x10017da1 ("CEventSequencer::Start"). QUIRK: `ending_` is cleared at the very end, after the first Start; a Stop()
// issued from inside the first event's Start() therefore does not stop the sequence.
void CEventSequencer::StartSequence(int index, Callback cb, uintptr_t ud1, uintptr_t ud2) {
    if (playing_) return; // original: DebugMsg("CEventSequencer::Start called while playing!") removed
    cb_ = cb;
    userData1_ = ud1;
    userData2_ = ud2;
    playing_ = 1;
    OnStart();
    if (cb_) cb_(this, 1, userData1_, userData2_);
    curEvent_ = GetAt(index);
    if (!StartAt(index)) EndSequence(1);
    ending_ = 0;
}

// ---------------------------------------------------------------------------------------------
// CEventSpriteSequencer
CEventSpriteSequencer::CEventSpriteSequencer(CGameBase* game, int growBy, int mode, CEventList* root)
    : CEventSequencer(growBy, mode, root), game_(game) {} // 0x10019f50

CEventSpriteSequencer::~CEventSpriteSequencer() { // 0x10019fac (the original leaks the 0x20-byte records)
    for (SpriteRec* r : sprites_) delete r;
    sprites_.clear();
}

bool CEventSpriteSequencer::Init() { return true; } // 0x1001a2fa creates the PtrArray: the vector always exists

// 0x1001a2a6
CEventSpriteSequencer::SpriteRec* CEventSpriteSequencer::FindSprite(int id) {
    for (SpriteRec* r : sprites_)
        if (r->id == id) return r;
    return nullptr;
}

// 0x1001a065
CEventSpriteSequencer::SpriteRec* CEventSpriteSequencer::AddSprite(int id, u32 resId, QUEUEMSG** msgs, u32 nMsgs, int x, int y,
                                                                   int level, aaron::AnimSprite* existing, int play,
                                                                   int forceNew) {
    bool isNew = false;
    SpriteRec* rec = FindSprite(id);
    if (!rec) {
        isNew = true;
        rec = new SpriteRec();
        forceNew = 1;
    }
    if (existing == nullptr && forceNew != 0) {
        aaron::AnimSprite* s;
        if (play == 0) s = new aaron::AnimSprite(aaron::WidgetParent(static_cast<aaron::Scene*>(game_)), resId, x, y, level, 0, 1); // hidden
        else s = new aaron::AnimSprite(aaron::WidgetParent(static_cast<aaron::Scene*>(game_)), resId, x, y, level, 0, 0, 0u);      // frame 0
        existing = static_cast<aaron::AnimSprite*>(aaron::Widget::InitOrDelete(s));
        // QUIRK: on failure the (possibly new, unlinked) record is returned as is: a brand new record is leaked.
        if (!existing) return rec;
    }
    if (forceNew == 0) {
        if (rec->widget == nullptr) rec->widget = existing;
    } else {
        rec->id = id;
        rec->resId = resId;
        rec->msgs = msgs;
        rec->nMsgs = nMsgs;
        rec->level = level;
        rec->widget = existing;
    }
    if (isNew) sprites_.push_back(rec);
    if (play != 0 && rec->resId != SpriteRec::kNone && rec->msgs != nullptr) rec->widget->Play(rec->resId, rec->msgs, rec->nMsgs, 1);
    return rec;
}

// 0x1001a393.  After a sequence: sprites without rest animation are destroyed, the others go back to the rest pose.
void CEventSpriteSequencer::OnEnd() {
    for (size_t i = 0; i < sprites_.size(); ++i) {
        SpriteRec* r = sprites_[i];
        if (!r) break;
        if (r->widget == nullptr || r->resId == SpriteRec::kNone || r->msgs == nullptr) {
            if (r->widget) {
                moses::CAnimObj* a = static_cast<moses::CAnimDib*>(r->widget);
                a->GetAnimView()->QStop(a);
                r->widget->Show(false);
                delete r->widget;
                r->widget = nullptr;
            }
        } else {
            static_cast<moses::CDrawObj*>(static_cast<moses::CDrawDib*>(r->widget))->SetLevel(r->level);
            r->widget->Play(r->resId, r->msgs, r->nMsgs, 1);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// CEventUserSprite
CEventUserSprite::CEventUserSprite(int spriteId, u32 animResId, QUEUEMSG** msgs, u32 nMsgs, int level, u32 flags,
                                   u32 soundId, int soundActive)
    : msgs_(msgs), soundActive_(soundActive), spriteId_(spriteId), level_(level), flags_(flags), nMsgs_(nMsgs),
      animResId_(animResId), soundId_(soundId) {} // 0x10017246

CEventUserSprite::~CEventUserSprite() { delete queue_; } // 0x100172de

// 0x10017360: copy the messages (+1 slot) and, if WAIT_END, append {5, OnAnimDone, this, 0}.
bool CEventUserSprite::Init() {
    queue_ = new CQueueMsgArray(int(nMsgs_) + 1, msgs_, nMsgs_);
    if (flags_ & WAIT_END) {
        doneMsg_ = moses::qm::Call(&CEventUserSprite::OnAnimDone, reinterpret_cast<uintptr_t>(this), 0);
        queue_->Insert(&doneMsg_);
    }
    return queue_ != nullptr;
}

// 0x1001745e
bool CEventUserSprite::Start() {
    CEventList* o = owner;
    if (!o) return false;
    CEventSpriteSequencer* root = static_cast<CEventSpriteSequencer*>(o->GetRoot());
    if (!root) return false;
    CEventSpriteSequencer::SpriteRec* rec = root->FindSprite(spriteId_);
    if (rec == nullptr || rec->widget == nullptr) {
        if (animResId_ == 0xffffffffu) return false;
        rec = root->AddSprite(spriteId_, 0xffffffffu, nullptr, 0, 0, 0, level_, nullptr, 0, 1);
        if (rec == nullptr || rec->widget == nullptr) return false;
        rec->widget->Play(animResId_, queue_->Data(), queue_->Count(), 1);
    } else {
        static_cast<moses::CDrawObj*>(static_cast<moses::CDrawDib*>(rec->widget))->SetLevel(level_);
        if (animResId_ == 0xffffffffu) {
            if (rec->resId == 0xffffffffu || rec->msgs == nullptr) return false;
            rec->widget->Play(rec->resId, rec->msgs, rec->nMsgs, 1);
        } else {
            rec->widget->Play(animResId_, queue_->Data(), queue_->Count(), 1);
        }
    }
    if (soundId_ != 0xffffffffu) {
        aaron::Scene* sc = root->Game();
        sc->LoadResource(soundId_);
        if (soundActive_ != 0) sc->PlaySound(soundId_, 1, 0, 100, 0, 0, 0);
    }
    if ((flags_ & WAIT_END) == 0) o->OnChildDone();
    return true;
}

// 0x1001764d
bool CEventUserSprite::End() {
    if (soundId_ != 0xffffffffu && owner) {
        CEventSpriteSequencer* root = static_cast<CEventSpriteSequencer*>(owner->GetRoot());
        if (root) {
            aaron::Scene* sc = root->Game();
            sc->StopSound(soundId_);
            sc->FreeResource(soundId_);
        }
    }
    return true;
}

// 0x10017160: animation finished.  RETURN_TO_REST: put the sprite back to its rest pose (or hide it); then tell the owner.
void CEventUserSprite::OnAnimDone(moses::CAnimObj*, uintptr_t evp, uintptr_t) {
    CEventUserSprite* ev = reinterpret_cast<CEventUserSprite*>(evp);
    if (!ev || !ev->owner) return;
    CEventList* o = ev->owner;
    if (ev->flags_ & RETURN_TO_REST) {
        CEventSpriteSequencer* root = static_cast<CEventSpriteSequencer*>(o->GetRoot());
        if (root) {
            CEventSpriteSequencer::SpriteRec* rec = root->FindSprite(ev->spriteId_);
            if (rec && rec->widget) {
                static_cast<moses::CDrawObj*>(static_cast<moses::CDrawDib*>(rec->widget))->SetLevel(rec->level);
                if (rec->resId == 0xffffffffu || rec->nMsgs == 0) rec->widget->Show(false);
                else rec->widget->Play(rec->resId, rec->msgs, rec->nMsgs, 1);
            }
        }
    }
    o->OnChildDone();
}

// ---------------------------------------------------------------------------------------------
// CEventUserFunc
CEventUserFunc::CEventUserFunc(uintptr_t p1, uintptr_t p2, Fn fn, QUEUEMSG** msgs, u32 nMsgs)
    : msgs_(msgs), p1_(p1), nMsgs_(nMsgs), p2_(p2), fn_(fn) { // 0x10017f06
    doneMsg_ = moses::qm::Call(&CEventUserFunc::OnDone, reinterpret_cast<uintptr_t>(this), 0);
}

CEventUserFunc::~CEventUserFunc() { delete queue_; } // 0x10017faa

// 0x1001802c: with a message table: copy it and append the done message.  Returns fn != 0.
bool CEventUserFunc::Init() {
    if (msgs_ != nullptr && nMsgs_ != 0) {
        queue_ = new CQueueMsgArray(int(nMsgs_) + 1, msgs_, nMsgs_);
        queue_->Insert(&doneMsg_);
    }
    return fn_ != nullptr;
}

// 0x10018110: result = fn(this, p1, p2, 1, queue).  The function must arrange the completion (OnDone) itself.
bool CEventUserFunc::Start() {
    result_ = fn_(this, p1_, p2_, 1, queue_);
    return true;
}

// 0x10018147: phase 2, or 3 when the root sequencer is being stopped.
bool CEventUserFunc::End() {
    if (!owner) return false;
    CEventSequencer* root = static_cast<CEventSequencer*>(owner->GetRoot());
    if (!root) return false;
    fn_(this, p1_, p2_, root->IsEnding() ? 3 : 2, result_);
    return true;
}

// 0x10017ed0 / 0x100181f0 (same code, QM_CALL callback)
void CEventUserFunc::OnDone(moses::CAnimObj*, uintptr_t evp, uintptr_t) {
    IEvent* ev = reinterpret_cast<IEvent*>(evp);
    if (ev && ev->owner) ev->owner->OnChildDone();
}

// ---------------------------------------------------------------------------------------------
// CEventSoundTimer
CEventSoundTimer::CEventSoundTimer(aaron::Scene* scene, u32 soundId) : scene_(scene), soundId_(soundId) { // 0x10019cf0
    listener_.ev = this;
}

CEventSoundTimer::~CEventSoundTimer() = default; // 0x10019ed0

bool CEventSoundTimer::Init() { return scene_ != nullptr && soundId_ != 0xffffffffu; } // 0x10019d75

void CEventSoundTimer::OnSoundEnd() { // 0x10019db0
    if (owner) owner->OnChildDone();
}

// 0x10019dd9
bool CEventSoundTimer::Start() {
    scene_->LoadResource(soundId_);
    int ok = scene_->PlaySoundSimple(soundId_, 0, &listener_);
    if (ok == 0 && owner) owner->OnChildDone();
    return true;
}

// 0x10019e52
bool CEventSoundTimer::End() {
    scene_->StopSound(soundId_);
    scene_->FreeResource(soundId_);
    return true;
}

// ---------------------------------------------------------------------------------------------
// CEventRandomSelect
CEventRandomSelect::CEventRandomSelect(int growBy, CEventList* root) : CEventList(growBy, root) {} // 0x10013e50

CEventRandomSelect::~CEventRandomSelect() { CEventRandomSelect::RemoveAll(); } // 0x10013ea5

bool CEventRandomSelect::Init() { return true; } // 0x10014128 (creates the flag array)

// 0x10013f2f.  QUIRK: `last_` is set to the new count (an index that cannot match) after every Add.
int CEventRandomSelect::Add(IEvent* e) {
    int r = CEventList::Add(e);
    if (r != -1) used_.push_back(0);
    last_ = Count();
    return r;
}

// 0x100141c1
int CEventRandomSelect::Insert(IEvent* e, int index) {
    int r = CEventList::Insert(e, index);
    if (r != -1) {
        size_t pos = index < 0 || size_t(index) > used_.size() ? used_.size() : size_t(index);
        used_.insert(used_.begin() + long(pos), 0);
    }
    return r;
}

// 0x10013fa1
void CEventRandomSelect::RemoveAll() {
    CEventList::RemoveAll();
    used_.clear();
}

// 0x1001429f
bool CEventRandomSelect::RemoveAt(int index) {
    if (!CEventList::RemoveAt(index)) return false;
    if (index >= 0 && size_t(index) < used_.size()) used_.erase(used_.begin() + index);
    return true;
}

// 0x100142e2
bool CEventRandomSelect::Remove(IEvent* e) {
    int idx = IndexOf(e);
    if (!CEventList::Remove(e)) return false;
    if (idx >= 0 && size_t(idx) < used_.size()) used_.erase(used_.begin() + idx);
    return true;
}

// 0x10014334 (vtbl [9]): forget the used flags and the current event
void CEventRandomSelect::ResetIndex() {
    cur_ = 0;
    usedCount_ = 0;
    for (int& f : used_) f = 0;
    current_ = nullptr;
}

// 0x10014227
void CEventRandomSelect::OnChildDone() {
    if (owner) owner->OnChildDone();
}

// 0x1001426b
bool CEventRandomSelect::End() {
    if (current_) {
        current_->End();
        current_ = nullptr;
    }
    return true;
}

// 0x10014039.  allowRepeat == 0: take a random free slot, skipping the used ones and the last picked one (when every
// slot was used the flags are reset first).
int CEventRandomSelect::PickRandomFree(int allowRepeat) {
    int n = Count();
    int r = n < 2 ? 0 : aaron::Random(0, n - 1);
    if (n == 0) return 0; // (original would write through a null flag; keep it safe)
    if (used_.size() < size_t(n)) used_.resize(size_t(n), 0); // port: Init() after Add() must not break the flags
    if (allowRepeat == 0) {
        if (n <= usedCount_) ResetIndex();
        int k = 0;
        while (k < n && (used_[size_t(r)] != 0 || r == last_)) {
            r += 1;
            if (n <= r) r = 0;
            k += 1;
        }
    }
    used_[size_t(r)] = 1;
    usedCount_ += 1;
    last_ = r;
    return r;
}

// 0x10014017
IEvent* CEventRandomSelect::GetRandomEvent(int allowRepeat) { return GetAt(PickRandomFree(allowRepeat)); }

// 0x1001439c
int CEventRandomSelect::PickAndStart(int allowRepeat) {
    int idx = PickRandomFree(allowRepeat);
    current_ = GetAt(idx);
    StartAt(idx);
    return idx;
}

// ---------------------------------------------------------------------------------------------
// CEventRandomList
CEventRandomList::CEventRandomList(int growBy) { // 0x10013930
    avail_ = new aaron::PtrArray(growBy);
    used_ = new aaron::PtrArray(growBy);
}

CEventRandomList::~CEventRandomList() { // 0x10013a0b: both arrays are emptied (items are borrowed) and deleted
    used_->Clear();
    avail_->Clear();
    delete used_;
    delete avail_;
}

bool CEventRandomList::Add(void* item) { return avail_ ? avail_->Insert(item, -1) != -1 : false; } // 0x10013b05

// 0x10013b73
void* CEventRandomList::GetNext() {
    if (!avail_ || !used_ || avail_->Count() == 0) return nullptr;
    void* item = avail_->At(0);
    if (!item) return nullptr;
    if (avail_->RemoveAt(0) != nullptr) used_->Insert(item, -1);
    return item;
}

// 0x10013c33
void CEventRandomList::Return(void* item) {
    if (avail_ && used_ && used_->Remove(item) != -1) avail_->Insert(item, -1);
}

// 0x10013c7a: moves every used item back (the count is read once: ReleaseAt(0) shrinks `used`)
void CEventRandomList::ResetAll() {
    u32 n = u32(used_->Count());
    for (u32 i = 0; i < n; ++i) ReleaseAt(0);
}

// 0x10013cbc
void CEventRandomList::ReleaseAt(int usedIndex) {
    if (usedIndex < 0 || usedIndex >= used_->Count()) return;
    void* item = used_->At(usedIndex);
    if (item && used_->RemoveAt(usedIndex) != nullptr) avail_->Insert(item, -1);
}

void* CEventRandomList::GetUsedAt(u32 i) { return i < u32(used_->Count()) ? used_->At(int(i)) : nullptr; }   // 0x10013a83
void* CEventRandomList::GetAvailAt(u32 i) { return i < u32(avail_->Count()) ? avail_->At(int(i)) : nullptr; } // 0x10013ac4
int CEventRandomList::Total() const { return avail_->Count() + used_->Count(); }                             // 0x10013bec
int CEventRandomList::UsedCount() const { return used_->Count(); }                                           // 0x10013c0b
int CEventRandomList::AvailCount() const { return avail_->Count(); }                                         // 0x10013c1f

// 0x10013d10: `avail.count` times: take a random item and move it to the end.
void CEventRandomList::Shuffle() {
    if (avail_->Count() > 1) {
        for (u32 i = 0; i < u32(avail_->Count()); ++i) {
            int r = aaron::Random(0, avail_->Count() - 1);
            void* item = avail_->At(r);
            if (!item) return;
            avail_->Remove(item);
            avail_->Insert(item, -1);
        }
    }
}

// CHunRandomList::GetNext 0x10013e0f: nothing available -> recycle the OLDEST used item (ReleaseAt(0)) and retry.
void* CHunRandomList::GetNext() {
    void* r = CEventRandomList::GetNext();
    if (r == nullptr) {
        ReleaseAt(0);
        r = CEventRandomList::GetNext();
    }
    return r;
}

} // namespace rapture
