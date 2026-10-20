// CResfile: reader of one .rrs archive (moses.dll 0x1000c2e0..0x1000c8e0). Package 2.
// Archive parsing itself is platform/Rrs.cpp (rrs::Archive); this class adds the CResfile API.
#include "moses/ResMgr.h"

#include <cstring>

namespace moses {

// moses.dll 0x1000c260: zero-fills the 0x58 bytes, chunk size 0x4000
CResfile::CResfile() {}

// moses.dll 0x100022a0
CResfile::~CResfile() { CloseFile(); }

// moses.dll 0x10002340 (see ResMgr.h): the port re-opens the archive instead of sharing the CFileIO
CResfile::CResfile(const CResfile& o) { *this = o; }
CResfile& CResfile::operator=(const CResfile& o) {
    if (this == &o) return *this;
    CloseFile();
    audio_ = o.audio_;
    loadSize_ = o.loadSize_;
    loadProc_ = o.loadProc_;
    loadCtx_ = o.loadCtx_;
    if (o.open_) OpenFile(o.archive_.Path().c_str(), o.loadSize_);
    return *this;
}

// moses.dll 0x1000c5f0. The archive is loaded in memory by rrs::Archive; a CFileIO stays open on the file for the raw
// accessors (load / LoadKeyResource / load_palette).  `chunk` = size of the progress chunks of `load`.
int CResfile::OpenFile(const char* path, u32 chunk) {
    if (open_) CloseFile();
    if (chunk) loadSize_ = chunk;
    std::string err;
    if (!archive_.Open(path ? path : "", &err)) {
        open_ = 0;
        return 0;
    }
    open_ = 1;
    io_.reset(new CFileIO(path));
    if (io_->Open(0)) {
        // header: 5 key resources {offset,size} at 0x10 + 8k (rrs_format.md sec. 1)
        u8 hdr[0x38];
        if (io_->Read(hdr, sizeof hdr)) {
            for (int k = 0; k < 5; ++k) {
                const u8* p = hdr + 0x10 + 8 * k;
                key_[k].off = u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24;
                key_[k].size = u32(p[4]) | u32(p[5]) << 8 | u32(p[6]) << 16 | u32(p[7]) << 24;
            }
        }
    }
    return 1;
}

// moses.dll CResfile::CloseFile
int CResfile::CloseFile() {
    archive_ = rrs::Archive();
    io_.reset();
    for (auto& k : key_) k.off = k.size = 0;
    open_ = 0;
    return 1;
}

// moses.dll 0x1000c730
RESLOADPROC CResfile::SetLoadProc(RESLOADPROC proc, void* ctx) {
    RESLOADPROC old = loadProc_;
    loadProc_ = proc;
    loadCtx_ = ctx;
    return old;
}

// moses.dll 0x1000c400
void* CResfile::LoadKeyResource(RESKEY key, u32* size) {
    if (!io_ || !io_->IsOpen()) return nullptr;
    if (int(key) < 0 || int(key) > 4) return nullptr;
    u32 n = key_[key].size;
    if (size) *size = n;
    if (n == 0) return nullptr;
    static CMem mem(0, 0);
    void* p = mem.Alloc(n, 0);
    if (!p) return nullptr;
    io_->Seek(long(key_[key].off), 0);
    if (!io_->Read(p, n)) p = mem.Free(p);
    return p;
}

// moses.dll 0x1000c030
int CResfile::load(void* buf, u32 offset, u32 n) {
    if (!io_) return 0;
    if (io_->Seek(long(offset), 0) != offset) return 0;
    if (!loadProc_) return io_->Read(buf, n);
    for (;;) {
        u32 chunk = n < loadSize_ ? n : loadSize_;
        if (!io_->Read(buf, chunk)) return 0;
        loadProc_(this, loadCtx_);
        n -= chunk;
        if (n == 0) return 1;
        offset += n; // ORIGINAL BUG: should be `+= chunk` (the remaining count is added)
        buf = static_cast<u8*>(buf) + chunk;
        if (io_->Seek(long(offset), 0) != offset) return 0;
    }
}

// moses.dll 0x1000c180
std::unique_ptr<CDibPal> CResfile::load_palette(u32 id) {
    u32 i = id & 0xffff;
    if (!archive_.Valid(i)) return nullptr;
    const rrs::Record& r = archive_.Rec(i);
    u32 n = r.size;
    static CMem mem(0, 0);
    u32* quad = static_cast<u32*>(mem.Alloc(n, 0));
    if (!quad) return nullptr;
    if (!load(quad, r.offset, n)) {
        mem.Free(quad);
        return nullptr;
    }
    std::unique_ptr<CDibPal> pal(new CDibPal(quad, n >> 2));
    mem.Free(quad);
    return pal;
}

// moses.dll 0x1000c370 (all three accessors mask id & 0xffff)
u32 CResfile::GetRecOffset(u32 id) const {
    u32 i = id & 0xffff;
    return archive_.Valid(i) ? archive_.Rec(i).offset : 0;
}
// moses.dll 0x1000c3a0
u32 CResfile::GetRecSize(u32 id) const {
    u32 i = id & 0xffff;
    return archive_.Valid(i) ? archive_.Rec(i).size : 0;
}
// moses.dll 0x1000c3d0 (raw type, alias not followed)
RESTYPE CResfile::GetRecType(u32 id) const {
    u32 i = id & 0xffff;
    return archive_.Valid(i) ? RESTYPE(archive_.Rec(i).type) : RESTYPE(0);
}

// moses.dll 0x1000c2e0: the RAW type must be 6, 8, 9 or 11 (an alias to a group fails: original quirk);
// then the group is read following aliases (LoadResource).
int CResfile::GetIndexedResId(u32* out, u32 groupId, u32 i) const {
    RESTYPE t = GetRecType(groupId);
    if (t != RT_GROUP && t != RT_GROUP2 && t != RT_DIBLIST && t != RT_DIBTILE) return 0;
    std::vector<u32> ids;
    if (!archive_.GetGroup(groupId & 0xffff, ids)) return 0;
    if (i >= ids.size()) return 0;
    if (out) *out = ids[i];
    return 1;
}

// moses.dll 0x1000c590: raw type must be 12; copies min(size, n) like lstrcpynA (at most n-1 chars + NUL)
// and returns that count (the original returns param_3 after clamping to the record size).
u32 CResfile::LoadStringA(u32 id, char* buf, u32 n) const {
    u32 i = id & 0xffff;
    if (!archive_.Valid(i)) return 0;
    const rrs::Record& r = archive_.Rec(i);
    if (r.type != RT_STRING) return 0;
    const u8* p;
    u32 size;
    if (!archive_.GetPool(i, p, size)) return 0;
    if (size < n) n = size;
    if (buf && n) {
        u32 k = 0;
        while (k + 1 < n && p[k]) { buf[k] = char(p[k]); ++k; }
        buf[k] = 0;
    }
    return n;
}

// moses.dll 0x1000c0d0 (load_dib): DIBREC + RLE decoded once (platform DecodeDib).
std::unique_ptr<CDibData> CResfile::load_dib(u32 id) const {
    const u8* p;
    u32 size;
    if (archive_.TypeOf(id & 0xffff) != RT_DIB || !archive_.GetData(id & 0xffff, p, size)) return nullptr;
    ::Dib d;
    if (!DecodeDib(p, size, d)) return nullptr;
    return std::unique_ptr<CDibData>(new CDibData(d));
}

// moses.dll 0x1000c8e0 (the copy + OffsetPos part): CDibData copy with the new hot-spot.
// SIGNS: OffsetPos(ref.x - origin.x, ref.y - origin.y), exactly as in the decompiled code.
std::unique_ptr<CDibData> CResfile::load_dibref(u32 id, CDibData* target) const {
    rrs::DibRefInfo info;
    if (!target || !load_dibref_info(id, info)) return nullptr;
    std::unique_ptr<CDibData> c(new CDibData(*target));
    c->OffsetPos(int(info.x) - c->Origin().x, int(info.y) - c->Origin().y);
    return c;
}

int CResfile::load_dibref_info(u32 id, rrs::DibRefInfo& out) const {
    return archive_.GetDibRef(id & 0xffff, out) ? 1 : 0;
}

// moses.dll 0x1000c220 (load_sound) -> CAudioLib::SoundCreate
CAudioSnd* CResfile::load_sound(u32 id) const {
    if (!audio_) return nullptr;
    const u8* p;
    u32 size;
    if (archive_.TypeOf(id & 0xffff) != RT_SOUND || !archive_.GetData(id & 0xffff, p, size)) return nullptr;
    ::Sound s;
    if (!LoadSound(p, size, s)) return nullptr;
    return audio_->SoundCreate(std::move(s));
}

// type 3 (raw blob)
const u8* CResfile::LoadRaw(u32 id, u32* size) const {
    const u8* p;
    u32 sz;
    if (archive_.TypeOf(id & 0xffff) != RT_RAW || !archive_.GetData(id & 0xffff, p, sz)) return nullptr;
    if (size) *size = sz;
    return p;
}

int CResfile::GetGroup(u32 id, std::vector<u32>& ids) const {
    return archive_.GetGroup(id & 0xffff, ids) ? 1 : 0;
}

// moses.dll 0x1000c490: raw bytes (file types 2..5) or pool pointer (6..12); aliases followed.
void* CResfile::LoadResource(u32 id, u32* size) const {
    u32 i = id & 0xffff;
    const u8* p = nullptr;
    u32 sz = 0;
    uint16_t t = archive_.TypeOf(i);
    bool ok = false;
    if (t >= RT_DIB && t <= RT_SOUND) ok = archive_.GetData(i, p, sz);
    else if (t >= RT_GROUP && t <= RT_STRING && t != 10) ok = archive_.GetPool(i, p, sz);
    if (!ok) return nullptr;
    if (size) *size = sz;
    return const_cast<u8*>(p);
}

} // namespace moses
