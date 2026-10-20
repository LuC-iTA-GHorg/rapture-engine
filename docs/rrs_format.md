# Format of the `*.rrs` archives (RAPT) - Disney's Arcade Frenzy / MuShoot

Reconstructed from analysis of `moses.dll` (base 0x10000000, MSVC C++, 1999). All addresses are VAs (base + RVA),
quotable as `// moses.dll 0x1000c5f0` (provenance remarks derived from analysis of the binaries). Only `moses.dll` contains the `RAPT` constant and the parser;
`Aaron.dll` / `Shoot.dll` / `Shoot.exe` do not speak the format directly (they use the `CResMgr` exported by moses).
Reference extractor: `tools/rrs_extract.py` of this repository (verified: all the entries of all 5 archives
decode without errors).

All values are little-endian.

## 1. Header (0x38 bytes)

`CResfile::OpenFile` (0x1000c5f0) creates a `CFileIO`, then `0x1000c6d0` reads 0x0c bytes into `CResfile+0x14`
(magic/version/header size) and another 0x2c bytes right after.

| off  | field | notes |
|------|-------|------|
| 0x00 | magic `RAPT` = 0x54504152 | otherwise error |
| 0x04 | version dword: hi word = 1, lo word = 0 | `shr 16 == 1` and `word == 0`, otherwise it refuses |
| 0x08 | header size = 0x38 | |
| 0x0c | 0 | reserved |
| 0x10 + 8*k (k=0..4) | `{offset, size}` of "key" k | `LoadKeyResource` (0x1000c400), k must be < 5 |
| 0x38 | start of data | |

Keys used:
* **key 0** = *directory/index*: array of 12-byte records; `count = size / 12` (0x1000c682, multiplication by 0xaaaaaaab).
  Loaded into `CResfile+0x08`.
* **key 1** = *pool* of variable structures (lists of ids, strings, references). Loaded into `CResfile+0x04`.
  Optional (`size == 0` in `frenzy.rrs`).
* keys 2..4: always 0 in the existing archives (palettes/others never used).

Physical layout: the data of the entries comes first (from 0x38, or after index+pool in `mus`, `frenzylz`, `muslin`);
index and pool may be at the end (`frenzy`, `musfx`: the index is at the end of the file) or at the beginning (`mus`, `muslin`, `frenzylz`:
index at 0x38, pool right after, data after).

| archive | key0 (index) off/size | key1 (pool) off/size | entries |
|---|---|---|---|
| frenzy.rrs | 0x846b2 / 0x24 | - | 3 |
| frenzylz.rrs | 0x38 / 0x1f8 | 0x230 / 0x404 | 42 |
| mus.rrs | 0x38 / 0x30d8 | 0x3110 / 0x12b8 | 1042 |
| musfx.rrs | 0x99826 / 0x150 | 0x99976 / 0x6c | 28 |
| muslin.rrs | 0x38 / 0x5aa8 | 0x5ae0 / 0x1dd8 | 1934 |

## 2. Index records (12 bytes) and identification

```
struct INDEXREC { u32 offset; u32 size; u16 type; u16 tag; };   // CResfile::GetRecOffset 0x1000c370, GetRecSize 0x1000c3a0, GetRecType 0x1000c3d0
```

* Resources are identified by a **32-bit id**; only the **low word is the index** into the index
  (`id & 0xffff`, all the functions mask it). The high word in the ids found in the groups is a "class"
  (0x0082 = bitmap, 0x0085 = dib list, 0x0185 = sounds): not used by the parser, ignored by the extractor (reported in `index.json` as `class`).
  There are no resource names: only numeric indices (the names are in the game code, `Shoot.dll`). The only name is the filename of the AVIs (see below).
* `tag` (word at +10): not read by the parser; in `mus`/`muslin` it takes values like 0, 20..25, 187, 203, 65535 (group/level
  number of the authoring tool; meaning not verified).
* Types (`enum RESTYPE`), dispatch in `CResfile::LoadResource` (0x1000c490):

