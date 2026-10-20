// Aaron base classes: RObject, PtrArray, TreeNode, Broadcaster/Subscriber, SceneListener, Random (aaron.md sec. 1).
// Package 3.
#include "aaron/RObject.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace aaron {

// ---------------------------------------------------------------------------------------------
// RObject
// Aaron ord 562: --refcount; delete at 0. (Release on an object whose count is already 0 deletes it too,
// which is how a freshly `new`ed, never AddRef'd object is disposed of.)
long RObject::Release() {
    long n = --refcount_;
    if (n <= 0) {
        delete this;
        return 0;
    }
    return n;
}

// ---------------------------------------------------------------------------------------------
// PtrArray (Aaron 0x10001e95 ...): +8 count, +0xc capacity, +0x10 data
PtrArray::PtrArray(int capacity) {                                         // ord 46 0x10001e95: the argument is the INITIAL CAPACITY (+0xc)
    if (capacity > 0) {
        data_ = static_cast<void**>(std::malloc(size_t(capacity) * sizeof(void*)));
        capacity_ = data_ ? capacity : 0;
    }
}

PtrArray::PtrArray(const PtrArray& src) : RObject(src) {                  // ord 45 0x10001f78
    count_ = capacity_ = src.count_; // the original copies `src.count` into BOTH +8 and +0xc
    if (count_ > 0) {
        data_ = static_cast<void**>(std::malloc(size_t(count_) * sizeof(void*)));
        if (data_) std::memcpy(data_, src.data_, size_t(count_) * sizeof(void*));
        else count_ = capacity_ = 0;
    } else {
        count_ = capacity_ = 0;
    }
}

PtrArray::~PtrArray() { std::free(data_); }                                // ord 76 0x10001f1c

int PtrArray::Resize(int newCapacity) {                                    // ord 663 0x1000242c
    if (newCapacity < count_) newCapacity = count_;
    if (newCapacity == capacity_) return 1;
    if (newCapacity == 0) {
        std::free(data_);
        data_ = nullptr;
        capacity_ = 0;
        return 1;
    }
    void** p = static_cast<void**>(std::realloc(data_, size_t(newCapacity) * sizeof(void*)));
    if (!p) return 0;
    data_ = p;
    capacity_ = newCapacity;
    return 1;
}

int PtrArray::Insert(void* p, int idx) {                                   // ord 135 0x10002056
    if (count_ >= capacity_) { // original: capacity grows by exactly 1 when full
        if (!Resize(capacity_ + 1)) return -1;
    }
    if (idx < 0 || idx > count_) idx = count_; // original (unsigned compare): an index >= count appends
    for (int i = count_; i > idx; --i) data_[i] = data_[i - 1];
    data_[idx] = p;
    ++count_;
    return idx;
}

void* PtrArray::RemoveAt(int idx) {                                        // ord 569 0x100023af
    if (idx < 0 || idx >= count_) return nullptr;
    void* r = data_[idx];
    for (int i = idx; i < count_ - 1; ++i) data_[i] = data_[i + 1];
    --count_;
    return r;
}

int PtrArray::Remove(void* p) {                                            // ord 568 0x10002341
    for (int i = 0; i < count_; ++i) {
        if (data_[i] == p) {
            RemoveAt(i);
            return i;
        }
    }
    return -1;
}

void PtrArray::Clear() { count_ = 0; }                                     // ord 203 0x10002560

void PtrArray::Free() {                                                    // ord 204 0x10002234
    std::free(data_);
    data_ = nullptr;
    count_ = capacity_ = 0;
}

void PtrArray::DeleteAll() {                                               // ord 156 0x100024a5
    // The original calls the element's virtual destructor (slot 0). Elements are RObject-derived by convention
    // (events, properties); callers whose elements are not RObject must Clear() instead.
    for (int i = 0; i < count_; ++i) delete static_cast<RObject*>(data_[i]);
    count_ = 0;
}

void PtrArray::Shrink() {                                                  // ord 162 0x10002150
    if (count_ == 0) {
        Free(); // virtual slot 3 in the original
    } else if (count_ < capacity_) {
        void** p = static_cast<void**>(std::realloc(data_, size_t(count_) * sizeof(void*)));
        if (p) data_ = p;
        capacity_ = count_;
    }
}

int PtrArray::ForEach(ForEachFn fn, void* a, void* b) const {              // ord 190 0x100021c5
    int r = 1;
    for (int i = 0; r != 0 && i < count_; ++i) r = fn(data_[i], a, b);
    return r;
}

