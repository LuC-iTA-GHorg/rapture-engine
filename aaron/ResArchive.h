// ResArchive + ResTable: how Aaron maps 32-bit resource ids to archives (moses.md sec. 7, aaron.md sec. 2.5).
//
//   id = slot << 24 | class << 16 | index
// Scene::OpenArchive(slot, "mus.rrs") binds a ResArchive to a slot of the scene's ResTable (256 slots).
// ResArchives are shared between scenes (key = path, Application keeps the list) and refcounted.
// Entry refcount ("Aaron's refcount") sits on top of CResMgr's own refcount: Aaron increments its own,
// calls CResMgr::Load only on the 0->1 transition and Free on 1->0.
#pragma once

#include "aaron/RObject.h"
#include "moses/ResMgr.h"

#include <string>
#include <vector>

namespace aaron {

// ResArchive (0x180 bytes): CResMgr + RObject. Aaron ord 84? no - class id 0x20084.
class ResArchive : public RObject, public moses::CResMgr {
public:
    ResArchive(moses::CAudioLib* audio) : moses::CResMgr(audio) {}
    virtual u32 GetClassId() const { return CID_RESARCHIVE; } // Aaron ord 385 0x1000c160 (vtable slot 1: 0x20084)
    // Opens (relative to `baseDir` if given, else the current directory = game folder). false on failure.
    bool OpenPath(const char* path, const char* baseDir);
    const std::string& FullPath() const { return fullPath_; }

private:
    std::string fullPath_; // normalized key used for sharing (CombinePath, Aaron ord 438)
};

class ResTable {
public:
    struct Entry {
        void* obj = nullptr;  // loaded object (as returned by CResMgr::Load)
        u32 refcount = 0;     // Aaron-side count
    };
    struct Slot {
        ResArchive* archive = nullptr;     // not owned (Release()d by CloseSlot)
        std::vector<Entry> entries;        // indexed by id & 0xffff (size = number of records)
    };
    static constexpr int kSlots = 256;

    ResTable() : slots_(kSlots) {}
    Slot& At(u32 slot) { return slots_[slot & 0xff]; }
    // open: binds `archive` (AddRef) to `slot`, sizing `entries`; closing (archive==nullptr) frees every entry.
    void Bind(u32 slot, ResArchive* archive);
    void* Load(u32 id);                // refcount++, loads on first use; nullptr if the slot/record is missing. Aaron ord 427 0x1000b6d3
    void Free(u32 id);                 // refcount--, frees at 0. Aaron ord 205 0x1000b699 (the archive's vtable Free thunk, returns 0)
    // Aaron ord 192 0x1000ba17: fn(slotIndex, a, b) for every slot that has an archive; stops at the first 0 (returns 0), else returns 1.
    using SlotFn = int (*)(u32 slot, void* a, void* b);
    int ForEachSlot(SlotFn fn, void* a, void* b) const;
    // Aaron ord 193 0x1000ba76: fn(&info, a, b) for the resources of the slots [firstSlot .. lastSlot] (low bytes) that are loaded - or all of them
    // when `includeUnloaded` is nonzero. info.id = slot << 24 | (type & 0x3f | 0x80) << 16 | index, info.type = RESTYPE, info.recSize = the record
    // size, info.refcount = the Aaron-side count with bit 31 set when the resource is loaded. fn returning 0 stops the walk (returns 0).
    struct ResInfo { u32 id, type, recSize, refcount; };
    using ResFn = int (*)(const ResInfo* info, void* a, void* b);
    int ForEachResource(ResFn fn, u32 firstSlot, u32 lastSlot, int includeUnloaded, void* a, void* b);
    // Aaron ord 352 0x1000b6b9 / ord 580 0x1000b75d (static): the two process-wide words DAT_10023ccc / DAT_10023cd0 ("last loaded resource"
    // bookkeeping of Aaron's resource callback; nothing in the DLL ever writes them, so they stay 0). 580 clears the second one.
    static void GetLastLoad(u32* a, u32* b);
    static void ResetLastLoad();
    ResArchive* Archive(u32 id) { return slots_[ResSlot(id) & 0xff].archive; }
    // Resolves "group + index" ids (class without kResDirect): CResMgr::GetIndexedResId. Direct ids are returned unchanged.
    u32 GetIndexedResId(u32 groupId, u32 n);

private:
    std::vector<Slot> slots_;
};

} // namespace aaron