| type | meaning | where the data is | handler |
|---|---|---|---|
| 1 | **alias**: `offset & 0xffff` = index of the destination (the size copies that of the target) | - | 0x1000c490 (follows 1 level) |
| 2 | **DIB** (16-bit RLE bitmap, see sec. 3) | file, `offset/size` | `load_dib` 0x1000c0d0 |
| 3 | raw blob (in the only case: text script of the credits) | file | 0x1000c52e (copies `size` bytes) |
| 4 | palette: `size` bytes = `RGBQUAD[size/4]` (max 256) | file | `load_palette` 0x1000c180 -> `CDibPal` ctor 0x10006d40. **Never used in the existing archives** |
| 5 | **sound** (ADPCM wave / MIDI / AVI reference, sec. 4) | file | `load_sound` 0x1000c220 -> `CAudioLib::SoundCreate` 0x10010995 |
| 6 | group of ids (4-byte header) | **pool** at `offset` | `CResMgr::load_group` 0x1000ca60 / `LoadList` 0x1000cf90 |
| 7 | **DIBREF**: same bitmap with another hot-spot | pool | `load_dibref` 0x1000c8e0 |
| 8 | **DIBLIST**: list of dibs (`CDibList`) | pool | `load_diblist` 0x1000c850 |
| 9 | **DIBTILE**: mosaic of dibs (`CDibTile`) | pool | `load_dibtile` 0x1000c970 (not present in the archives) |
| 11 (0xb) | group of ids (8-byte header) | pool | `load_group` 0x1000ca60 |
| 12 (0xc) | string (`LoadStringA`, `size` bytes, NUL-terminated) | pool | `CResfile::LoadStringA` 0x1000c590, `CResMgr::LoadStringA` 0x1000cff0 |

For types 6..12 `offset` is relative to the start of the pool (key1), not of the file (`GetResType` 0x1000c3d0, `CResfile::LoadResource` 0x1000c490:
for type 6..0xc it returns `pool + offset`).
`LoadResource` jump table at 0x1000c574 (2->dib 0xc505, 3->raw 0xc52e, 4->palette 0xc513, 5->sound 0xc520);
`CResMgr::load_id` (0x1000cb80) jump table at 0x1000cc68 (6->group, 7->dibref, 8->diblist, 9->dibtile, 11->group);
`free_id` (0x1000ca90) jump table at 0x1000cb50.

`CResMgr::load_id` (0x1000cb80) keeps a table of `{u32, ptr}` per id (`CResMgr+0x5c`, stride 8, indexed by `id & 0xffff`).
`GetIndexedResId` (`CResfile` 0x1000c2e0 / `CResMgr` 0x1000cdf0) reads the n-th id of a group
(for type 6/8/9 the ids start at +4, for type 11 at +8; the count is the word at +0).

### Structures in the pool

```
GROUP  (type 6)  : u16 count; u16 pad;               u32 ids[count]
GROUP2 (type 11) : u16 count; u8 pad[6];              u32 ids[count]   // 8-byte header
DIBLIST(type 8)  : u16 count; u16 pad;               u32 ids[count]   // CDibList::AddDib per id
DIBREF (type 7)  : u32 id; i16 x; i16 y; u8 pad[4]                      // copy of the CDibData with OffsetPos(x-orig.x, y-orig.y)
DIBTILE(type 9)  : u16 count; u16 pad; DIBTILEHDR(12 bytes); u32 ids[count]  // header at +4, ids at +0x10
STRING (type 12) : bytes, NUL
```
(`load_dibref` 0x1000c8e0: new `CDibData(copy)` 0x10003b00, then `OffsetPos` 0x10003700 with `(ref.y - origin.y, ref.x - origin.x)` read from `[edi+0x14/0x18]`.)

## 3. DIB (type 2) - 16-bit bitmap with RLE

`load_dib` (0x1000c0d0) reads the first 10 bytes (DIBREC), allocates `size-10` and reads the bits; `CDibData::CDibData(DIBREC&, bits, own)` (0x10003a20) interprets them:

```
struct DIBREC {            // 10 bytes
  u16 width;  u16 height;  // +0, +2
  i16 origin_x; i16 origin_y;   // +4, +6  (hot-spot, CDibObj+0x14/0x18, used by OffsetPos/GetBounds)
  u8  bpp;                 // +8   always 16 in the archives
  u8  flags;               // +9
};
```
`flags` (0x10003a85..): bit0 -> `CDibData+0x20`; **bit1 = compressed** (passed to `CDibData::CDibData` 0x100038e0 as 6th arg -> `0x10003740` chooses the compressed class,
16-bit vtable 0x10016574 / 8-bit 0x10016548); **bit2 = row table of dwords** (otherwise words; read from `[eax+0x13]`
and passed to 0x100049c0); bit3 -> `CDibData+0x21`. Values seen: 2, 3, 6, 7, 10 (all with bit1: **no uncompressed DIB**).
`CDibData::Expand` (0x10003d60) decompresses into a raw bitmap (blit of the compressed version into a new `CDibObj`).

After the DIBREC:
1. **row offset table**, `height` entries, `u32` if `flags & 4` otherwise `u16`; each entry = offset of the row *relative to the start of the table* (the first is always `height*4` or `height*2`).
2. RLE data of each row (16-bit compressed blit: `0x10004b00` -> worker `0x100049c0` (word row table) / `0x10004870` / `0x10004730`; 8-bit version `0x100058b0`):
   * read byte `c`; `c == 0` -> **end of row**; the pixels not emitted at the end of the row are transparent;
   * `c & 0x80` -> **literal**: `n = c & 0x7f` 16-bit pixels follow;
   * otherwise -> **run**: a single 16-bit pixel, repeated `c` times.
   * **pixel 0x0000 = transparent** (the blit skips the write: `test ax,ax; je` at 0x10004a76 / 0x10004ab1). Opaque pixels **always have bit15 = 1** (0x8000 | RGB555), so opaque black is 0x8000.
3. pixel format **RGB555** (bit15 ignored by the display).
4. **Rows are bottom-up**: row 0 = bottom row (DIB convention; the blitter goes down with `ecx -= stride`). The extractor flips them in the PNG.

There is no LZ: the "lz" of `frenzylz` has nothing to do with compression (probably the variant with localised strings, credits and titles of `frenzy`: hypothesis); the only data compression is this RLE on the DIBs and the ADPCM on the sounds.

## 4. Sounds (type 5)

`CAudioLib::SoundCreate(CFileIO*, off, size)` (0x10010995) seeks to `offset`, reads 8 bytes `{u32 kind; u32 param}`:

| kind | meaning | handler |
|---|---|---|
| 1 | **wave** | 0x10010a10: reads 0x14 bytes of wave header and passes `(off+0x1c, size-0x1c)` to `0x1001188b` -> `CAudioWave::Init` 0x10013e23 |
| 2 | **MIDI** (compact stream) | 0x10010a74: reads `size-8` raw bytes (`0x100141df`), then `0x10011e70` |
| 3 | **file reference** (AVI): 0x104 bytes = NUL-terminated filename | 0x10010ab9 -> `0x100124dc` (e.g. `dilogo.avi`, `ddlogo.avi` in `frenzy.rrs`) |

`param` (2nd dword) is copied into the sound object as id (+8).

### Wave (kind 1), 0x1c bytes of header + data

```
+0x00 u32 kind=1 ; +0x04 u32 param
+0x08 u16 wFormatTag(1) ; u16 nChannels ; u32 nSamplesPerSec ; u32 nAvgBytesPerSec ; u16 nBlockAlign ; u16 wBitsPerSample   // = output PCM (16 bit, 22050 Hz mono seen)
+0x18 u8 stream_flag ; +0x19 u8 compressed ; +0x1a u16 0
+0x1c data
```
`CAudioWave::Init` (0x10013e23): copies the 16-byte WAVEFORMAT to `+0x30`; `compressed = hdr[0x19]` (`+0x40`); if compressed the PCM length = `size*4` (0x10013e8a: `shl 2`); `stream_flag = hdr[0x18]`.
All the waves of the archives (92) are **compressed**: 4-bit ADPCM -> 16-bit PCM (4:1 ratio).

### ADPCM (0x10014710 -> 0x10014790)

