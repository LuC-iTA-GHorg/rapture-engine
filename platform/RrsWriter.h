// RAPT (*.rrs) archive writer (WP-7). Format: docs/rrs_format.md.
//
// rrs::Image is an editable in-memory model of an archive: header fields, the directory (one Record per index) and
// the bytes of every entry (file-resident blobs for types 2..5, pool-resident blobs for types 6..12, nothing for
// aliases). Load() + Serialize() reproduce all shipped archives BYTE-IDENTICAL: the entry bytes are kept raw, the
// physical order of the data / pool entries and the layout (directory+pool first, or data first) are remembered.
// New content is built with the Make*() blob builders (which reproduce the shipped encodings) and Add().
//
// The record index is the resource id (low word): entries are never renumbered, Add() appends.
#pragma once

#include "platform/Rrs.h"
#include "platform/Sound.h"

#include <string>
#include <vector>

namespace rrs {

class Image {
public:
    struct Entry {
        Record rec;                  // offset/size are recomputed by Serialize() except for aliases (offset = target)
        std::vector<uint8_t> blob;   // types 2..5: file bytes; 6..12: pool bytes; alias: empty
    };
    enum Layout {
        kDirFirst = 0,  // header, directory, pool, data   (mus, muslin, frenzylz, ...)
        kDataFirst = 1, // header, data, directory, pool    (frenzy, *fx)
    };

    bool Load(const Archive& ar, std::string* err = nullptr);
    bool LoadFile(const std::string& path, std::string* err = nullptr);

    // Full file image. Alias sizes are written as stored (see RefreshAliasSizes()).
    std::vector<uint8_t> Serialize() const;
    bool Save(const std::string& path, std::string* err = nullptr) const;

    uint32_t Count() const { return uint32_t(ents_.size()); }
    Entry& At(uint32_t i) { return ents_[i]; }
    const Entry& At(uint32_t i) const { return ents_[i]; }
    Layout layout = kDirFirst;
    uint32_t version = 0x10000; // header +4
    uint32_t reserved = 0;      // header +0x0c

    // Appends an entry (its data goes last in its section) and returns its index.
    uint32_t Add(uint16_t type, uint16_t tag, std::vector<uint8_t> blob);
    uint32_t AddAlias(uint32_t target, uint16_t tag = 0); // size copied from the target now
    // Replaces the bytes of an entry (position in the archive unchanged). Aliases of it are fixed by RefreshAliasSizes().
    bool SetBlob(uint32_t index, std::vector<uint8_t> blob);
    // alias.size = size of the (final) target, as the authoring tool wrote it (no-op on the shipped archives).
    void RefreshAliasSizes();
    // Physical order of the entries inside the data / pool sections (default: Add() order; Load() keeps the original one).
    const std::vector<uint32_t>& FileOrder() const { return fileOrder_; }
    const std::vector<uint32_t>& PoolOrder() const { return poolOrder_; }
    void SetOrder(std::vector<uint32_t> fileOrder, std::vector<uint32_t> poolOrder) {
        fileOrder_ = std::move(fileOrder);
        poolOrder_ = std::move(poolOrder);
    }
    static bool IsPoolType(uint16_t t) { return t >= kGroup && t <= kString; }
    static bool IsFileType(uint16_t t) { return t >= kDib && t <= kSound; }

    // --- blob builders (pool) -------------------------------------------------------------------------------
    static std::vector<uint8_t> MakeGroup(const std::vector<uint32_t>& ids, bool group2 = false); // type 6 / 11
    static std::vector<uint8_t> MakeDibList(const std::vector<uint32_t>& ids);                       // type 8
    static std::vector<uint8_t> MakeDibRef(uint32_t targetId, int x, int y);                         // type 7
    static std::vector<uint8_t> MakeDibTile(const DibTileInfo& info, const std::vector<uint32_t>& ids); // type 9
    static std::vector<uint8_t> MakeString(const std::string& s);                                    // type 12 (NUL included)
    // --- blob builders (file) -------------------------------------------------------------------------------
    // kind 1. `adpcm` = encode with AdpcmEncode (the shipped waves all are); otherwise raw 16 bit PCM.
    static std::vector<uint8_t> MakeWaveSound(const PcmBuffer& pcm, bool streamFlag, uint32_t param = 0, bool adpcm = true);
    static std::vector<uint8_t> MakeMidiSound(const std::vector<uint8_t>& compactStream, uint32_t param = 0); // kind 2
    static std::vector<uint8_t> MakeAviSound(const std::string& name, uint32_t param = 0);                    // kind 3

private:
    std::vector<Entry> ents_;
    std::vector<uint32_t> fileOrder_; // indices of the file-resident entries in physical order
    std::vector<uint32_t> poolOrder_; // indices of the pool-resident entries in physical order
};

} // namespace rrs
