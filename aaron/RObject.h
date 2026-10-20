// Aaron base classes: RObject, PtrArray, TreeNode, Broadcaster/Subscriber, SceneListener (aaron.md sec. 1, 2.7).
//
// MEMORY / OWNERSHIP RULE (see README): plain `new` / `delete`. The original gave every class its
// own operator new/delete (malloc of Aaron's CRT, ordinals 85..122); in the port they all map to the
// global ones. Ownership is exactly the original one:
//   * Widget tree: a parent deletes its children (TreeNode::DeleteChildren); game code `new`s widgets
//     and hands them to the parent (`Widget::InitOrDelete` = "attach or delete").
//   * Scene: owned by the Window (deleted in Window::LoadScene / Application dtor).
//   * RObject-derived objects shared between owners use AddRef/Release (Module, ResArchive, events of
//     Shoot's arcade framework): Release() at 0 -> `delete this`.
#pragma once

#include "aaron/Types.h"

#include <cstddef>
#include <new>

namespace aaron {

// ---------------------------------------------------------------------------------------------
// RObject: vptr + refcount at +4.
class RObject {
public:
    // Aaron ord 2 0x10001022: RObject::RObject (vptr = 0x1001d490, refcount (+4) = 1). QUIRK: the original starts at 1, the port at 0
    // (a freshly `new`ed object is disposed of with Release(), which deletes at <= 0, so both conventions behave the same).
    RObject() = default;
    RObject(const RObject&) {}            // refcount is not copied
    virtual ~RObject() = default;         // Aaron ord 59 0x10001990 (RObject::~RObject: restores the base vptr 0x1001d490)
    // Aaron ord 137 0x10001043: InterlockedIncrement(&refcount). (port: single threaded, plain increment)
    long AddRef() { return ++refcount_; }
    // Aaron ord 562 0x1000105b: InterlockedDecrement; at 0 `delete this` (virtual dtor slot 0 with flag 0 + free). Returns the
    // new count, or 0 after the delete. Release() on count 0 deletes too.
    long Release();
    long RefCount() const { return refcount_; }
    // Aaron ord 366 0x100010c0: vtable slot 1 of the base class, returns 0x20000 (CID_ROBJECT). Derived classes of the original override
    // it; in the port TreeNode / Window / Module ... keep their own `GetClassId`, this is the base-class one.
    virtual u32 GetObjClassId() const { return CID_ROBJECT; }