It is IMA/DVI-style ADPCM with the step table expanded to 8 columns: the standard 89-entry IMA step table, each step replicated into an 8-column row
(`STEP[0x2c8]`, 712 u16 values, 89 x 8; the column picks the magnitude bits of the nibble), and an index-adjustment table `ADJ[16]` (i16).
The tables are no longer stored in the port as data: they are computed by formula in `platform/AdpcmTables.h`, and `tools/rrs_extract.py` has an equivalent `adpcm_tables()`.
(In the original they sit in moses.dll at 0x1001bba0 for `STEP` and 0x1001c130 for `ADJ`.)
State per channel: `pred` (i32, starts at 0) and `idx` (u16, starts at 0, multiple of 8, range 0..0x2c0).
For each byte: low nibble then high nibble; for each nibble `n`:
```
delta = STEP[idx + (n & 7)]; if (n & 8) delta = -delta
pred  = clamp16(pred + delta);  emit pred (i16 LE)
idx   = clamp(idx + ADJ[n], 0, 0x2c0)
```
mono: both nibbles on the same channel; stereo: low nibble = channel 0, high nibble = channel 1 (`CAudioWave+0x32 == 2`).
`ADJ = {-8,-8,-8,-8,16,32,48,64, -8,-8,-8,-8,16,32,48,64}`.
Check: smooth waveforms (mean absolute error ~10% of the RMS, no clipping).

### MIDI (kind 2)
After the initial 8 bytes: stream of dwords. `dw & 0x80000000` -> *delta time* (`dw & 0x7fffffff`, unit not verified; the extractor assumes 1 tick = 1 ms);
otherwise **short MIDI event** packed as `status | d1<<8 | d2<<16` (program change/channel pressure = 2 bytes). Used in `musfx.rrs` index 0 (percussion on channel 16, 1106 events, ~70 s if the deltas are ms).
Tempo/delay handling in `CAudioLib::MidiSetTempo` 0x10010708 (not examined in depth: the extractor's conversion to .mid is approximate on timing).

## 5. Content of the archives

| archive | content | entries |
|---|---|---|
| `frenzy.rrs` | 2 AVI references (`dilogo.avi`, `ddlogo.avi`) + 1 DIB 640x480 (background with fireworks, 16 bit) | 3 |
| `frenzylz.rrs` | 21 strings (game titles, error messages, display requirements), 4 DIB 640x480 (titles: "Abu sui tetti" = localised *Rooftop Ruckus*, etc.), 1 raw (text script of the "Digital Doorway" credits), 4 groups, 12 aliases | 42 |
| `mus.rrs` | 825 DIB (UI/menu/HUD of MuShoot in Italian: "Punteggio massimo" (high score), "Opzioni" (options), buttons, 640x480 background), 140 groups (frame lists), 62 aliases, 5 DIBREF, 1 DIBLIST, 3 list groups, 6 very short waves (~0.05 s, probably UI clicks: hypothesis) | 1042 |
| `musfx.rrs` | 26 waves (sound effects, 0.04-14.9 s) + 1 MIDI (percussion) + 1 group | 28 |
| `muslin.rrs` | 556 DIB (sprites of Mushu and of the characters, 72x31..333x258, transparency by pixel 0), 1293 aliases (frame sequences that reuse the same bitmaps), 24+1 groups, 60 waves (music/dialogue, 0.1-29.9 s, ~254 s in total) | 1934 |

* All the images are **16-bit RGB555**, no palette (despite the error message "256 colors or higher"); the backgrounds are **640x480**,
  so the game resolution is **640x480 at 16 bit** (the strings talk about "display ... resolution %d-bit color or higher").
* All the waves: 22050 Hz, 16 bit, mono (decoded from ADPCM).
* Sprites of size 1x1 (flags 10, 16 bytes) = placeholders for hot-spots.

## 6. Not decoded / doubts

* Meaning of `tag` (word at +10 of the record), of the high word of the ids (0x82/0x85/0x185 = presumed class), of the DIB flags bit0 and bit3, of the 4 trailing bytes of the DIBREF and of the DIBTILE header (no dibtile present).
* Unit of the MIDI deltas and tempo.
* `CDibData::Read/Write` (0x10004070/0x100041a0) read/write a DIB format on file (not used by the rrs).
* `Shoot.dll`/`Aaron.dll` were not analysed for this document (game scripts; the `CAnim*` classes in moses, `process_animate` etc., contain the animation message language, not the rrs format).
