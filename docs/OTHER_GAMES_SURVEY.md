# Survey of the other Disney (1999) games that use the MuShoot engine

Read-only survey. No existing file was modified; the analysis scripts (`pe_survey.py`, `summarize.py`, output `pe_survey.json`) were kept in a scratch directory and are not part of the repository.

Reference: the folder of the MuShoot game (`<games-dir>/MUSHOOT`). The other games are sibling folders in the same parent directory.

## 1. Folders examined

Search for `moses.dll` / `Aaron.dll` (case-insensitive, depth 3 from the parent directory): found in **Flying**, **Flounder**, **Ruckus** (besides MUSHOOT). No other folder examined contains them.

## 2. File list (recursive, max depth 3)

Sizes in bytes.

### 2.1 Flying

| File | Size |
|---|---:|
| Aaron.dll | 167.936 |
| DDLogo.avi | 7.008.768 |
| DILogo.avi | 8.880.128 |
| Flying.exe | 110.592 |
| Flying.hlp | 26.395 |
| Frenzy.dll | 212.992 |
| Launcher.exe | 32.768 |
| Uninst.dll | 28.160 |
| frenzy.rrs | 542.422 |
| frenzylz.rrs | 2.161.201 |
| moses.dll | 126.976 |
| peg.rrs | 3.324.719 |
| pegfx.rrs | 799.508 |
| peglin.rrs | 9.414.898 |

### 2.2 Flounder

| File | Size |
|---|---:|
| Aaron.dll | 167.936 |
| DDLogo.avi | 7.008.768 |
| DILogo.avi | 8.880.128 |
| Flounder.hlp | 26.597 |
| Launcher.exe | 32.768 |
| Quest.exe | 110.592 |
| Uninst.dll | 28.160 |
| frenzy.rrs | 542.422 |
| frenzylz.rrs | 2.161.201 |
| moses.dll | 126.976 |
| quest.dll | 512.000 |
| seb.rrs | 6.118.090 |
| sebfx.rrs | 3.550.417 |
| seblin.rrs | 18.050.302 |

### 2.3 Ruckus

| File | Size |
|---|---:|
| Aaron.dll | 167.936 |
| DDLogo.avi | 7.008.768 |
| DILogo.avi | 8.880.128 |
| Launcher.exe | 32.768 |
| Ruckus.dll | 237.568 |
| Ruckus.exe | 114.688 |
| Ruckus.hlp | 26.236 |
| Uninst.dll | 28.160 |
| abu.rrs | 4.297.679 |
| abufx.rrs | 3.559.545 |
| abulin.rrs | 2.503.318 |
| frenzy.rrs | 542.422 |
| frenzylz.rrs | 2.161.201 |
| moses.dll | 126.976 |

### 2.4 MUSHOOT (reference, top-level files only)

| File | Size |
|---|---:|
| Aaron.dll | 167.936 |
| DDLogo.avi | 7.008.768 |
| DILogo.avi | 8.880.128 |
| Launcher.exe | 32.768 |
| MUSHOOT.hlp | 25.725 |
| Shoot.dll | 229.376 |
| Shoot.exe | 114.688 |
| Uninst.dll | 28.160 |
| frenzy.rrs | 542.422 |
| frenzylz.rrs | 2.161.201 |
| moses.dll | 126.976 |
| mus.rrs | 4.441.051 |
| musfx.rrs | 629.218 |
| muslin.rrs | 13.779.292 |

Besides the files above, the MUSHOOT folder also holds the maintainer's working folders (data extracted from the .rrs files, and the project's source tree). They are not part of the comparison.

## 3. Comparison of moses.dll and Aaron.dll