    // Per-class operator new / delete of the original: all map to the global operator.
    // Aaron ord 85 0x10001000 (RObject::operator_new) and 88,89,90,92,94,96,97,99,100,101,102,103 for
    //   RegKey, Button, DibSprite, RectSprite, AnimSprite, AnimRectSprite, AnimTextSprite,
    //   JoystickSource, KeyboardSource, MouseSource, Scene, Application
    // Aaron ord 104 0x10001011 (RObject::operator_delete) and 107,108,109,111,113,115,116,118,119,120,121,122 (same classes)
    // (the detailed per-ordinal list is in aaron/Ordinals.h)
    // WP-4: the remaining per-class operator new / delete thunks (all `malloc` / `free` of Aaron's CRT = the global operators here;
    // class names inferred from the neighbouring constructor, ordinals are alphabetical per decorated name):
    //   new  ord 86  0x100086a2  AnimCursor      delete ord 105 0x100086b3
    //   new  ord 87  0x1000849a  DibCursor       delete ord 106 0x100084ab
    //   new  ord 91  0x1000579e  PolySprite      delete ord 110 0x100057af
    //   new  ord 93  0x10005aeb  TextSprite      delete ord 112 0x10005afc
    //   new  ord 95  0x10005033  AnimPolySprite  delete ord 114 0x10005044
    //   new  ord 98  0x10009140  Controller      delete ord 117 0x10009151
    static void* operator new(std::size_t n) { return ::operator new(n); }
    static void operator delete(void* p) { ::operator delete(p); }
    static void* operator new[](std::size_t n) { return ::operator new[](n); }
    static void operator delete[](void* p) { ::operator delete[](p); }

private:
    long refcount_ = 0;
};

// ---------------------------------------------------------------------------------------------
// PtrArray: dynamic array of void*. Base of Shoot's CEventList / CPlayer lists:
// +8 count, +0xc capacity, +0x10 data (so it derives from RObject).
class PtrArray : public RObject {
public:
    explicit PtrArray(int capacity = 1);     // Aaron ord 46 0x10001e95: argument = initial capacity (+0xc); grows by 1 when full
    // Aaron ord 45 0x10001f78: copy constructor (capacity = count = src.count, elements copied; no elements -> no storage)
    PtrArray(const PtrArray& src);
    ~PtrArray() override;                    // Aaron ord 76 0x10001f1c
    // Insert(ptr, idx): idx == -1 appends. Returns the index or -1.
    int Insert(void* p, int idx = -1);       // Aaron ord 135 0x10002056
    void* RemoveAt(int idx);                 // Aaron ord 569 0x100023af (returns the removed element)
    int Remove(void* p);                     // Aaron ord 568 0x10002341 (first occurrence; returns its index or -1)
    void Clear();                            // Aaron ord 203 0x10002560 (count = 0, keeps the storage)
    void Free();                             // Aaron ord 204 0x10002234 (releases the storage)
    void DeleteAll();                        // Aaron ord 156 0x100024a5 (delete every element, then Clear)
    void Truncate(int newCount);             // Aaron ord 662 0x1000257e
    int Resize(int newCapacity);             // Aaron ord 663 0x1000242c
    // Aaron ord 157 0x10002610 (vtable slot 2 of the base PtrArray): count = 0 WITHOUT touching the elements. Derived arrays override
    // the slot with DeleteAll (ord 156), so Reset() through a derived pointer deletes the elements - as in the original.
    virtual void Reset() { count_ = 0; }
    // Aaron ord 162 0x10002150 (Shrink): count == 0 -> Free() (virtual slot 3); else if count < capacity realloc to `count` entries.
    void Shrink();
    // Aaron ord 190 0x100021c5: fn(element, a, b) for every element while fn returns nonzero; returns the last result (1 when the array
    // is empty or every call returned nonzero). The original holds the array's CMem lock meanwhile (single threaded here).
    using ForEachFn = int (*)(void* element, void* a, void* b);
    int ForEach(ForEachFn fn, void* a, void* b) const;
    // Aaron ord 442 0x10002277 (args (dst, src)): moves the element at `src` to index `dst`, shifting the ones in between.
    // No-op if dst == src or either index is out of range.
    void MoveElement(unsigned dst, unsigned src);
    // Aaron ord 176 0x100024fb: if idx < count: DisposeElement(element) (vtable slot 6) then RemoveAt(idx) (slot 4); returns idx < count.
    // QUIRK kept: the element is disposed BEFORE it is removed, so its destructor still sees itself in the array.
    bool DeleteAt(unsigned idx);
    // vtable slot 6 of the derived arrays (0x100116c0 and friends): `delete element` through its virtual destructor.
    virtual void DisposeElement(void* element) { delete static_cast<RObject*>(element); }
    int Count() const { return count_; }
    int Capacity() const { return capacity_; } // +0xc of the original
    void* At(int i) const { return data_[i]; }
    void*& operator[](int i) { return data_[i]; }

protected:
    int count_ = 0;       // +8
    int capacity_ = 0;    // +0xc
    void** data_ = nullptr; // +0x10
};

// Typed convenience over PtrArray (port addition; same storage).
template <class T>
class TPtrArray : public PtrArray {
public:
    using PtrArray::PtrArray;
    T* Get(int i) const { return static_cast<T*>(At(i)); }
};

class TreeNode;

// ---------------------------------------------------------------------------------------------
// TreeRoot (class id 0x20004, vtable 0x1001d4d8): the object a tree hangs from (Scene+0x538 "TreeRoot"): +8 root node, +0xc owner.
// The first TreeNode created with a TreeRoot* becomes its root and remembers the holder in its +8.
class TreeRoot : public RObject {
public:
    explicit TreeRoot(void* owner = nullptr) : owner_(owner) {}   // Aaron ord 55 0x10002c1c (+8 = 0, +0xc = owner)
    ~TreeRoot() override;                                         // Aaron ord 82 0x10002c53 (deletes the root node through its virtual dtor)
    u32 GetObjClassId() const override { return CID_TREEROOT; }   // Aaron ord 389 0x10002d70 (0x20004)
    // Aaron ord 660 0x10002cd5: if `node` is non-null and the holder has no root yet: root = node and node.holder = this.
    void SetRoot(TreeNode* node);
    TreeNode* Root() const { return root_; }
    void* Owner() const { return owner_; }

private:
    TreeNode* root_ = nullptr; // +8
    void* owner_ = nullptr;    // +0xc
    friend class TreeNode;
};

// ---------------------------------------------------------------------------------------------
// TreeNode (class id 0x20100): intrusive tree. Offsets: +08 root, +0c parent, +10 first child, +14 last,
// +18 next sibling, +1c previous sibling.
class TreeNode : public RObject {
public:
    TreeNode() = default;
    // Aaron ord 57 0x1000286b: TreeNode(TreeRoot* holder): all links 0, then holder->SetRoot(this).
    explicit TreeNode(TreeRoot* holder);
    // Aaron ord 56 0x100028d6: TreeNode(parent, pos): all links 0, then LinkTo(parent, pos).
    TreeNode(TreeNode* parent, uintptr_t pos);
    ~TreeNode() override;                       // Aaron ord 83 0x10002944 (deletes the children, last to first - verified in the decomp - then unlinks from the parent)
    virtual u32 GetClassId() const { return CID_NODE; } // slot 1
    // Aaron ord 390 0x10002d80: the base TreeNode vtable (0x1001d4cc) returns 0x20003 from slot 1.
    static constexpr u32 BaseClassId() { return CID_TREEBASE; }
    // vtable slot 2 (Aaron ord 453/456/468/471/473/474/516 0x10003520 NopVirtual): empty hook called on every node of a subtree after a
    // reparent (FUN_10002818). Widget::OnReserved2 is this very slot.
    virtual void OnReserved2() {}
    static constexpr uintptr_t kAtFirst = 0, kAtLast = 1; // `pos` of LinkTo / Reparent: 0 = first child, 1 = last child, otherwise "after that sibling"
    // Aaron 0x10002630 (FUN_10002630): links this (unlinked) node under `parent` at `pos`.
    void LinkTo(TreeNode* parent, uintptr_t pos);
    // Aaron ord 655 0x10002b58 (Reparent): if newParent != this: unlink (0x10002754), LinkTo(newParent, pos), NotifyTree.
    void Reparent(TreeNode* newParent, uintptr_t pos);
    // Aaron 0x10002818 (FUN_10002818): calls OnReserved2() on every descendant of this node (recursively).
    void NotifyTree();
    // Aaron ord 139 0x100029a7: moves the sibling run [first .. last] under this node at `pos`. The walk goes from `last` BACKWARDS
    // (previous sibling) up to `first`, linking each node at `pos`: with pos == kAtLast the run ends up REVERSED (kept).
    void AdoptRun(TreeNode* first, TreeNode* last, uintptr_t pos);
    // Aaron ord 140 0x10002a03: AdoptRun(src.first, src.last, pos) = move every child of `src` under this node.
    void AdoptChildrenOf(TreeNode* src, uintptr_t pos);
    // Aaron ord 404 0x10002ae1: wedges this node between `src` and its children: remembers src's children, makes this the first child
    // of src, then adopts the remembered children under this node (AdoptRun(first, last, kAtLast): reversed order, see ord 139).
    void WedgeUnder(TreeNode* src);
    // Aaron ord 412 0x10002b22: true if `ancestor` is on the parent chain of this node. QUIRK: for a non-null `ancestor` that is NOT on
    // the chain the original walks off the top and dereferences null; the port returns false.
    bool HasAncestor(const TreeNode* ancestor) const;
    // Aaron ord 191 0x10002a86: fn(child, a, b) on every child (AddRef'ing it around the call) while fn returns nonzero.
    // QUIRK (verified with objdump, same shape as ord 189 at 0x10001d78): the original advances to the next sibling BEFORE the matching
    // Release, i.e. it releases the NEXT child instead of the current one (refcount underflow / use after free). The port releases the
    // current child.
    using VisitFn = int (*)(TreeNode* node, void* a, void* b);
    int ForEachChild(VisitFn fn, void* a, void* b);
    // Aaron ord 698 0x10002b95: starts AT this node, walks the following siblings and recurses into the children of every visited node
    // (pre-order). Stops at the first zero result.
    int ForEachFromHere(VisitFn fn, void* a, void* b);
    TreeRoot* Holder() const { return holder_; }
    void DeleteChildren();                      // Aaron ord 174 0x10002a2a (delete every child, from the last one, recursively)
    // port helpers (original: internal Link/Unlink functions)
    void AddChild(TreeNode* child);             // appends at the end of the children list
    void Unlink();
    TreeNode* Parent() const { return parent_; }
    TreeNode* Root() const { return root_; }
    TreeNode* FirstChild() const { return first_; }
    TreeNode* LastChild() const { return last_; }
    TreeNode* NextSibling() const { return next_; }
    TreeNode* PrevSibling() const { return prev_; }

protected:
    TreeNode* root_ = nullptr;   // +08
    TreeNode* parent_ = nullptr; // +0c
    TreeNode* first_ = nullptr;  // +10
    TreeNode* last_ = nullptr;   // +14
    TreeNode* next_ = nullptr;   // +18
    TreeNode* prev_ = nullptr;   // +1c
    TreeRoot* holder_ = nullptr; // +08 of the original (the TreeRoot the tree hangs from); the port's root_ is the topmost node
    friend class TreeRoot;
};

// ---------------------------------------------------------------------------------------------
// Broadcaster / Subscriber: observer pattern used for properties (CProperty derives from
// Broadcaster), CDisplay, the TimerMgr and the Pause broadcast.
class Broadcaster;

class Subscriber {
public:
    Subscriber() = default;
    Subscriber(const Subscriber&) {}           // subscriptions are not copied
    virtual ~Subscriber();                     // Aaron ord 61 0x1000829a (removes itself from its broadcaster)
    // Called by Broadcaster::Broadcast(code, value, src). Shoot: `if (code == -1 && ...)`.
    virtual void OnAdvise(int code, intptr_t value, Broadcaster* src) { (void)code; (void)value; (void)src; }
    // The original Broadcaster::Broadcast(code, a, b) hands TWO opaque dwords to slot 1 of every subscriber (Aaron ord 141 0x10008243).
    // Most users pass (value, the broadcaster); a game that needs the second dword as data overrides this instead of OnAdvise.
    // Default: forwards to OnAdvise with `b` reinterpreted as the source pointer.
    virtual void OnAdvise2(int code, intptr_t a, intptr_t b) { OnAdvise(code, a, reinterpret_cast<Broadcaster*>(b)); }

private:
    // port addition (pkg3): the broadcasters this subscriber is registered with, so that ~Subscriber can unlink itself.
    friend class Broadcaster;
    Broadcaster* bcasts_[4] = {nullptr, nullptr, nullptr, nullptr};
};

class Broadcaster : public RObject {
public:
    Broadcaster() = default;
    ~Broadcaster() override;                   // Aaron ord 80 0x10008130
    void AddListener(Subscriber* s);           // Aaron ord 603 0x100081b5
    void RemoveListener(Subscriber* s);        // Aaron ord 700 0x10008218
    // Calls s->OnAdvise(code, value, src) on every listener. Shoot: Broadcast(-1, value, this).
    void Broadcast(int code, intptr_t value, Broadcaster* src);   // Aaron ord 141 0x10008243
    // Same function with the second dword as plain data: calls s->OnAdvise2(code, a, b) on every listener.
    void Broadcast2(int code, intptr_t a, intptr_t b);
    int ListenerCount() const { return listeners_.Count(); }

private:
    PtrArray listeners_;
};

// ---------------------------------------------------------------------------------------------
// SceneListener: base of the small listener sub-objects the scenes embed (Title scene: KeyListener
// at +0x6b0, SoundListener at +0x6c8). Registered with Scene::Add*Listener (see Scene.h).
// TODO(package 3): the original has two different interfaces (OnInput(msg, wParam) for keyboard/mouse
// events, OnEnd() for end-of-sound/AVI). They are merged here as two virtuals with empty defaults.
class SceneListener {
public:
    virtual ~SceneListener();                  // Aaron ord 713 0x10001988
    virtual void OnInput(u32 msg, uintptr_t wParam) { (void)msg; (void)wParam; } // Shoot.exe 0x408f91: msg = WMSG_KEYUP / WMSG_?BUTTONUP
    virtual void OnEnd() {}                    // Shoot.exe 0x409031: sound / AVI finished
};

// Random(min, max) inclusive: Aaron ord 558 0x10004391 = rand() % (max - min + 1) + min.
// Port: MSVC-compatible LCG (x = x*214013+2531011, (x>>16)&0x7fff) so sequences match a given seed.
int Random(int minValue, int maxValue);
void SeedRandom(u32 seed);

} // namespace aaron
