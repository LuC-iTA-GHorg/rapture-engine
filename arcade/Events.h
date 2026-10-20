// G1 - event / sequence system of the Arcade framework.
//
//   IEvent                  (vtbl 0x10024350/0x10024340: [0] dtor [1] Init [2] Start [3] End; +4 refcount, +8 owner list)
//   CEventList              Shoot.dll 0x1001bca0   (Shoot.exe CEventList 0x40f6d0)
//     CEventGroupList       0x100177e0  parallel execution
//     CEventSequencer       0x100179d0  serial execution          (Shoot.exe 0x40f250)
//       CEventSpriteSequencer 0x10019f50  "CEventManager": owns the user sprites   (Shoot.exe 0x40e7b0)
//     CEventRandomSelect    0x10013e50  start one random child (names_shoot_dll_b.tsv: CEventListDerived)
//   CEventUserSprite        0x10017246  "animate sprite id" (+ optional sound)
//   CEventUserFunc          0x10017f06  event driven by a callback
//   CEventSoundTimer        0x10019cf0  "play this sound and finish when it ends"
//   CEventRandomList        0x10013930  two lists (available / used) with random shuffle  (+ CHunRandomList 0x10013de0)
//   CQueueMsgArray          0x100134b0  CArray<QUEUEMSG*> (Shoot.exe CQueueMsgList 0x40edd0)
//
// OWNERSHIP (as the original): an event added to a list gets refcount+1 and owner = list; the list deletes it when
// the refcount drops below 1 (RemoveAll / RemoveAt / Remove / destructor).  Events that are never added to a list
// (the two CEventSpriteSequencer of CGameBase) are deleted explicitly by their holder.
//
// A completed event tells its owner with `owner->OnChildDone()` (vtbl +0x28).
#pragma once

#include "aaron/RObject.h"
#include "moses/QueueMsg.h"

#include <cstdint>
#include <vector>

namespace aaron {
class AnimSprite;
class Scene;
} // namespace aaron

namespace rapture {

using aaron::u32;
class CEventList;
class CGameBase;

// ---------------------------------------------------------------------------------------------
class IEvent {
public:
    virtual ~IEvent() = default;
    virtual bool Init() { return true; }   // slot 1 (default 0x10017740: returns 1)
    virtual bool Start() { return true; }  // slot 2
    virtual bool End() { return true; }    // slot 3

    int refcount = 0;                      // +4
    CEventList* owner = nullptr;           // +8
};

// ---------------------------------------------------------------------------------------------
// CQueueMsgArray: array of QUEUEMSG* (vtbl 0x1002411c; +4 count, +8 capacity, +0xc data).
class CQueueMsgArray {
public:
    CQueueMsgArray(int capacity, moses::QUEUEMSG* const* src, u32 count); // 0x100134b0
    CQueueMsgArray(const CQueueMsgArray& o) : v_(o.v_) {}                 // 0x1001353b
    void SetParam100(intptr_t param);                                     // 0x100135f8: msg->a = param for every msg with op == 100
    int Remove(u32 from, u32 to = 0xffffffffu);                           // 0x1001365a (1 = ok)
    int Insert(moses::QUEUEMSG* m, u32 index = 0xffffffffu);              // 0x100136e7 -> position, -1 on failure
    int InsertN(moses::QUEUEMSG* const* src, u32 n, u32 index);           // 0x100137da
    moses::QUEUEMSG** Data() { return v_.data(); }                        // +0xc
    u32 Count() const { return u32(v_.size()); }                          // +4
private:
    std::vector<moses::QUEUEMSG*> v_;
};

// ---------------------------------------------------------------------------------------------
class CEventList : public aaron::PtrArray, public IEvent {
public:
    // 0x1001bca0: (growBy, root).  root == nullptr -> this is its own root.
    explicit CEventList(int growBy = 1, CEventList* root = nullptr);
    ~CEventList() override;                                   // 0x1001bd2b -> RemoveAll()

    // ---- vtbl 0x100244b0 -------------------------------------------------------------------------
    virtual void RemoveAll();                                 // [2] 0x1001bdf3
    virtual bool RemoveAt(int index);                         // [4] 0x1001c0c4 (true = something removed)
    virtual int Add(IEvent* e);                               // [6] 0x1001bdb7
    virtual int Insert(IEvent* e, int index);                 // [7] 0x1001c086
    virtual bool Remove(IEvent* e);                           // [8] 0x1001c137
    virtual void ResetIndex() { cur_ = 0; }                   // [9] 0x100179b0
    virtual void OnChildDone() {}                             // [10] nop (0x10016e00)

