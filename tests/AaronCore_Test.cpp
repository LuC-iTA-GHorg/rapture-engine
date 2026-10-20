// WP-4 headless tests, group "core": RObject / PtrArray / TreeNode / TreeRoot additions, MemBuffer, LinkList.
// Called from RunPkg3Tests. No window is ever created.
#include "aaron/Aaron.h"
#include "aaron/Containers.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace aaron;

namespace {

int g_checks = 0, g_fail = 0;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        ++g_checks;                                                                      \
        if (!(c)) {                                                                      \
            ++g_fail;                                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                   \
        }                                                                                \
    } while (0)

int g_hooks = 0;
struct HookNode : TreeNode {
    void OnReserved2() override { ++g_hooks; }
};

int sumFn(void* e, void* a, void*) {
    *static_cast<int*>(a) += int(reinterpret_cast<intptr_t>(e));
    return 1;
}
int stopAt3(void* e, void* a, void*) {
    ++*static_cast<int*>(a);
    return reinterpret_cast<intptr_t>(e) == 3 ? 0 : 1;
}

struct Elem : RObject {
    static int alive;
    Elem() { ++alive; }
    ~Elem() override { --alive; }
};
int Elem::alive = 0;

int visitCount(TreeNode*, void* a, void*) {
    ++*static_cast<int*>(a);
    return 1;
}

int listVisit(LinkItem*, void* a, void*) {
    ++*static_cast<int*>(a);
    return 1;
}

void test_robject() {
    RObject o;
    CHECK(o.GetObjClassId() == 0x20000);       // ord 366
    CHECK(o.RefCount() == 0);
    CHECK(o.AddRef() == 1);                    // ord 137
    CHECK(o.AddRef() == 2);
    CHECK(o.Release() == 1);                   // ord 562 (not deleted)
    CHECK(TreeNode::BaseClassId() == 0x20003); // ord 390
    TreeRoot tr;
    CHECK(tr.GetObjClassId() == 0x20004);      // ord 389
}

void test_ptrarray() {
    PtrArray a(4);
    for (intptr_t i = 1; i <= 5; ++i) a.Insert(reinterpret_cast<void*>(i), -1);
    CHECK(a.Count() == 5);
    int sum = 0;
    CHECK(a.ForEach(sumFn, &sum, nullptr) == 1); // ord 190
    CHECK(sum == 15);
    int calls = 0;
    CHECK(a.ForEach(stopAt3, &calls, nullptr) == 0); // stops at the first zero
    CHECK(calls == 3);
    PtrArray c(a); // ord 45
    CHECK(c.Count() == 5 && c.Capacity() == 5);
    CHECK(c.At(0) == a.At(0) && c.At(4) == a.At(4));
    a.RemoveAt(4);
    a.RemoveAt(3);
    CHECK(a.Capacity() == 5 && a.Count() == 3);
    a.Shrink(); // ord 162
    CHECK(a.Capacity() == 3);
    // MoveElement(dst, src): element at src goes to dst
    a.MoveElement(0, 2); // [1,2,3] -> [3,1,2]
    CHECK(a.At(0) == reinterpret_cast<void*>(3) && a.At(1) == reinterpret_cast<void*>(1) && a.At(2) == reinterpret_cast<void*>(2));
    a.MoveElement(2, 0); // [3,1,2] -> [1,2,3]
    CHECK(a.At(0) == reinterpret_cast<void*>(1) && a.At(2) == reinterpret_cast<void*>(3));
    a.MoveElement(5, 0); // out of range: no-op
    CHECK(a.At(0) == reinterpret_cast<void*>(1));
    a.Reset(); // ord 157
    CHECK(a.Count() == 0 && a.Capacity() == 3);
    a.Shrink();
    CHECK(a.Capacity() == 0); // count 0 -> Free()
    // DeleteAt disposes the element
    PtrArray d(2);
    d.Insert(new Elem, -1);
    d.Insert(new Elem, -1);
    CHECK(Elem::alive == 2);
    CHECK(d.DeleteAt(0)); // ord 176
    CHECK(Elem::alive == 1 && d.Count() == 1);
    CHECK(!d.DeleteAt(5));
    d.DeleteAll();
    CHECK(Elem::alive == 0);
}

