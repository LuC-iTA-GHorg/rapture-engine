#!/usr/bin/env python3
"""queuemsg_usage.py - WP-6: which QUEUEMSG opcodes do the game binaries use?

Usage: python -I tools/queuemsg_usage.py [--md]     (stdlib only; game folders are read only)
Set RAPTURE_GAMES_ROOT to the folder that contains the game folders (default: current directory).

Heuristic (static, no emulation):
  A queue in the ORIGINAL binaries is an array of pointers to messages (QUEUEMSG**, moses.md sec. 5); each message
  starts with a dword opcode (1..15 common, 100..105 class specific).  For every binary (host exe + scene dll) we
    1. take the candidate pointer cells: if the image has a .reloc table, the cells it relocates (absolute pointers
       in a DLL are always relocated); for a fixed-base exe without relocations, every aligned dword whose value
       points inside a non-executable section;
    2. keep a cell as "message pointer" when its target is a plausible message: opcode valid and the fields sane for
       that opcode (e.g. LOOPEND/FLIP/SYNC/SHOW have small fields, CALL's +4 is a pointer into an executable
       section, SUBLIST/GOTO's +4 points to a data array, count <= 4096, ...);
    3. a table is a run of >= 2 consecutive message-pointer cells (or a single cell that is the target of a
       SUBLIST/GOTO); the opcodes counted are those of the DISTINCT messages referenced by tables (messages shared by
       several tables count once), per game.
The host exe is scanned only to show the heuristic's false-positive noise (17 hits in every game, all identical
in kind: it has no animation queues), and is NOT counted.
Limits: tables built at run time (queues filled by code) are invisible; a single-message queue that nothing nests is
missed (it is in practice built with QStart(obj,&p,1) on stack/static data); false positives are possible for small
opcodes (1..4) in runs of unrelated pointers, so counts are an upper bound, and "0" means "not found", not
"proved unused".  Class opcodes >= 100 are reported once (CAnimDib 100..103 vs CAnimRect iris 100..105 share values:
100/101 are attributed to the CAnimDib meaning; 104/105 are certainly iris).
"""
import os
import struct
import sys

ROOT = os.environ.get('RAPTURE_GAMES_ROOT', os.getcwd())
GAMES = [
    ('M', 'MuShoot', os.path.join(ROOT, 'MUSHOOT'), ['Shoot.exe', 'Shoot.dll']),
    ('F', 'Flying', os.path.join(ROOT, 'Flying'), ['Flying.exe', 'Frenzy.dll']),
    ('Q', 'Flounder', os.path.join(ROOT, 'Flounder'), ['Quest.exe', 'quest.dll']),
    ('R', 'Ruckus', os.path.join(ROOT, 'Ruckus'), ['Ruckus.exe', 'Ruckus.dll']),
]
NAMES = {1: 'LOOP', 2: 'DELAY', 3: 'LOOPEND', 4: 'FLIP', 5: 'CALL', 6: 'SUBLIST', 7: 'GOTO', 8: 'LINETO', 9: 'NOTIFY',
         10: 'MOVE', 11: 'POST', 12: 'SEND', 13: 'SYNC', 14: 'SHOW', 15: 'SETSPEED', 100: 'ANIM / IRIS_CLOSE_XY',
         101: 'ANIM_CB / IRIS_OPEN_XY', 102: 'ANIMLIST / IRIS_CLOSE_X', 103: 'SETRANGE / IRIS_OPEN_X',
         104: 'IRIS_CLOSE_Y', 105: 'IRIS_OPEN_Y'}


