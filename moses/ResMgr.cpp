// CResMgr: refcounted object cache over a CResfile (moses.dll 0x1000c750..0x1000d010). Package 2.
//
// NOTE GetObj (0x1000ce10): returns the already loaded object WITHOUT touching the refcount; if absent it
// calls the virtual Load (which increments). So GetObj of an unloaded id leaves refcount 1, of a loaded id
// leaves it unchanged. Verified on the decompiled code, preserved.
// NOTE GetResType (0x1000ce50): the original returns the RAW record type (an alias gives 1); the header
// comment "follows aliases" is not what the decompiled code does. Preserved.
#include "moses/ResMgr.h"

#include <cstring>

namespace moses {

CResMgr::CResMgr(CAudioLib* audio) : audio_(audio) { file_.SetAudio(audio); }  // moses.dll 0x1000cc80

CResMgr::~CResMgr() { Close(); }

// moses.dll 0x10002410 (see ResMgr.h)
CResMgr& CResMgr::operator=(const CResMgr& o) {
    if (this == &o) return *this;
    Close();
    audio_ = o.audio_;
    file_.SetAudio(o.audio_);
    if (o.loaded_ != 0 && !o.path_.empty()) Open(o.path_.c_str());
    return *this;
}

// moses.dll 0x1000d010: only when no table yet.
int CResMgr::Open(const char* path) {
    if (loaded_ != 0) return 0;
    if (!file_.OpenFile(path, 0)) return 0;
    u32 n = file_.GetNumResources();
    if (!grow(n)) return 0;
    loaded_ = n;
    path_ = path;
    return 1;
}

// moses.dll CResMgr::grow
int CResMgr::grow(u32 n) {
    table_.assign(n, RESREC());
    return 1;
}

// moses.dll 0x1000ccc0
int CResMgr::Close() {
    FreeAll(nullptr);
    table_.clear();
    loaded_ = 0;
    return file_.CloseFile();
}

// moses.dll 0x1000ced0
void* CResMgr::Load(u32 id, void* ctx) {
    u32 idx = id & 0xffff;
    if (idx >= loaded_) return nullptr;
    if (file_.GetRecType(id) == RT_ALIAS) {
        id = file_.GetRecOffset(id);
        idx = id & 0xffff;
        if (idx >= loaded_) return nullptr;
    }
    if (table_[idx].obj == nullptr) {
        if (!load_id(id, ctx)) return nullptr;
    }
    table_[idx].refcount++;
    return table_[idx].obj;
}

// moses.dll 0x1000ce10
void* CResMgr::GetObj(u32 id, void* ctx) {
    if ((id & 0xffff) >= loaded_) return nullptr;
    void* p = table_[id & 0xffff].obj;
    if (!p) p = Load(id, ctx);
    return p;
}

// moses.dll 0x1000cd00
void* CResMgr::Free(u32 id, void* ctx) {
    u32 idx = id & 0xffff;
    if (idx < loaded_) {
        if (file_.GetRecType(id) == RT_ALIAS) {
            id = file_.GetRecOffset(id);
            idx = id & 0xffff;
            if (idx >= loaded_) return nullptr;
        }
        RESREC& r = table_[idx];
        if (r.refcount != 0 && --r.refcount == 0) free_id(id, ctx);
    }
    return nullptr;
}

// moses.dll 0x1000cd70
void CResMgr::FreeAll(void* ctx) {
    for (u32 i = 0; i < table_.size(); ++i) {
        if (table_[i].refcount != 0) {
            table_[i].refcount = 0;
            free_id(i, ctx);
        }
    }
}

// moses.dll 0x1000cf50
int CResMgr::LoadAll(void* ctx) {
    for (u32 i = 0; i < table_.size(); ++i)
        if (!Load(i, ctx)) return 0;
    return 1;
}

// moses.dll 0x1000cf90: on failure frees what was loaded so far and returns FreeList's result (0)
int CResMgr::LoadList(u32* ids, u32 n, void* ctx) {
    for (u32 i = 0; i < n; ++i) {
        if (!Load(ids[i], ctx)) return FreeList(ids, i, ctx);
    }
    return 1;
}

// moses.dll 0x1000cdb0
int CResMgr::FreeList(u32* ids, u32 n, void* ctx) {
    for (u32 i = 0; i < n; ++i) Free(ids[i], ctx);
    return 0;
}

RESTYPE CResMgr::GetResType(u32 id) { // moses.dll 0x1000ce50
    if ((id & 0xffff) < table_.size()) return file_.GetRecType(id);
    return RESTYPE(0);
}

int CResMgr::IsLoaded(u32 id) { // moses.dll 0x1000ce80
    if (file_.GetRecType(id) == RT_ALIAS) id = file_.GetRecOffset(id);
    if ((id & 0xffff) < loaded_) return table_[id & 0xffff].obj != nullptr;
    return 0;
}

int CResMgr::GetIndexedResId(u32* out, u32 groupId, u32 i) { return file_.GetIndexedResId(out, groupId, i); } // 0x1000cdf0
u32 CResMgr::LoadStringA(u32 id, char* buf, u32 n) { return file_.LoadStringA(id, buf, n); }                  // 0x1000cff0

// moses.dll 0x1000ca60
int CResMgr::load_group(const std::vector<u32>& ids, void* ctx) {
    std::vector<u32> tmp = ids;
    return LoadList(tmp.data(), u32(tmp.size()), ctx);
}

// moses.dll 0x1000c7b0
void CResMgr::free_group(GROUPREC* g, void* ctx) {
    if (g) FreeList(g->ids.data(), u32(g->ids.size()), ctx);
}

// moses.dll 0x1000c8e0: loads the target through the virtual Load (refcount+1), copies it, OffsetPos.
CDibObj* CResMgr::load_dibref(u32 id, void* ctx) {
    rrs::DibRefInfo info;
    if (!file_.load_dibref_info(id, info)) return nullptr;
    CDibObj* t = static_cast<CDibObj*>(Load(info.targetId, ctx));
    if (!t) return nullptr;
    // the target of a DIBREF must be a CDibData (type 2); anything else is bad data
    if (file_.Archive().TypeOf(info.targetId & 0xffff) != RT_DIB) {
        Free(info.targetId, ctx);
        return nullptr;
    }
    std::unique_ptr<CDibData> c = file_.load_dibref(id, static_cast<CDibData*>(t));
    if (!c) { Free(info.targetId, ctx); return nullptr; }
    return c.release();
}

// moses.dll 0x1000c850: members are loaded (refcount+1 each); the CDibList does not own them.
CDibObj* CResMgr::load_diblist(u32 id, void* ctx) {
    std::vector<u32> ids;
    if (!file_.GetGroup(id, ids)) return nullptr;
    std::unique_ptr<CDibList> l(new CDibList(u32(ids.size())));
    if (!LoadList(ids.data(), u32(ids.size()), ctx)) return nullptr;
    for (u32 m : ids) l->AddDib(static_cast<CDibObj*>(table_[m & 0xffff].obj));
    return l.release();
}

// moses.dll 0x1000c970: cells are loaded (refcount+1 each, must be type 2) and COPIED into the tile (SetDib).
// A failure frees the half-built tile. The cell ids stay referenced until free_id (free_dibobj releases them).
CDibObj* CResMgr::load_dibtile(u32 id, void* ctx) {
    rrs::DibTileInfo hdr;
    std::vector<u32> ids;
    if (!file_.Archive().GetDibTile(id & 0xffff, hdr) || !file_.GetGroup(id, ids)) return nullptr;
    std::unique_ptr<CDibTile> t(new CDibTile());
    CDibTile::HDR h{hdr.cols, hdr.rows, hdr.tileW, hdr.tileH, hdr.x, hdr.y};
    if (!t->Init(h)) return nullptr;
    if (!LoadList(ids.data(), u32(ids.size()), ctx)) return nullptr; // LoadList releases what it loaded
    bool ok = true;
    for (u32 i = 0; i < ids.size() && ok; ++i) {
        if (ids[i] == 0) continue; // empty cell
        ok = file_.Archive().TypeOf(ids[i] & 0xffff) == RT_DIB &&
             t->SetDib(static_cast<CDibData*>(table_[ids[i] & 0xffff].obj), i) != 0;
    }
    if (!ok) { FreeList(ids.data(), u32(ids.size()), ctx); return nullptr; }
    return t.release();
}

// moses.dll 0x1000c750
void CResMgr::free_dibobj(RESREC&, u32 id, RESTYPE type, void* ctx) {
    if (type == RT_DIBLIST || type == RT_DIBTILE) {
        std::vector<u32> ids;
        if (file_.GetGroup(id, ids)) FreeList(ids.data(), u32(ids.size()), ctx);
    } else if (type == RT_DIBREF) {
        rrs::DibRefInfo info;
        if (file_.load_dibref_info(id, info)) Free(info.targetId, ctx);
    }
}

// moses.dll 0x1000cb80 (type switch) + CResfile::LoadResource
int CResMgr::load_id(u32 id, void* ctx) {
    u32 idx = id & 0xffff;
    u16 t = file_.Archive().TypeOf(idx);
    void* obj = nullptr;
    switch (t) {
    case RT_DIB: {
        std::unique_ptr<CDibData> d = file_.load_dib(id);
        obj = static_cast<CDibObj*>(d.release());
        break;
    }
    case RT_PALETTE: obj = file_.load_palette(id).release(); break; // moses.dll 0x1000c180 load_palette
    case RT_RAW: {
        u32 sz = 0;
        const u8* p = file_.LoadRaw(id, &sz);
        if (p) {
            u8* c = new u8[sz ? sz : 1];
            std::memcpy(c, p, sz);
            obj = c;
        }
        break;
    }
    case RT_SOUND: {
        CAudioSnd* s = file_.load_sound(id);
        if (s) {
            audio_->SoundSetId(s, id);
            obj = s;
        }
        break;
    }
    case RT_GROUP:
    case RT_GROUP2: {
        std::unique_ptr<GROUPREC> g(new GROUPREC());
        if (file_.GetGroup(id, g->ids) && load_group(g->ids, ctx)) obj = g.release();
        break;
    }
    case RT_DIBREF: obj = load_dibref(id, ctx); break;
    case RT_DIBLIST: obj = load_diblist(id, ctx); break;
    case RT_DIBTILE: obj = load_dibtile(id, ctx); break;
    case RT_STRING: obj = file_.LoadResource(id, nullptr); break; // pointer into the pool
    default: break; // 4 palette (never used): load fails
    }
    RESREC& r = table_[idx]; // re-fetch: nested loads do not resize the table, but be explicit
    r.obj = obj;
    r.type = t;
    return obj != nullptr;
}

// moses.dll 0x1000ca90
int CResMgr::free_id(u32 id, void* ctx) {
    u32 idx = id & 0xffff;
    if (idx >= table_.size()) return 0;
    void* obj = table_[idx].obj;
    if (obj == nullptr) return 0;
    RESTYPE type = RESTYPE(table_[idx].type);
    table_[idx].obj = nullptr; // clear first: freeing members re-enters the table
    table_[idx].refcount = 0;
    switch (type) {
    case RT_RAW: delete[] static_cast<u8*>(obj); break;
    case RT_SOUND:
        if (audio_) audio_->SoundDelete(static_cast<CAudioSnd*>(obj));
        break;
    case RT_GROUP:
    case RT_GROUP2: {
        GROUPREC* g = static_cast<GROUPREC*>(obj);
        free_group(g, ctx);
        delete g;
        break;
    }
    case RT_DIBREF:
    case RT_DIBLIST:
    case RT_DIBTILE:
        free_dibobj(table_[idx], id, type, ctx);
        delete static_cast<CDibObj*>(obj);
        break;
    case RT_DIB: delete static_cast<CDibObj*>(obj); break;
    case RT_PALETTE: delete static_cast<CDibPal*>(obj); break;
    default: break; // strings point into the pool
    }
    return 0;
}

} // namespace moses
