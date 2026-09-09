# launch_server.ps1
# Starts the TES3MP dedicated server.

param(
    [string]$ConfigPath,
    [string]$ServerBin
)

$ErrorActionPreference = "Stop"
$repoRoot = $PSScriptRoot

# 1. Locate server binary
if (-not $ServerBin -and $env:TES3MP_SERVER_BIN) {
    $ServerBin = $env:TES3MP_SERVER_BIN
}

if (-not $ServerBin) {
    $binCandidates = @(
        "$repoRoot\build\vnext-product\tes3mp_server.exe",
        "$repoRoot\build\vnext-product\apps\tes3mp-server\tes3mp_server.exe",
        "$repoRoot\build\slice82-openmw-full\RelWithDebInfo\tes3mp_server.exe",
        "$repoRoot\build\slice82-openmw-full\Release\tes3mp_server.exe",
        "$repoRoot\build\vnext-baseline-install\bin\tes3mp_server.exe",
        "$repoRoot\build\vnext-baseline\tes3mp-server\tes3mp_server.exe",
        "$repoRoot\build\vnext-baseline\tes3mp_server.exe"
    )
    foreach ($cand in $binCandidates) {
        if (Test-Path $cand) {
            $ServerBin = (Resolve-Path $cand).Path
            break
        }
    }
}

if (-not $ServerBin -or -not (Test-Path $ServerBin)) {
    Write-Error "Could not find tes3mp_server.exe. Please build it first with build_windows.bat."
    return
}

# Ensure dependency DLLs are accessible
$vcpkgBin = "$repoRoot\deps\installed\x64-windows\bin"
if ((Test-Path $vcpkgBin) -and ($env:PATH -notlike "*$vcpkgBin*")) {
    $env:PATH = "$vcpkgBin;$env:PATH"
}

# 2. Locate server configuration file
if (-not $ConfigPath -and $env:TES3MP_SERVER_CONFIG) {
    $ConfigPath = $env:TES3MP_SERVER_CONFIG
}

if (-not $ConfigPath) {
    $configCandidates = @(
        "$repoRoot\build\vnext-product\resources\vfs\tes3mp\server.cfg",
        "$repoRoot\build\slice82-openmw-full\RelWithDebInfo\resources\vfs\tes3mp\server.cfg",
        "$repoRoot\build\slice82-openmw-full\Release\resources\vfs\tes3mp\server.cfg",
        "$repoRoot\files\data\tes3mp\server.cfg"
    )
    foreach ($cand in $configCandidates) {
        if (Test-Path $cand) {
            $ConfigPath = (Resolve-Path $cand).Path
            break
        }
    }
}

if (-not $ConfigPath -or -not (Test-Path $ConfigPath)) {
    Write-Error "Could not find server.cfg. Please specify with -ConfigPath."
    return
}

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Starting TES3MP Dedicated Server" -ForegroundColor Cyan
Write-Host "Binary: $ServerBin" -ForegroundColor Gray
Write-Host "Config: $ConfigPath" -ForegroundColor Gray
Write-Host "==========================================" -ForegroundColor Cyan

& "$ServerBin" "$ConfigPath"
