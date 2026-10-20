// CResfile / CResMgr: resource archives (*.rrs) with reference-counted objects (moses.md sec. 7,
// rrs_format.md). One CResMgr per archive. The archive parsing itself is platform/Rrs.h
// (rrs::Archive); here only the object cache and the typed loaders of the original.
//
// OWNERSHIP: CResMgr owns every object it returns from Load/GetObj (CDibData, CDibList,
// CAudioSnd, byte blobs, group records). Callers never delete them: they call Free(id) and the
// object is destroyed when its refcount reaches 0 (free_id). The refcount is per `id & 0xffff`.
//
// ID RULE: only the low word is the record index. The slot (id >> 24) and the class byte belong to
// Aaron (aaron::Scene::ResTable); CResMgr ignores them.
#pragma once

#include "moses/AudioLib.h"
#include "moses/DibData.h"
#include "moses/FileIO.h"
#include "moses/Types.h"
#include "platform/Rrs.h"

#include <memory>
#include <string>
#include <vector>

namespace moses {

enum RESTYPE : u16 { // = rrs::Type
    RT_ALIAS = 1, RT_DIB = 2, RT_RAW = 3, RT_PALETTE = 4, RT_SOUND = 5, RT_GROUP = 6,
    RT_DIBREF = 7, RT_DIBLIST = 8, RT_DIBTILE = 9, RT_GROUP2 = 11, RT_STRING = 12,
};

// Group record returned by CResMgr::Load for types 6/11 (moses: GROUPREC pointing into the pool).
struct GROUPREC {
    std::vector<u32> ids;
};

// Loaded blob (type 3) and string (type 12) are returned as `const u8*` / `const char*` owned by the manager.

// ---------------------------------------------------------------------------------------------
// CResfile (0x58): the reader of one archive.
// RESKEY of CResfile::LoadKeyResource: the 5 {offset,size} "key" slots of the header (rrs_format.md sec. 1). Only 0
// (record index) and 1 (pool) are used by the shipped archives; 2..4 are always empty.
enum RESKEY : int { RESKEY_INDEX = 0, RESKEY_POOL = 1, RESKEY_2 = 2, RESKEY_3 = 3, RESKEY_4 = 4 };

// CResfile::SetLoadProc callback: called after every chunk of `load` (progress). Original __stdcall int(CResfile*, void*).
using RESLOADPROC = int (*)(class CResfile*, void*);

class CResfile {
public:
    CResfile();                                    // moses.dll 0x1000c260 (chunk size 0x4000)
    ~CResfile();                                   // moses.dll 0x100022a0 (= CloseFile)
    // moses.dll 0x10002340 operator=: the original copies the 0x58 bytes (including the CFileIO pointer, so both objects
    // would close the same file).  Port: the destination re-opens the same archive (if the source was open).
    CResfile(const CResfile& o);
    CResfile& operator=(const CResfile& o);

    int OpenFile(const char* path, u32 chunk = 0x4000);   // moses.dll 0x1000c5f0  1 = ok (whole file loaded in memory)
    int CloseFile();                                      // moses.dll CResfile::CloseFile
    int IsOpen() const { return open_; }                  // moses.dll CResfile::IsOpen
    u32 GetNumResources() const { return archive_.Count(); } // moses.dll CResfile::GetNumResources
    u32 GetRecOffset(u32 id) const;                       // moses.dll 0x1000c370
    u32 GetRecSize(u32 id) const;                         // moses.dll 0x1000c3a0
    RESTYPE GetRecType(u32 id) const;                     // moses.dll 0x1000c3d0 (raw record type, alias NOT followed)
    int GetIndexedResId(u32* out, u32 groupId, u32 i) const; // moses.dll 0x1000c2e0
    u32 LoadStringA(u32 id, char* buf, u32 n) const;      // moses.dll 0x1000c590  returns the length, copies at most n-1 + NUL
    void SetAudio(CAudioLib* a) { audio_ = a; }           // moses.dll CResfile::SetAudio
    CAudioLib* Audio() const { return audio_; }
    // Chunk size / load proc exist in the original (SetLoadSize/SetLoadProc, progress callbacks while streaming
    // a big resource). Not needed: the file is read whole. Kept as no-ops so Aaron code transcribes.
    u32 GetLoadSize() const { return loadSize_; }         // moses.dll CResfile::GetLoadSize
    void SetLoadSize(u32 n) { loadSize_ = n; }            // moses.dll CResfile::SetLoadSize

