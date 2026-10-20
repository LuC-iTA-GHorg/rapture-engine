#!/usr/bin/env python3
"""
rrs_extract.py - extractor for the RAPT archives (*.rrs) of the Disney Rapture games (Moses/Aaron engine).

Format reconstructed from moses.dll (CResfile/CResMgr, CDibData, CAudioLib);
see docs/rrs_format.md (optional, not part of the public repository).  Standard library only (use: python -I rrs_extract.py ...).

Usage:
    python -I rrs_extract.py [--game DIR] [--out DIR] [archivio.rrs ...]

Default: --game = three levels above engine/tools (i.e. the game folder when the repo lives in <game>/src), --out = <game>/extracted.
For each archive it creates <out>/<name>/ with the converted entries and <out>/<name>/index.json.
"""
import argparse
import json
import os
import struct
import sys
import zlib

RAPT_MAGIC = 0x54504152  # 'RAPT'

TYPE_NAMES = {
    1: "alias",        # refers to another index
    2: "dib",          # 16-bit RLE bitmap (CDibData)
    3: "raw",          # raw blob (text/script)
    4: "palette",      # RGBQUAD[] (never used in the known archives)
    5: "sound",        # ADPCM wave / midi / reference to an avi
    6: "group",        # list of ids (pool)
    7: "dibref",       # same bitmap with another hot spot (pool)
    8: "diblist",      # list of dibs (pool)
    9: "dibtile",      # tiled mosaic of dibs (pool)
    11: "group2",      # list of ids with an 8-byte header (pool)
    12: "string",      # string (pool)
}

# ----------------------------------------------------------------------------
# ADPCM tables: the codec is standard IMA/DVI ADPCM, so the tables are computed from the
# published 89-entry IMA step sequence (same as engine/platform/AdpcmTables.h).
# ----------------------------------------------------------------------------
IMA_STEPS = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
    1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]


def adpcm_tables():
    """Return (step[712], adj[16]); index values are stored premultiplied by 8."""
    step = []
    for s in IMA_STEPS:
        for m in range(8):
            d = s >> 3
            if m & 1:
                d += s >> 2
            if m & 2:
                d += s >> 1
            if m & 4:
                d += s
            step.append(d)
    adj = [x * 8 for x in (-1, -1, -1, -1, 2, 4, 6, 8)] * 2
    return step, adj


def adpcm_decode(data, step, adj, stereo):
    """CAudioWave ADPCM 4:1 (moses.dll 0x10014790). Low nibble first.
    mono: both nibbles on the same channel; stereo: lo=L, hi=R."""
    out = bytearray()
    pred = [0, 0]
    idx = [0, 0]
    pack = struct.Struct("<h").pack
    for b in data:
        for k, nib in enumerate((b & 0xF, b >> 4)):
            c = k if stereo else 0
            delta = step[idx[c] + (nib & 7)]
            if nib & 8:
                delta = -delta
            p = pred[c] + delta
            if p > 32767:
                p = 32767
            elif p < -32768:
                p = -32768
            pred[c] = p
            i = idx[c] + adj[nib]
            idx[c] = 0 if i < 0 else (0x2C0 if i > 0x2C0 else i)
            out += pack(p)
    return bytes(out)


# ----------------------------------------------------------------------------
# DIB (CDibData, 16 bit RLE)
# ----------------------------------------------------------------------------
def dib_decode(buf):
    """DIBREC (10 byte) + line-table + righe RLE.  Returns a dict with 16-bit pixels (0 = transparent)."""
    w, h, xo, yo, bpp, fl = struct.unpack_from("<HHhhBB", buf, 0)
    if bpp != 16:
        raise ValueError("bpp %d not supported" % bpp)
    d = memoryview(buf)[10:]
    if not (fl & 2):
        raise ValueError("uncompressed DIB (flag %d) never seen in the archives" % fl)
    # row offset table: dwords if flag bit 2 is set, otherwise words (moses 0x10004b00 -> 0x100049c0)
    if fl & 4:
        tab = struct.unpack_from("<%dI" % h, d, 0)
    else:
        tab = struct.unpack_from("<%dH" % h, d, 0)
    rows = []
    for y in range(h):
        p = tab[y]
        row = []
        while True:
            c = d[p]
            p += 1
            if c == 0:
                break
            if c & 0x80:
                n = c & 0x7F
                row += struct.unpack_from("<%dH" % n, d, p)
                p += 2 * n
            else:
                v = struct.unpack_from("<H", d, p)[0]
                p += 2
                row += [v] * c
        if len(row) > w:
            raise ValueError("row %d too long" % y)
        row += [0] * (w - len(row))  # implicit transparent tail
        rows.append(row)
    return {"w": w, "h": h, "xo": xo, "yo": yo, "bpp": bpp, "flags": fl, "rows": rows}