    int Prev(int wrap);                                       // 0x1001be90
    IEvent* GetAt(int index);                                 // 0x1001bee1
    int IndexOf(const IEvent* e);                             // 0x1001bf22 (-1 if absent)
    IEvent* GetNextEvent(int wrap);                           // 0x1001bf75
    IEvent* GetPrevEvent(int wrap);                           // 0x1001bfee
    CEventList* GetRoot();                                    // 0x1001bfb9: climbs the owner chain
    int NextIndex(int wrap);                                  // 0x1001c032
    int SetIndex(int index);                                  // 0x1001c1b9 (returns the previous index, -1 if out of range)
    int StartAt(int index);                                   // 0x1001c1f3: SetIndex + item->Start()
    int StartNext(int wrap);                                  // 0x1001c232
    int StartPrev(int wrap);                                  // 0x1001c265

    int Size() const { return Count(); }
    int CurrentIndex() const { return cur_; }                 // +0x20

protected:
    int cur_ = 0;                  // +0x20
    CEventList* root_ = nullptr;   // +0x24
};

// ---------------------------------------------------------------------------------------------
// CEventGroupList: all children run in parallel; done when every child called OnChildDone.
class CEventGroupList : public CEventList {
public:
    explicit CEventGroupList(int growBy = 1, CEventList* root = nullptr); // 0x100177e0
    void OnChildDone() override;                              // [10] 0x10017855 (OnObjectEnd)
    bool Start() override;                                    // iface [2] 0x1001789f
    bool End() override;                                      // iface [3] 0x10017911
private:
    int done_ = 0;                 // +0x2c
};

// ---------------------------------------------------------------------------------------------
// CEventSequencer: children run one after the other.
class CEventSequencer : public CEventList {
public:
    using Callback = void (*)(CEventSequencer* seq, int code /*1 start, 2 end*/, uintptr_t ud1, uintptr_t ud2); // +0x48
    CEventSequencer(int growBy, int mode, CEventList* root);  // 0x100179d0. mode 1: clear the list at the end; 2: delete itself at the end (top level only)

    // list modification is refused while playing (vtbl [2][4][6][7][8][9] = 0x10017aba/c28/a87/b94/c5b/c8e)
    void RemoveAll() override;
    bool RemoveAt(int index) override;
    int Add(IEvent* e) override;
    int Insert(IEvent* e, int index) override;
    bool Remove(IEvent* e) override;
    void ResetIndex() override;

    void OnChildDone() override { Advance(0); }              // [10] 0x10017bcb (Next)
    bool Stop(int notify);                                    // [11] 0x10017a2c
    void StartSequence(int index, Callback cb, uintptr_t ud1, uintptr_t ud2); // [12] 0x10017da1 ("CEventSequencer::Start")
    void EndSequence(int notify);                             // [13] 0x10017ad6 ("CEventSequencer::End")
    virtual void OnStart() {}                                 // [14] nop
    virtual void OnEnd() {}                                   // [15] nop (CEventSpriteSequencer overrides)

    // as an event inside another sequence (iface vtbl 0x10024390)
    bool Start() override { StartSequence(0, nullptr, 0, 0); return true; } // 0x10017be0
    bool End() override { Stop(1); return true; }             // 0x10017c07

    IEvent* Advance(int wrap);                                // 0x10017cac (returns the current event or null)
    bool IsPlaying() const { return playing_ != 0; }          // +0x34
    bool IsEnding() const { return ending_ != 0; }            // +0x38
    IEvent* CurrentEvent() const { return curEvent_; }        // +0x30
    int Mode() const { return mode_; }

protected:
    uintptr_t userData2_ = 0;      // +0x28
    uintptr_t userData1_ = 0;      // +0x2c
    IEvent* curEvent_ = nullptr;   // +0x30
    int playing_ = 0;              // +0x34
    int ending_ = 0;               // +0x38
    int busy_ = 0;                 // +0x3c  re-entrancy guard of Advance
    int pendingAdvance_ = 0;       // +0x40
    int mode_ = 0;                 // +0x44
    Callback cb_ = nullptr;        // +0x48
};

// ---------------------------------------------------------------------------------------------
// CEventSpriteSequencer ("CEventManager"): a sequencer that also owns the sprites the events animate.
class CEventSpriteSequencer : public CEventSequencer {
public:
    struct SpriteRec {             // 0x20 bytes at 0x1001a065
        int id = 0;                // +0
        int level = 0;             // +0xc  z level of the rest pose
        u32 resId = kNone;         // +0x10 resource of the "rest" animation (-1 = no rest animation)
        moses::QUEUEMSG** msgs = nullptr; // +0x14 queue of the rest animation
        u32 nMsgs = 0;             // +0x18
        aaron::AnimSprite* widget = nullptr; // +0x1c the sprite (owned by the scene's widget tree)
        static constexpr u32 kNone = 0xffffffffu;
    };

