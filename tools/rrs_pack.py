#!/usr/bin/env python3
"""
rrs_pack.py - rebuilds a RAPT archive (*.rrs) from the folder produced by rrs_extract.py (WP-7).

Mirror image of the extractor: extract -> pack reproduces the archive BYTE FOR BYTE.  Standard library only
(use: python -I rrs_pack.py ...).  The format is described in docs/rrs_format.md (optional, not part of the
public repository); the equivalent C++ writer is platform/RrsWriter.* (same encoders: DIB RLE16, ADPCM, compact MIDI).

Usage:
    python -I rrs_pack.py EXTRACTED_DIR/NOME [-o OUT.rrs] [--verify ORIGINAL.rrs] [--no-raw]
    python -I rrs_pack.py --all EXTRACTED_DIR --out FOLDER [--orig ORIGINALS_FOLDER]   (all sub-folders containing index.json)

For each entry:
  * if bin/NNNN.bin exists (extraction with --raw) and the converted file (PNG/WAV/MID) is unchanged (CRC32 in index.json),
    the original bytes are used;
  * otherwise it is re-encoded from the converted file: PNG -> DIB RLE16 (encoder identical to the original: verified 4659/4659),
    WAV -> ADPCM (greedy, inverse of the decoder: identical on decoded waves), .mid -> compact MIDI stream.
    With --no-raw it always re-encodes (encoder fidelity check).
Layout, physical order of data/pool, header and aliases are derived from index.json (entry offsets).
"""
import argparse
import itertools
import json
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rrs_extract as X  # noqa: E402  (ADPCM tables)

RAPT_MAGIC = 0x54504152


# ----------------------------------------------------------------------------
# Minimal PNG reader (8 bit, RGB/RGBA, non-interlaced, all filters)
# ----------------------------------------------------------------------------
def png_read(path):
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: not a PNG" % path)
    p = 8
    idat = b""
    w = h = ct = None
    while p < len(d):
        ln, t = struct.unpack_from(">I4s", d, p)
        body = d[p + 8:p + 8 + ln]
        p += 12 + ln
        if t == b"IHDR":
            w, h, bd, ct, _, _, il = struct.unpack(">IIBBBBB", body)
            if bd != 8 or ct not in (2, 6) or il:
                raise ValueError("%s: 8-bit non-interlaced RGB/RGBA PNG required" % path)
        elif t == b"IDAT":
            idat += body
        elif t == b"IEND":
            break
    bpp = 4 if ct == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray()
    prev = bytearray(stride)
    q = 0
    for _ in range(h):
        ft = raw[q]
        line = bytearray(raw[q + 1:q + 1 + stride])
        q += 1 + stride
        if ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif ft == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif ft == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        elif ft != 0:
            raise ValueError("PNG filter %d" % ft)
        out += line
        prev = line
    return w, h, bpp, bytes(out)


# ----------------------------------------------------------------------------
# DIB RLE16: same rules as platform/DibEncode.cpp
# ----------------------------------------------------------------------------
def rle_row(r):
    n = len(r)
    z = 0
    while z < n and r[n - 1 - z] == 0:
        z += 1
    if z >= 2:
        r = r[:n - z]
    o = bytearray()
    lit = []

    def flush():
        i = 0
        while i < len(lit):
            c = lit[i:i + 127]
            o.append(0x80 | len(c))
            o.extend(struct.pack("<%dH" % len(c), *c))
            i += 127
        del lit[:]

    for v, g in itertools.groupby(r):
        k = len(list(g))
        if k >= 2:
            flush()
            while k > 0:
                c = min(k, 127)
                o.append(c)
                o.extend(struct.pack("<H", v))
                k -= c
        else:
            lit.append(v)
    flush()
    o.append(0)
    return bytes(o)


def dib_encode(w, h, ox, oy, rows_bottom_up):
    """rows_bottom_up: list of h rows (lists of w RGB555 words; 0 = transparent)."""
    rs = [rle_row(r) for r in rows_bottom_up]
    total = 2 * h + sum(len(r) for r in rs)
    zeros = sum(r.count(0) for r in rows_bottom_up)
    fl = 2 | (4 if total > 0xFFFF else 0) | (1 if zeros == 0 else 0) | (8 if zeros == w * h else 0)
    off = h * (4 if fl & 4 else 2)
    tab = []
    for r in rs:
        tab.append(off)
        off += len(r)
    return (struct.pack("<HHhhBB", w, h, ox, oy, 16, fl)
            + struct.pack("<%d%s" % (h, "I" if fl & 4 else "H"), *tab) + b"".join(rs))


