#!/usr/bin/env python3
"""engine_coverage.py - which moses.dll exports / Aaron.dll ordinals are used by each game of the family and which of
them the port (engine/) implements.  Re-runnable; writes engine/docs/ENGINE_API_COVERAGE.md.

    python -I engine/tools/engine_coverage.py [--out engine/docs/ENGINE_API_COVERAGE.md]
                                       [--game NAME=FOLDER ...]   (default: the 4 games, see DEFAULT_GAMES; the FIRST one is the reference
                                                  game the port already runs: what it imports counts as implemented)
                                       [--engine-dir FOLDER]      (folder with the reference moses.dll / Aaron.dll; default = first game)

What it does
  1. reads with pefile the import tables of every binary of each game folder (all *.exe / *.dll except moses.dll, Aaron.dll,
     Uninst.dll and Launcher.exe): imports from MOSES.DLL are by decorated name, from AARON.DLL by ordinal;
  2. reads the export tables of the reference moses.dll / Aaron.dll (byte-identical in the 4 games) and names them with
     optional name lists from a separate analysis folder (not part of this repository; without them raw mangled names / addresses are shown);
  3. greps the port sources (engine/moses, engine/aaron; tests and Ordinals.h excluded) for each function:
       - by original address (every port function carries `// moses.dll 0x1000xxxx` / `// Aaron ord N 0x1000xxxx`);
       - for moses also by name (Class + Method in the same file) as a fallback.
     "yes"     = the address (or Class::Method) appears in a .cpp, or in a header line that has a body ({ ... });
     "partial" = only declared in a header (no body found): check it by hand;
     "no"      = no trace in the port;     "n.a." = compiler artefact (default constructor closure) or class operator new/delete.
It is a heuristic (text search, not a compiler): the "no" rows are certain, "yes" rows are very likely, "partial" must be
checked.  MANUAL below overrides single rows when a human has verified them.

pefile: the user site-packages is added to sys.path (needed with -I).
"""
import argparse
import collections
import datetime
import os
import re
import site
import sys

try:
    import pefile
except ImportError:
    sys.path.insert(0, site.getusersitepackages())
    import pefile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import completeness_report  # noqa: E402
ENGINE = os.path.normpath(os.path.join(HERE, '..'))   # the engine folder (engine/)
REPO = os.path.normpath(os.path.join(ENGINE, '..')) if os.path.basename(ENGINE) == 'engine' else ENGINE   # the game repository (its parent folder holds the game folders)
GAMES_ROOT = os.path.normpath(os.path.join(REPO, '..', '..'))   # folder containing the game folders (adjust, or use --game NAME=FOLDER)

DEFAULT_GAMES = collections.OrderedDict([
    ('MuShoot', os.path.normpath(os.path.join(REPO, '..'))),
    ('Flying', os.path.join(GAMES_ROOT, 'Flying')),
    ('Flounder', os.path.join(GAMES_ROOT, 'Flounder')),
    ('Ruckus', os.path.join(GAMES_ROOT, 'Ruckus')),
])
SHORT = {'MuShoot': 'M', 'Flying': 'F', 'Flounder': 'Q', 'Ruckus': 'R'}   # column letters (Q = Quest)
ENGINE_DLLS = ('moses.dll', 'aaron.dll')
SKIP = ENGINE_DLLS + ('uninst.dll', 'launcher.exe')

# Verified by hand: key = ('moses', mangled) or ('aaron', ordinal) -> (status, note)
MANUAL = {
    # empty exported functions, implemented inline in the header (the address search only scans .cpp bodies)
    ('aaron', 226): ('yes', 'empty function, inline in aaron/Sprites.h'),
    ('aaron', 237): ('yes', 'empty function, inline in aaron/Widget.h'),
}

# --------------------------------------------------------------------------------------------------------------------
# PE helpers


def load_pe(path):
    return pefile.PE(path, fast_load=False)


def game_binaries(folder):
    out = []
    for n in sorted(os.listdir(folder)):
        if n.lower().endswith(('.exe', '.dll')) and n.lower() not in SKIP:
            out.append(os.path.join(folder, n))
    return out


