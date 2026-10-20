# Shared helpers for the setup scripts (dot-sourced by setup-build-env.ps1 and
# setup-re-env.ps1). Messages are plain ASCII on purpose (no code page problems in cmd.exe).

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

function Info($msg) { Write-Host $msg }

function Ask([string]$prompt, [string]$default) {
    $answer = Read-Host "$prompt [$default]"
    if ([string]::IsNullOrWhiteSpace($answer)) { return $default }
    return $answer.Trim().Trim('"')
}

# Enter = yes. With no input at all (script started without a console) the answer is NO,
# so nothing is ever downloaded unattended.
function AskYesNo([string]$prompt) {
    $answer = Read-Host "$prompt [Y/n]"
    if ($null -eq $answer) { return $false }
    return ([string]::IsNullOrWhiteSpace($answer) -or $answer.Trim().ToLower() -in @('s', 'si', 'y', 'yes'))
}

# Looks for an already downloaded tool: first-level and second-level sub-folders of the
# remembered tools folders for which $test returns true. Returns the folder or $null.
function Find-InToolsRoots([string[]]$roots, [scriptblock]$test) {
    foreach ($root in ($roots | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique)) {
        $dirs = @(Get-ChildItem $root -Directory -ErrorAction SilentlyContinue)
        $dirs += @($dirs | ForEach-Object { Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue })
        foreach ($d in ($dirs | Sort-Object FullName -Descending)) {
            if (& $test $d.FullName) { return $d.FullName }
        }
    }
    return $null
}

function Get-LatestAssetUrl([string]$repo, [string]$pattern, [string]$fallback) {
    try {
        $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" -Headers @{ 'User-Agent' = 'rapture-build' }
        $asset = $rel.assets | Where-Object { $_.name -match $pattern } | Select-Object -First 1
        if ($asset) { return $asset.browser_download_url }
    } catch {}
    return $fallback
}

function Test-Url([string]$url) {
    try {
        $r = Invoke-WebRequest -Uri $url -Method Head -UseBasicParsing -MaximumRedirection 5 -Headers @{ 'User-Agent' = 'rapture-build' }
        $mb = [math]::Round([double]$r.Headers['Content-Length'] / 1MB, 1)
        Info "  OK   $url ($mb MB)"
        return $true
    } catch {
        Info "  FAIL $url ($($_.Exception.Message))"
        return $false
    }
}

function Read-Config([string]$file) {
    $cfg = @{}
    if (Test-Path $file) {
        try { (Get-Content $file -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $cfg[$_.Name] = $_.Value } } catch {}
    }
    return $cfg
}

function Save-Config([string]$file, $cfg) {
    ($cfg | ConvertTo-Json) | Set-Content -Path $file -Encoding ASCII
}

# Downloads a zip and unpacks it into $destination (created if needed).
function Download-And-Unpack([string]$name, [string]$url, [string]$destination, [string]$fileName = '') {
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    if (-not $fileName) { $fileName = [IO.Path]::GetFileName(([Uri]$url).AbsolutePath) }
    $zip = Join-Path $destination $fileName
    Info "Downloading $name..."
    Info "  from $url"
    & "$env:SystemRoot\System32\curl.exe" -L --fail -# -o $zip $url
    if ($LASTEXITCODE -ne 0) { throw "Download of $name failed." }
    Info "Unpacking $name (can take a minute)..."
    & "$env:SystemRoot\System32\tar.exe" -xf $zip -C $destination
    if ($LASTEXITCODE -ne 0) { throw "Unpacking of $name failed." }
    Remove-Item $zip -Force
}

# Folder where portable tools are placed. Asked once, remembered in $cfg.toolsRoot
# (and reused from the other setup script's config if it already knows one).
function Get-ToolsRoot($cfg, [string]$otherConfigFile = '') {
    if ($cfg.toolsRoot) { return $cfg.toolsRoot }
    $default = $null
    if ($otherConfigFile) { $other = Read-Config $otherConfigFile; if ($other.toolsRoot) { $default = $other.toolsRoot } }
    if (-not $default) { $default = Join-Path $env:USERPROFILE 'RaptureBuildTools' }
    Info ''
    Info 'Some tools are missing. They are downloaded in portable form (nothing is installed).'
    $folder = Ask 'Folder where the tools should be placed (remembered for next time)' $default
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    $cfg.toolsRoot = (Resolve-Path $folder).Path
    return $cfg.toolsRoot
}

# Root of the GAME repository (where build.bat, .buildtools.json and build.env.bat live).
#   * $env:RAPTURE_ROOT, when set (build.bat / re.bat set it), else
#   * the folder that contains engine\ when this script is <root>\engine\tools\ (the engine is a submodule of the game), else
#   * the parent of tools\ (the engine repo used on its own).
function Get-ProjectRoot {
    if ($env:RAPTURE_ROOT) { return (Resolve-Path $env:RAPTURE_ROOT).Path.TrimEnd('\') }
    $engineDir = Split-Path -Parent $PSScriptRoot
    if ((Split-Path -Leaf $engineDir) -eq 'engine') { return Split-Path -Parent $engineDir }
    return $engineDir
}
