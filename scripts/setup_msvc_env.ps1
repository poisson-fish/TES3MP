# setup_msvc_env.ps1
# Configures Visual Studio x64 environment, CMake, Ninja, and vNext dependency roots for PowerShell.

[CmdletBinding()]
param(
    [switch]$PreferLatest
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path "$scriptDir\..").Path

function Find-VCVars64 {
    param([bool]$allowNon17 = $false)

    $pf86 = ${env:ProgramFiles(x86)}
    if (-not $pf86) { $pf86 = "C:\Program Files (x86)" }
    $pf = $env:ProgramFiles
    if (-not $pf) { $pf = "C:\Program Files" }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

    # Product dependencies can be provisioned with a newer MSVC STL. When the
    # caller explicitly prefers latest, honor that before probing VS 2022.
    if ($allowNon17 -and (Test-Path $vswhere)) {
        $latestPath = & $vswhere -latest -property installationPath
        if ($latestPath -and (Test-Path "$latestPath\VC\Auxiliary\Build\vcvars64.bat")) {
            return "$latestPath\VC\Auxiliary\Build\vcvars64.bat"
        }
    }

    # Direct candidate paths for VS 2022 (VSCMD_VER 17.x)
    $candidates = @(
        "$pf86\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
        "$pf\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "$pf\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "$pf\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "$pf86\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "$pf86\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "$pf86\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    )
    foreach ($cand in $candidates) {
        if ($cand -and (Test-Path $cand)) {
            return $cand
        }
    }

    # vswhere probe for VS 2022 [17.0, 18.0)
    if (Test-Path $vswhere) {
        $vs2022Path = & $vswhere -version "[17.0,18.0)" -latest -property installationPath
        if ($vs2022Path -and (Test-Path "$vs2022Path\VC\Auxiliary\Build\vcvars64.bat")) {
            return "$vs2022Path\VC\Auxiliary\Build\vcvars64.bat"
        }
    }

    return $null
}

$vcvars = Find-VCVars64 -allowNon17:$PreferLatest
if (-not $vcvars) {
    # If not found yet and we didn't search non-17, try non-17 as fallback
    $vcvars = Find-VCVars64 -allowNon17:$true
}

if (-not $vcvars) {
    Write-Error "Could not find vcvars64.bat. Please ensure Visual Studio 2022 or Build Tools with C++ x64 is installed."
    return
}

# Determine VS install root from vcvars path
$vsInstallDir = (Resolve-Path "$vcvars\..\..\..\..").Path

Write-Host "Initializing MSVC x64 environment from: $vcvars" -ForegroundColor Cyan

# Invoke vcvars64.bat and capture environment variables
$cmdOutput = cmd.exe /s /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $cmdOutput) {
    if ($line -match "^([^=]+)=(.*)$") {
        $varName = $matches[1]
        $varVal = $matches[2]
        # Skip special cmd variables
        if ($varName -notin @("PROMPT", "_")) {
            [System.Environment]::SetEnvironmentVariable($varName, $varVal, [System.EnvironmentVariableTarget]::Process)
        }
    }
}

# Ensure bundled CMake and Ninja from VS are in PATH if not already accessible
$cmakeBinDir = "$vsInstallDir\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$ninjaBinDir = "$vsInstallDir\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"

$pathParts = $env:PATH -split [IO.Path]::PathSeparator
if ((Test-Path "$cmakeBinDir\cmake.exe") -and ($pathParts -notcontains $cmakeBinDir)) {
    $env:PATH = "$cmakeBinDir;$env:PATH"
}
if ((Test-Path "$ninjaBinDir\ninja.exe") -and ($pathParts -notcontains $ninjaBinDir)) {
    $env:PATH = "$ninjaBinDir;$env:PATH"
}

# Ensure default vNext baseline dependency roots are set
if (-not $env:VNEXT_VCPKG_ROOT) {
    $env:VNEXT_VCPKG_ROOT = "$repoRoot\deps"
}
if (-not $env:VNEXT_QT_ROOT) {
    $env:VNEXT_QT_ROOT = "$repoRoot\deps\Qt\6.6.3\msvc2019_64"
}

# Report status
$clCmd = Get-Command cl.exe -ErrorAction SilentlyContinue
$cmakeCmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
$ninjaCmd = Get-Command ninja.exe -ErrorAction SilentlyContinue

$clPath = if ($clCmd) { $clCmd.Source } else { $null }
$cmakePath = if ($cmakeCmd) { $cmakeCmd.Source } else { $null }
$ninjaPath = if ($ninjaCmd) { $ninjaCmd.Source } else { $null }

Write-Host "MSVC Compiler : $clPath (VSCMD_VER=$env:VSCMD_VER)" -ForegroundColor Green
Write-Host "CMake         : $cmakePath" -ForegroundColor Green
Write-Host "Ninja         : $ninjaPath" -ForegroundColor Green
Write-Host "VCPKG Root    : $env:VNEXT_VCPKG_ROOT" -ForegroundColor Green
Write-Host "Qt Root       : $env:VNEXT_QT_ROOT" -ForegroundColor Green
