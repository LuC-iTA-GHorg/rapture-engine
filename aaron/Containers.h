// Aaron container classes that Shoot.* never uses (WP-4): MemBuffer (the VirtualAlloc backed growable byte buffer), LinkItem / LinkList
// (doubly linked list of RObjects). Names are invented (from analysis of Aaron.dll); addresses are Aaron.dll's.
#pragma once

#include "aaron/RObject.h"

#include <cstdint>
#include <vector>

namespace aaron {

// ---------------------------------------------------------------------------------------------
// MemBuffer (vtable 0x1001d49c, 0x20 bytes): a byte buffer that RESERVES address space with VirtualAlloc(MEM_RESERVE) and COMMITS pages
// as the used size grows, so the data pointer stays valid while it grows inside the reservation.
//   +08 base   +0c used size   +10 committed bytes (page multiple)   +14 reserved bytes   +18 growBy (-1 = fixed)   +1c "may move" flag
// Port: one zero-initialised allocation of the reserved size (calloc: lazy pages); "committed" is tracked arithmetically, page = 4096.
// The process-wide statistics (DAT_10023c88..c94: reserved, committed, used, buffer count) are kept too (Stats()).
class MemBuffer : public RObject {
public:
    static constexpr unsigned kPage = 4096;                 // DAT_10023c80 (GetSystemInfo.dwPageSize)
    // Aaron ord 4 0x10001100: MemBuffer(reserveBytes, growBy). growBy < 0 -> -1 (never moves). The reservation is rounded up to whole pages.
    MemBuffer(size_t reserveBytes, int growBy);
    ~MemBuffer() override;                                  // Aaron ord 60 0x100011ff (Free(); --count)
    u32 GetObjClassId() const override { return CID_ROBJECT; } // vtable 0x1001d49c slot 1 = ord 366 (0x20000)