def imports_of(path):
    """-> {'moses': set(mangled names), 'aaron': set(ordinals)}"""
    res = {'moses': set(), 'aaron': set()}
    pe = load_pe(path)
    for ent in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
        dll = ent.dll.decode().lower()
        key = 'moses' if dll == 'moses.dll' else 'aaron' if dll == 'aaron.dll' else None
        if not key:
            continue
        for imp in ent.imports:
            if key == 'moses':
                if imp.name:
                    res['moses'].add(imp.name.decode())
            else:
                if imp.ordinal is not None and not imp.name:
                    res['aaron'].add(int(imp.ordinal))
    return res


def exports_of(path):
    """-> list of (ordinal, name or None, rva)"""
    pe = load_pe(path)
    return [(s.ordinal, s.name.decode() if s.name else None, s.address) for s in pe.DIRECTORY_ENTRY_EXPORT.symbols]


# --------------------------------------------------------------------------------------------------------------------
# Names from the RE notes


def read_exports_txt(path):
    """re/out/current/<dll>/exports.txt -> (by_ordinal, by_mangled, by_addr)  value = demangled name"""
    by_ord, by_mang, by_addr = {}, {}, {}
    if not os.path.exists(path):
        return by_ord, by_mang, by_addr
    for line in open(path, encoding='utf-8', errors='replace'):
        if line.startswith('//') or not line.strip():
            continue
        p = line.rstrip('\n').split('\t')
        if len(p) < 3:
            continue
        addr, ords, name = p[0], p[1], p[2]
        mang = p[3] if len(p) > 3 else ''
        by_addr[addr.lower()] = name
        for o in ords.split(','):
            if o.strip().isdigit():
                by_ord[int(o)] = name
        if mang:
            by_mang[mang] = name
    return by_ord, by_mang, by_addr


def read_function_names(path, binary):
    d = {}
    if os.path.exists(path):
        for line in open(path, encoding='utf-8', errors='replace'):
            if line.startswith('#'):
                continue
            p = line.rstrip('\n').split('\t')
            if len(p) == 3 and p[0].lower() == binary.lower():
                d[p[1].lower().replace('0x', '')] = p[2]
    return d


# --------------------------------------------------------------------------------------------------------------------
# Search in the port


class PortIndex:
    def __init__(self, dirs):
        self.files = []   # (path, text, lines)
        for d in dirs:
            for root, _, names in os.walk(d):
                for n in names:
                    if not n.endswith(('.cpp', '.h', '.inc')) or n == 'Ordinals.h':
                        continue
                    p = os.path.join(root, n)
                    t = open(p, encoding='utf-8', errors='replace').read()
                    self.files.append((p, t, t.split('\n')))
        self.addr_cache = {}

    def by_address(self, addr_hex):
        """addr_hex like '10008d18' -> 'yes' | 'partial' | None"""
        if addr_hex in self.addr_cache:
            return self.addr_cache[addr_hex]
        status = None
        pat = re.compile(r'0x' + addr_hex + r'\b', re.I)
        for p, t, lines in self.files:
            if not pat.search(t):
                continue
            if p.endswith(('.cpp', '.inc')):
                status = 'yes'
                break
            for i, ln in enumerate(lines):
                if pat.search(ln):
                    # a header: body on the same line, or the next line opens a brace / `= default` / `override {`
                    if '{' in ln or '= default' in ln or '=default' in ln:
                        status = 'yes'
                    elif status is None:
                        status = 'partial'
            if status == 'yes':
                break
        self.addr_cache[addr_hex] = status
        return status

    def by_name(self, cls, method):
        """Class::Method defined in a .cpp -> yes; mentioned in a header of the class -> partial"""
        defn = re.compile(r'\b' + re.escape(cls) + r'::(?:~)?' + re.escape(method) + r'\s*\(')
        decl = re.compile(r'\b(?:~)?' + re.escape(method) + r'\s*\(')
        cls_re = re.compile(r'\b' + re.escape(cls) + r'\b')
        st = None
        for p, t, lines in self.files:
            if defn.search(t) and p.endswith('.cpp'):
                return 'yes'
            if p.endswith('.h') and cls_re.search(t):
                for ln in lines:
                    if decl.search(ln):
                        if '{' in ln:
                            return 'yes'
                        st = 'partial'
        return st


