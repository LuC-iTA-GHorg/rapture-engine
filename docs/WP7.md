# WP-7 - writing the `.rrs` archives

Status: complete. Byte-for-byte round-trip verified on **all** the archives of the 4 games (MuShoot, Flying Frenzy, Flounder Quest, Rooftop Ruckus).

## Files

| file | content |
|---|---|
| `platform/RrsWriter.{h,cpp}` | `rrs::Image`: editable model (header, directory, blob of each entry, physical order, layout). `Load(Archive)` + `Serialize()`/`Save()`; `Add`, `AddAlias`, `SetBlob`, `RefreshAliasSizes`; builders `MakeGroup/DibList/DibRef/DibTile/String`, `MakeWaveSound/MidiSound/AviSound` |
| `platform/DibEncode.{h,cpp}` | `DibRaw` (exact RGB555, top-down), `DecodeDibRaw`, `EncodeDib`, `Argb8888ToRgb555`/`DibRawFromArgb` (for the editor: ARGB -> DIB) |
| `platform/AdpcmEncode.{h,cpp}` | `AdpcmEncode` (inverse of `AdpcmDecode`), `SnrDb` |
| `platform/MidiEncode.{h,cpp}` | `SmfToCompactMidi` (SMF -> compact MIDI stream, inverse of `CompactMidiToSmf`) |
| `platform/Rrs.h` | additive only: `Archive::File()`, `PoolOffset()`, `PoolSize()` (to be noted in `docs/CHANGES.md`) |
| `tests/RrsWrite_Test.{h,cpp}` | `RunRrsRoundtrip` (synthetic self-test + round-trip of a folder) |
| `tests/RrsTest.cpp` | 2 lines: `--roundtrip` in argv -> `RunRrsRoundtrip` (the `--rrs-test` dispatch of `HostMain` is unchanged) |
| `tools/rrs_extract.py` | new options `--raw` (saves `bin/NNNN.bin`, original bytes), `version`/`reserved`/CRC of the converted files in `index.json`; the ADPCM step/adjust tables come from `adpcm_tables()` (computed by formula, same as `platform/AdpcmTables.h`; no game binary needed) |
| `tools/rrs_pack.py` | extract -> byte-identical pack, in pure Python (same encoders as the C++) |

## Usage

```
rapture_tests.exe --rrs-test --roundtrip [FOLDER]       (also <game>.exe; searches *.rrs recursively, default ".")
rapture_tests.exe --rrs-test --roundtrip --selftest-only   (synthetic test only, without game data)
python -I tools/rrs_extract.py --raw --game <game folder> --out <extracted>
python -I tools/rrs_pack.py --all <extracted> --out <out> --orig <game folder>       (verification; --no-raw = always re-encode)
```
The test is headless and does not write into the data folder (only into the temp folder, for the self-test). It exits with 1 if an archive is not identical.

## How the identity is obtained

The existing archives have a very regular structure (verified with a script): no gaps, no overlaps, no padding,
keys 2..4 = 0, `reserved` = 0. It is therefore enough to remember: (1) the bytes of each entry, (2) the physical order of the entries in the data section
and in the pool (in the pool it does NOT coincide with the order of the indices in `mus`, `peg`, `seb`, `abu`: it is the creation order of the authoring tool),
(3) the layout: `kDirFirst` (header, directory, pool, data: `mus`, `muslin`, `frenzylz`, `*lin`, `peg`, `seb`, `abu`) or
`kDataFirst` (header, data, directory, pool: `frenzy` and all the `*fx`). Empty pool = key 1 = {0,0}. The aliases keep the offset
verbatim (high word = `alias_sub`, always 0) and `size` = size of the final target (verified on all the aliases, 0 mismatches; `tag` belongs to the alias itself).

## Encoders (derived from the original data: the authoring tool is not in the binaries)

