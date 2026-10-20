# Finds (or downloads, in portable form) the tools needed to BUILD the game:
#   - a MinGW-w64 compiler (g++ and mingw32-make)  -> WinLibs
#   - CMake
#   - SDL2 development files for MinGW
#
# Called by build.bat. Remembers what it found in .buildtools.json and writes
# build.env.bat, which build.bat loads. Nothing is installed system-wide:
# downloads are unpacked into a folder you choose (asked once, then remembered).
param(
    [switch]$CheckUrls   # only resolve the download addresses and test them, then exit
)

. "$PSScriptRoot\common.ps1"

$root = Get-ProjectRoot
$cfgFile = Join-Path $root '.buildtools.json'
$otherCfgFile = Join-Path $root '.retools.json'
$envFile = Join-Path $root 'build.env.bat'
$sdl2Version = '2.32.10'
$sdl2Url = "https://github.com/libsdl-org/SDL/releases/download/release-$sdl2Version/SDL2-devel-$sdl2Version-mingw.zip"

function Get-CmakeUrl {
    Get-LatestAssetUrl 'Kitware/CMake' '^cmake-[\d\.]+-windows-x86_64\.zip$' 'https://github.com/Kitware/CMake/releases/download/v3.31.7/cmake-3.31.7-windows-x86_64.zip'
}
function Get-MingwUrl {
    Get-LatestAssetUrl 'brechtsanders/winlibs_mingw' '^winlibs-x86_64-posix-seh-gcc-[\d\.]+-mingw-w64ucrt-[\d\.]+-r\d+\.zip$' 'https://github.com/brechtsanders/winlibs_mingw/releases/download/14.2.0posix-12.0.0-ucrt-r2/winlibs-x86_64-posix-seh-gcc-14.2.0-mingw-w64ucrt-12.0.0-r2.zip'
}

if ($CheckUrls) {
    Info 'Checking download addresses...'
    $ok = (Test-Url (Get-MingwUrl)) -and (Test-Url (Get-CmakeUrl)) -and (Test-Url $sdl2Url)
    if ($ok) { exit 0 } else { exit 1 }
}

$cfg = Read-Config $cfgFile
$knownRoots = @($cfg.toolsRoot, (Read-Config $otherCfgFile).toolsRoot)

function Test-Gcc([string]$bin) {
    return $bin -and (Test-Path (Join-Path $bin 'g++.exe')) -and (Test-Path (Join-Path $bin 'mingw32-make.exe'))
}
function Test-Cmake([string]$bin) { return $bin -and (Test-Path (Join-Path $bin 'cmake.exe')) }
function Test-Sdl2([string]$dir) { return $dir -and (Test-Path (Join-Path $dir 'cmake\sdl2-config.cmake')) }

# ---------------------------------------------------------------- compiler
$gccBin = $cfg.gccBin
if (-not (Test-Gcc $gccBin)) {
    $gccBin = $null
    $gpp = Get-Command g++.exe -ErrorAction SilentlyContinue
    if ($gpp -and (Test-Gcc (Split-Path -Parent $gpp.Source))) { $gccBin = Split-Path -Parent $gpp.Source }
}
if (-not $gccBin) {
    $gccBin = Find-InToolsRoots $knownRoots { param($d) Test-Gcc (Join-Path $d 'bin') }
    if ($gccBin) { $gccBin = Join-Path $gccBin 'bin' }
}
if (-not $gccBin) {
    Info ''
    Info 'MinGW-w64 compiler (g++ and mingw32-make) not found.'
    $url = Get-MingwUrl
    $toolsRoot = Get-ToolsRoot $cfg $otherCfgFile
    if (-not (AskYesNo "Download WinLibs MinGW-w64 (about 260 MB) into $toolsRoot?")) { Info 'Cannot build without a compiler.'; exit 1 }
    $dest = Join-Path $toolsRoot 'winlibs'
    Download-And-Unpack 'MinGW-w64 (WinLibs)' $url $dest
    $gccBin = Join-Path $dest 'mingw64\bin'
    if (-not (Test-Gcc $gccBin)) { throw "Compiler not found in $gccBin after unpacking." }
}
$cfg.gccBin = $gccBin

# ---------------------------------------------------------------- CMake
$cmakeBin = $cfg.cmakeBin
if (-not (Test-Cmake $cmakeBin)) {
    $cmakeBin = $null
    $cm = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cm) { $cmakeBin = Split-Path -Parent $cm.Source }
}
if (-not $cmakeBin) {
    $cmakeBin = Find-InToolsRoots $knownRoots { param($d) Test-Cmake (Join-Path $d 'bin') }
    if ($cmakeBin) { $cmakeBin = Join-Path $cmakeBin 'bin' }
}
if (-not $cmakeBin) {
    Info ''
    Info 'CMake not found.'
    $url = Get-CmakeUrl
    $toolsRoot = Get-ToolsRoot $cfg $otherCfgFile
    if (-not (AskYesNo "Download portable CMake (about 52 MB) into $toolsRoot?")) { Info 'Cannot build without CMake.'; exit 1 }
    $dest = Join-Path $toolsRoot 'cmake'
    Download-And-Unpack 'CMake' $url $dest
    $found = Get-ChildItem $dest -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'bin\cmake.exe') } | Select-Object -First 1
    if (-not $found) { throw "CMake not found in $dest after unpacking." }
    $cmakeBin = Join-Path $found.FullName 'bin'
}
$cfg.cmakeBin = $cmakeBin

# ---------------------------------------------------------------- SDL2
$sdl2Root = $cfg.sdl2Root
if (-not (Test-Sdl2 $sdl2Root)) {
    $sdl2Root = $null
    if ($env:SDL2_DIR) {
        $candidate = if ((Split-Path -Leaf $env:SDL2_DIR) -eq 'cmake') { Split-Path -Parent $env:SDL2_DIR } else { $env:SDL2_DIR }
        if (Test-Sdl2 $candidate) { $sdl2Root = $candidate }
    }
}
if (-not $sdl2Root) {
    $sdl2Root = Find-InToolsRoots $knownRoots { param($d) Test-Sdl2 $d }
}
if (-not $sdl2Root) {
    Info ''
    Info "SDL2 $sdl2Version (development files for MinGW) not found."
    $toolsRoot = Get-ToolsRoot $cfg $otherCfgFile
    if (-not (AskYesNo "Download SDL2 (about 14 MB) into $toolsRoot?")) { Info 'Cannot build without SDL2.'; exit 1 }
    $dest = Join-Path $toolsRoot 'SDL2-mingw'
    Download-And-Unpack 'SDL2' $sdl2Url $dest
    $found = Get-ChildItem $dest -Directory | Where-Object { Test-Sdl2 $_.FullName } | Select-Object -First 1
    if (-not $found) { throw "SDL2 not found in $dest after unpacking." }
    $sdl2Root = $found.FullName
}
$cfg.sdl2Root = $sdl2Root

# ---------------------------------------------------------------- remember + hand over to build.bat
Save-Config $cfgFile $cfg
@(
    '@echo off',
    "set `"MS_GCC_BIN=$gccBin`"",
    "set `"MS_CMAKE_BIN=$cmakeBin`"",
    "set `"MS_SDL2_DIR=$(Join-Path $sdl2Root 'cmake')`""
) | Set-Content -Path $envFile -Encoding ASCII

Info "Build tools ready:"
Info "  compiler: $gccBin"
Info "  cmake:    $cmakeBin"
Info "  SDL2:     $sdl2Root"