class PE:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        nsec, = struct.unpack_from('<H', d, pe + 6)
        osz, = struct.unpack_from('<H', d, pe + 20)
        self.base, = struct.unpack_from('<I', d, pe + 24 + 28)
        self.relocdir = struct.unpack_from('<II', d, pe + 24 + 96 + 5 * 8)
        self.secs = []
        for i in range(nsec):
            o = pe + 24 + osz + i * 40
            vs = struct.unpack_from('<I', d, o + 8)[0]
            va = struct.unpack_from('<I', d, o + 12)[0]
            rs = struct.unpack_from('<I', d, o + 16)[0]
            ro = struct.unpack_from('<I', d, o + 20)[0]
            ch = struct.unpack_from('<I', d, o + 36)[0]
            self.secs.append((va, max(vs, rs), ro, rs, ch))

    def off(self, va):
        r = va - self.base
        for sva, sz, ro, rs, ch in self.secs:
            if sva <= r < sva + sz and r - sva + 4 <= rs:
                return ro + r - sva
        return None

    def sec(self, va):
        r = va - self.base
        for s in self.secs:
            if s[0] <= r < s[0] + s[1]:
                return s
        return None

    def isdata(self, va):
        s = self.sec(va)
        return s is not None and not (s[4] & 0x20000000) and s[3] > 0

    def iscode(self, va):
        s = self.sec(va)
        return s is not None and bool(s[4] & 0x20000000)

    def u32(self, va):
        o = self.off(va)
        return None if o is None else struct.unpack_from('<I', self.d, o)[0]

    def relocs(self):
        out = set()
        rva, size = self.relocdir
        if not rva or not size:
            return None
        o = None
        for sva, sz, ro, rs, ch in self.secs:
            if sva <= rva < sva + sz:
                o = ro + rva - sva
        if o is None:
            return None
        end = o + size
        while o + 8 <= end:
            page, blk = struct.unpack_from('<II', self.d, o)
            if blk < 8:
                break
            for i in range((blk - 8) // 2):
                e, = struct.unpack_from('<H', self.d, o + 8 + 2 * i)
                if e >> 12 == 3:
                    out.add(self.base + page + (e & 0xfff))
            o += blk
        return out


def valid_msg(pe, a):
    op = pe.u32(a)
    if op is None:
        return False
    w = [pe.u32(a + 4 * i) for i in range(5)]
    s = lambda v: v - (1 << 32) if v is not None and v & 0x80000000 else v
    if op in (1,):
        return w[1] is not None and 0 <= w[1] < 100000
    if op == 2:
        return w[1] is not None and 0 <= w[1] < 600000
    if op == 3:
        return True
    if op == 4:
        return w[1] is not None and (w[1] & 0xffff0000) == 0
    if op == 5:
        return w[1] is not None and pe.iscode(w[1])
    if op in (6, 7):
        return w[1] is not None and pe.isdata(w[1]) and w[2] is not None and 0 < w[2] < 4096 and pe.u32(w[1]) is not None and pe.isdata(pe.u32(w[1]) or 0)
    if op == 8:
        return all(abs(s(x)) < 100000 for x in w[1:4])
    if op == 9:
        return True and w[1] is not None and w[1] < 0x10000
    if op == 10:
        return abs(s(w[1])) < 10000 and abs(s(w[2])) < 10000 and w[3] is not None and w[3] < 0x10000
    if op in (11, 12):
        return w[1] is not None and 0 < w[1] < 0x10000
    if op in (13, 14):
        return w[1] is not None and w[1] < 0x100
    if op == 15:
        return abs(s(w[1])) < 100000 and abs(s(w[2])) < 100000
    if op == 100:
        return abs(s(w[1])) < 100000 and w[2] is not None and (w[2] >> 16) < 0x1000
    if op == 101:
        return abs(s(w[1])) < 100000 and w[2] is not None and (w[2] >> 16) < 0x1000 and pe.u32(a + 0x10) is not None and pe.iscode(pe.u32(a + 0x10))
    if op == 102:
        return w[2] is not None and 1 <= w[2] <= 4 and abs(s(w[1])) < 100000
    if op == 103:
        return True
    if op in (104, 105):
        return abs(s(w[1])) < 100000 and w[2] is not None and 0 < w[2] < 1000
    return False


def scan(path):
    """Returns {msg address: opcode} of distinct messages referenced from tables of one binary."""
    pe = PE(path)
    rel = pe.relocs()
    cells = {}  # cell address -> message address
    for sva, sz, ro, rs, ch in pe.secs:
        if ch & 0x20000000 or rs == 0:
            continue
        for o in range(0, rs - 3, 4):
            va = pe.base + sva + o
            if rel is not None and va not in rel:
                continue
            v = struct.unpack_from('<I', pe.d, ro + o)[0]
            if v & 3 or not pe.isdata(v) or v == va:
                continue
            if valid_msg(pe, v):
                cells[va] = v
    msgs = {}
    # runs of >= 2 consecutive cells
    for va in sorted(cells):
        if (va + 4 in cells) or (va - 4 in cells):
            m = cells[va]
            msgs[m] = pe.u32(m)
    # single cells that are targets of SUBLIST / GOTO
    for m in list(msgs):
        if msgs[m] in (6, 7):
            t = pe.u32(m + 4)
            if t in cells:
                msgs[cells[t]] = pe.u32(cells[t])
    return msgs


def main():
    use = {}
    notes = []
    for code, name, folder, bins in GAMES:
        total = 0
        for b in bins:
            p = os.path.join(folder, b)
            if not os.path.exists(p):
                notes.append('%s: %s missing' % (name, b))
                continue
            msgs = scan(p)
            total += len(msgs)
            if b.lower().endswith('.exe'):  # host exe: no queue tables (hits are runs of unrelated data); not counted
                notes.append('  (host exe not counted: %d hits are false positives)' % len(msgs))
                continue
            for op in msgs.values():
                use.setdefault(op, {}).setdefault(code, 0)
                use[op][code] += 1
            notes.append('%s: %s -> %d distinct messages in tables' % (name, b, len(msgs)))
    rows = []
    for op in sorted(NAMES):
        u = use.get(op, {})
        by = '/'.join('%s(%d)' % (c, u[c]) for c in 'MFQR' if c in u) or '-'
        rows.append((op, NAMES[op], by, sum(u.values())))
    md = '--md' in sys.argv
    for n in notes:
        print(('<!-- %s -->' if md else '# %s') % n)
    print('| opcode | name | used by (M=MuShoot F=Flying Q=Flounder R=Ruckus; distinct messages in the tables) |')
    print('|---:|---|---|')
    for op, nm, by, tot in rows:
        print('| %d | %s | %s |' % (op, nm, by))


if __name__ == '__main__':
    main()