def split_demangled(name):
    """'CDrawObj::GetPosition' -> ('CDrawObj','GetPosition'); free functions -> (None, name)"""
    name = re.sub(r'`[^\']*\'', '', name)
    if '::' in name:
        cls, _, m = name.rpartition('::')
        return cls.split('::')[-1], m
    return None, name


# --------------------------------------------------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ENGINE, 'docs', 'ENGINE_API_COVERAGE.md'))
    ap.add_argument('--game', action='append', metavar='NAME=FOLDER')
    ap.add_argument('--engine-dir', default=None)
    ap.add_argument('--completeness', default=os.path.join(ENGINE, 'docs', 'COMPLETENESS.md'),
                    help='plan whose block between the GENERATED markers is rewritten (skipped if the file is missing)')
    a = ap.parse_args()

    games = collections.OrderedDict(DEFAULT_GAMES)
    if a.game:
        games = collections.OrderedDict()
        for g in a.game:
            n, _, f = g.partition('=')
            games[n] = f
    engine_dir = a.engine_dir or next(iter(games.values()))
    for n, f in games.items():
        if not os.path.isdir(f):
            sys.exit('game folder not found: %s=%s' % (n, f))

    moses = os.path.join(engine_dir, 'moses.dll')
    aaron = os.path.join(engine_dir, 'Aaron.dll')
    mexp = exports_of(moses)
    aexp = exports_of(aaron)

    # --- usage
    used = {'moses': collections.defaultdict(set), 'aaron': collections.defaultdict(set)}   # key -> set(game)
    per_game_bins = {}
    for g, folder in games.items():
        per_game_bins[g] = [os.path.basename(b) for b in game_binaries(folder)]
        for b in game_binaries(folder):
            im = imports_of(b)
            for m in im['moses']:
                used['moses'][m].add(g)
            for o in im['aaron']:
                used['aaron'][o].add(g)
    # Aaron.dll's own imports from moses (the engine uses moses internally)
    aaron_uses_moses = imports_of(aaron)['moses']

    ref_game = next(iter(games))   # the game the port already runs (first of the list): what it uses is implemented by definition

    # --- names
    out_dir = os.path.join(ENGINE, 're', 'out', 'current')
    m_by_ord, m_by_mang, m_by_addr = read_exports_txt(os.path.join(out_dir, 'moses.dll', 'exports.txt'))
    a_by_ord, _, a_by_addr = read_exports_txt(os.path.join(out_dir, 'Aaron.dll', 'exports.txt'))
    fn_names = os.path.join(ENGINE, 're', 'notes', 'function_names.tsv')
    a_names = read_function_names(fn_names, 'Aaron.dll')
    for line in open(os.path.join(ENGINE, 're', 'notes', 'aaron_ordinals.tsv'), encoding='utf-8', errors='replace') if os.path.exists(os.path.join(ENGINE, 're', 'notes', 'aaron_ordinals.tsv')) else []:
        p = line.rstrip('\n').split('\t')
        if len(p) == 3 and p[1].startswith('0x'):
            a_names.setdefault(p[1][2:].lower(), p[2])

    idx_m = PortIndex([os.path.join(ENGINE, 'moses')])
    idx_a = PortIndex([os.path.join(ENGINE, 'aaron')])
    idx_all = PortIndex([os.path.join(ENGINE, 'moses'), os.path.join(ENGINE, 'aaron')])

    def port_status(kind, key, addr, dem):
        if (kind, key) in MANUAL:
            return MANUAL[(kind, key)]
        if kind == 'moses' and dem and 'default_constructor_closure' in dem:
            return 'n.a.', 'compiler thunk'
        st = (idx_m if kind == 'moses' else idx_a).by_address('%08x' % addr)
        if st == 'yes':
            return 'yes', ''
        if kind == 'moses' and dem:
            cls, meth = split_demangled(dem)
            if cls and meth in ('operator_new', 'operator_delete') or (dem and re.search(r'operator[ _](new|delete)', dem)):
                return 'n.a.', 'class operator new/delete: the port uses the global new/delete (to be verified)'
            if cls:
                s2 = idx_m.by_name(cls, meth if meth != cls else cls)
                if s2 == 'yes':
                    return 'yes', 'by name'
                if s2 == 'partial' and st is None:
                    return 'partial', 'declared only (by name)'
            else:
                s2 = idx_m.by_name('', meth) if False else None
                for p, t, _ in idx_m.files:
                    if re.search(r'\b' + re.escape(meth.lstrip('_')) + r'\s*\(', t):
                        return 'partial', 'free function found by name'
        if kind == 'aaron' and dem and re.search(r'operator_(new|delete)', dem):
            return 'n.a.', 'class operator new/delete: global new/delete (Ordinals.h)'
        if st == 'partial':
            return 'partial', 'declared only'
        return 'no', ''

    # --- moses rows
    base_m = pefile.PE(moses).OPTIONAL_HEADER.ImageBase
    mrows = []
    for ordv, name, rva in sorted(mexp, key=lambda e: e[0]):
        addr = base_m + rva
        mang = name or ''
        dem = m_by_mang.get(mang) or m_by_ord.get(ordv) or mang
        st, note = port_status('moses', mang, addr, dem)
        if st in ('partial', 'no') and ref_game in used['moses'].get(mang, set()):
            st, note = 'yes', 'used by %s (the port builds and runs): declared/inline, address not in a comment' % ref_game
        users = used['moses'].get(mang, set())
        mrows.append(dict(ord=ordv, addr=addr, mang=mang, dem=dem, st=st, note=note, users=users,
                          aaron_uses=mang in aaron_uses_moses))

    # --- aaron rows
    base_a = pefile.PE(aaron).OPTIONAL_HEADER.ImageBase
    arows = []
    for ordv, name, rva in sorted(aexp, key=lambda e: e[0]):
        addr = base_a + rva
        ah = '%08x' % addr
        nm = a_names.get(ah)
        dem0 = a_by_ord.get(ordv) or a_by_addr.get(ah) or ''
        if not nm and dem0 and not dem0.startswith('FUN_') and not dem0.startswith('AARON_'):
            nm = dem0
        st, note = port_status('aaron', ordv, addr, nm or dem0)
        if st in ('partial', 'no') and ref_game in used['aaron'].get(ordv, set()):
            st, note = 'yes', 'used by %s (the port builds and runs): declared/inline or alias, address not in a comment' % ref_game
        arows.append(dict(ord=ordv, addr=addr, name=nm or '', st=st, note=note, users=used['aaron'].get(ordv, set())))

    write_md(a.out, games, per_game_bins, mrows, arows, aaron_uses_moses)
    completeness_report.write_completeness(a.completeness, ENGINE, games, SHORT, mrows, arows, mark)
    # summary on stdout
    for title, rows in (('moses', mrows), ('Aaron', arows)):
        c = collections.Counter(r['st'] for r in rows)
        print('%s: %d exports  %s' % (title, len(rows), dict(c)))
        for g in games:
            ug = [r for r in rows if g in r['users']]
            gaps = [r for r in ug if r['st'] in ('no', 'partial')]
            print('  %-9s uses %3d, gaps (no/partial) %d' % (g, len(ug), len(gaps)))
    print('written', a.out)


