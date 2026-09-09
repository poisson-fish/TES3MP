# launch_client.ps1
# Starts the OpenMW client with TES3MP multiplayer and Steam Morrowind assets.

param(
    [string]$DataDir,
    [string]$OpenMWBin,
    [string]$ResourcesDir,
    [string]$Connect,
    [string]$Password,
    [switch]$NoGrab
)

$ErrorActionPreference = "Stop"
$repoRoot = $PSScriptRoot

# 1. Locate OpenMW client binary
if (-not $OpenMWBin -and $env:OPENMW_BIN) {
    $OpenMWBin = $env:OPENMW_BIN
}

if (-not $OpenMWBin) {
    $binCandidates = @(
        "$repoRoot\build\vnext-product\openmw.exe",
        "$repoRoot\build\slice82-openmw-full\RelWithDebInfo\openmw.exe",
        "$repoRoot\build\slice82-openmw-full\Release\openmw.exe",
        "$repoRoot\build\vnext-baseline-install\bin\openmw.exe",
        "$repoRoot\build\vnext-baseline\openmw.exe"
    )
    foreach ($cand in $binCandidates) {
        if (Test-Path $cand) {
            $OpenMWBin = (Resolve-Path $cand).Path
            break
        }
    }
}

if (-not $OpenMWBin -or -not (Test-Path $OpenMWBin)) {
    Write-Error "Could not find openmw.exe. Please build it first with build_windows.bat."
    return
}

# Ensure OSG plugins and runtime dependencies are in PATH and OSG_LIBRARY_PATH
$vcpkgBin = "$repoRoot\deps\installed\x64-windows\bin"
if (Test-Path "$vcpkgBin\osgPlugins-3.6.5") {
    $env:OSG_LIBRARY_PATH = "$vcpkgBin\osgPlugins-3.6.5"
    if ($env:PATH -notlike "*$vcpkgBin*") {
        $env:PATH = "$vcpkgBin;$env:PATH"
    }
}
$qtBin = "$repoRoot\deps\Qt\6.6.3\msvc2019_64\bin"
if ((Test-Path $qtBin) -and ($env:PATH -notlike "*$qtBin*")) {
    $env:PATH = "$qtBin;$env:PATH"
}

# 2. Locate Morrowind Data Files
if (-not $DataDir -and $env:MORROWIND_DATA_DIR) {
    $DataDir = $env:MORROWIND_DATA_DIR
}

if (-not $DataDir) {
    $steamCandidates = @(
        "${env:ProgramFiles(x86)}\Steam\steamapps\common\Morrowind\Data Files",
        "$env:ProgramFiles\Steam\steamapps\common\Morrowind\Data Files",
        "D:\Steam\steamapps\common\Morrowind\Data Files",
        "D:\SteamLibrary\steamapps\common\Morrowind\Data Files",
        "E:\Steam\steamapps\common\Morrowind\Data Files",
        "E:\SteamLibrary\steamapps\common\Morrowind\Data Files",
        "F:\SteamLibrary\steamapps\common\Morrowind\Data Files",
        "G:\SteamLibrary\steamapps\common\Morrowind\Data Files"
    )
    foreach ($cand in $steamCandidates) {
        if ($cand -and (Test-Path "$cand\Morrowind.esm")) {
            $DataDir = (Resolve-Path $cand).Path
            break
        }
    }
}

if (-not $DataDir -or -not (Test-Path "$DataDir\Morrowind.esm")) {
    Write-Error "Could not find Morrowind Data Files with Morrowind.esm. Please specify with -DataDir."
    return
}

# 3. Locate Resources directory
if (-not $ResourcesDir -and $env:OPENMW_RESOURCES) {
    $ResourcesDir = $env:OPENMW_RESOURCES
}

if (-not $ResourcesDir) {
    $binDir = Split-Path -Parent $OpenMWBin
    $resCandidates = @(
        "$binDir\resources",
        "$repoRoot\build\vnext-product\resources",
        "$repoRoot\build\vnext-baseline\resources",
        "$repoRoot\build\vnext-baseline-install\resources"
    )
    foreach ($cand in $resCandidates) {
        if (Test-Path $cand) {
            $ResourcesDir = (Resolve-Path $cand).Path
            break
        }
    }
}

# 4. Build argument list
$clientArgs = @(
    "--tes3mp-enable=1",
    "--data=$DataDir"
)

if ($ResourcesDir -and (Test-Path $ResourcesDir)) {
    $clientArgs += "--resources=$ResourcesDir"
}

# Archives (BSAs)
$bsas = @("Morrowind.bsa", "Tribunal.bsa", "Bloodmoon.bsa")
foreach ($bsa in $bsas) {
    if (Test-Path "$DataDir\$bsa") {
        $clientArgs += "--fallback-archive=$bsa"
    }
}

# Content (ESMs)
$esms = @("Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm")
foreach ($esm in $esms) {
    if (Test-Path "$DataDir\$esm") {
        $clientArgs += "--content=$esm"
    }
}

if ($NoGrab) {
    $clientArgs += "--no-grab=1"
}

# Direct connect or menu mode
$tempPasswordFile = $null
if ($Connect) {
    $hostPart = $Connect
    $portPart = 25565
    if ($Connect -match "^(.*):(\d+)$") {
        $hostPart = $matches[1]
        $portPart = [int]$matches[2]
    }
    $clientArgs += "--skip-menu=1"
    $clientArgs += "--new-game=1"
    $clientArgs += "--tes3mp-host=$hostPart"
    $clientArgs += "--tes3mp-port=$portPart"

    if ($Password) {
        $tempPasswordFile = [System.IO.Path]::GetTempFileName()
        Set-Content -Path $tempPasswordFile -Value $Password -NoNewline
        $clientArgs += "--tes3mp-password-file=$tempPasswordFile"
    }
}

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Starting TES3MP Client (OpenMW)" -ForegroundColor Cyan
Write-Host "Binary    : $OpenMWBin" -ForegroundColor Gray
Write-Host "Data Files: $DataDir" -ForegroundColor Gray
if ($ResourcesDir) { Write-Host "Resources : $ResourcesDir" -ForegroundColor Gray }
if ($Connect) { Write-Host "Connect   : $Connect" -ForegroundColor Yellow }
Write-Host "==========================================" -ForegroundColor Cyan

try {
    & "$OpenMWBin" @clientArgs $args
} finally {
    if ($tempPasswordFile -and (Test-Path $tempPasswordFile)) {
        Remove-Item -Path $tempPasswordFile -Force -ErrorAction SilentlyContinue
    }
}