| Game | File | SHA-256 | Identical to MUSHOOT | Size | PE timestamp (UTC) |
|---|---|---|---|---:|---|
| MUSHOOT | moses.dll | `25fe630b73e896c359a775aa593fb4c58859d647beada9142bf59b2ae4356457` | (reference) | 126.976 | 1999-08-26 23:16:41 |
| Flying | moses.dll | `25fe630b...4356457` | **Yes** | 126.976 | 1999-08-26 23:16:41 |
| Flounder | moses.dll | `25fe630b...4356457` | **Yes** | 126.976 | 1999-08-26 23:16:41 |
| Ruckus | moses.dll | `25fe630b...4356457` | **Yes** | 126.976 | 1999-08-26 23:16:41 |
| MUSHOOT | Aaron.dll | `0fab435c60c2c4ea159d79309ba94a4f09a2ec9d5edfb4ac080bef2114c9b49d` | (reference) | 167.936 | 1999-08-24 21:26:15 |
| Flying | Aaron.dll | `0fab435c...c9b49d` | **Yes** | 167.936 | 1999-08-24 21:26:15 |
| Flounder | Aaron.dll | `0fab435c...c9b49d` | **Yes** | 167.936 | 1999-08-24 21:26:15 |
| Ruckus | Aaron.dll | `0fab435c...c9b49d` | **Yes** | 167.936 | 1999-08-24 21:26:15 |

Versions (FileVersion resource): moses.dll 2.0.0, Aaron.dll 2.0.0, both "Rapture Technologies, Inc.".

### 3.1 Exports

| Game | moses.dll exports (MUSHOOT = 344) | Aaron.dll exports (MUSHOOT = 555) | Name/ordinal differences |
|---|---:|---:|---|
| Flying | 344 | 555 | none |
| Flounder | 344 | 555 | none |
| Ruckus | 344 | 555 | none |

The number of exports and the name/ordinal list coincide with MUSHOOT. Aaron.dll exports by ordinal only (no names); the ordinals range from 1 to 713 with 555 entries.

Cross-check with `objdump -p`: the timestamp matches pefile (objdump shows local time, UTC+2) and the count of 344 moses.dll exports matches. The Aaron.dll export table is ordinal-only, so `objdump` gives no named count.

## 4. Executables and game DLLs

| Game | File | PE timestamp (UTC) | FileVersion | ProductName | InternalName | Exports | Imports |
|---|---|---|---|---|---|---|---|
| MUSHOOT | Shoot.exe | 2000-03-28 21:55:21 | 1.1.02 | Disney's MuShoot | MuShoot | CreateScene | KERNEL32 (43), USER32 (4), Moses (38), AARON (126) |
| MUSHOOT | Shoot.dll | 1999-08-24 21:45:53 | 1.0.24 | MuShoot | Shoot | CreateScene | USER32 (3), Moses (35), AARON (111), KERNEL32 (40) |
| Flying | Flying.exe | 2000-03-28 21:55:19 | 1.1.02 | Disney's Flying Frenzy | Flying Frenzy | CreateScene | KERNEL32 (43), USER32 (4), Moses (38), AARON (126) |
| Flying | Frenzy.dll | 1999-08-24 21:44:26 | 1.0.24 | Flying Frenzy | Frenzy | CreateScene | USER32 (3), Moses (39), AARON (110), KERNEL32 (42) |
| Flounder | Quest.exe | 2000-03-28 21:55:17 | 1.1.02 | Disney's Flounder Quest | Flounder Quest | CreateScene | KERNEL32 (43), USER32 (4), Moses (38), AARON (126) |
| Flounder | quest.dll | 1999-08-24 21:47:48 | 1.0.24 | Flounder Quest | Quest | CreateScene | USER32 (4), Moses (51), AARON (121), KERNEL32 (40) |
| Ruckus | Ruckus.exe | 2000-03-28 21:55:24 | 1.1.02 | Disney's Rooftop Ruckus | Rooftop Ruckus | CreateScene | KERNEL32 (43), USER32 (4), Moses (38), AARON (126) |
| Ruckus | Ruckus.dll | 1999-08-24 21:43:11 | 1.0.24 | Rooftop Ruckus | Ruckus | CreateScene | USER32 (2), Moses (37), AARON (113), KERNEL32 (40) |

All the games use the `CreateScene` export (exe and DLL). All the executables have the same import structure as the MUSHOOT exe.