**DIB RLE16**: reproduces byte for byte **4659/4659** DIBs (all 4 games). Inferred rules:
* every group of >= 2 equal pixels = run (count 1..127 + pixel), split in blocks of 127 (a remainder of 1 is still a run: 128 transparent = `7f 0000 01 0000`);
  everything else in literals `0x80|n` (n <= 127);
* a trailing run of >= 2 transparent pixels (0x0000) of a row is omitted (a single trailing 0 stays as a literal);
* flags: `2` | `4` if the row table is dword (data > 0xffff bytes: maximum word seen 59784, minimum dword 66953) | `1` if the DIB has no
  transparent pixel | `8` if they are all transparent (the 1x1 placeholders of the hot-spots have flags 10). Hence **bit0 = opaque, bit3 = empty** (previously "not decoded");
* opaque pixels have bit15 = 1 (0x8000|RGB555); `Argb8888ToRgb555` enforces it, alpha < 128 -> 0.

**ADPCM**: greedy encoding (nibble with the closest decoded value, on a tie the lowest), with the same tables as the decoder. Re-encoding the decoded PCM
of each of the 449 counted waves (MuShoot 92, Flying 102, Flounder 113, Ruckus 142; including the only stereo one) gives back the original bytes (449/449).
This only says that the rule is compatible with the original encoder, not that it is the same.
SNR on real audio that is not already ADPCM (the decoded original waves x 0.73, so not exactly representable): average 29.5-31.3 dB per archive, minimum 23.6 dB per file;
synthetic signals (sine + noise): 39 dB mono, 33 dB stereo. Fixed 4:1 compression ratio as in the original.

**Compact MIDI**: `SmfToCompactMidi` merges the tracks, converts ticks to milliseconds with the tempo map (same hypothesis 1 unit = 1 ms as `CompactMidiToSmf`),
emits a delta dword only when time advances (never two in a row, none before events at the same instant) and keeps the final delta up to End-of-Track;
sysex/meta are discarded. The MIDI of each `*fx` -> SMF -> compact comes back identical (4/4 games). If it were ever found that the delta unit is not 1 ms
(`MidiTempoScale`), it must be applied here too (inverse).

## Results (`--rrs-test --roundtrip`)

| game | archives | load->serialize identical | rebuilt from decoded content | DIB | wave | MIDI | pool |
|---|---|---|---|---|---|---|---|
| MuShoot | 5 | 5/5 | 5/5 | 1386/1386 | 92/92 | 1/1 | 200/200 |
| Flying Frenzy | 5 | 5/5 | 5/5 | 1041/1041 | 102/102 | 1/1 | 196/196 |
| Flounder Quest | 5 | 5/5 | 5/5 | 1351/1351 | 113/113 | 1/1 | 163/163 |
| Rooftop Ruckus | 5 | 5/5 | 5/5 | 896/896 | 142/142 | 1/1 | 223/223 |

"Rebuilt" = no byte copied: DIBs re-encoded from the pixels, waves from the PCM, MIDI via SMF, groups/strings/dibrefs from the values, aliases with `RefreshAliasSizes`.
Python: `rrs_extract.py` -> `rrs_pack.py` identical for all the archives of the 4 games, both with `bin/` (original bytes) and with `--no-raw` (PNG -> DIB, WAV -> ADPCM, MID -> compact).

## Limits / TODO

* Types not present in the archives (palette 4, dibtile 9) and raw 3: opaque blobs / unverified builder (`MakeDibTile`: header at +4, ids at +0x10, as in `GetDibTile`).
* `Image::Load` refuses archives with non-empty keys 2..4 or a header other than 0x38 (they do not exist). With gaps/overlaps between the entries it would not be identical (there are none).
* Uncompressed DIBs (flag bit1 = 0) are supported neither in reading nor in writing (like `DecodeDib`).
* Stable indices: the editor appends at the end; deleting an entry would require rewriting the ids (not provided).
* The wave `stream_flag` (byte +0x18) cannot be deduced from the data (0/1 with no correlation with duration or archive): the editor must preserve it (`Sound::streamFlag`).
* The unit of the MIDI deltas is still unverified (see `Midi.h`).