    // Typed loaders. The original CResfile::LoadResource(id,&size) returns a raw pointer whose meaning
    // depends on the type; the port exposes one function per kind. All follow aliases (1 level).
    std::unique_ptr<CDibData> load_dib(u32 id) const;                // moses.dll 0x1000c0d0 (type 2)
    std::unique_ptr<CDibData> load_dibref(u32 id, CDibData* target) const; // type 7: copy of `target` with OffsetPos (0x1000c8e0)
    int load_dibref_info(u32 id, rrs::DibRefInfo& out) const;        // port helper: target id + new hot-spot
    CAudioSnd* load_sound(u32 id) const;                             // moses.dll 0x1000c220 (type 5) via audio_->SoundCreate
    const u8* LoadRaw(u32 id, u32* size) const;                      // type 3 (pointer valid while open)
    int GetGroup(u32 id, std::vector<u32>& ids) const;               // types 6, 8, 9, 11
    // moses.dll CResfile::LoadResource(id, &size): legacy entry point; returns the raw bytes for types 2,3,4,5 (size = record size).
    void* LoadResource(u32 id, u32* size) const;                     // moses.dll 0x1000c490
    const rrs::Archive& Archive() const { return archive_; }

    // ---- raw file access through CFileIO (WP-5) -------------------------------------------------
    // moses.dll 0x1000c730: installs the progress callback of `load`; returns the previous one.
    RESLOADPROC SetLoadProc(RESLOADPROC proc, void* ctx);
    // moses.dll 0x1000c400: reads key resource `key` (0..4) from the file into a block allocated with CMem; *size = its size
    // (0 and nullptr when the slot is empty).  Free the block with CMem::Free.
    void* LoadKeyResource(RESKEY key, u32* size);
    // moses.dll 0x1000c030 (private in the original): seek to `offset` and read `n` bytes into buf.  With a load proc the
    // read is split in chunks of GetLoadSize() bytes and the proc is called after each one.
    // ORIGINAL BUG PRESERVED: after the first chunk the file offset is advanced by the REMAINING byte count instead of the
    // chunk size, so multi-chunk loads read from the wrong place.
    int load(void* buf, u32 offset, u32 n);
    // moses.dll 0x1000c180 (private; takes an INDEXREC): reads the palette record `id` (type 4, size/4 RGBQUADs) and builds a CDibPal.
    std::unique_ptr<CDibPal> load_palette(u32 id);
    CFileIO* File() { return io_.get(); }              // +0 of the original (null when closed)

private:
    rrs::Archive archive_;
    int open_ = 0;
    u32 loadSize_ = 0x4000;
    CAudioLib* audio_ = nullptr;
    std::unique_ptr<CFileIO> io_;                      // +0
    struct { u32 off = 0, size = 0; } key_[5];         // +24 key resources
    RESLOADPROC loadProc_ = nullptr;                   // +4c
    void* loadCtx_ = nullptr;                          // +50
};

// ---------------------------------------------------------------------------------------------
// CResMgr (0x70): cache of loaded objects, indexed by id & 0xffff.
struct RESREC {         // 8 bytes in the original: {u32 refcount; void* obj}
    u32 refcount = 0;
    void* obj = nullptr;
    u16 type = 0;       // port: cached RESTYPE of the loaded object (selects the deleter in free_id)
};

class CResMgr {
public:
    explicit CResMgr(CAudioLib* audio = nullptr);   // moses.dll CResMgr::CResMgr(CAudioLib*)
    virtual ~CResMgr();                             // moses.dll CResMgr::~CResMgr (not virtual in the original)
    CResMgr(const CResMgr&) = delete;
    // moses.dll 0x10002410 operator=: the original copies the table pointer/count and the CResfile (shallow, both managers
    // would then free the same objects).  Port: the destination opens the same archive and gets an EMPTY table (nothing loaded).
    CResMgr& operator=(const CResMgr& o);

