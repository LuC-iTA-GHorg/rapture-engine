// MemBuffer, LinkItem / LinkList (WP-4). See Containers.h.
#include "aaron/Containers.h"

#include <cstdlib>
#include <cstring>

namespace aaron {

namespace {
// DAT_10023c88 reserved, DAT_10023c8c committed, DAT_10023c90 used, DAT_10023c94 live buffers
long g_reserved = 0, g_committed = 0, g_used = 0, g_buffers = 0;

unsigned round_up_page(size_t n) { return unsigned((n + MemBuffer::kPage - 1) / MemBuffer::kPage * MemBuffer::kPage); }
} // namespace

// ---------------------------------------------------------------------------------------------
MemBuffer::MemBuffer(size_t reserveBytes, int growBy) { // ord 4 0x10001100
    growBy_ = growBy < 0 ? -1 : growBy;
    if (reserveBytes != 0) {
        unsigned r = round_up_page(reserveBytes); // VirtualAlloc(MEM_RESERVE) + VirtualQuery(RegionSize)
        base_ = static_cast<unsigned char*>(std::calloc(r, 1));
        if (base_) {
            reserved_ = r;
            g_reserved += long(reserved_);
        }
    }
    Unlock(); // FUN_10001943 (ord 709)
    ++g_buffers;
}

MemBuffer::~MemBuffer() { // ord 60 0x100011ff
    Free();
    --g_buffers;
}

void MemBuffer::Free() { // ord 201 0x10001444
    if (base_) {
        std::free(base_);
        base_ = nullptr;
        g_used -= long(used_);
        g_reserved -= long(reserved_);
        g_committed -= long(committed_);
        used_ = committed_ = reserved_ = 0;
    }
}

int MemBuffer::SetSize(unsigned newSize, unsigned growBy) { // ord 661 0x10001727
    if (committed_ < newSize) {
        if (reserved_ < newSize) {
            if (!movable_ || growBy == 0xffffffffu) return 0;
            if (growBy == 0) growBy = growBy_ < 1 ? reserved_ >> 1 : unsigned(growBy_);
            unsigned want = reserved_ + growBy < newSize ? newSize : reserved_ + growBy;
            unsigned r = round_up_page(want);
            unsigned char* nb = static_cast<unsigned char*>(std::calloc(r, 1));
            if (!nb) return 0;
            if (used_) std::memcpy(nb, base_, used_);
            Free(); // statistics of the old region; used_/committed_/reserved_ -> 0
            base_ = nb;
            reserved_ = r;
            g_reserved += long(reserved_);
        } else {
            g_committed -= long(committed_);
        }
        committed_ = round_up_page(newSize); // VirtualQuery(base).RegionSize of the committed prefix
        if (committed_ > reserved_) committed_ = reserved_;
        g_committed += long(committed_);
    } else if (newSize < used_) {
        std::memset(base_ + newSize, 0, used_ - newSize);
    }
    g_used += long(newSize) - long(used_);
    used_ = newSize;
    return 1;
}

int MemBuffer::Append(const void* data, unsigned n, unsigned growBy) { // ord 145 0x10001260
    unsigned old = used_;
    if (!SetSize(used_ + n, growBy)) return -1;
    if (n) std::memcpy(base_ + old, data, n);
    return int(used_); // QUIRK: the new used size, not `old`
}

void MemBuffer::AppendString(const char* s, unsigned growBy) { // ord 147 0x100012be
    Append(s, unsigned(std::strlen(s)) + 1, growBy);
}

int MemBuffer::AppendLine(const char* s, unsigned growBy) { // ord 146 0x100012e9
    if (Append(s, unsigned(std::strlen(s)), growBy) != -1) {
        if (Append("\r\n", 2, growBy) != -1) return 1;
    }
    return 0;
}

int MemBuffer::Insert(unsigned offset, const void* data, unsigned n, unsigned growBy) { // ord 403 0x10001557
    if (offset >= used_) return Append(data, n, growBy);
    unsigned oldUsed = used_;
    if (!SetSize(used_ + n, growBy)) return -1;
    std::memmove(base_ + offset + n, base_ + offset, oldUsed - offset);
    if (n) std::memcpy(base_ + offset, data, n);
    return int(offset + n); // QUIRK: offset + n
}

void MemBuffer::InsertString(unsigned offset, const char* s, unsigned growBy) { // ord 405 0x100015f9
    Insert(offset, s, unsigned(std::strlen(s)) + 1, growBy);
}

unsigned MemBuffer::Remove(unsigned offset, int n) { // ord 567 0x10001643
    if (offset >= used_) return 0xffffffffu;
    unsigned cnt;
    if (n < 0) cnt = used_ - offset;
    else cnt = int(used_ - offset) > n ? unsigned(n) : used_ - offset;
    std::memmove(base_ + offset, base_ + offset + cnt, used_ - offset - cnt);
    SetSize(used_ - cnt, 0);
    return offset;
}

unsigned MemBuffer::RemoveString(unsigned offset) { // ord 571 0x100016ea
    if (offset >= used_) return 0xffffffffu;
    return Remove(offset, int(std::strlen(reinterpret_cast<const char*>(base_ + offset))) + 1);
}

void MemBuffer::Decommit() { // ord 161 0x1000134b
    if (!base_) return;
    unsigned keep = used_ ? round_up_page(used_) : 0; // ((used - 1 + page) / page) * page
    unsigned oldCommitted = committed_;
    if (keep < oldCommitted) std::memset(base_ + keep, 0, oldCommitted - keep); // decommitted pages come back zeroed
    committed_ = keep < committed_ ? keep : committed_;
    g_committed -= long(oldCommitted - committed_);
}

void MemBuffer::Reset(bool decommit) { // ord 180 0x100013ef
    g_used -= long(used_);
    if (base_ && used_) std::memset(base_, 0, used_);
    used_ = 0;
    if (decommit) Decommit();
    if (base_ && committed_) std::memset(base_, 0, committed_);
}

unsigned MemBuffer::Info(unsigned* used, unsigned* reserved, unsigned* committed) const { // ord 351 0x10001511
    if (used) *used = used_;
    if (reserved) *reserved = reserved_;
    if (committed) *committed = committed_;
    return used_;
}

MemBuffer::Totals MemBuffer::Stats() { // ord 303 0x100014e6
    return Totals{unsigned(g_reserved), unsigned(g_committed), unsigned(g_used), unsigned(g_buffers)};
}

// ---------------------------------------------------------------------------------------------
LinkItem::LinkItem(LinkList* list, uintptr_t pos) { Link(list, pos); } // ord 40 0x10001b74

LinkItem::~LinkItem() { Unlink(); } // ord 73 0x10001be1

void LinkItem::Link(LinkList* list, uintptr_t pos) { // FUN_100019b0
    if (!list) return; // the original dereferences the list unconditionally
    if (!list->first_) {
        prev_ = next_ = nullptr;
        list->last_ = list->first_ = this;
    } else if (pos == kAtFirst) {
        next_ = list->first_;
        prev_ = nullptr;
        list->first_ = this;
        next_->prev_ = this;
    } else if (pos == kAtLast) {
        next_ = nullptr;
        prev_ = list->last_;
        list->last_ = this;
        prev_->next_ = this;
    } else {
        LinkItem* after = reinterpret_cast<LinkItem*>(pos);
        next_ = after->next_;
        prev_ = after;
        after->next_ = this;
        if (!next_) list->last_ = this; else next_->prev_ = this;
    }
    list_ = list;
    ++list->count_;
}

void LinkItem::Unlink() { // FUN_10001ab2
    if (!list_ || (!next_ && !prev_ && list_->first_ != this)) return; // (port) not linked
    if (list_->first_ == this) {
        if (!next_) list_->last_ = nullptr; else next_->prev_ = nullptr;
        list_->first_ = next_;
    } else if (list_->last_ == this) {
        prev_->next_ = nullptr;
        list_->last_ = prev_;
    } else {
        next_->prev_ = prev_;
        prev_->next_ = next_;
    }
    prev_ = next_ = nullptr;
    --list_->count_;
}

void LinkItem::Reposition(uintptr_t pos) { // ord 443 0x10001c33
    if (reinterpret_cast<LinkItem*>(pos) != this) {
        LinkList* l = list_;
        Unlink();
        Link(l, pos);
    }
}

// ---------------------------------------------------------------------------------------------
LinkList::~LinkList() { DeleteAll(); } // ord 72 0x10001c96

void LinkList::DeleteAll() { // ord 155 0x10001ce8
    while (last_) delete last_; // ~LinkItem unlinks it, so `last_` moves backwards
}

int LinkList::ForEach(ForEachFn fn, void* a, void* b) { // ord 189 0x10001d2d
    int r = 1;
    LinkItem* it = first_;
    while (it && r) {
        LinkItem* next = it->next_;
        r = fn(it, a, b);
        it = next;
    }
    return r;
}

bool LinkList::Contains(const LinkItem* item) const { // ord 419 0x10001d88
    const LinkItem* it = first_;
    while (it && it != item) it = it->next_;
    return it != nullptr;
}

} // namespace aaron