void PtrArray::MoveElement(unsigned dst, unsigned src) {                   // ord 442 0x10002277
    if (dst == src || src >= unsigned(count_) || dst >= unsigned(count_)) return;
    void* v = data_[src];
    if (src < dst) std::memmove(&data_[src], &data_[src + 1], size_t(dst - src) * sizeof(void*));
    else std::memmove(&data_[dst + 1], &data_[dst], size_t(src - dst) * sizeof(void*));
    data_[dst] = v;
}

bool PtrArray::DeleteAt(unsigned idx) {                                    // ord 176 0x100024fb
    bool ok = idx < unsigned(count_);
    if (ok) {
        DisposeElement(data_[idx]); // the element dies first (it may still look for itself in the array)
        RemoveAt(int(idx));
    }
    return ok;
}

void PtrArray::Truncate(int newCount) {                                    // ord 662 0x1000257e
    if (newCount < 0) newCount = 0;
    if (newCount < count_) count_ = newCount;
}

// ---------------------------------------------------------------------------------------------
// TreeNode (Aaron 0x10002944): the destructor unlinks from the parent; children are NOT deleted here.
TreeNode::TreeNode(TreeRoot* holder) { if (holder) holder->SetRoot(this); }          // ord 57 0x1000286b
TreeNode::TreeNode(TreeNode* parent, uintptr_t pos) { LinkTo(parent, pos); }       // ord 56 0x100028d6

TreeNode::~TreeNode() { // 0x10002944: if it has children, DeleteChildren(); then unlink from the parent
    if (first_) DeleteChildren();
    if (holder_ && holder_->root_ == this) holder_->root_ = nullptr; // (port: the holder must not keep a dangling root)
    Unlink();
}

void TreeNode::AddChild(TreeNode* child) {
    if (!child) return;
    child->Unlink();
    child->LinkTo(this, kAtLast);
}

// Aaron 0x10002630 (FUN_10002630). pos: 0 = first, 1 = last, otherwise "insert after that sibling".
void TreeNode::LinkTo(TreeNode* parent, uintptr_t pos) {
    parent_ = parent;
    if (!parent) return;
    root_ = parent->root_ ? parent->root_ : parent; // port convention: the topmost node (the original copies the parent's holder at +8)
    holder_ = parent->holder_;
    if (!parent->first_) {
        prev_ = next_ = nullptr;
        parent->first_ = parent->last_ = this;
    } else if (pos == kAtFirst) {
        next_ = parent->first_;
        prev_ = nullptr;
        parent->first_ = this;
        next_->prev_ = this;
    } else if (pos == kAtLast) {
        next_ = nullptr;
        prev_ = parent->last_;
        parent->last_ = this;
        prev_->next_ = this;
    } else {
        TreeNode* after = reinterpret_cast<TreeNode*>(pos);
        next_ = after->next_;
        prev_ = after;
        after->next_ = this;
        if (next_) next_->prev_ = this; else parent->last_ = this;
    }
}

void TreeNode::Reparent(TreeNode* newParent, uintptr_t pos) {              // ord 655 0x10002b58
    if (newParent == this) return;
    Unlink();
    LinkTo(newParent, pos);
    NotifyTree();
}

void TreeNode::NotifyTree() { // 0x10002818. ORIGINAL BUG: the recursive call passes `this` again instead of the child, so a child that has
    // children makes the original recurse forever (stack overflow). Never hit in practice (the hook is empty); the port recurses properly.
    for (TreeNode* c = first_; c; c = c->next_) {
        c->OnReserved2();
        if (c->first_) c->NotifyTree();
    }
}

void TreeNode::AdoptRun(TreeNode* first, TreeNode* last, uintptr_t pos) {  // ord 139 0x100029a7
    TreeNode* n = last;
    for (;;) {
        if (!n) return;
        TreeNode* prev = n->prev_;
        bool more = n != first;
        n->Unlink();
        n->LinkTo(this, pos);
        NotifyTree();
        n = prev;
        if (!more) return;
    }
}

void TreeNode::AdoptChildrenOf(TreeNode* src, uintptr_t pos) {             // ord 140 0x10002a03
    AdoptRun(src->first_, src->last_, pos);
}

void TreeNode::WedgeUnder(TreeNode* src) {                                 // ord 404 0x10002ae1
    TreeNode* first = src->first_;
    TreeNode* last = src->last_;
    Reparent(src, kAtFirst);
    AdoptRun(first, last, kAtLast);
}

bool TreeNode::HasAncestor(const TreeNode* ancestor) const {               // ord 412 0x10002b22
    const TreeNode* n = parent_;
    while (n && n != ancestor) n = n->parent_;
    return n != nullptr;
}

int TreeNode::ForEachChild(VisitFn fn, void* a, void* b) {                 // ord 191 0x10002a86
    // The original brackets every call with AddRef/Release (see the quirk in the header). The port's refcount base is 0 (an owned,
    // never AddRef'd widget would be deleted by the matching Release), so the bracket is omitted and `next` is read BEFORE the call:
    // the callback may delete the node it is given.
    int r = 1;
    TreeNode* n = first_;
    while (n && r) {
        TreeNode* next = n->next_;
        r = fn(n, a, b);
        n = next;
    }
    return r;
}