_C5 = [(i * 255 + 15) // 31 for i in range(32)]


def png_write(path, w, h, rgba):
    def chunk(t, data):
        c = struct.pack(">I", len(data)) + t + data
        return c + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def dib_to_png(dib, path):
    w, h = dib["w"], dib["h"]
    buf = bytearray(w * h * 4)
    o = 0
    for row in reversed(dib["rows"]):  # DIBs are bottom-up (row 0 = bottom): flip for PNG
        for v in row:
            if v:  # 0 = transparent; opaque pixels always have bit 15 = 1, RGB555 format
                buf[o] = _C5[(v >> 10) & 31]
                buf[o + 1] = _C5[(v >> 5) & 31]
                buf[o + 2] = _C5[v & 31]
                buf[o + 3] = 255
            o += 4
    png_write(path, w, h, bytes(buf))


# ----------------------------------------------------------------------------
# sounds
# ----------------------------------------------------------------------------
def wav_bytes(pcm, ch, rate, bits):
    ba = ch * bits // 8
    hdr = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack(
        "<IHHIIHH", 16, 1, ch, rate, rate * ba, ba, bits) + b"data" + struct.pack("<I", len(pcm))
    return hdr + pcm


def midi_to_smf(data):
    """Compact MIDI stream: dword with bit 31 = delta (unit unknown, here 1 tick = 1 ms),
    otherwise short MIDI event (status | d1<<8 | d2<<16)."""
    def vlq(n):
        out = [n & 0x7F]
        n >>= 7
        while n:
            out.append(0x80 | (n & 0x7F))
            n >>= 7
        return bytes(reversed(out))

    trk = bytearray()
    delta = 0
    n = len(data) // 4
    for i in range(n):
        dw = struct.unpack_from("<I", data, 4 * i)[0]
        if dw & 0x80000000:
            delta += dw & 0x7FFFFFFF
            continue
        st = dw & 0xFF
        if st < 0x80:
            continue
        ln = 2 if (st & 0xF0) in (0xC0, 0xD0) else 3
        trk += vlq(delta) + struct.pack("<I", dw)[:ln]
        delta = 0
    trk += vlq(delta) + b"\xFF\x2F\x00"
    tempo = b"\x00\xFF\x51\x03" + (1000000).to_bytes(3, "big")  # 1 quarter = 1 s -> with division 1000: 1 tick = 1 ms
    trk = tempo + trk
    return (b"MThd" + struct.pack(">IHHH", 6, 0, 1, 1000) + b"MTrk" + struct.pack(">I", len(trk)) + bytes(trk))


# ----------------------------------------------------------------------------
# archive
# ----------------------------------------------------------------------------
class Rrs:
    def __init__(self, path):
        self.path = path
        self.b = open(path, "rb").read()
        magic, ver, hsize = struct.unpack_from("<III", self.b, 0)
        if magic != RAPT_MAGIC or (ver >> 16) != 1 or (ver & 0xFFFF) != 0:
            raise ValueError("%s: not a RAPT 1.0 archive" % path)
        self.hsize = hsize
        self.keys = [struct.unpack_from("<II", self.b, 0x10 + 8 * k) for k in range(5)]
        io, isz = self.keys[0]
        self.n = isz // 12
        self.recs = [struct.unpack_from("<IIHH", self.b, io + 12 * i) for i in range(self.n)]
        self.pool_off, self.pool_size = self.keys[1]

    def pool(self, off, size):
        return self.b[self.pool_off + off:self.pool_off + off + size]

    def resolve(self, i):
        """follows aliases (type 1) like CResfile::GetRecType/LoadResource."""
        seen = 0
        while self.recs[i][2] == 1 and seen < 8:
            i = self.recs[i][0] & 0xFFFF
            seen += 1
        return i


def ids_from(p, hdr):
    cnt = struct.unpack_from("<H", p, 0)[0]
    return [struct.unpack_from("<I", p, hdr + 4 * k)[0] for k in range(cnt)]


def idfmt(v):
    return {"id": v, "index": v & 0xFFFF, "class": v >> 16}


def extract_archive(path, outdir, step, adj, raw=False):
    rr = Rrs(path)
    name = os.path.splitext(os.path.basename(path))[0]
    od = os.path.join(outdir, name)
    os.makedirs(od, exist_ok=True)
    index = {"archive": os.path.basename(path), "size": len(rr.b), "header_size": rr.hsize,
             "version": struct.unpack_from("<I", rr.b, 4)[0], "reserved": struct.unpack_from("<I", rr.b, 0xC)[0],
             "keys": [{"offset": o, "size": s} for o, s in rr.keys], "count": rr.n, "entries": []}
    stats = {}

    def bump(k):
        stats[k] = stats.get(k, 0) + 1

    for i, (off, size, typ, tag) in enumerate(rr.recs):
        e = {"index": i, "type": typ, "type_name": TYPE_NAMES.get(typ, "?"), "tag": tag}
        try:
            if typ == 1:
                e["alias_of"] = off & 0xFFFF
                e["alias_sub"] = off >> 16
                e["size"] = size
            elif typ == 2:
                e["offset"], e["size"] = off, size
                dib = dib_decode(rr.b[off:off + size])
                fn = "dib_%04d.png" % i
                os.makedirs(os.path.join(od, "dib"), exist_ok=True)
                dib_to_png(dib, os.path.join(od, "dib", fn))
                e.update(file="dib/" + fn, width=dib["w"], height=dib["h"], origin_x=dib["xo"], origin_y=dib["yo"],
                         bpp=dib["bpp"], dib_flags=dib["flags"], line_table="dword" if dib["flags"] & 4 else "word")
            elif typ == 3:
                e["offset"], e["size"] = off, size
                data = rr.b[off:off + size]
                txt = all(c in (9, 10, 13) or 32 <= c < 127 for c in data[:200])
                fn = "raw_%04d.%s" % (i, "txt" if txt else "bin")
                os.makedirs(os.path.join(od, "raw"), exist_ok=True)
                open(os.path.join(od, "raw", fn), "wb").write(data)
                e["file"] = "raw/" + fn
            elif typ == 4:
                e["offset"], e["size"] = off, size
                data = rr.b[off:off + size]
                fn = "pal_%04d.bin" % i
                os.makedirs(os.path.join(od, "pal"), exist_ok=True)
                open(os.path.join(od, "pal", fn), "wb").write(data)
                e["file"] = "pal/" + fn
            elif typ == 5:
                e["offset"], e["size"] = off, size
                data = rr.b[off:off + size]
                kind, par = struct.unpack_from("<II", data, 0)
                e["sound_kind"] = {1: "wave", 2: "midi", 3: "avi"}.get(kind, str(kind))
                e["sound_param"] = par
                os.makedirs(os.path.join(od, "sound"), exist_ok=True)
                if kind == 1:
                    tag_, ch, rate, avg, align, bits = struct.unpack_from("<HHIIHH", data, 8)
                    f18, f19 = data[0x18], data[0x19]
                    body = data[0x1C:]
                    if f19:
                        pcm = adpcm_decode(body, step, adj, ch == 2)
                    else:
                        pcm = body
                    fn = "snd_%04d.wav" % i
                    open(os.path.join(od, "sound", fn), "wb").write(wav_bytes(pcm, ch, rate, bits))
                    e.update(file="sound/" + fn, channels=ch, rate=rate, bits=bits, adpcm=bool(f19),
                             stream_flag=f18, pcm_bytes=len(pcm), seconds=round(len(pcm) / float(avg or 1), 3))
                elif kind == 2:
                    body = data[8:]
                    fn = "mid_%04d.mid" % i
                    open(os.path.join(od, "sound", fn), "wb").write(midi_to_smf(body))
                    open(os.path.join(od, "sound", "mid_%04d.raw" % i), "wb").write(body)
                    e.update(file="sound/" + fn, raw_file="sound/mid_%04d.raw" % i, midi_bytes=len(body))
                elif kind == 3:
                    s = data[8:8 + 0x104].split(b"\0")[0].decode("latin1")
                    e["avi_name"] = s
                else:
                    open(os.path.join(od, "sound", "snd_%04d.bin" % i), "wb").write(data)
                    e["file"] = "sound/snd_%04d.bin" % i
            elif typ in (6, 11, 8):
                p = rr.pool(off, size)
                e["pool_offset"], e["size"] = off, size
                e["ids"] = [idfmt(v) for v in ids_from(p, 8 if typ == 11 else 4)]
            elif typ == 7:
                p = rr.pool(off, size)
                e["pool_offset"], e["size"] = off, size
                idv, x, y = struct.unpack_from("<Ihh", p, 0)
                e.update(target=idfmt(idv), hotspot_x=x, hotspot_y=y)
            elif typ == 9:
                p = rr.pool(off, size)
                e["pool_offset"], e["size"] = off, size
                e["hex"] = p.hex()
            elif typ == 12:
                p = rr.pool(off, size)
                e["pool_offset"], e["size"] = off, size
                e["text"] = p.split(b"\0")[0].decode("latin1")
            else:
                e["offset"], e["size"] = off, size
            bump(e["type_name"] if "error" not in e else "error")
        except Exception as ex:  # do not abort the extraction
            e["error"] = repr(ex)
            bump("error")
        if raw and typ in (2, 3, 4, 5) and "error" not in e:
            os.makedirs(os.path.join(od, "bin"), exist_ok=True)
            open(os.path.join(od, "bin", "%04d.bin" % i), "wb").write(rr.b[off:off + size])
            e["bin"] = "bin/%04d.bin" % i
        for key in ("file", "raw_file"):  # CRC32 of the converted files: rrs_pack.py uses the original bytes (bin/) only while they are unchanged
            if key in e:
                e[key + "_crc32"] = zlib.crc32(open(os.path.join(od, e[key]), "rb").read()) & 0xFFFFFFFF
        index["entries"].append(e)
    index["summary"] = stats
    with open(os.path.join(od, "index.json"), "w", encoding="utf-8") as f:
        json.dump(index, f, indent=1, ensure_ascii=False)
    return name, stats


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    game_default = os.path.abspath(os.path.join(here, "..", "..", ".."))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", default=game_default, help="folder with the *.rrs files and moses.dll")
    ap.add_argument("--out", default=None, help="output folder (default <game>/extracted)")
    ap.add_argument("--raw", action="store_true", help="also save the original bytes of each entry (bin/NNNN.bin) for rrs_pack.py")
    ap.add_argument("archives", nargs="*", help="archives to extract (default: all *.rrs)")
    a = ap.parse_args()
    out = a.out or os.path.join(a.game, "extracted")
    arch = a.archives or sorted(os.path.join(a.game, f) for f in os.listdir(a.game) if f.lower().endswith(".rrs"))
    arch = [x if os.path.isabs(x) or os.path.exists(x) else os.path.join(a.game, x) for x in arch]
    dll = os.path.join(a.game, "moses.dll")
    step, adj = adpcm_tables()
    for p in arch:
        n, st = extract_archive(p, out, step, adj, a.raw)
        print("%-10s %s" % (n, ", ".join("%s=%d" % kv for kv in sorted(st.items()))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
