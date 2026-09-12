# Builds the small TES3MP product surface by default. Broader verification is opt-in.

[CmdletBinding()]
param(
    [ValidateSet("product", "client", "server", "headless", "checks", "contracts", "protocol", "server-logic", "adapter-tests", "desktop-evidence", "baseline", "doctor", "full", "standalone")]
    [string]$Target = "product",

    [string]$Config = "RelWithDebInfo",
    [switch]$Clean,
    [switch]$Doctor,
    [switch]$VerboseOutput,
    [ValidateRange(1, 100)]
    [int]$TailLines = 12
)

$ErrorActionPreference = "Stop"
$repoRoot = $PSScriptRoot
$logDirectory = Join-Path $repoRoot "build\logs"
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$buildLog = Join-Path $logDirectory "$timestamp-$Target.log"
Set-Content -LiteralPath $buildLog -Value "TES3MP build target: $Target`nStarted: $(Get-Date -Format o)"

function Invoke-LoggedStage {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    Write-Host "[stage] $Name" -ForegroundColor Cyan
    Add-Content -LiteralPath $buildLog -Value "`n===== $Name ====="
    if ($VerboseOutput) {
        & $Command @Arguments 2>&1 | Tee-Object -FilePath $buildLog -Append
    } else {
        & $Command @Arguments *>> $buildLog
    }
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        Write-Host "[failed] $Name (exit $exitCode)" -ForegroundColor Red
        Write-Host "Last $TailLines log lines:" -ForegroundColor Yellow
        Get-Content -LiteralPath $buildLog -Tail $TailLines
        throw "$Name failed. Full log: $buildLog"
    }
}

function Complete-LoggedBuild {
    $recent = Get-Content -LiteralPath $buildLog -Tail ([Math]::Max(40, $TailLines * 4)) |
        Where-Object {
            $_ -notmatch '^\[\d+/\d+\]' -and
            $_ -notmatch '[\\/]generated[\\/].*\.h(pp)?\b'
        } |
        Select-Object -Last $TailLines
    if ($recent) {
        Write-Host "[tail] recent non-progress output" -ForegroundColor DarkGray
        $recent
    }
    Write-Host "[done] Full build log: $buildLog" -ForegroundColor Green
}

# The baseline is pinned to VS 2022. Product transport dependencies may have
# been provisioned with a newer installed MSVC and must use a matching STL.
$runningBaseline = $Doctor -or $Target -in @("baseline", "doctor")
$preferLatest = -not $runningBaseline
Write-Host "[stage] Initialize MSVC environment" -ForegroundColor Cyan
Add-Content -LiteralPath $buildLog -Value "`n===== Initialize MSVC environment ====="
try {
    if ($VerboseOutput) {
        & { . "$repoRoot\scripts\setup_msvc_env.ps1" -PreferLatest:$preferLatest } 2>&1 |
            Tee-Object -FilePath $buildLog -Append
    } else {
        & { . "$repoRoot\scripts\setup_msvc_env.ps1" -PreferLatest:$preferLatest } *>> $buildLog
    }
} catch {
    Write-Host "[failed] Initialize MSVC environment" -ForegroundColor Red
    Get-Content -LiteralPath $buildLog -Tail $TailLines
    throw
}

if ($Doctor -or $Target -eq "doctor") {
    Invoke-LoggedStage -Name "Baseline doctor" -Command "python" -Arguments @("$repoRoot\scripts\run_vnext_baseline.py", "doctor")
    Complete-LoggedBuild
    return
}

if ($Target -eq "baseline") {
    Invoke-LoggedStage -Name "Full baseline suite" -Command "python" -Arguments @("$repoRoot\scripts\run_vnext_baseline.py", "all")
    Complete-LoggedBuild
    return
}

if ($Target -in @("contracts", "standalone", "protocol", "server-logic", "adapter-tests")) {
    $standalonePreset = @{
        contracts       = "tes3mp-standalone"
        standalone      = "tes3mp-standalone"
        protocol        = "tes3mp-protocol-contracts"
        "server-logic" = "tes3mp-server-logic"
        "adapter-tests" = "tes3mp-adapter-tests"
    }[$Target]
    $compDir = "$repoRoot\components\tes3mp"
    Push-Location $compDir
    try {
        $configureArgs = @("--preset", "tes3mp-standalone", "-DCMAKE_BUILD_TYPE=$Config")
        if ($Clean) { $configureArgs += "--fresh" }
        Invoke-LoggedStage -Name "Configure engine-independent TES3MP graph" -Command "cmake" -Arguments $configureArgs
        Invoke-LoggedStage -Name "Build and run $Target" -Command "cmake" -Arguments @("--build", "--preset", $standalonePreset, "--parallel")
    } finally {
        Pop-Location
    }
    Complete-LoggedBuild
    return
}

$manifest = "$repoRoot\build\vnext-transport-dependencies\manifest.json"
Invoke-LoggedStage -Name "Check verified transport dependencies" -Command "python" -Arguments @("$repoRoot\scripts\provision_vnext_transport.py")
if (-not (Test-Path -LiteralPath $manifest)) { throw "TES3MP transport dependency manifest was not created." }

$presetByTarget = @{
    product  = "vnext-product-windows"
    full     = "vnext-product-windows"
    client   = "vnext-product-client-windows"
    server   = "vnext-product-server-windows"
    headless = "vnext-product-headless-windows"
    checks   = "vnext-product-checks-windows"
    "desktop-evidence" = "vnext-desktop-evidence-windows"
}
$buildPreset = $presetByTarget[$Target]
if (-not $buildPreset) { throw "Unsupported build target: $Target" }

$configurePreset = if ($Target -eq "desktop-evidence") { "vnext-desktop-evidence-windows" } else { "vnext-product-windows" }
$configureArgs = @("--preset", $configurePreset, "-DCMAKE_BUILD_TYPE=$Config")
if ($Clean) { $configureArgs += "--fresh" }
Invoke-LoggedStage -Name "Configure $configurePreset" -Command "cmake" -Arguments $configureArgs
Invoke-LoggedStage -Name "Build $buildPreset" -Command "cmake" -Arguments @("--build", "--preset", $buildPreset, "--parallel")

# Ensure runtime dependencies that lack automatic CMake deployment are present
$productBinDir = if ($Target -eq "desktop-evidence") {
    "$repoRoot\build\vnext-desktop-evidence"
} else {
    "$repoRoot\build\vnext-product"
}
$myguiCandidate = "$repoRoot\deps\installed\x64-windows\bin\Release\MyGUIEngine.dll"
$vcpkgBin = "$repoRoot\deps\installed\x64-windows\bin"

if (-not (Test-Path "$vcpkgBin\MyGUIEngine.dll") -and (Test-Path $myguiCandidate)) {
    Copy-Item $myguiCandidate "$vcpkgBin\MyGUIEngine.dll" -Force
}
if ((Test-Path $myguiCandidate) -and (Test-Path $productBinDir)) {
    Copy-Item $myguiCandidate "$productBinDir\MyGUIEngine.dll" -Force
}

Complete-LoggedBuild
