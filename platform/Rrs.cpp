#include "platform/Rrs.h"

#include <cstdio>
#include <cstring>

namespace rrs {

static uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
static uint32_t Le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    bool ok = n <= 0 || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

// moses.dll 0x1000c5f0 (OpenFile) / 0x1000c6d0 (header) / 0x1000c400 (LoadKeyResource)
bool Archive::Open(const std::string& path, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = path + ": " + m;
        return false;
    };
    path_ = path;
    recs_.clear();
    if (!ReadWholeFile(path, file_)) return fail("cannot read file");
    if (file_.size() < 0x38 || Le32(&file_[0]) != 0x54504152 /* 'RAPT' */) return fail("not a RAPT file");
    uint32_t ver = Le32(&file_[4]);
    if ((ver >> 16) != 1 || (ver & 0xffff) != 0) return fail("unsupported RAPT version");
    uint32_t idxOff = Le32(&file_[0x10]), idxSize = Le32(&file_[0x14]); // key 0: directory
    poolOff_ = Le32(&file_[0x18]);
    poolSize_ = Le32(&file_[0x1c]); // key 1: pool
    if (uint64_t(idxOff) + idxSize > file_.size()) return fail("directory out of range");
    if (uint64_t(poolOff_) + poolSize_ > file_.size()) return fail("pool out of range");
    uint32_t n = idxSize / 12;
    recs_.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t* p = &file_[idxOff + 12 * i];
        recs_[i] = {Le32(p), Le32(p + 4), Le16(p + 8), Le16(p + 10)};
    }
    return true;
}

uint32_t Archive::ResolveAlias(uint32_t index) const {
    for (int depth = 0; depth < 8; ++depth) {
        if (!Valid(index)) return 0xffffffffu;
        if (recs_[index].type != kAlias) return index;
        index = recs_[index].offset & 0xffff;
    }
    return 0xffffffffu;
}

uint16_t Archive::TypeOf(uint32_t index) const {
    uint32_t r = ResolveAlias(index);
    return r == 0xffffffffu ? 0 : recs_[r].type;
}

bool Archive::GetData(uint32_t index, const uint8_t*& data, uint32_t& size) const {
    uint32_t r = ResolveAlias(index);
    if (r == 0xffffffffu) return false;
    const Record& rc = recs_[r];
    if (rc.type < kDib || rc.type > kSound) return false;
    if (uint64_t(rc.offset) + rc.size > file_.size()) return false;
    data = file_.data() + rc.offset;
    size = rc.size;
    return true;
}

bool Archive::GetPool(uint32_t index, const uint8_t*& data, uint32_t& size) const {
    uint32_t r = ResolveAlias(index);
    if (r == 0xffffffffu) return false;
    const Record& rc = recs_[r];
    if (rc.type < kGroup || rc.type > kString) return false;
    if (uint64_t(rc.offset) + rc.size > poolSize_) return false;
    data = file_.data() + poolOff_ + rc.offset;
    size = rc.size;
    return true;
}

// CResfile::GetIndexedResId moses.dll 0x1000c2e0: count word at +0, ids at +4 (types 6,8,9) or +8 (type 11)
bool Archive::GetGroup(uint32_t index, std::vector<uint32_t>& ids) const {
    const uint8_t* p;
    uint32_t size;
    ids.clear();
    uint16_t t = TypeOf(index);
    if (t != kGroup && t != kDibList && t != kGroup2 && t != kDibTile) return false;
    if (!GetPool(index, p, size) || size < 4) return false;
    uint32_t hdr = (t == kGroup2) ? 8 : (t == kDibTile ? 0x10 : 4);
    uint32_t cnt = Le16(p);
    if (hdr + 4ull * cnt > size) return false;
    for (uint32_t i = 0; i < cnt; ++i) ids.push_back(Le32(p + hdr + 4 * i));
    return true;
}

bool Archive::GetString(uint32_t index, std::string& out) const {
    const uint8_t* p;
    uint32_t size;
    if (TypeOf(index) != kString || !GetPool(index, p, size)) return false;
    size_t n = 0;
    while (n < size && p[n]) ++n;
    out.assign(reinterpret_cast<const char*>(p), n);
    return true;
}

bool Archive::GetDibRef(uint32_t index, DibRefInfo& out) const {
    const uint8_t* p;
    uint32_t size;
    if (TypeOf(index) != kDibRef || !GetPool(index, p, size) || size < 8) return false;
    out.targetId = Le32(p);
    out.x = int16_t(Le16(p + 4));
    out.y = int16_t(Le16(p + 6));
    return true;
}

// DIBTILEREC (pool): u16 count, u16 pad, DIBTILEHDR[12] at +4, u32 ids[count] at +0x10 (load_dibtile 0x1000c970)
bool Archive::GetDibTile(uint32_t index, DibTileInfo& out) const {
    const uint8_t* p;
    uint32_t size;
    if (TypeOf(index) != kDibTile || !GetPool(index, p, size) || size < 0x10) return false;
    out.cols = Le16(p + 4);
    out.rows = Le16(p + 6);
    out.tileW = Le16(p + 8);
    out.tileH = Le16(p + 10);
    out.x = int16_t(Le16(p + 12));
    out.y = int16_t(Le16(p + 14));
    return true;
}

} // namespace rrs
