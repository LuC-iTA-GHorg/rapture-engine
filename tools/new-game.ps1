# Bootstraps the analysis / port of ANOTHER game of the Disney Rapture-engine family (same Moses/Aaron engine).
# Playbook: docs/REUSE.md.   Architecture: one engine repo (rapture-engine, this folder when it is a submodule) + one repo per game.
#
#   .\engine\tools\new-game.ps1 -GameDir "<games>\Flying" -Name flying [-Title "Flying Frenzy"]
#                               [-EngineUrl <git url of rapture-engine>] [-CopyEngine] [-NoEngineRe] [-NoCommit] [-Force] [-DryRun]
#
# By default the new repo gets the engine as a GIT SUBMODULE `engine/` (url = `origin` of this engine clone, or -EngineUrl).
# Clone it later with `git clone --recurse-submodules` (build.bat also runs `git submodule update --init` by itself).
#
# What it does (ONLY writes inside <GameDir>\src, which must not exist or be empty, unless -Force):
#   1. checks that GameDir has moses.dll + Aaron.dll and whether they are byte-identical to the reference game's (SHA-256);
#      detects the host exe (*.exe except Launcher.exe) and the game DLL (*.dll except moses/Aaron/Uninst);
#   2. creates the game repo skeleton: CMakeLists.txt (project <name>: add_subdirectory(engine), game/*.cpp, link rapture),
#      build.bat / re.bat / .gitignore / .gitattributes (copied from the reference game repo, names adapted), game/main.cpp,
#      game/HostConfig<Name>.{h,cpp} (every field of rapture::HostConfig with the values to fill in: see docs/REUSE.md sec. 1.3),
#      game/Strings.inc (the host exe's string table, via engine/tools/extract_host_strings.py when python is there);
#   3. the engine: `git submodule add <EngineUrl> engine` (-EngineUrl), or a plain COPY of the reference engine folder (-CopyEngine);
#   4. re/game.cfg (+ re.bat wrapper: the Ghidra scripts, the engine's notes/names/decompilation are in the submodule, engine-re/re/),
#      re/lib_start.tsv, re/notes skeletons + the reference game's notes; CLAUDE.md, docs/NOTES.md;   (-NoEngineRe is obsolete and ignored)
#   5. git init + first commit (skip the commit with -NoCommit).
# It never touches the original game files and never launches anything. -DryRun only prints what it would do.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [Parameter(Mandatory = $true)][string]$Name,        # short name: flying, flounder, ruckus ... (letters/digits only)
    [string]$Title = '',                                 # human title, e.g. "Flying Frenzy" (default = $Name)
    [string]$ReferenceRepo = '',                         # the game repo to copy build scripts / RE notes from (default: the repo that contains this engine)
    [string]$EngineUrl = '',                             # git url of rapture-engine, added as submodule `engine` (default: the origin of the engine this script belongs to)
    [switch]$CopyEngine,                                 # no submodule: copy the reference repo's engine folder
    [switch]$NoEngineRe,
    [switch]$NoCommit,
    [switch]$Force,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$enc = New-Object System.Text.UTF8Encoding $false

$EngineDir = Split-Path -Parent $PSScriptRoot
if (-not $ReferenceRepo) { $ReferenceRepo = Split-Path -Parent $EngineDir }
$ReferenceRepo = (Resolve-Path $ReferenceRepo).Path
if ($Name -notmatch '^[A-Za-z][A-Za-z0-9]*$') { throw "-Name must be letters/digits only (got '$Name')" }
if (-not $Title) { $Title = $Name }
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw "Game folder not found: $GameDir" }
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
$Dest = Join-Path $GameDir 'src'
$Pascal = $Name.Substring(0, 1).ToUpper() + $Name.Substring(1)
$Lower = $Name.ToLower()

function Say($m) { Write-Host $m }
function Put([string]$path, [string]$text) {
    $full = Join-Path $Dest $path
    if ($DryRun) { Say "  [dry] write $path"; return }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $full) | Out-Null
    [IO.File]::WriteAllText($full, $text, $enc)
}
function PutCrLf([string]$path, [string]$text) { Put $path (($text -replace "`r?`n", "`r`n")) }
function CopyRel([string]$rel, [switch]$Optional) {
    $src = Join-Path $ReferenceRepo $rel
    if (-not (Test-Path -LiteralPath $src)) { if ($Optional) { return } else { throw "Missing in reference repo: $rel" } }
    $dst = Join-Path $Dest $rel
    if ($DryRun) { Say "  [dry] copy $rel"; return }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    if (Test-Path -LiteralPath $src -PathType Container) {
        Copy-Item -LiteralPath $src -Destination (Split-Path -Parent $dst) -Recurse -Force
        Get-ChildItem -LiteralPath $dst -Recurse -Directory -Filter '__pycache__' -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
    } else {
        Copy-Item -LiteralPath $src -Destination $dst -Force
    }
}