void test_tree() {
    HookNode root;
    HookNode* kids[4];
    for (auto& k : kids) {
        k = new HookNode;
        root.AddChild(k);
    }
    CHECK(root.FirstChild() == kids[0] && root.LastChild() == kids[3]);
    CHECK(kids[2]->HasAncestor(&root)); // ord 412
    CHECK(!root.HasAncestor(&root));
    HookNode other;
    CHECK(!kids[2]->HasAncestor(&other)); // the original would crash; the port says no
    int n = 0;
    CHECK(root.ForEachChild(visitCount, &n, nullptr) == 1); // ord 191
    CHECK(n == 4);
    // pos semantics: Reparent(kAtFirst) / kAtLast / after
    g_hooks = 0;
    HookNode dst;
    kids[3]->Reparent(&dst, TreeNode::kAtFirst); // ord 655
    CHECK(dst.FirstChild() == kids[3] && root.LastChild() == kids[2]);
    kids[0]->Reparent(&dst, TreeNode::kAtLast);
    CHECK(dst.FirstChild() == kids[3] && dst.LastChild() == kids[0]);
    kids[1]->Reparent(&dst, reinterpret_cast<uintptr_t>(kids[3])); // after kids[3]
    CHECK(dst.FirstChild() == kids[3] && dst.FirstChild()->NextSibling() == kids[1] && dst.LastChild() == kids[0]);
    CHECK(g_hooks == 0); // NotifyTree walks the CHILDREN of the reparented node: none here
    // reparent a node with children: hook runs on its descendants
    HookNode* gk = new HookNode;
    kids[2]->AddChild(gk);
    kids[2]->Reparent(&dst, TreeNode::kAtLast);
    CHECK(g_hooks == 1);
    // AdoptChildrenOf: all children of dst move under `adopter` at the head -> order kept; at the tail -> reversed
    HookNode adopter;
    adopter.AdoptChildrenOf(&dst, TreeNode::kAtFirst); // ord 140 / 139
    CHECK(dst.FirstChild() == nullptr);
    std::string order;
    for (TreeNode* c = adopter.FirstChild(); c; c = c->NextSibling()) {
        for (int i = 0; i < 4; ++i)
            if (c == kids[i]) order += char('0' + i);
    }
    CHECK(order == "3102");
    HookNode rev;
    rev.AdoptChildrenOf(&adopter, TreeNode::kAtLast); // reversed (quirk)
    order.clear();
    for (TreeNode* c = rev.FirstChild(); c; c = c->NextSibling()) {
        for (int i = 0; i < 4; ++i)
            if (c == kids[i]) order += char('0' + i);
    }
    CHECK(order == "2013");
    // WedgeUnder (ord 404): this node becomes the only child of src, the old children go below it
    HookNode wedge;
    wedge.WedgeUnder(&rev);
    CHECK(rev.FirstChild() == &wedge);
    int below = 0;
    for (TreeNode* c = wedge.FirstChild(); c; c = c->NextSibling()) ++below;
    CHECK(below == 4);
    // ForEachFromHere: this + following siblings, recursing into children (pre-order)
    int visited = 0;
    wedge.ForEachFromHere(visitCount, &visited, nullptr); // ord 698
    CHECK(visited == 1 + 4 + 1); // wedge + its 4 children + the grandchild under kids[2]
    // TreeRoot / TreeNode(TreeRoot*)
    TreeRoot* holder = new TreeRoot(&root);
    TreeNode* rn = new TreeNode(holder); // ord 57 -> holder->SetRoot
    CHECK(holder->Root() == rn && rn->Holder() == holder && holder->Owner() == &root);
    TreeNode* second = new TreeNode(holder);
    CHECK(holder->Root() == rn && second->Holder() == nullptr); // a second node does not replace the root
    delete second;
    delete holder; // ord 82: deletes the root node
    TreeNode parent;
    TreeNode* c1 = new TreeNode(&parent, TreeNode::kAtLast); // ord 56
    TreeNode* c0 = new TreeNode(&parent, TreeNode::kAtFirst);
    CHECK(parent.FirstChild() == c0 && parent.LastChild() == c1);
}

