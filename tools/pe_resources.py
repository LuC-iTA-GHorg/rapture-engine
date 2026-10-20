"""pe_resources.py - read the resources of a PE32/PE32+ file (standard library only, no pefile).

Generic tool: it knows nothing about any particular game.  It is used at build time by rapture_add_win32_resources()
(see cmake/RaptureWin32Resources.cmake) to turn the icon of a file the USER owns into a multi-image .ico, and by
extract_host_strings.py to read the RT_STRING table.  Nothing extracted here is ever stored in a repository.

Command line:
    python -I pe_resources.py list    <pe>                    list the resource types / ids / languages
    python -I pe_resources.py ico     <pe> <out.ico> [group]  write the icon group `group` (id, default: first) as .ico
Exit status of `ico`: 0 = written, 1 = no icon in the file (nothing written), 2 = unreadable file.
"""
import struct
import sys

RT_ICON, RT_STRING, RT_GROUP_ICON, RT_VERSION, RT_MANIFEST = 3, 6, 14, 16, 24
_NAMES = {1: 'CURSOR', 2: 'BITMAP', 3: 'ICON', 4: 'MENU', 5: 'DIALOG', 6: 'STRING', 9: 'ACCELERATOR', 10: 'RCDATA',
          12: 'GROUP_CURSOR', 14: 'GROUP_ICON', 16: 'VERSION', 24: 'MANIFEST'}


class PEResources:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.d = f.read()
        d = self.d
        if d[:2] != b'MZ' or len(d) < 0x40:
            raise ValueError('%s: not a PE file' % path)
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        if d[pe:pe + 4] != b'PE\0\0':
            raise ValueError('%s: not a PE file' % path)
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        osz = struct.unpack_from('<H', d, pe + 20)[0]
        magic = struct.unpack_from('<H', d, pe + 24)[0]
        ddir = pe + 24 + (96 if magic == 0x10b else 112)
        self.rsrc_rva = struct.unpack_from('<I', d, ddir + 2 * 8)[0]
        self.secs = []
        for i in range(nsec):
            o = pe + 24 + osz + i * 40
            vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
            self.secs.append((va, vs, ro, rs))
        self.root = self._off(self.rsrc_rva) if self.rsrc_rva else None

    def _off(self, rva):
        for va, vs, ro, rs in self.secs:
            if va <= rva < va + max(vs, rs):
                return ro + rva - va
        raise ValueError('RVA 0x%x not in any section' % rva)

    def _dir(self, off):
        nn, ni = struct.unpack_from('<HH', self.d, off + 12)
        ents = []
        for i in range(nn + ni):
            nid, tgt = struct.unpack_from('<II', self.d, off + 16 + 8 * i)
            is_name = bool(nid & 0x80000000)
            ents.append((nid & 0x7fffffff, is_name, bool(tgt & 0x80000000), tgt & 0x7fffffff))
        return ents

    def entries(self):
        """All resource leaves: [(type, id, lang, bytes)]; named types/ids are reported as the string-offset value (rare)."""
        out = []
        if self.root is None:
            return out
        for t, _tn, tdir, toff in self._dir(self.root):
            if not tdir:
                continue
            for i, _in, idir, ioff in self._dir(self.root + toff):
                if not idir:
                    continue
                for lang, _ln, ldir, loff in self._dir(self.root + ioff):
                    if ldir:
                        continue
                    rva, size = struct.unpack_from('<II', self.d, self.root + loff)
                    o = self._off(rva)
                    out.append((t, i, lang, self.d[o:o + size]))
        return out

    def get(self, rtype, rid=None):
        """First resource of a type (and id, if given): bytes or None."""
        for t, i, _l, data in self.entries():
            if t == rtype and (rid is None or i == rid):
                return data
        return None

    def group_icons(self):
        """[group id] of the RT_GROUP_ICON resources, in directory order."""
        seen = []
        for t, i, _l, _d in self.entries():
            if t == RT_GROUP_ICON and i not in seen:
                seen.append(i)
        return seen

    def icon_as_ico(self, group=None):
        """Bytes of a .ico built from RT_GROUP_ICON `group` (default first) and its RT_ICON images, or None."""
        ents = self.entries()
        icons = {i: data for t, i, _l, data in ents if t == RT_ICON}
        grp = None
        for t, i, _l, data in ents:
            if t == RT_GROUP_ICON and (group is None or i == group):
                grp = data
                break
        if grp is None:
            return None
        _res, typ, count = struct.unpack_from('<HHH', grp, 0)
        if typ != 1:
            return None
        items = []
        for k in range(count):
            w, h, cc, rsv, planes, bits, size, iid = struct.unpack_from('<BBBBHHIH', grp, 6 + 14 * k)
            if iid in icons:
                items.append((w, h, cc, rsv, planes, bits, icons[iid]))
        if not items:
            return None
        out = bytearray(struct.pack('<HHH', 0, 1, len(items)))
        off = 6 + 16 * len(items)
        for w, h, cc, rsv, planes, bits, img in items:
            out += struct.pack('<BBBBHHII', w, h, cc, rsv, planes, bits, len(img), off)
            off += len(img)
        for it in items:
            out += it[6]
        return bytes(out)

    def string_table(self, want_lang=0x410):
        """RT_STRING resources -> {string id: text}.  Language `want_lang` wins, otherwise the first one found."""
        best = {}
        for t, bid, lang, data in self.entries():
            if t != RT_STRING:
                continue
            pos = 0
            for i in range(16):
                n = int.from_bytes(data[pos:pos + 2], 'little')
                pos += 2
                s = data[pos:pos + 2 * n].decode('utf-16le')
                pos += 2 * n
                if n:
                    sid = (bid - 1) * 16 + i
                    if sid not in best or lang == want_lang:
                        best[sid] = s
        return best


def _main(argv):
    if len(argv) < 3 or argv[1] not in ('list', 'ico'):
        print(__doc__)
        return 2
    try:
        r = PEResources(argv[2])
    except (OSError, ValueError, struct.error) as e:
        print('pe_resources: %s' % e, file=sys.stderr)
        return 2
    if argv[1] == 'list':
        for t, i, lang, data in r.entries():
            print('%-12s id=%-6s lang=0x%04x size=%d' % (_NAMES.get(t, t), i, lang, len(data)))
        return 0
    group = int(argv[4]) if len(argv) > 4 else None
    ico = r.icon_as_ico(group)
    if ico is None:
        print('pe_resources: no icon in %s' % argv[2], file=sys.stderr)
        return 1
    with open(argv[3], 'wb') as f:
        f.write(ico)
    print('%s: icon group -> %s (%d bytes)' % (argv[2], argv[3], len(ico)))
    return 0


if __name__ == '__main__':
    sys.exit(_main(sys.argv))