# ---------------------------------------------------------------- 1. inspect the game folder
$knownMoses = '25FE630B73E896C359A775AA593FB4C58859D647BEADA9142BF59B2AE4356457'
$knownAaron = '0FAB435C60C2C4EA159D79309BA94A4F09A2EC9D5EDFB4AC080BEF2114C9B49D'
$mosesPath = Join-Path $GameDir 'moses.dll'
$aaronPath = Join-Path $GameDir 'Aaron.dll'
if (-not (Test-Path -LiteralPath $mosesPath) -or -not (Test-Path -LiteralPath $aaronPath)) { throw "moses.dll / Aaron.dll not found in $GameDir : not a game of this family" }
$sameEngine = ((Get-FileHash -LiteralPath $mosesPath -Algorithm SHA256).Hash -eq $knownMoses) -and ((Get-FileHash -LiteralPath $aaronPath -Algorithm SHA256).Hash -eq $knownAaron)
if ($sameEngine) { Say 'Engine: moses.dll and Aaron.dll are byte-identical to the reference game''s -> names/notes/engine port are reusable as they are.' }
else { Write-Warning 'moses.dll / Aaron.dll DIFFER from the reference game''s: the engine port, the ordinal table and the engine notes may not apply. Check engine/docs/REUSE.md first.' }

$files = Get-ChildItem -LiteralPath $GameDir -File
$launcher = $files | Where-Object { $_.Name -ieq 'Launcher.exe' } | Select-Object -First 1
$exes = @($files | Where-Object { $_.Extension -ieq '.exe' -and $_.Name -ine 'Launcher.exe' })
$dlls = @($files | Where-Object { $_.Extension -ieq '.dll' -and $_.Name -inotin @('moses.dll', 'Aaron.dll', 'Uninst.dll') })
if ($exes.Count -ne 1) { Write-Warning "Expected one host exe, found $($exes.Count): $($exes.Name -join ', ') (using the first; fix MAIN_EXE in re\game.cfg)" }
if ($dlls.Count -ne 1) { Write-Warning "Expected one game DLL, found $($dlls.Count): $($dlls.Name -join ', ') (using the first; fix re\game.cfg)" }
if ($exes.Count -eq 0 -or $dlls.Count -eq 0) { throw 'Host exe or game DLL not found.' }
$exe = $exes[0].Name
$dll = $dlls[0].Name
$rrs = @($files | Where-Object { $_.Extension -ieq '.rrs' } | ForEach-Object { $_.Name })
$bins = @($exe, $dll, 'Aaron.dll', 'moses.dll')
$order = @('moses.dll', 'Aaron.dll', $dll, $exe)
if ($launcher) { $bins += 'Launcher.exe'; $order += 'Launcher.exe' }

Say "Game folder : $GameDir"
Say "Host exe    : $exe    Game DLL: $dll    Archives: $($rrs -join ', ')"
Say "Destination : $Dest"
if ((Test-Path -LiteralPath $Dest) -and (Get-ChildItem -LiteralPath $Dest -Force | Select-Object -First 1) -and -not $Force) {
    throw "$Dest already exists and is not empty (use -Force to write into it)."
}

