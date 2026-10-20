// RAPT (*.rrs) archive reader. Format: docs/rrs_format.md.
// Reimplements the read side of CResfile / CResMgr in moses.dll (no engine API:
// only raw access to records). Resource ids are 32 bit; only the low word is the
// index into the directory (`id & 0xffff`), the high word is a class tag.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rrs {

// enum RESTYPE (CResfile::LoadResource, moses.dll 0x1000c490)
enum Type : uint16_t {
    kAlias = 1,
    kDib = 2,
    kRaw = 3,
    kPalette = 4,
    kSound = 5,
    kGroup = 6,    // pool, 4 byte header
    kDibRef = 7,   // pool
    kDibList = 8,  // pool, 4 byte header
    kDibTile = 9,  // pool (not present in the shipped archives)
    kGroup2 = 11,  // pool, 8 byte header
    kString = 12,  // pool
};

// moses.dll 0x1000c370 / 0x1000c3a0 / 0x1000c3d0 (GetRecOffset/Size/Type)
struct Record {
    uint32_t offset = 0; // file offset, or pool offset for types 6..12, or alias target in low word
    uint32_t size = 0;
    uint16_t type = 0;
    uint16_t tag = 0;
};

struct DibTileInfo { // DIBTILEHDR, load_dibtile moses.dll 0x1000c970 (pool +4)
    uint16_t cols = 0, rows = 0, tileW = 0, tileH = 0;
    int16_t x = 0, y = 0; // hot-spot position
};

struct DibRefInfo { // load_dibref moses.dll 0x1000c8e0
    uint32_t targetId = 0;
    int16_t x = 0, y = 0; // new hot-spot of the shared bitmap
};

inline uint32_t IdIndex(uint32_t id) { return id & 0xffff; }

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out);

class Archive {
public:
    // CResfile::OpenFile moses.dll 0x1000c5f0. Loads the whole file in memory.
    bool Open(const std::string& path, std::string* err = nullptr);
    const std::string& Path() const { return path_; }
    // WP-7 (writer support, additive): the whole loaded file and the pool location.
    const std::vector<uint8_t>& File() const { return file_; }
    uint32_t PoolOffset() const { return poolOff_; }
    uint32_t PoolSize() const { return poolSize_; }

    uint32_t Count() const { return uint32_t(recs_.size()); }
    const Record& Rec(uint32_t index) const { return recs_[index]; }
    bool Valid(uint32_t index) const { return index < recs_.size(); }

    // Follows alias records (type 1). Returns the final index, or 0xffffffff if broken.
    uint32_t ResolveAlias(uint32_t index) const;
    // Type after alias resolution (0 if invalid).
    uint16_t TypeOf(uint32_t index) const;

    // Raw bytes of a file-resident record (types 2,3,4,5) after alias resolution.
    bool GetData(uint32_t index, const uint8_t*& data, uint32_t& size) const;
    // Pool-resident record bytes (types 6..12) after alias resolution.
    bool GetPool(uint32_t index, const uint8_t*& data, uint32_t& size) const;

    // Group / DibList / Group2 / DibTile ids (low word is the index).
    bool GetGroup(uint32_t index, std::vector<uint32_t>& ids) const;
    bool GetString(uint32_t index, std::string& out) const; // CResfile::LoadStringA 0x1000c590
    bool GetDibRef(uint32_t index, DibRefInfo& out) const;
    bool GetDibTile(uint32_t index, DibTileInfo& out) const; // header of a type 9 record (ids: GetGroup)

private:
    std::string path_;
    std::vector<uint8_t> file_;
    std::vector<Record> recs_;
    uint32_t poolOff_ = 0, poolSize_ = 0;
};

} // namespace rrs