def dib_from_png(path, ox, oy):
    w, h, bpp, px = png_read(path)
    rows = []
    for y in range(h):  # PNG top-down -> file bottom-up
        row = []
        for x in range(w):
            q = (y * w + x) * bpp
            if bpp == 4 and px[q + 3] < 128:
                row.append(0)
            else:
                row.append(0x8000 | (px[q] >> 3) << 10 | (px[q + 1] >> 3) << 5 | (px[q + 2] >> 3))
        rows.append(row)
    rows.reverse()
    return dib_encode(w, h, ox, oy, rows)


# ----------------------------------------------------------------------------
# ADPCM (inverse of adpcm_decode): closest nibble, lowest one on ties
# ----------------------------------------------------------------------------
def adpcm_encode(pcm, step, adj, stereo):
    n = len(pcm)
    if n & 1:
        pcm = list(pcm) + [pcm[-1]]
    pred = [0, 0]
    idx = [0, 0]
    nib = bytearray(len(pcm))
    for i, x in enumerate(pcm):
        c = (i & 1) if stereo else 0
        base = idx[c]
        pr = pred[c]
        best = None
        for k in range(16):
            dl = step[base + (k & 7)]
            p = pr - dl if k & 8 else pr + dl
            p = 32767 if p > 32767 else (-32768 if p < -32768 else p)
            e = x - p
            if e < 0:
                e = -e
            if best is None or e < best[0]:
                best = (e, k, p)
        _, k, p = best
        pred[c] = p
        v = base + adj[k]
        idx[c] = 0 if v < 0 else (0x2C0 if v > 0x2C0 else v)
        nib[i] = k
    return bytes(nib[i] | nib[i + 1] << 4 for i in range(0, len(nib), 2))