void test_membuffer() {
    MemBuffer::Totals t0 = MemBuffer::Stats(); // ord 303
    {
        MemBuffer b(10000, 4096); // ord 4: reservation rounded up to 12288
        MemBuffer::Totals t1 = MemBuffer::Stats();
        CHECK(t1.buffers == t0.buffers + 1);
        CHECK(t1.reserved == t0.reserved + 12288);
        unsigned used = 99, res = 99, com = 99;
        CHECK(b.Info(&used, &res, &com) == 0 && used == 0 && res == 12288 && com == 0); // ord 351
        CHECK(b.SetSize(100, 0) == 1);                                                   // ord 661
        CHECK(b.Used() == 100 && b.Committed() == 4096);
        CHECK(b.Append("abc", 3, 0) == 103);          // ord 145: returns the NEW size
        CHECK(std::memcmp(b.Data() + 100, "abc", 3) == 0);
        b.SetSize(0, 0);
        b.AppendString("hello", 0);                    // ord 147
        CHECK(b.Used() == 6 && std::strcmp(reinterpret_cast<char*>(b.Data()), "hello") == 0);
        CHECK(b.AppendLine("x", 0) == 1);              // ord 146
        CHECK(b.Used() == 6 + 1 + 2 && std::memcmp(b.Data() + 6, "x\r\n", 3) == 0);
        b.SetSize(0, 0);
        b.AppendString("world", 0);
        b.InsertString(0, "hello ", 0);                // ord 405 / 403: inserts "hello \0" shifting the old content
        CHECK(std::strcmp(reinterpret_cast<char*>(b.Data()), "hello ") == 0);
        CHECK(std::strcmp(reinterpret_cast<char*>(b.Data()) + 7, "world") == 0);
        CHECK(b.RemoveString(0) == 0);                 // ord 571
        CHECK(std::strcmp(reinterpret_cast<char*>(b.Data()), "world") == 0 && b.Used() == 6);
        CHECK(b.Remove(100, 1) == 0xffffffffu);        // ord 567 out of range
        CHECK(b.Remove(1, 2) == 1 && b.Used() == 4);   // "wld\0"
        CHECK(std::strcmp(reinterpret_cast<char*>(b.Data()), "wld") == 0);
        // growth beyond the reservation moves the data (growBy given)
        CHECK(b.SetSize(20000, 0) == 1);
        CHECK(b.Reserved() >= 20000 && std::strcmp(reinterpret_cast<char*>(b.Data()), "wld") == 0);
        // Lock (ord 437) forbids moving; Unlock (ord 709) allows it again
        unsigned char* p = b.Lock();
        CHECK(p == b.Data());
        CHECK(b.SetSize(b.Reserved() + 5000, 0) == 0);
        b.Unlock();
        CHECK(b.SetSize(b.Reserved() + 5000, 0) == 1);
        // Decommit (ord 161) keeps only the pages that hold data; Reset (ord 180)
        b.SetSize(5000, 0);
        b.Decommit();
        CHECK(b.Committed() == 8192);
        b.Reset(true);                                  // ord 180
        CHECK(b.Used() == 0 && b.Committed() == 0);
        // fixed buffer: growBy -1 cannot move
        MemBuffer f(4096, -1);
        CHECK(f.SetSize(5000, 0) == 0);
        CHECK(f.SetSize(4096, 0) == 1);
        CHECK(f.SetSize(5000, 100) == 0); // flag clear (growBy_ == -1)
    }
    MemBuffer::Totals t2 = MemBuffer::Stats();
    CHECK(t2.buffers == t0.buffers && t2.reserved == t0.reserved && t2.used == t0.used && t2.committed == t0.committed);
    MemBuffer z(0, 0); // nothing reserved: every growth re-reserves
    CHECK(z.Data() == nullptr);
    CHECK(z.SetSize(10, 0) == 1 && z.Data() != nullptr);
}

void test_linklist() {
    LinkList* l = new LinkList; // ord 39
    CHECK(l->GetObjClassId() == 0x20002); // ord 381
    LinkItem* a = new LinkItem(l, LinkItem::kAtLast); // ord 40
    CHECK(a->GetObjClassId() == 0x20001); // ord 382
    LinkItem* b = new LinkItem(l, LinkItem::kAtLast);
    LinkItem* c = new LinkItem(l, LinkItem::kAtFirst);
    CHECK(l->First() == c && l->Last() == b && l->Count() == 3);
    LinkItem* d = new LinkItem(l, reinterpret_cast<uintptr_t>(a)); // after a
    CHECK(a->Next() == d && d->Next() == b && l->Count() == 4);
    CHECK(l->Contains(d) && l->Contains(c));                 // ord 419
    int n = 0;
    CHECK(l->ForEach(listVisit, &n, nullptr) == 1 && n == 4); // ord 189
    d->Reposition(LinkItem::kAtFirst);                       // ord 443
    CHECK(l->First() == d && d->Next() == c && l->Count() == 4);
    d->Reposition(reinterpret_cast<uintptr_t>(d)); // pos == this: no-op
    CHECK(l->First() == d);
    LinkItem* lone = new LinkItem(l, LinkItem::kAtLast);
    delete lone; // ~LinkItem unlinks (ord 73)
    CHECK(l->Count() == 4 && !l->Contains(lone));
    l->DeleteAll();                                           // ord 155
    CHECK(l->Count() == 0 && l->First() == nullptr && l->Last() == nullptr);
    new LinkItem(l, LinkItem::kAtLast);
    delete l; // ord 72: deletes the remaining items
}

} // namespace

int RunAaronCoreTests(int* checks) {
    g_checks = g_fail = 0;
    test_robject();
    test_ptrarray();
    test_tree();
    test_membuffer();
    test_linklist();
    std::printf("aaron-wp4 core: %d checks, %d failures\n", g_checks, g_fail);
    if (checks) *checks += g_checks;
    return g_fail;
}