    // Aaron ord 661 0x10001727 (SetSize(newSize, growBy)): the core. Commits pages up to newSize, zeroes the bytes of a shrink, and when
    // newSize exceeds the reservation re-reserves max(reserved + growBy, newSize) (growBy 0 -> the buffer's own growBy, or half the reservation if
    // that is <= 0) and moves the data - only if the "may move" flag is set and the effective growBy != -1. Returns 1 on success, 0 on failure.
    int SetSize(unsigned newSize, unsigned growBy);
    // Aaron ord 145 0x10001260 (Append): SetSize(used + n, growBy) then copy. Returns the NEW used size (QUIRK: not the offset of the data), or -1.
    int Append(const void* data, unsigned n, unsigned growBy);
    // Aaron ord 147 0x100012be: Append(str, strlen + 1).
    void AppendString(const char* s, unsigned growBy);
    // Aaron ord 146 0x100012e9: Append(str, strlen) then Append("\r\n", 2). Returns 1 / 0.
    int AppendLine(const char* s, unsigned growBy);
    // Aaron ord 403 0x10001557 (Insert): inserts n bytes at `offset` (< used), shifting the tail up; offset >= used appends. Returns offset + n
    // (QUIRK), the new used size when it appended, or -1.
    int Insert(unsigned offset, const void* data, unsigned n, unsigned growBy);
    // Aaron ord 405 0x100015f9: Insert(offset, str, strlen + 1).
    void InsertString(unsigned offset, const char* s, unsigned growBy);
    // Aaron ord 567 0x10001643 (Remove): removes `n` bytes at `offset` (n < 0: to the end; clamped), shrinking by SetSize(used - n, 0). Returns offset or -1.
    unsigned Remove(unsigned offset, int n);
    // Aaron ord 571 0x100016ea: Remove(offset, strlen(at offset) + 1) = removes the C string stored at `offset`. Returns offset or -1.
    unsigned RemoveString(unsigned offset);
    // Aaron ord 161 0x1000134b (Decommit): releases the committed pages beyond the used size.
    void Decommit();
    // Aaron ord 180 0x100013ef (Reset(decommit)): used = 0, optionally Decommit(), then zero the committed bytes.
    void Reset(bool decommit);
    // Aaron ord 201 0x10001444 (Free): releases the whole reservation (statistics adjusted).
    void Free();
    // Aaron ord 437 0x10001628: clears the "may move" flag and returns the base pointer (so the pointer stays valid). Pair with ord 709.
    unsigned char* Lock() { movable_ = 0; return base_; }
    // Aaron ord 709 0x10001943: flag = (growBy != -1)
    void Unlock() { movable_ = growBy_ != -1 ? 1 : 0; }
    // Aaron ord 351 0x10001511: outputs used / reserved / committed (each may be null); returns used.
    unsigned Info(unsigned* used, unsigned* reserved, unsigned* committed) const;
    unsigned char* Data() const { return base_; }
    unsigned Used() const { return used_; }
    unsigned Committed() const { return committed_; }
    unsigned Reserved() const { return reserved_; }
    // Aaron ord 303 0x100014e6 (static): process-wide {reserved bytes, committed bytes, used bytes, live buffers} (DAT_10023c88..c94).
    struct Totals { unsigned reserved, committed, used, buffers; };
    static Totals Stats();

private:
    unsigned char* base_ = nullptr; // +8
    unsigned used_ = 0;             // +0xc
    unsigned committed_ = 0;        // +0x10
    unsigned reserved_ = 0;         // +0x14
    int growBy_ = -1;               // +0x18
    int movable_ = 0;               // +0x1c
};

// ---------------------------------------------------------------------------------------------
// LinkItem (class id 0x20001) / LinkList (class id 0x20002): intrusive doubly linked list. Item: +8 next, +0xc prev, +0x10 list.
// List: +8 first, +0xc last, +0x10 count. Items are owned by whoever created them; the list deletes the remaining ones when it dies.
class LinkList;

class LinkItem : public RObject {
public:
    static constexpr uintptr_t kAtFirst = 0, kAtLast = 1; // `pos`: 0 = head, 1 = tail, otherwise "after that item"
    // Aaron ord 40 0x10001b74: LinkItem(list, pos) -> Link (FUN_100019b0).
    LinkItem(LinkList* list, uintptr_t pos);
    ~LinkItem() override;                                  // Aaron ord 73 0x10001be1 (Unlink: FUN_10001ab2)
    u32 GetObjClassId() const override { return CID_LISTITEM; } // Aaron ord 382 0x10001e40
    // Aaron ord 443 0x10001c33 (Reposition): if pos != this: unlink and link again in the same list at `pos`.
    void Reposition(uintptr_t pos);
    LinkItem* Next() const { return next_; }
    LinkItem* Prev() const { return prev_; }
    LinkList* List() const { return list_; }

private:
    friend class LinkList;
    void Link(LinkList* list, uintptr_t pos);              // Aaron 0x100019b0
    void Unlink();                                         // Aaron 0x10001ab2
    LinkItem* next_ = nullptr; // +8
    LinkItem* prev_ = nullptr; // +0xc
    LinkList* list_ = nullptr; // +0x10
};

class LinkList : public RObject {
public:
    LinkList() = default;                                  // Aaron ord 39 0x10001c64
    ~LinkList() override;                                  // Aaron ord 72 0x10001c96 (DeleteAll)
    u32 GetObjClassId() const override { return CID_LIST; } // Aaron ord 381 0x10001e30
    // Aaron ord 155 0x10001ce8: deletes the items from the LAST one (each item's destructor unlinks it) until the list is empty.
    void DeleteAll();
    // Aaron ord 189 0x10001d2d: fn(item, a, b) on each item while fn returns nonzero; returns the last result (1 if empty).
    // QUIRK (objdump 0x10001d75): the original Releases the NEXT item after the call (see TreeNode::ForEachChild); the port does not.
    using ForEachFn = int (*)(LinkItem* item, void* a, void* b);
    int ForEach(ForEachFn fn, void* a, void* b);
    // Aaron ord 419 0x10001d88: true if `item` is linked in this list (walks from the head).
    bool Contains(const LinkItem* item) const;
    LinkItem* First() const { return first_; }
    LinkItem* Last() const { return last_; }
    int Count() const { return count_; }

private:
    friend class LinkItem;
    LinkItem* first_ = nullptr; // +8
    LinkItem* last_ = nullptr;  // +0xc
    int count_ = 0;             // +0x10
};

} // namespace aaron