### 4.1 Use of Aaron (by ordinal)

MUSHOOT (exe+dll) uses 155 distinct Aaron.dll ordinals.

| Game | Aaron ordinals used (exe+dll) | Not present in MUSHOOT | Present in MUSHOOT but unused |
|---|---:|---|---:|
| Flying | 154 | none | 1 |
| Flounder | 162 | 47, 77, 138, 226, 234, 237, 452, 488, 515 | 2 |
| Ruckus | 157 | 144, 529, 576 | 1 |

### 4.2 Use of moses.dll (by name)

The moses.dll imports are by name (decorated C++). All the games import the 38 symbols of Shoot.exe; the differences are in the game DLLs.

| Game | moses symbols imported | New relative to MUSHOOT |
|---|---:|---|
| Flying (Frenzy.dll) | 39 | `?GetFlip@CDrawObj@@QBEGXZ`, `?GetPosition@CDrawObj@@QBEPBUtagPOINT@@XZ` |
| Flounder (quest.dll) | 51 | `??0CDibData@@QAE@ABV0@@Z`, `??0CDibList@@QAE@I@Z`, `??0CDibTile@@QAE@XZ`, `??2CDibObj@@SAPAXI@Z`, `??3CDibObj@@SAXPAX@Z`, `?AddDib@CDibList@@QAEHPAVCDibObj@@@Z`, `?GetPosition@CDrawObj@@QBEPBUtagPOINT@@XZ`, `?Hide@CDrawObj@@QAEXXZ`, `?Init@CDibTile@@QAEHPAUDIBTILEHDR@@@Z`, `?OffsetPos@CDibObj@@QAEXHH@Z`, `?Scroll@CDrawView@@QAEXHH@Z`, `?SetDib@CDibTile@@QAEHPBVCDibData@@I@Z`, `?SetRedraw@CDrawView@@QAEXHH@Z`, `?SetScrollBox@CDrawView@@QAEXPBUtagRECT@@@Z` |
| Ruckus (Ruckus.dll) | 37 | `?Hide@CDrawObj@@QAEXXZ`, `?PtInObject@CDrawObj@@QBEHHH@Z`, `?SetRedraw@CDrawView@@QAEXHH@Z` |

For Flounder, the CDibTile/CDibList/Scroll symbols point to a tiled / scrolling level that is absent in MUSHOOT. The CDibTile code was not disassembled: this is only a reading of the names.

## 5. Data resources (.rrs)

All the .rrs files of all 4 games start with the same 16 bytes:

`52 41 50 54 00 00 01 00 38 00 00 00 00 00 00 00` ("RAPT", then `00 00 01 00` and `38 00 00 00`)

So the `RAPT` magic is identical everywhere. The meaning of the next two fields is not verified here (see `rrs_format.md` for the decoded format).

| Game | .rrs files (size) |
|---|---|
| MUSHOOT | frenzy.rrs (542.422), frenzylz.rrs (2.161.201), mus.rrs (4.441.051), musfx.rrs (629.218), muslin.rrs (13.779.292) |
| Flying | frenzy.rrs (542.422), frenzylz.rrs (2.161.201), peg.rrs (3.324.719), pegfx.rrs (799.508), peglin.rrs (9.414.898) |
| Flounder | frenzy.rrs (542.422), frenzylz.rrs (2.161.201), seb.rrs (6.118.090), sebfx.rrs (3.550.417), seblin.rrs (18.050.302) |
| Ruckus | frenzy.rrs (542.422), frenzylz.rrs (2.161.201), abu.rrs (4.297.679), abufx.rrs (3.559.545), abulin.rrs (2.503.318) |

The `frenzy.rrs` and `frenzylz.rrs` files have identical sizes in all 4 games (shared).

## 6. AVI videos

| Game | File | Size | Container | Video stream | Codec (fourcc) |
|---|---|---:|---|---|---|
| All (4) | DDLogo.avi | 7.008.768 | RIFF AVI | vids | msvc (Microsoft Video 1) |
| All (4) | DILogo.avi | 8.880.128 | RIFF AVI | vids | msvc (Microsoft Video 1) |