# ---------------------------------------------------------------- 2. the game repo skeleton
Say 'Writing the game skeleton ...'
foreach ($f in 'build.bat', 're.bat', '.gitignore', '.gitattributes', 're/scripts/extract_shoot_tables.py') { CopyRel $f -Optional }
if (-not $DryRun) {
    $p = Join-Path $Dest 'build.bat'
    if (Test-Path $p) { [IO.File]::WriteAllText($p, ([IO.File]::ReadAllText($p)).Replace('mushoot', $Lower).Replace('Mushoot', $Pascal), $enc) }
}
Put 'CMakeLists.txt' (@'
cmake_minimum_required(VERSION 3.16)
project(@@PASCAL@@ CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL2: point SDL2_DIR at the SDL2 cmake package, e.g. -DSDL2_DIR=<tools-dir>/SDL2/cmake
find_package(SDL2 REQUIRED CONFIG)

# The shared engine (git submodule `engine/`, repo rapture-engine): static library `rapture`.
set(RAPTURE_BUILD_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory(engine)

# The game: everything under game/ (includes are relative to the repo root: "game/@@LOWER@@/...").
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS game/*.cpp)
add_executable(@@LOWER@@ WIN32 ${GAME_SOURCES})
target_include_directories(@@LOWER@@ PRIVATE .)
target_link_libraries(@@LOWER@@ PRIVATE rapture SDL2::SDL2main)

if(MSVC)
    target_compile_options(@@LOWER@@ PRIVATE /W3)
else()
    target_compile_options(@@LOWER@@ PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Icon + version info + manifest (uncomment; the icon is read at configure time from the user's own host exe, see engine/cmake/RaptureWin32Resources.cmake):
# rapture_add_win32_resources(@@LOWER@@ ICON_FROM "${GAME_DATA}/<Host>.exe" VERSION 1.0.0 PRODUCT "<Title> (unofficial port)"
#     DESCRIPTION "<Title> - unofficial SDL2 port" INTERNAL_NAME "<Title>" FILENAME "@@LOWER@@.exe")

rapture_copy_sdl2_dll(@@LOWER@@)
'@.Replace('@@PASCAL@@', $Pascal).Replace('@@LOWER@@', $Lower))

Put 'game/main.cpp' (@'
// @@TITLE@@ - SDL2 port.  All the work is done by the shared engine (engine/host/HostMain.cpp); the command line is documented in
// engine/host/HostMain.h; everything specific to this game is in HostConfig@@PASCAL@@.cpp.
#include "game/HostConfig@@PASCAL@@.h"
#include "host/HostMain.h"

int main(int argc, char** argv) { return rapture::HostMain(argc, argv, @@PASCAL@@HostConfig()); }
'@.Replace('@@TITLE@@', $Title).Replace('@@PASCAL@@', $Pascal))

Put "game/HostConfig$Pascal.h" (@'
// The host configuration of @@TITLE@@ (everything the shared engine needs to know about this game).
#pragma once

#include "host/HostConfig.h"

const rapture::HostConfig& @@PASCAL@@HostConfig();
'@.Replace('@@TITLE@@', $Title).Replace('@@PASCAL@@', $Pascal))

$rrsList = ($rrs | ForEach-Object { '    {"' + $_ + '", false},' }) -join "`n"
Put "game/HostConfig$Pascal.cpp" (@'
// The host configuration of @@TITLE@@.  TODO: fill in every value (measured on @@EXE@@ / @@DLL@@: see engine/docs/REUSE.md sec. 1.3).
// Differences between the host exes of the family: the LoadStringA ids (.text +0x1b83 title, +0x1bdb game name), the title sprite
// id (+0x9497), and a few .data strings (default name, registry key, scene DLL, FX archive).
#include "game/HostConfig@@PASCAL@@.h"

using namespace rapture;

namespace {

// The RT_STRING table of @@EXE@@ (generated: python -I engine/tools/extract_host_strings.py <@@EXE@@> game/Strings.inc)
const StringEntry kStrings[] = {
#include "game/Strings.inc"
};

const DataFile kDataFiles[] = {
@@RRS@@
};
const char* const kMovies[] = {"DDLogo.avi", "DILogo.avi"};

HostConfig Make() {
    HostConfig c;
    c.productName = "@@PASCAL@@";
    c.defaultTitle = "Disney's @@TITLE@@";                 // TODO: the default name in @@EXE@@ .data

    c.strings = kStrings;
    c.stringCount = int(sizeof kStrings / sizeof kStrings[0]);
    c.titleStrId = 0;                                      // TODO: id at @@EXE@@ .text +0x1b83 (0x5013..0x5016)
    c.registryNameStrId = 0;                               // TODO: id at @@EXE@@ .text +0x1bdb (0x5019..0x501c)
    c.registryNameFallback = "@@TITLE@@ HotShot";          // TODO: the registry key name in @@EXE@@ .data

    c.settingsFolder = "@@PASCAL@@";                       // %APPDATA%\<folder>\<file>
    c.settingsFile = "@@LOWER@@.ini";

    c.hostBinary = "@@EXE@@";
    c.sceneDll = "@@DLL@@";                                // TODO: exactly as in @@EXE@@ .data (GetModule name)
    c.sceneEntry = "CreateScene";
    c.createGameScene = nullptr;                           // TODO: the port's CreateScene of the game scene
    c.titleSpriteId = 0;                                   // TODO: 0x658b00xx at @@EXE@@ .text +0x9497

    c.commonArchive = "Frenzy.RRS";
    c.commonStringsArchive = "FrenzyLZ.RRS";
    c.fxArchive = "";                                      // TODO: the FX archive in @@EXE@@ .data ("<x>fx.rrs")
    c.fxSlot = 1;
    c.mainArchive = "";                                    // TODO: the game's picture archive ("<x>.rrs")

    c.dataFiles = kDataFiles;
    c.dataFileCount = int(sizeof kDataFiles / sizeof kDataFiles[0]);
    c.movies = kMovies;
    c.movieCount = int(sizeof kMovies / sizeof kMovies[0]);
    c.arcadeDef = nullptr;                                 // TODO: the game's ArcadeGameDef (enables the popup tests)
    c.autoplay = nullptr;                                  // TODO: game-specific --autoplay commands (autoplay::Extension)
    return c;
}

} // namespace

const HostConfig& @@PASCAL@@HostConfig() {
    static const HostConfig cfg = Make();
    return cfg;
}
'@.Replace('@@TITLE@@', $Title).Replace('@@PASCAL@@', $Pascal).Replace('@@LOWER@@', $Lower).Replace('@@EXE@@', $exe).Replace('@@DLL@@', $dll).Replace('@@RRS@@', $rrsList))

# the host exe's string table
$stringsOut = Join-Path $Dest 'game/Strings.inc'
$genOk = $false
if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $stringsOut) | Out-Null
    $py = Get-Command python -ErrorAction SilentlyContinue
    $tool = Join-Path $EngineDir 'tools/extract_host_strings.py'
    if ($py -and (Test-Path $tool)) {
        & $py.Source -I $tool (Join-Path $GameDir $exe) $stringsOut 2>&1 | Out-Null
        $genOk = ($LASTEXITCODE -eq 0) -and (Test-Path $stringsOut)
    }
    if (-not $genOk) {
        [IO.File]::WriteAllText($stringsOut, "// TODO: python -I engine/tools/extract_host_strings.py `"../$exe`" game/Strings.inc`n{0, `"`"},`n", $enc)
        Write-Warning 'game/Strings.inc is empty: generate it with engine/tools/extract_host_strings.py (python).'
    }
}

# ---------------------------------------------------------------- 3. the engine
if (-not $EngineUrl -and -not $CopyEngine) {
    try { $EngineUrl = (& git -C $EngineDir remote get-url origin 2>$null | Select-Object -First 1) } catch { $EngineUrl = '' }
    if ($EngineUrl) { $EngineUrl = "$EngineUrl".Trim() }
}
if ($EngineUrl -and -not $CopyEngine) {
    Say "Engine: git submodule add $EngineUrl engine"
} elseif ($CopyEngine) {
    Say 'Engine: COPY of the reference engine folder (it will diverge from rapture-engine: prefer a submodule)'
    if (-not $DryRun) {
        New-Item -ItemType Directory -Force -Path $Dest | Out-Null
        Copy-Item -LiteralPath $EngineDir -Destination (Join-Path $Dest 'engine') -Recurse -Force
        Get-ChildItem -LiteralPath (Join-Path $Dest 'engine') -Recurse -Directory -Filter '__pycache__' -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
        $gitdir = Join-Path $Dest 'engine/.git'
        if (Test-Path $gitdir) { Remove-Item -Recurse -Force $gitdir }
    }
} else {
    Write-Warning 'No engine url found (this engine folder has no git origin) and no -CopyEngine: add the engine yourself:  git submodule add <rapture-engine url> engine'
}

# ---------------------------------------------------------------- 4. RE data + notes
# The engine's notes (moses.md, aaron.md, rrs_format.md, ...), names and decompilation are in the submodule: engine-re/re/.
# Notes on the reference game's own binaries (Shoot.dll halves from the game repo, the host exe notes from the engine): examples to imitate.
$refNotes = @((Join-Path $ReferenceRepo 're/notes/binaries.md'), (Join-Path $ReferenceRepo 're/notes/shoot_dll_a.md'),
              (Join-Path (Join-Path $EngineDir '..\engine-re') 're/notes/shoot_exe.md'), (Join-Path (Join-Path $EngineDir '..\engine-re') 're/notes/shoot_dll_b.md'))
foreach ($src in $refNotes) {
    if ((Test-Path $src) -and -not $DryRun) {
        New-Item -ItemType Directory -Force -Path (Join-Path $Dest 're/notes/reference-game') | Out-Null
        Copy-Item -LiteralPath $src -Destination (Join-Path $Dest 're/notes/reference-game') -Force
    }
}

# function_names.tsv: this game's own rows start empty (engine-re\re\notes\function_names.tsv holds moses / Aaron / the Arcade range)
PutCrLf 're/notes/function_names.tsv' ("# binary<TAB>0xADDR<TAB>Name   (Name may be Class::Method; applied by re.bat names; binary = $($bins -join ', '))`n# The names of moses.dll / Aaron.dll (identical in all games) and of the Arcade range come from engine-re\re\notes\function_names.tsv (see NAMES in game.cfg).`n# Add the rows of $exe / $dll below.`n")

# lib_start.tsv: the game binaries (the engine's file has the engine ones); fill in the TODO addresses
PutCrLf 're/lib_start.tsv' ("# Start of the statically linked / compiler-generated library code of the GAME binaries (engine binaries: engine-re/re/scripts/lib_start.tsv).`n# Format: <binary><TAB>0xADDRESS   (everything below it in .text is user code)`n# TODO($Name): address where the CRT / import stubs start in $exe and $dll (see engine/docs/REUSE.md, phase 'Ghidra project').`n# $exe`t0x0040fe05   <- the reference host exe's value (the host exe code is identical): check it`n# $dll`t0x1001XXXX`n")

PutCrLf 're/game.cfg' @"
# Reverse-engineering configuration for engine-re\re.bat (KEY=VALUE, '#' = comment). Game: $Title ($Name).
PROJ_NAME=$Pascal
GAMEDIR=..
MAIN_EXE=$dll
# Binaries of the GAME (moses.dll / Aaron.dll belong to the engine: engine\re). The host exe is the same program as Shoot.exe with different strings.
BINARIES=$(@($dll, $exe) -join ' ')$(if ($launcher) { ' Launcher.exe' })
IMPORT_ORDER=$(@($dll, $exe) -join ' ')$(if ($launcher) { ' Launcher.exe' })
# Binaries whose imports of Aaron.dll (ordinals) and moses.dll (names) are to be renamed, using the engine's exports.txt tables
RESOLVE=$dll $exe
NAMES=engine-re\re\notes\function_names.tsv re\notes\function_names.tsv
EXPORTS_FROM=engine-re\re\out\current
"@


foreach ($b in @($exe, $dll)) {
    $stem = [IO.Path]::GetFileNameWithoutExtension($b).ToLower() + '_' + ([IO.Path]::GetExtension($b).TrimStart('.'))
    Put "re/notes/$stem.md" @"
# $b - analysis notes ($Title)

> Skeleton created by engine/tools/new-game.ps1. Fill in function by function: original address, proposed name, what it does,
> callers, quirks. Final names go in function_names.tsv ($b<TAB>0xADDR<TAB>Name).
> Reference for the host exe: re/notes/reference-game/shoot_exe.md (same code).

## Binary map
(sections, user code / CRT boundary, classes, data tables)

## Functions
| address | name | notes |
|---|---|---|

## Open questions
"@
}
Put 're/notes/binaries.md' @"
# Binaries of $Title

Folder   : $GameDir
Host exe : $exe   (same code as Shoot.exe: comparison in engine/docs/REUSE.md)
Game DLL : $dll   (scene: game logic, to be analysed)
Engine   : moses.dll, Aaron.dll  (identical to the reference game's: $sameEngine)
Archives : $($rrs -join ', ')
Launcher : $(if ($launcher) { 'Launcher.exe (same code, different strings)' } else { 'absent' })

## To fill in
- image base, sections, imports/exports (re.bat export -> re/out/current/<binary>/imports.txt)
- engine APIs used and not yet in the port: engine/docs/ENGINE_API_COVERAGE.md
"@
Put 'docs/NOTES.md' @"
# Project notes: $Title

History, method, open questions. General playbook: engine/docs/REUSE.md. Checklist for this game: 'Checklist' section below.

## Checklist (from engine/docs/REUSE.md, section 'Per-game checklist')
- [ ] binary comparison (moses/Aaron identical, host exe, scene DLL) recorded in re/notes/binaries.md
- [ ] Ghidra project (re.bat rebuild), re/lib_start.tsv, re.bat export
- [ ] per-binary notes (re/notes/*.md) + names in function_names.tsv
- [ ] engine API gaps: engine/docs/ENGINE_API_COVERAGE.md
- [ ] game/HostConfig$Pascal.cpp filled in (title, registry key, title sprite id, scene DLL, FX file, archives)
- [ ] generated data tables (re/scripts/extract_*.py -> game/$Lower/gen)
- [ ] game scene (game/$Lower) + headless tests (HostConfig::gameTests) + autoplay extension
- [ ] verification with --autoplay

## Open questions
"@
PutCrLf 'CLAUDE.md' @"
# $Title (port of a 1999 Disney 'Rapture' game, Moses/Aaron engine)

Unofficial fan project for preservation and interoperability: analysis and modern port (C++17 + SDL2) of $Title. Same engine as the other games of the family
(moses.dll + Aaron.dll byte-identical): the engine, the platform layer, the Arcade library and the host live in the ``engine/`` submodule (rapture-engine repo).
This repository must not contain any game data or original/decompiled code; you need your own copy of the original game.

## Read first
1. engine/docs/REUSE.md  (full playbook: phases, agents, pitfalls, checklist)
2. engine/docs/ENGINE_API_COVERAGE.md  (what the engine is still missing for this game)
3. engine-re/re/notes/  (aaron.md, moses.md, rrs_format.md, shoot_exe.md apply to all games, if present) and re/notes/reference-game/ (notes of the reference game)
4. game/HostConfig$Pascal.cpp  (the game-specific values for the shared host)

## Rules
- **Portable** tools (no system-wide installation), no Visual Studio: MinGW-w64 + CMake. build.bat and re.bat find/download whatever is missing.
- Original binaries and data stay **outside** the repository, in the game folder (the one above src): $exe, $dll, moses.dll, Aaron.dll, $($rrs -join ', ').
- Fidelity to the original: 1:1 logic, quirks preserved and flagged; every function carries its **original address** in a comment.
- **Do not start the game with a visible window** and do not kill processes without warning: use --autoplay (hidden window) and the headless tests.
- On some machines cmd does not search the current folder: run the .bat files with a path (.\build.bat, & .\re.bat).
- Antivirus software may flag freshly built executables (false positive); add an exclusion for src and the build folder if needed.
- Ghidra: do not open the project in two places at once, do not commit while Ghidra is open.
- Clone with ``git clone --recurse-submodules <url>`` (``build.bat`` runs ``git submodule update --init`` by itself if ``engine/`` is empty).
- Engine fixes are made in the ``engine/`` repo (submodule) and committed there, then the submodule pointer is updated.

## Commands
- re.bat config | setup | rebuild | export | names | gui   (wrapper around engine-re\re.bat; configuration in re/game.cfg)
- build.bat | build.bat run | build.bat clean
- python -I engine/tools/engine_coverage.py   (regenerates engine/docs/ENGINE_API_COVERAGE.md)

## Status
Just created by engine/tools/new-game.ps1: nothing analysed or ported yet. Update this section at every milestone.
"@

# ---------------------------------------------------------------- 5. git
if (-not $DryRun) {
    Push-Location $Dest
    $oldEap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'   # git writes progress on stderr
    try {
        if (-not (Test-Path .git)) { & git init -q 2>&1 | Out-Null }
        if ($EngineUrl -and -not $CopyEngine -and -not (Test-Path 'engine')) {
            & git -c protocol.file.allow=always submodule add $EngineUrl engine 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) { Write-Warning "git submodule add $EngineUrl failed: add it by hand." }
        }
        & git add -A 2>&1 | Out-Null
        if (-not $NoCommit) {
            & git commit -q -m "Bootstrap $Title from the Rapture engine (engine/tools/new-game.ps1)" 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) { Write-Warning 'git commit failed (user.name / user.email not set?). Files are staged: commit by hand.' }
        }
    } finally { $ErrorActionPreference = $oldEap; Pop-Location }
}

Say ''
Say "Done: $Dest"
Say "Next: fill game/HostConfig$Pascal.cpp, then build.bat; re.bat config, re.bat rebuild (see engine/docs/REUSE.md, phase 2); fill lib_start.tsv."