    // 0x10019f50 (game, growBy, mode, root)
    CEventSpriteSequencer(CGameBase* game, int growBy, int mode, CEventList* root);
    ~CEventSpriteSequencer() override;                        // 0x10019fac
    bool Init() override;                                     // iface [1] 0x1001a2fa: creates the sprite list
    void OnEnd() override;                                    // [15] 0x1001a393: restore rest poses / drop temporary sprites

    // 0x1001a065. `existing` = an already created sprite to register; `play`: create with Animate(frame 0) instead of
    // hidden and start the rest animation; `forceNew`: overwrite the record even if it existed.
    SpriteRec* AddSprite(int id, u32 resId, moses::QUEUEMSG** msgs, u32 nMsgs, int x, int y, int level,
                         aaron::AnimSprite* existing, int play, int forceNew);
    SpriteRec* FindSprite(int id);                            // 0x1001a2a6
    CGameBase* Game() const { return game_; }                 // +0x4c
    int SpriteCount() const { return int(sprites_.size()); }

private:
    CGameBase* game_ = nullptr;                               // +0x4c
    std::vector<SpriteRec*> sprites_;                         // +0x50 (PtrArray of 0x20-byte records)
};

// ---------------------------------------------------------------------------------------------
// CEventUserSprite: animate the sprite `spriteId` with a queue; optionally play a sound.
class CEventUserSprite : public IEvent {
public:
    enum Flags : u32 { WAIT_END = 1, RETURN_TO_REST = 2 };    // +0x1c
    // 0x10017246 (spriteId, animResId (-1 = rest anim), msgs, nMsgs, level, flags, soundId (-1 none), soundActive)
    CEventUserSprite(int spriteId, u32 animResId, moses::QUEUEMSG** msgs, u32 nMsgs, int level, u32 flags,
                     u32 soundId, int soundActive);
    ~CEventUserSprite() override;                             // 0x100172de
    bool Init() override;                                     // 0x10017360
    bool Start() override;                                    // 0x1001745e
    bool End() override;                                      // 0x1001764d
    // QM_CALL callback {5, OnAnimDone, this, 0} (0x10017160)
    static void OnAnimDone(moses::CAnimObj* obj, uintptr_t ev, uintptr_t unused);

private:
    moses::QUEUEMSG** msgs_ = nullptr;   // +0x0c
    int soundActive_ = 0;                // +0x10
    int spriteId_ = 0;                   // +0x14
    int level_ = 0;                      // +0x18
    u32 flags_ = 0;                      // +0x1c
    u32 nMsgs_ = 0;                      // +0x20
    u32 animResId_ = 0;                  // +0x24
    u32 soundId_ = 0xffffffffu;          // +0x28
    CQueueMsgArray* queue_ = nullptr;    // +0x2c
    moses::QUEUEMSG doneMsg_;            // +0x30..+0x3c
};

// ---------------------------------------------------------------------------------------------
class CEventUserFunc : public IEvent {
public:
    // phase: 1 = Start, 2 = End, 3 = End while the root sequencer is ending (Stop/skip).  `data`: phase 1 -> the
    // CQueueMsgArray (queue_ + its done message), phase 2/3 -> the value returned by the phase 1 call.
    using Fn = void* (*)(CEventUserFunc* ev, uintptr_t p1, uintptr_t p2, int phase, void* data);
    // 0x10017f06 (p1, p2, fn, msgs, nMsgs)
    CEventUserFunc(uintptr_t p1, uintptr_t p2, Fn fn, moses::QUEUEMSG** msgs, u32 nMsgs);
    ~CEventUserFunc() override;                               // 0x10017faa
    bool Init() override;                                     // 0x1001802c
    bool Start() override;                                    // 0x10018110
    bool End() override;                                      // 0x10018147
    static void OnDone(moses::CAnimObj* obj, uintptr_t ev, uintptr_t unused); // 0x10017ed0 / 0x100181f0: owner->OnChildDone()
    CQueueMsgArray* Queue() const { return queue_; }

private:
    void* result_ = nullptr;             // +0x0c
    moses::QUEUEMSG** msgs_ = nullptr;   // +0x10
    uintptr_t p1_ = 0;                   // +0x14
    u32 nMsgs_ = 0;                      // +0x18
    uintptr_t p2_ = 0;                   // +0x1c
    CQueueMsgArray* queue_ = nullptr;    // +0x20
    moses::QUEUEMSG doneMsg_;            // +0x24
    Fn fn_ = nullptr;                    // +0x34
};

// ---------------------------------------------------------------------------------------------
// CEventSoundTimer (name hypothetical): starts a sound and completes when it ends (Scene::PlaySoundSimple + listener).
class CEventSoundTimer : public IEvent {
public:
    CEventSoundTimer(aaron::Scene* scene, u32 soundId);       // 0x10019cf0
    ~CEventSoundTimer() override;                             // 0x10019ed0
    bool Init() override;                                     // 0x10019d75: scene != 0 && sound != -1
    bool Start() override;                                    // 0x10019dd9
    bool End() override;                                      // 0x10019e52
    void OnSoundEnd();                                        // listener slot 1 0x10019db0

private:
    struct Listener : aaron::SceneListener {                  // sub-object at +0xc (vtbl 0x100243f0)
        CEventSoundTimer* ev = nullptr;
        void OnEnd() override { if (ev) ev->OnSoundEnd(); }
    } listener_;
    aaron::Scene* scene_ = nullptr;      // +0x14
    u32 soundId_ = 0xffffffffu;          // +0x18
};

// ---------------------------------------------------------------------------------------------
// CEventRandomSelect (0x10013e50 / iface 0x10024138): a list whose Start picks ONE child at random, avoiding the
// last picked and (until all were used) the already used ones.
class CEventRandomSelect : public CEventList {
public:
    explicit CEventRandomSelect(int growBy = 1, CEventList* root = nullptr); // 0x10013e50
    ~CEventRandomSelect() override;                           // 0x10013ea5
    void RemoveAll() override;                                // 0x10013fa1
    int Add(IEvent* e) override;                              // 0x10013f2f
    int Insert(IEvent* e, int index) override;                // 0x100141c1
    bool RemoveAt(int index) override;                        // 0x1001429f
    bool Remove(IEvent* e) override;                          // 0x100142e2
    void ResetIndex() override;                               // 0x10014334: forget which were used
    void OnChildDone() override;                              // 0x10014227: forwards to the owner
    bool Init() override;                                     // iface [1] 0x10014128
    bool Start() override { PickAndStart(0); return true; }   // 0x1001424e
    bool End() override;                                      // 0x1001426b
    int PickRandomFree(int allowRepeat);                      // 0x10014039 (allowRepeat != 0: plain random)
    IEvent* GetRandomEvent(int allowRepeat);                  // 0x10014017 (pick without starting)
    int PickAndStart(int allowRepeat);                        // 0x1001439c

private:
    IEvent* current_ = nullptr;          // +0x28
    std::vector<int> used_;              // +0x2c PtrArray of int* flags (parallel to the children)
    int usedCount_ = 0;                  // +0x30
    int last_ = 0;                       // +0x34
};

// ---------------------------------------------------------------------------------------------
// CEventRandomList (0x10013930): `avail` / `used` pointer lists.  Items are borrowed pointers.
class CEventRandomList {
public:
    explicit CEventRandomList(int growBy = 1);                // 0x10013930
    virtual ~CEventRandomList();                              // 0x10013a0b / 0x10013db0
    virtual bool Add(void* item);                             // [1] 0x10013b05: to `avail`
    virtual void* GetNext();                                  // [2] 0x10013b73: first available -> used
    virtual void Return(void* item);                          // [3] 0x10013c33: used -> end of avail
    virtual void ResetAll();                                  // [4] 0x10013c7a
    virtual void ReleaseAt(int usedIndex);                    // [5] 0x10013cbc
    void* GetUsedAt(u32 i);                                   // 0x10013a83
    void* GetAvailAt(u32 i);                                  // 0x10013ac4
    int Total() const;                                        // 0x10013bec
    int UsedCount() const;                                    // 0x10013c0b
    int AvailCount() const;                                   // 0x10013c1f
    void Shuffle();                                           // 0x10013d10 (aaron::Random)
protected:
    aaron::PtrArray* avail_ = nullptr;   // +4
    aaron::PtrArray* used_ = nullptr;    // +8
};

// CHunRandomList (0x10013de0, vtbl 0x1002353c): like CEventRandomList but GetNext recycles the oldest used item when
// nothing is available.  Used by the first half of Shoot.dll for the Huns (generic otherwise).
class CHunRandomList : public CEventRandomList {
public:
    explicit CHunRandomList(int growBy = 1) : CEventRandomList(growBy) {} // 0x10013de0
    void* GetNext() override;                                 // 0x10013e0f
};

} // namespace rapture
