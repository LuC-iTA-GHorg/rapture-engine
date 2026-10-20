#include "platform/RrsWriter.h"

#include "platform/AdpcmEncode.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace rrs {

namespace {
uint32_t Le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void Put16(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
}
void Put32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i)));
}
} // namespace

// Mirrors CResfile::OpenFile moses.dll 0x1000c5f0, but keeps everything needed to write the same bytes back.
bool Image::Load(const Archive& ar, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = ar.Path() + ": " + m;
        return false;
    };
    const std::vector<uint8_t>& f = ar.File();
    if (f.size() < 0x38) return fail("too short");
    if (Le32(&f[8]) != 0x38) return fail("unsupported header size");
    for (int k = 2; k < 5; ++k)
        if (Le32(&f[0x10 + 8 * k]) || Le32(&f[0x14 + 8 * k])) return fail("keys 2..4 are not supported by the writer");
    version = Le32(&f[4]);
    reserved = Le32(&f[0x0c]);
    uint32_t dirOff = Le32(&f[0x10]);
    layout = dirOff == 0x38 ? kDirFirst : kDataFirst;
    ents_.clear();
    fileOrder_.clear();
    poolOrder_.clear();
    uint32_t n = ar.Count();
    ents_.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        const Record& r = ar.Rec(i);
        Entry& e = ents_[i];
        e.rec = r;
        if (r.type == kAlias) continue;
        if (IsFileType(r.type)) {
            if (uint64_t(r.offset) + r.size > f.size()) return fail("entry out of range");
            e.blob.assign(f.begin() + r.offset, f.begin() + r.offset + r.size);
            fileOrder_.push_back(i);
        } else if (IsPoolType(r.type)) {
            if (uint64_t(r.offset) + r.size > ar.PoolSize()) return fail("pool entry out of range");
            e.blob.assign(f.begin() + ar.PoolOffset() + r.offset, f.begin() + ar.PoolOffset() + r.offset + r.size);
            poolOrder_.push_back(i);
        } else {
            return fail("unsupported record type " + std::to_string(r.type));
        }
    }
    auto byOffset = [&](uint32_t a, uint32_t b) {
        return ents_[a].rec.offset != ents_[b].rec.offset ? ents_[a].rec.offset < ents_[b].rec.offset : a < b;
    };
    std::stable_sort(fileOrder_.begin(), fileOrder_.end(), byOffset);
    std::stable_sort(poolOrder_.begin(), poolOrder_.end(), byOffset);
    return true;
}

bool Image::LoadFile(const std::string& path, std::string* err) {
    Archive ar;
    if (!ar.Open(path, err)) return false;
    return Load(ar, err);
}

std::vector<uint8_t> Image::Serialize() const {
    std::vector<uint8_t> pool, data;
    std::vector<Record> recs(ents_.size());
    for (size_t i = 0; i < ents_.size(); ++i) recs[i] = ents_[i].rec;
    for (uint32_t i : poolOrder_) {
        recs[i].offset = uint32_t(pool.size());
        recs[i].size = uint32_t(ents_[i].blob.size());
        pool.insert(pool.end(), ents_[i].blob.begin(), ents_[i].blob.end());
    }
    uint32_t dirSize = uint32_t(12 * ents_.size());
    uint32_t dirOff, poolOff, dataOff;
    if (layout == kDirFirst) {
        dirOff = 0x38;
        poolOff = pool.empty() ? 0 : dirOff + dirSize;
        dataOff = 0x38 + dirSize + uint32_t(pool.size());
    } else {
        dataOff = 0x38;
        uint64_t total = 0;
        for (uint32_t i : fileOrder_) total += ents_[i].blob.size();
        dirOff = dataOff + uint32_t(total);
        poolOff = pool.empty() ? 0 : dirOff + dirSize;
    }
    uint32_t pos = dataOff;
    for (uint32_t i : fileOrder_) {
        recs[i].offset = pos;
        recs[i].size = uint32_t(ents_[i].blob.size());
        pos += recs[i].size;
        data.insert(data.end(), ents_[i].blob.begin(), ents_[i].blob.end());
    }
    std::vector<uint8_t> dir;
    for (const Record& r : recs) {
        Put32(dir, r.offset);
        Put32(dir, r.size);
        Put16(dir, r.type);
        Put16(dir, r.tag);
    }
    std::vector<uint8_t> o;
    Put32(o, 0x54504152);
    Put32(o, version);
    Put32(o, 0x38);
    Put32(o, reserved);
    Put32(o, dirOff);
    Put32(o, dirSize);
    Put32(o, poolOff);
    Put32(o, uint32_t(pool.size()));
    for (int k = 2; k < 5; ++k) {
        Put32(o, 0);
        Put32(o, 0);
    }
    if (layout == kDirFirst) {
        o.insert(o.end(), dir.begin(), dir.end());
        o.insert(o.end(), pool.begin(), pool.end());
        o.insert(o.end(), data.begin(), data.end());
    } else {
        o.insert(o.end(), data.begin(), data.end());
        o.insert(o.end(), dir.begin(), dir.end());
        o.insert(o.end(), pool.begin(), pool.end());
    }
    return o;
}

