#include "platform/DibEncode.h"

#include <algorithm>

namespace {
uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void Put16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
}
void Put32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i)));
}

// One row (RLE stream incl. the terminating 0). See DibEncode.h for the inferred rules.
void EncodeRow(const uint16_t* r, int w, std::vector<uint8_t>& o) {
    int n = w;
    int z = 0;
    while (z < n && r[n - 1 - z] == 0) ++z;
    if (z >= 2) n -= z; // trailing transparent run (>= 2 pixels) is implicit
    std::vector<uint16_t> lit;
    auto flush = [&]() {
        size_t i = 0;
        while (i < lit.size()) {
            size_t c = std::min<size_t>(127, lit.size() - i);
            o.push_back(uint8_t(0x80 | c));
            for (size_t k = 0; k < c; ++k) Put16(o, lit[i + k]);
            i += c;
        }
        lit.clear();
    };
    int i = 0;
    while (i < n) {
        int j = i;
        while (j < n && r[j] == r[i]) ++j;
        if (j - i >= 2) {
            flush();
            int k = j - i;
            while (k > 0) {
                int c = std::min(k, 127);
                o.push_back(uint8_t(c));
                Put16(o, r[i]);
                k -= c;
            }
            i = j;
        } else {
            lit.push_back(r[i]);
            ++i;
        }
    }
    flush();
    o.push_back(0);
}

struct Encoded {
    std::vector<std::vector<uint8_t>> rows; // file order: row 0 = bottom row
    size_t total = 0;                       // line table (word entries) + rows
    size_t zeros = 0;
};

Encoded EncodeRows(const DibRaw& d) {
    Encoded e;
    e.rows.resize(size_t(d.h));
    for (int y = 0; y < d.h; ++y) { // file row y = top-down row h-1-y
        const uint16_t* r = d.px.data() + size_t(d.h - 1 - y) * d.w;
        EncodeRow(r, d.w, e.rows[size_t(y)]);
        for (int x = 0; x < d.w; ++x) e.zeros += r[x] == 0;
    }
    e.total = size_t(d.h) * 2;
    for (auto& r : e.rows) e.total += r.size();
    return e;
}

int FlagsOf(const DibRaw& d, const Encoded& e) {
    int f = 2;
    if (e.total > 0xffff) f |= 4;
    if (e.zeros == 0) f |= 1;
    if (e.zeros == size_t(d.w) * size_t(d.h)) f |= 8;
    return f;
}

std::vector<uint8_t> Build(const DibRaw& d, const Encoded& e, int flags) {
    std::vector<uint8_t> o;
    Put16(o, uint16_t(d.w));
    Put16(o, uint16_t(d.h));
    Put16(o, uint16_t(int16_t(d.ox)));
    Put16(o, uint16_t(int16_t(d.oy)));
    o.push_back(16);
    o.push_back(uint8_t(flags));
    bool big = flags & 4;
    size_t off = size_t(d.h) * (big ? 4 : 2);
    for (auto& r : e.rows) {
        if (big) Put32(o, uint32_t(off));
        else Put16(o, uint16_t(off));
        off += r.size();
    }
    for (auto& r : e.rows) o.insert(o.end(), r.begin(), r.end());
    return o;
}
} // namespace

bool DecodeDibRaw(const uint8_t* data, size_t size, DibRaw& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    if (size < 10) return fail("DIB too short");
    int w = Le16(data), h = Le16(data + 2);
    int fl = data[9];
    if (data[8] != 16) return fail("unsupported bpp");
    if (!(fl & 2)) return fail("uncompressed DIB not supported");
    const uint8_t* d = data + 10;
    size_t dsize = size - 10;
    size_t entry = (fl & 4) ? 4 : 2;
    if (size_t(h) * entry > dsize) return fail("line table out of range");
    out.w = w;
    out.h = h;
    out.ox = int16_t(Le16(data + 4));
    out.oy = int16_t(Le16(data + 6));
    out.flags = fl;
    out.px.assign(size_t(w) * size_t(h), 0);
    for (int y = 0; y < h; ++y) {
        size_t p = (fl & 4) ? Le32(d + 4 * y) : Le16(d + 2 * y);
        uint16_t* row = out.px.data() + size_t(h - 1 - y) * w;
        int x = 0;
        for (;;) {
            if (p >= dsize) return fail("row runs past data");
            uint8_t c = d[p++];
            if (c == 0) break;
            if (c & 0x80) {
                int n = c & 0x7f;
                if (p + 2 * size_t(n) > dsize || x + n > w) return fail("literal run out of range");
                for (int i = 0; i < n; ++i, p += 2) row[x++] = Le16(d + p);
            } else {
                if (p + 2 > dsize || x + c > w) return fail("repeat run out of range");
                uint16_t v = Le16(d + p);
                p += 2;
                for (int i = 0; i < c; ++i) row[x++] = v;
            }
        }
    }
    return true;
}

int DibFlagsFor(const DibRaw& d, size_t encodedBytesAfterHeader) {
    int f = 2;
    if (encodedBytesAfterHeader > 0xffff) f |= 4;
    size_t zeros = 0;
    for (uint16_t v : d.px) zeros += v == 0;
    if (zeros == 0) f |= 1;
    if (zeros == d.px.size()) f |= 8;
    return f;
}

std::vector<uint8_t> EncodeDib(const DibRaw& d) {
    Encoded e = EncodeRows(d);
    return Build(d, e, FlagsOf(d, e));
}

std::vector<uint8_t> EncodeDibWithFlags(const DibRaw& d, int flags) {
    Encoded e = EncodeRows(d);
    flags = (flags | 2) & ~4;
    if (e.total > 0xffff) flags |= 4;
    return Build(d, e, flags);
}

uint16_t Argb8888ToRgb555(uint32_t argb) {
    if ((argb >> 24) < 128) return 0;
    return uint16_t(0x8000 | ((argb >> 19) & 31) << 10 | ((argb >> 11) & 31) << 5 | ((argb >> 3) & 31));
}

DibRaw DibRawFromArgb(const uint32_t* argb, int w, int h, int ox, int oy) {
    DibRaw d;
    d.w = w;
    d.h = h;
    d.ox = ox;
    d.oy = oy;
    d.px.resize(size_t(w) * size_t(h));
    for (size_t i = 0; i < d.px.size(); ++i) d.px[i] = Argb8888ToRgb555(argb[i]);
    return d;
}