def wav_read(path):
    d = open(path, "rb").read()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise ValueError("%s: not a WAV" % path)
    p = 12
    fmt = data = None
    while p + 8 <= len(d):
        t, ln = struct.unpack_from("<4sI", d, p)
        if t == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", d, p + 8)
        elif t == b"data":
            data = d[p + 8:p + 8 + ln]
        p += 8 + ln + (ln & 1)
    if fmt is None or data is None or fmt[0] != 1 or fmt[5] != 16:
        raise ValueError("%s: 16-bit PCM WAV required" % path)
    return fmt[1], fmt[2], struct.unpack("<%dh" % (len(data) // 2), data[:len(data) // 2 * 2])


# ----------------------------------------------------------------------------
# MIDI: SMF -> compact stream (like platform/MidiEncode.cpp)
# ----------------------------------------------------------------------------
def smf_to_compact(d):
    if d[:4] != b"MThd":
        raise ValueError("not a MIDI file")
    hl, = struct.unpack_from(">I", d, 4)
    fmt, ntrk, div = struct.unpack_from(">HHH", d, 8)
    if div & 0x8000 or div == 0:
        raise ValueError("SMPTE time division not supported")
    p = 8 + hl
    evs = []
    order = 0

    def vlq(buf, q):
        v = 0
        while True:
            b = buf[q]
            q += 1
            v = v << 7 | (b & 0x7F)
            if not b & 0x80:
                return v, q

    for _ in range(ntrk):
        assert d[p:p + 4] == b"MTrk"
        ln, = struct.unpack_from(">I", d, p + 4)
        p += 8
        end = p + ln
        tick = 0
        running = 0
        while p < end:
            dt, p = vlq(d, p)
            tick += dt
            st = d[p]
            if st == 0xFF:
                typ = d[p + 1]
                ml, p = vlq(d, p + 2)
                if typ == 0x51 and ml == 3:
                    evs.append((tick, order, "t", int.from_bytes(d[p:p + 3], "big")))
                elif typ == 0x2F:
                    evs.append((tick, order, "e", 0))
                order += 1
                p += ml
                running = 0
            elif st in (0xF0, 0xF7):
                sl, p = vlq(d, p + 1)
                p += sl
                running = 0
            else:
                if st & 0x80:
                    running = st
                    p += 1
                n = 1 if (running & 0xF0) in (0xC0, 0xD0) else 2
                dw = running | d[p] << 8 | (d[p + 1] << 16 if n == 2 else 0)
                p += n
                evs.append((tick, order, "m", dw))
                order += 1
        p = end
    evs.sort(key=lambda e: (e[0], e[1]))
    out = bytearray()
    us = 0.0
    last = 0
    tempo = 500000
    cur = 0
    for tick, _, k, v in evs:
        us += (tick - last) * tempo / div
        last = tick
        if k == "t":
            tempo = v or 500000
            continue
        ms = int(us / 1000.0 + 0.5)
        if ms > cur:
            gap = ms - cur
            while gap > 0:
                c = min(gap, 0x7FFFFFFF)
                out += struct.pack("<I", 0x80000000 | c)
                gap -= c
            cur = ms
        if k == "m":
            out += struct.pack("<I", v)
    return bytes(out)


# ----------------------------------------------------------------------------
# archive construction
# ----------------------------------------------------------------------------
def crc_of(path):
    return zlib.crc32(open(path, "rb").read()) & 0xFFFFFFFF


def ids_blob(e, hdr):
    ids = [x["id"] for x in e["ids"]]
    return struct.pack("<H", len(ids)).ljust(hdr, b"\0") + b"".join(struct.pack("<I", v) for v in ids)


def build_entry(e, base, step, adj, use_raw):
    """Returns (blob, is_pool) for non-alias entries."""
    typ = e["type"]
    if typ in (2, 3, 4, 5) and use_raw and "bin" in e:
        # original bytes as long as the converted file has not been modified
        unchanged = True
        for key in ("file", "raw_file"):
            if key in e and (key + "_crc32") in e:
                unchanged = unchanged and os.path.exists(os.path.join(base, e[key])) \
                    and crc_of(os.path.join(base, e[key])) == e[key + "_crc32"]
        if unchanged:
            return open(os.path.join(base, e["bin"]), "rb").read(), False
    if typ == 2:
        return dib_from_png(os.path.join(base, e["file"]), e["origin_x"], e["origin_y"]), False
    if typ in (3, 4):
        return open(os.path.join(base, e["file"]), "rb").read(), False
    if typ == 5:
        kind = e.get("sound_kind")
        par = e.get("sound_param", 0)
        if kind == "wave":
            ch, rate, pcm = wav_read(os.path.join(base, e["file"]))
            ba = ch * 2
            hdr = struct.pack("<IIHHIIHHBBH", 1, par, 1, ch, rate, rate * ba, ba, 16,
                              e.get("stream_flag", 0), 1 if e.get("adpcm", True) else 0, 0)
            if e.get("adpcm", True):
                body = adpcm_encode(pcm, step, adj, ch == 2)
            else:
                body = struct.pack("<%dh" % len(pcm), *pcm)
            return hdr + body, False
        if kind == "midi":
            mid = open(os.path.join(base, e["file"]), "rb").read()
            return struct.pack("<II", 2, par) + smf_to_compact(mid), False
        if kind == "avi":
            return (struct.pack("<II", 3, par) + e["avi_name"].encode("latin1")).ljust(8 + 0x104, b"\0"), False
        return open(os.path.join(base, e["file"]), "rb").read(), False
    if typ in (6, 8):
        return ids_blob(e, 4), True
    if typ == 11:
        return ids_blob(e, 8), True
    if typ == 7:
        return struct.pack("<Ihh", e["target"]["id"], e["hotspot_x"], e["hotspot_y"]) + b"\0\0\0\0", True
    if typ == 9:
        return bytes.fromhex(e["hex"]), True
    if typ == 12:
        return e["text"].encode("latin1") + b"\0", True
    raise ValueError("type %d not supported (entry %d)" % (typ, e["index"]))


def pack_archive(base, step, adj, use_raw=True):
    idx = json.load(open(os.path.join(base, "index.json"), encoding="utf-8"))
    ents = idx["entries"]
    n = len(ents)
    blobs = [None] * n
    pool_flag = [False] * n
    for e in ents:
        if "error" in e:
            raise ValueError("entry %d: extraction had an error (%s)" % (e["index"], e["error"]))
        if e["type"] == 1:
            continue
        blobs[e["index"]], pool_flag[e["index"]] = build_entry(e, base, step, adj, use_raw)
    # physical order = original offsets
    key_pos = lambda e: e.get("pool_offset", e.get("offset", 0))  # noqa: E731
    file_order = sorted((e["index"] for e in ents if e["type"] in (2, 3, 4, 5)), key=lambda i: (key_pos(ents[i]), i))
    pool_order = sorted((e["index"] for e in ents if e["type"] in (6, 7, 8, 9, 11, 12)), key=lambda i: (key_pos(ents[i]), i))
    pool = bytearray()
    off = {}
    for i in pool_order:
        off[i] = len(pool)
        pool += blobs[i]
    dir_size = 12 * n
    dir_first = idx["keys"][0]["offset"] == 0x38
    if dir_first:
        dir_off = 0x38
        pool_off = dir_off + dir_size if pool else 0
        pos = 0x38 + dir_size + len(pool)
    else:
        pos = 0x38
    data = bytearray()
    for i in file_order:
        off[i] = pos + len(data)
        data += blobs[i]
    if not dir_first:
        dir_off = 0x38 + len(data)
        pool_off = dir_off + dir_size if pool else 0
    recs = bytearray()
    for e in ents:
        i = e["index"]
        if e["type"] == 1:
            tgt = e["alias_of"]
            while ents[tgt]["type"] == 1:  # size of the final target
                tgt = ents[tgt]["alias_of"]
            recs += struct.pack("<IIHH", e["alias_of"] | e.get("alias_sub", 0) << 16, len(blobs[tgt]), 1, e["tag"])
        else:
            recs += struct.pack("<IIHH", off[i], len(blobs[i]), e["type"], e["tag"])
    hdr = struct.pack("<IIII", RAPT_MAGIC, idx.get("version", 0x10000), 0x38, idx.get("reserved", 0))
    hdr += struct.pack("<II", dir_off, dir_size) + struct.pack("<II", pool_off, len(pool)) + b"\0" * 24
    if dir_first:
        return bytes(hdr + recs + pool + data)
    return bytes(hdr + data + recs + pool)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", nargs="?", help="<extracted>/<name> folder containing index.json")
    ap.add_argument("-o", "--output", help="output .rrs file (default <dir>/../<name>.rrs.packed)")
    ap.add_argument("--verify", help="compare the result with this original .rrs")
    ap.add_argument("--all", help="folder containing the extracted sub-folders")
    ap.add_argument("--out", help="with --all: output folder")
    ap.add_argument("--orig", help="with --all: folder with the originals (verification)")
    ap.add_argument("--no-raw", action="store_true", help="always re-encode from the converted files (ignore bin/)")
    a = ap.parse_args()
    step, adj = X.adpcm_tables()
    jobs = []
    if a.all:
        for nme in sorted(os.listdir(a.all)):
            if os.path.exists(os.path.join(a.all, nme, "index.json")):
                jobs.append((os.path.join(a.all, nme), os.path.join(a.out or a.all, nme + ".rrs"),
                             os.path.join(a.orig, nme + ".rrs") if a.orig else None))
    elif a.dir:
        base = a.dir.rstrip("/\\")
        jobs.append((base, a.output or base + ".rrs.packed", a.verify))
    else:
        ap.error("a folder or --all is required")
    bad = 0
    for base, outp, orig in jobs:
        data = pack_archive(base, step, adj, not a.no_raw)
        os.makedirs(os.path.dirname(os.path.abspath(outp)), exist_ok=True)
        open(outp, "wb").write(data)
        msg = "%-12s %9d bytes -> %s" % (os.path.basename(base), len(data), outp)
        if orig:
            o = open(orig, "rb").read()
            same = o == data
            bad += not same
            msg += "  %s" % ("IDENTICAL" if same else "DIFFERENT (first mismatch @%d)" % next(
                (i for i, (x, y) in enumerate(zip(o, data)) if x != y), min(len(o), len(data))))
        print(msg)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