int TreeNode::ForEachFromHere(VisitFn fn, void* a, void* b) {              // ord 698 0x10002b95
    int r = 1;
    TreeNode* n = this;
    while (n && r) {
        TreeNode* next = n->next_;
        TreeNode* kid = nullptr;
        r = fn(n, a, b);
        kid = n->first_;
        if (r && kid) r = kid->ForEachFromHere(fn, a, b);
        n = next;
    }
    return r;
}

TreeRoot::~TreeRoot() {                                                    // ord 82 0x10002c53
    if (root_) {
        TreeNode* r = root_;
        root_ = nullptr;
        delete r;
    }
}

void TreeRoot::SetRoot(TreeNode* node) {                                   // ord 660 0x10002cd5
    if (node && !root_) {
        root_ = node;
        node->holder_ = this;
    }
}

void TreeNode::Unlink() {
    if (parent_) {
        if (prev_) prev_->next_ = next_; else parent_->first_ = next_;
        if (next_) next_->prev_ = prev_; else parent_->last_ = prev_;
    }
    parent_ = nullptr;
    prev_ = next_ = nullptr;
    root_ = nullptr;
}

void TreeNode::DeleteChildren() {                                          // ord 174 0x10002a2a
    while (last_) { // 0x10002a2a: from the LAST child backwards
        TreeNode* c = last_;
        delete c; // ~TreeNode deletes its own children and unlinks it
    }
}

// ---------------------------------------------------------------------------------------------
// Broadcaster / Subscriber
Subscriber::~Subscriber() {                                                // ord 61 0x1000829a
    for (Broadcaster*& b : bcasts_) {
        if (b) {
            Broadcaster* bb = b;
            b = nullptr;
            bb->RemoveListener(this);
        }
    }
}

Broadcaster::~Broadcaster() {                                              // ord 80 0x10008130
    for (int i = 0; i < listeners_.Count(); ++i) {
        Subscriber* s = static_cast<Subscriber*>(listeners_.At(i));
        for (Broadcaster*& b : s->bcasts_)
            if (b == this) b = nullptr;
    }
}

void Broadcaster::AddListener(Subscriber* s) {                             // ord 603 0x100081b5
    if (!s) return;
    listeners_.Insert(s, -1);
    for (Broadcaster*& b : s->bcasts_) {
        if (!b) { b = this; break; }
    }
}

void Broadcaster::RemoveListener(Subscriber* s) {                          // ord 700 0x10008218
    if (!s) return;
    if (listeners_.Remove(s) < 0) return;
    for (Broadcaster*& b : s->bcasts_) {
        if (b == this) { b = nullptr; break; }
    }
}

void Broadcaster::Broadcast(int code, intptr_t value, Broadcaster* src) {  // ord 141 0x10008243
    Broadcast2(code, value, reinterpret_cast<intptr_t>(src));
}

void Broadcaster::Broadcast2(int code, intptr_t a, intptr_t b) {           // ord 141 0x10008243 (the two dwords are opaque)
    // Iterate over a snapshot: a listener may unsubscribe (or delete itself) while being advised.
    std::vector<Subscriber*> snap;
    snap.reserve(size_t(listeners_.Count()));
    for (int i = 0; i < listeners_.Count(); ++i) snap.push_back(static_cast<Subscriber*>(listeners_.At(i)));
    for (Subscriber* s : snap) {
        bool still = false;
        for (int i = 0; i < listeners_.Count(); ++i) {
            if (listeners_.At(i) == s) { still = true; break; }
        }
        if (still) s->OnAdvise2(code, a, b);
    }
}

// ---------------------------------------------------------------------------------------------
SceneListener::~SceneListener() {}                                         // ord 713 0x10001988

// ---------------------------------------------------------------------------------------------
// Random (ord 558 0x10004391): rand() % (max - min + 1) + min with the MSVC CRT generator
// (x = x * 214013 + 2531011; return (x >> 16) & 0x7fff). The initial seed of the MSVC CRT is 1.
static uint32_t g_randSeed = 1;

static int crt_rand() {
    g_randSeed = g_randSeed * 214013u + 2531011u;
    return int((g_randSeed >> 16) & 0x7fff);
}

void SeedRandom(u32 seed) { g_randSeed = seed; }

int Random(int minValue, int maxValue) {
    int range = maxValue - minValue + 1;
    if (range <= 0) return minValue; // original would divide by zero / use a negative modulus
    return crt_rand() % range + minValue;
}

} // namespace aaron