    // ---- virtual interface (vtable 0x1001645c; Aaron's ResArchive overrides parts) -----------------------
    virtual void* Free(u32 id, void* ctx);          // slot 0  moses.dll 0x1000cd00  refcount--, 0 -> free_id; returns nullptr
    virtual void* GetObj(u32 id, void* ctx);        // slot 1  moses.dll CResMgr::GetObj  already loaded object, else Load()
    virtual void* Load(u32 id, void* ctx);          // slot 2  moses.dll 0x1000ced0  refcount++, loads if absent
    virtual int Open(const char* path);             // slot 3  moses.dll 0x1000d010  allocates the table (nrec entries)

    // What Load returns, by record type (after alias resolution):
    //   2 DIB -> CDibData*   7 DIBREF -> CDibData*   8 DIBLIST -> CDibList* (its dibs are loaded, refcounted, released with it)
    //   5 sound -> CAudioSnd* (SoundSetId(id) applied)   3 raw -> const u8*   12 string -> const char*
    //   6/11 group -> GROUPREC* (loads every member id recursively)   4/9 -> nullptr (unused)

    int Close();                                    // moses.dll CResMgr::Close
    void FreeAll(void* ctx);                        // moses.dll 0x1000cd70
    int LoadAll(void* ctx);                         // moses.dll 0x1000cf50
    int LoadList(u32* ids, u32 n, void* ctx);       // moses.dll 0x1000cf90
    int FreeList(u32* ids, u32 n, void* ctx);       // moses.dll 0x1000cdb0
    RESTYPE GetResType(u32 id);                     // moses.dll CResMgr::GetResType (follows aliases)
    int IsLoaded(u32 id);                           // moses.dll CResMgr::IsLoaded
    int GetIndexedResId(u32* out, u32 groupId, u32 i); // moses.dll 0x1000cdf0
    u32 LoadStringA(u32 id, char* buf, u32 n);      // moses.dll 0x1000cff0
    CResfile& File() { return file_; }
    const std::string& Path() const { return path_; }
    u32 LoadedCount() const { return loaded_; }     // +60

protected:
    // internals, names/addresses from rrs_format.md
    int load_id(u32 id, void* ctx);                 // moses.dll 0x1000cb80  fills table_[index]
    int free_id(u32 id, void* ctx);                 // moses.dll 0x1000ca90
    int load_group(const std::vector<u32>& ids, void* ctx);   // moses.dll 0x1000ca60
    void free_group(GROUPREC* g, void* ctx);
    CDibObj* load_dibref(u32 id, void* ctx);        // moses.dll 0x1000c8e0 (CResMgr::load_dibref)
    CDibObj* load_diblist(u32 id, void* ctx);       // moses.dll 0x1000c850
    CDibObj* load_dibtile(u32 id, void* ctx);       // moses.dll 0x1000c970
    void free_dibobj(RESREC& r, u32 id, RESTYPE type, void* ctx); // moses.dll 0x1000c750 (port: id+type explicit)
    int grow(u32 n);                                // moses.dll CResMgr::grow

    CResfile file_;
    std::string path_;
    std::vector<RESREC> table_;   // +5c, indexed by id & 0xffff
    u32 loaded_ = 0;              // +60
    CAudioLib* audio_ = nullptr;  // +6c
};

} // namespace moses