def mark(users, games):
    return ' '.join(SHORT.get(g, g[0]) if g in users else '.' for g in games)


def write_md(path, games, bins, mrows, arows, aaron_uses_moses):
    L = []
    w = L.append
    gl = list(games)
    w('# Engine API coverage (moses.dll / Aaron.dll)')
    w('')
    w('Generated by `engine/tools/engine_coverage.py` on %s. **Do not edit by hand**: re-run the script' % datetime.date.today().isoformat())
    w('(`python -I engine/tools/engine_coverage.py`, needs `pefile` in the user site-packages) after every engine change or when')
    w('a new game is analysed (add `--game NAME=FOLDER`).')
    w('')
    w('* **Used by**: columns `' + ' '.join(SHORT.get(g, g[0]) for g in gl) + '` = ' + ', '.join('%s=%s' % (SHORT.get(g, g[0]), g) for g in gl) +
      '; a dot = not imported. Direct imports of every exe/DLL of the game are counted (host exe + scene DLL), excluding Launcher.exe and Uninst.dll.')
    w('* **In the port**: `yes` = the original address appears in a `.cpp` (or on a header line with a body); `partial` = only declared in a header, to be')
    w('  checked; `no` = no trace in `engine/`; `n.a.` = compiler artefact or class `operator new/delete` (the port uses the global new/delete).')
    w('  This is a text search (heuristic): the `no` rows are certain, the `yes` rows very likely.')
    w('* The binary comparison (`engine/docs/OTHER_GAMES_SURVEY.md`) shows that `moses.dll` and `Aaron.dll` are identical in the 4 games: addresses and ordinals hold for all of them.')
    w('')
    for g in gl:
        w('* `%s`: %s' % (g, ', '.join(bins[g])))
    w('')

    def cnt(rows, key):
        return collections.Counter(r['st'] for r in rows if key(r))
    w('## 1. Summary')
    w('')
    w('| API | exports | used by at least one game | `yes` | `partial` | `no` | `n.a.` |')
    w('|---|---:|---:|---:|---:|---:|---:|')
    for title, rows in (('moses.dll', mrows), ('Aaron.dll', arows)):
        c = cnt(rows, lambda r: True)
        u = cnt(rows, lambda r: r['users'])
        w('| %s | %d | %d | %d (%d) | %d (%d) | %d (%d) | %d (%d) |' % (title, len(rows), sum(u.values()),
          c['yes'], u['yes'], c['partial'], u['partial'], c['no'], u['no'], c['n.a.'], u['n.a.']))
    w('')
    w('(in parentheses: those used by at least one game)')
    w('')
    w('Per game (used / missing in the port = `no`+`partial`):')
    w('')
    w('| game | moses used | moses missing | Aaron used | Aaron missing |')
    w('|---|---:|---:|---:|---:|')
    for g in gl:
        mu = [r for r in mrows if g in r['users']]
        au = [r for r in arows if g in r['users']]
        w('| %s | %d | %d | %d | %d |' % (g, len(mu), sum(r['st'] in ('no', 'partial') for r in mu),
                                         len(au), sum(r['st'] in ('no', 'partial') for r in au)))
    w('')

    w('## 2. Gaps: used by a game and not (fully) in the port')
    w('')
    w('This is the engine work still needed to port that game.')
    w('')
    gaps_m = [r for r in mrows if r['users'] and r['st'] in ('no', 'partial')]
    gaps_a = [r for r in arows if r['users'] and r['st'] in ('no', 'partial')]
    w('### moses.dll')
    w('')
    if gaps_m:
        w('| address | name | in port | used by | note |')
        w('|---|---|---|---|---|')
        for r in gaps_m:
            w('| `0x%08x` | `%s` | %s | %s | %s |' % (r['addr'], r['dem'], r['st'], mark(r['users'], gl), r['note']))
    else:
        w('None.')
    w('')
    w('### Aaron.dll')
    w('')
    if gaps_a:
        w('| ordinale | address | name | in port | used by | note |')
        w('|---:|---|---|---|---|---|')
        for r in gaps_a:
            w('| %d | `0x%08x` | %s | %s | %s | %s |' % (r['ord'], r['addr'], '`%s`' % r['name'] if r['name'] else 'to be analysed', r['st'], mark(r['users'], gl), r['note']))
    else:
        w('None.')
    w('')

    w('## 3. All moses.dll exports (%d)' % len(mrows))
    w('')
    w('Column `Ae` = also imported by `Aaron.dll` (internal use by the engine).')
    w('')
    w('| ord | address | name | port | ' + ' '.join(SHORT.get(g, g[0]) for g in gl) + ' | Ae |')
    w('|---:|---|---|---|---|---|')
    for r in mrows:
        w('| %d | `0x%08x` | `%s` | %s | %s | %s |' % (r['ord'], r['addr'], r['dem'].replace('|', '\\|'), r['st'], mark(r['users'], gl), 'x' if r['aaron_uses'] else ''))
    w('')

    w('## 4. All Aaron.dll ordinals (%d)' % len(arows))
    w('')
    w('Name = as given in the analysis notes; "to be analysed" = still unnamed (`FUN_`) after analysis.')
    w('')
    w('| ord | address | name | port | ' + ' '.join(SHORT.get(g, g[0]) for g in gl) + ' |')
    w('|---:|---|---|---|---|')
    for r in arows:
        w('| %d | `0x%08x` | %s | %s | %s |' % (r['ord'], r['addr'], '`%s`' % r['name'] if r['name'] else 'to be analysed', r['st'], mark(r['users'], gl)))
    w('')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(L))


if __name__ == '__main__':
    main()
