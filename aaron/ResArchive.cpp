// ResArchive + ResTable (moses.md sec. 7, aaron.md sec. 2.5). Package 3.
// Aaron ResArchive::ResArchive 0x1000b4e8, ResTable::Load/Release (FUN_1000b470 callers), ResTable::Bind FUN_1000bf6a/1000b97a,
// GetIndexedResId FUN_1000bd75.
#include "aaron/ResArchive.h"

namespace aaron {

bool ResArchive::OpenPath(const char* path, const char* baseDir) {
    if (!path || !*path) return false;
    std::string full = path;
    if (baseDir && *baseDir) {
        bool abs = full.size() > 1 && (full[1] == ':' || full[0] == '/' || full[0] == '\\');
        if (!abs) full = std::string(baseDir) + "/" + full;
    }
    for (char& c : full) if (c == '\\') c = '/';
    if (!Open(full.c_str())) return false; // CResMgr::Open
    fullPath_ = full;
    return true;
}

// Binds `archive` to `slot` (AddRef), closing the previous binding first; archive == nullptr only closes the slot.
void ResTable::Bind(u32 slot, ResArchive* archive) { // FUN_1000bf6a / FUN_1000b97a
    Slot& s = At(slot);
    if (s.archive) {
        for (u32 i = 0; i < s.entries.size(); ++i) {
            Entry& e = s.entries[i];
            if (e.refcount && e.obj) s.archive->Free(u32(slot) << 24 | i, nullptr);
            e = Entry{};
        }
        s.entries.clear();
        s.archive->Release();
        s.archive = nullptr;
    }
    if (!archive) return;
    archive->AddRef();
    s.archive = archive;
    s.entries.assign(archive->File().GetNumResources(), Entry{});
}

void* ResTable::Load(u32 id) { // refcount++, CResMgr::Load on the 0->1 transition
    if (id == kNoRes) return nullptr;
    Slot& s = At(ResSlot(id));
    u32 idx = ResIndex(id);
    if (!s.archive || idx >= s.entries.size()) return nullptr;
    Entry& e = s.entries[idx];
    if (e.refcount == 0) {
        e.obj = s.archive->Load(id, nullptr);
        if (!e.obj) return nullptr;
    }
    ++e.refcount;
    return e.obj;
}

void ResTable::Free(u32 id) { // ResTable::Release: refcount--, CResMgr::Free on 1->0
    if (id == kNoRes) return;
    Slot& s = At(ResSlot(id));
    u32 idx = ResIndex(id);
    if (!s.archive || idx >= s.entries.size()) return;
    Entry& e = s.entries[idx];
    if (e.refcount == 0) return;
    if (--e.refcount == 0) {
        s.archive->Free(id, nullptr);
        e.obj = nullptr;
    }
}

// FUN_1000bd75: "group + index" id -> direct id of the n-th member. (Scene::OnResProc only calls it for ids without kResDirect.)
// The class byte of the result is 0x80 | the member's rrs type (aaron/Types.h), the slot is the group's.
u32 ResTable::GetIndexedResId(u32 groupId, u32 n) {
    Slot& s = At(ResSlot(groupId));
    if (!s.archive) return kNoRes;
    u32 out = 0;
    if (!s.archive->GetIndexedResId(&out, groupId, n)) return kNoRes;
    u32 type = u32(s.archive->GetResType(out)) & 0x7f;
    return MakeResId(ResSlot(groupId), 0x80 | type, out & 0xffff);
}

int ResTable::ForEachSlot(SlotFn fn, void* a, void* b) const { // ord 192 0x1000ba17
    for (u32 i = 0; i < u32(kSlots); ++i)
        if (slots_[i].archive && fn(i, a, b) == 0) return 0;
    return 1;
}

int ResTable::ForEachResource(ResFn fn, u32 firstSlot, u32 lastSlot, int includeUnloaded, void* a, void* b) { // ord 193 0x1000ba76
    for (u32 slot = firstSlot & 0xff; slot <= (lastSlot & 0xff); ++slot) {
        Slot& s = slots_[slot];
        if (!s.archive) continue;
        for (u32 i = 0; i < s.entries.size(); ++i) {
            u32 rid = slot << 24 | i;
            bool loaded = s.archive->IsLoaded(rid) != 0;
            if (!loaded && !includeUnloaded) continue;
            ResInfo info;
            u32 type = u32(s.archive->GetResType(rid));
            info.id = slot << 24 | ((type & 0x3f) | 0x80) << 16 | (i & 0xffff);
            info.type = type;
            info.recSize = s.archive->File().GetRecSize(rid);
            info.refcount = s.entries[i].refcount | (loaded ? 0x80000000u : 0u);
            if (fn(&info, a, b) == 0) return 0;
        }
    }
    return 1;
}

static u32 g_lastLoadA = 0, g_lastLoadB = 0; // DAT_10023ccc / DAT_10023cd0
void ResTable::GetLastLoad(u32* a, u32* b) { *a = g_lastLoadA; *b = g_lastLoadB; } // ord 352 0x1000b6b9
void ResTable::ResetLastLoad() { g_lastLoadB = 0; }                                // ord 580 0x1000b75d

} // namespace aaron