The two AVIs have identical sizes in all the games.

## 7. Help (.hlp)

| Game | File | Size | Header (first 4 bytes) |
|---|---|---:|---|
| MUSHOOT | MUSHOOT.hlp | 25.725 | 3f 5f 03 00 |
| Flying | Flying.hlp | 26.395 | 3f 5f 03 00 |
| Flounder | Flounder.hlp | 26.597 | 3f 5f 03 00 |
| Ruckus | Ruckus.hlp | 26.236 | 3f 5f 03 00 |

All are WinHelp files (standard header `3F 5F 03 00`).

## 8. Strings in the main executable (registry and title)

| Game | Registry key | Window title (string) |
|---|---|---|
| MUSHOOT | `Software\Disney Interactive\MuShoot HotShot\US` | Disney's MuShoot |
| Flying | `Software\Disney Interactive\Flying Frenzy HotShot\US` | Disney's Flying Frenzy |
| Flounder | `Software\Disney Interactive\Flounder Quest HotShot\US` | Disney's Flounder Quest |
| Ruckus | `Software\Disney Interactive\Rooftop Ruckus HotShot\US` | Disney's Rooftop Ruckus |

The title is the ProductName string; it was not verified that it is the window caption at runtime (the programs were not run in the survey).

The Shoot.exe strings also include "Frenzy.RRS", "FrenzyLZ.RRS", "MusFX.RRS": for MUSHOOT the effects .rrs file is `musfx.rrs`.

## 9. Launcher.exe

| Game | Launcher.exe identical to MUSHOOT? | Different bytes | Notes |
|---|---|---:|---|
| Flying | **No** | 3.632 | same size (32.768), timestamp 2000-06-27 16:25:56 UTC |
| Flounder | **No** | 3.682 | same size, timestamp 2000-06-27 16:26:51 UTC |
| Ruckus | **No** | 2.584 | same size, timestamp 2000-06-27 16:03:18 UTC |

The Launcher is the same code with different strings: title, game name, exe name and registry key (e.g. `Software\Disney Interactive\Flying Frenzy Hotshot\IT\Paths`). In the three games the strings are in Italian ("Inserire il CD di ... - Game Shot", i.e. "Insert the ... CD - Game Shot"). MUSHOOT uses "Disney SparaMu" as the name in the launcher.

Uninst.dll (SHA-256 `60c36bb70341626e...`, timestamp 1998-07-31) is identical in all 4 games.

## 10. Conclusions per game

| Game | Engine (moses + Aaron) | Main differences relative to MUSHOOT |
|---|---|---|
| Flying Frenzy | Identical (bit-for-bit binaries) | Frenzy.dll uses 2 more moses symbols (GetFlip, GetPosition); Aaron: no new ordinal; new peg* .rrs; different Launcher |
| Flounder Quest | Identical | quest.dll imports 14 more moses symbols (CDibTile, CDibList, Scroll, SetScrollBox): presumably a tiled / scrolling level; Aaron: 9 new ordinals; different Launcher |
| Rooftop Ruckus | Identical | Ruckus.dll imports 3 more moses symbols (Hide, PtInObject, SetRedraw) and 3 new Aaron ordinals; different Launcher |

In all three games moses.dll and Aaron.dll are bit-for-bit equal to MUSHOOT, with the same 344 and 555 exports. The executables have the same structure (CreateScene, same versions 1.1.02 / 1.0.24, same build dates). The differences are in the game DLLs and in the data (.rrs, .hlp, Launcher).

## 11. Limits

- The survey is static: no program was launched.
- The imported moses symbols are decorated C++ names; the semantics of the individual methods was not verified beyond the name.
- The Aaron ordinals are numbers without names: the meaning of the new ones (e.g. 47, 77, 138, 144, 529, 576) was not determined.
- The window title is the ProductName string, not the runtime caption.
- The export count was obtained with pefile; for moses.dll it was compared with `objdump -p` (344). Imports by ordinal and by name were read with pefile.