bool Image::Save(const std::string& path, std::string* err) const {
    std::vector<uint8_t> o = Serialize();
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (err) *err = path + ": cannot create file";
        return false;
    }
    bool ok = std::fwrite(o.data(), 1, o.size(), f) == o.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok && err) *err = path + ": write error";
    return ok;
}

uint32_t Image::Add(uint16_t type, uint16_t tag, std::vector<uint8_t> blob) {
    uint32_t i = uint32_t(ents_.size());
    Entry e;
    e.rec.type = type;
    e.rec.tag = tag;
    e.rec.size = uint32_t(blob.size());
    e.blob = std::move(blob);
    ents_.push_back(std::move(e));
    if (IsFileType(type)) fileOrder_.push_back(i);
    else if (IsPoolType(type)) poolOrder_.push_back(i);
    return i;
}

uint32_t Image::AddAlias(uint32_t target, uint16_t tag) {
    uint32_t i = Add(kAlias, tag, {});
    ents_[i].rec.offset = target & 0xffff;
    RefreshAliasSizes();
    return i;
}

bool Image::SetBlob(uint32_t index, std::vector<uint8_t> blob) {
    if (index >= ents_.size() || ents_[index].rec.type == kAlias) return false;
    ents_[index].rec.size = uint32_t(blob.size());
    ents_[index].blob = std::move(blob);
    return true;
}

void Image::RefreshAliasSizes() {
    for (Entry& e : ents_) {
        if (e.rec.type != kAlias) continue;
        uint32_t t = e.rec.offset & 0xffff;
        for (int depth = 0; depth < 8 && t < ents_.size() && ents_[t].rec.type == kAlias; ++depth) t = ents_[t].rec.offset & 0xffff;
        if (t < ents_.size() && ents_[t].rec.type != kAlias) e.rec.size = uint32_t(ents_[t].blob.size());
    }
}

std::vector<uint8_t> Image::MakeGroup(const std::vector<uint32_t>& ids, bool group2) {
    std::vector<uint8_t> o;
    Put16(o, uint32_t(ids.size()));
    o.resize(group2 ? 8 : 4, 0);
    for (uint32_t id : ids) Put32(o, id);
    return o;
}

std::vector<uint8_t> Image::MakeDibList(const std::vector<uint32_t>& ids) { return MakeGroup(ids, false); }

std::vector<uint8_t> Image::MakeDibRef(uint32_t targetId, int x, int y) {
    std::vector<uint8_t> o;
    Put32(o, targetId);
    Put16(o, uint32_t(x) & 0xffff);
    Put16(o, uint32_t(y) & 0xffff);
    Put32(o, 0);
    return o;
}

std::vector<uint8_t> Image::MakeDibTile(const DibTileInfo& t, const std::vector<uint32_t>& ids) {
    std::vector<uint8_t> o;
    Put16(o, uint32_t(ids.size()));
    Put16(o, 0);
    Put16(o, t.cols);
    Put16(o, t.rows);
    Put16(o, t.tileW);
    Put16(o, t.tileH);
    Put16(o, uint32_t(t.x) & 0xffff);
    Put16(o, uint32_t(t.y) & 0xffff);
    for (uint32_t id : ids) Put32(o, id);
    return o;
}

std::vector<uint8_t> Image::MakeString(const std::string& s) {
    std::vector<uint8_t> o(s.begin(), s.end());
    o.push_back(0);
    return o;
}

// Wave record: SoundCreate moses.dll 0x10010995 / 0x10010a10 (header layout: rrs_format.md sec. 4).
std::vector<uint8_t> Image::MakeWaveSound(const PcmBuffer& pcm, bool streamFlag, uint32_t param, bool adpcm) {
    std::vector<uint8_t> o;
    int ch = pcm.channels == 2 ? 2 : 1;
    Put32(o, 1);
    Put32(o, param);
    Put16(o, 1); // WAVE_FORMAT_PCM (decoded output format)
    Put16(o, uint32_t(ch));
    Put32(o, uint32_t(pcm.rate));
    Put32(o, uint32_t(pcm.rate) * 2 * ch);
    Put16(o, uint32_t(2 * ch));
    Put16(o, 16);
    o.push_back(streamFlag ? 1 : 0);
    o.push_back(adpcm ? 1 : 0);
    Put16(o, 0);
    if (adpcm) {
        std::vector<uint8_t> body;
        AdpcmEncode(pcm.samples.data(), pcm.samples.size(), ch == 2, body);
        o.insert(o.end(), body.begin(), body.end());
    } else {
        for (int16_t s : pcm.samples) Put16(o, uint16_t(s));
    }
    return o;
}

std::vector<uint8_t> Image::MakeMidiSound(const std::vector<uint8_t>& compact, uint32_t param) {
    std::vector<uint8_t> o;
    Put32(o, 2);
    Put32(o, param);
    o.insert(o.end(), compact.begin(), compact.end());
    return o;
}

std::vector<uint8_t> Image::MakeAviSound(const std::string& name, uint32_t param) {
    std::vector<uint8_t> o;
    Put32(o, 3);
    Put32(o, param);
    o.resize(8 + 0x104, 0);
    std::memcpy(&o[8], name.data(), std::min<size_t>(name.size(), 0x103));
    return o;
}

} // namespace rrs
