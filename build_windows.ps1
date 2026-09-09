# Builds the small TES3MP product surface by default. Broader verification is opt-in.

[CmdletBinding()]
param(
    [ValidateSet("product", "client", "server", "headless", "checks", "contracts", "baseline", "doctor", "full", "standalone")]
    [string]$Target = "product",

    [string]$Config = "RelWithDebInfo",
    [switch]$Clean,
    [switch]$Doctor
)

$ErrorActionPreference = "Stop"
$repoRoot = $PSScriptRoot

# The baseline is pinned to VS 2022. Product transport dependencies may have
# been provisioned with a newer installed MSVC and must use a matching STL.
$runningBaseline = $Doctor -or $Target -in @("baseline", "doctor")
$preferLatest = -not $runningBaseline
. "$repoRoot\scripts\setup_msvc_env.ps1" -PreferLatest:$preferLatest

if ($Doctor -or $Target -eq "doctor") {
    Write-Host "`nRunning baseline doctor..." -ForegroundColor Cyan
    python "$repoRoot\scripts\run_vnext_baseline.py" doctor
    return
}

if ($Target -eq "baseline") {
    Write-Host "`nRunning full baseline suite..." -ForegroundColor Cyan
    python "$repoRoot\scripts\run_vnext_baseline.py" all
    return
}

if ($Target -in @("contracts", "standalone")) {
    Write-Host "`nBuilding and running engine-independent TES3MP contracts..." -ForegroundColor Cyan
    $compDir = "$repoRoot\components\tes3mp"
    Push-Location $compDir
    try {
        $configureArgs = @("--preset", "tes3mp-standalone", "-DCMAKE_BUILD_TYPE=$Config")
        if ($Clean) { $configureArgs += "--fresh" }
        & cmake @configureArgs
        if ($LASTEXITCODE -ne 0) { throw "TES3MP contract configuration failed." }
        cmake --build --preset tes3mp-standalone --parallel
        if ($LASTEXITCODE -ne 0) { throw "TES3MP contract build failed." }
    } finally {
        Pop-Location
    }
    return
}

$manifest = "$repoRoot\build\vnext-transport-dependencies\manifest.json"
Write-Host "`nChecking verified transport dependencies..." -ForegroundColor Cyan
python "$repoRoot\scripts\provision_vnext_transport.py"
if ($LASTEXITCODE -ne 0) { throw "TES3MP transport dependency provisioning failed." }
if (-not (Test-Path -LiteralPath $manifest)) { throw "TES3MP transport dependency manifest was not created." }

$presetByTarget = @{
    product  = "vnext-product-windows"
    full     = "vnext-product-windows"
    client   = "vnext-product-client-windows"
    server   = "vnext-product-server-windows"
    headless = "vnext-product-headless-windows"
    checks   = "vnext-product-checks-windows"
}
$buildPreset = $presetByTarget[$Target]
if (-not $buildPreset) { throw "Unsupported build target: $Target" }

Write-Host "`nConfiguring the bounded TES3MP product graph..." -ForegroundColor Cyan
$configureArgs = @("--preset", "vnext-product-windows", "-DCMAKE_BUILD_TYPE=$Config")
if ($Clean) { $configureArgs += "--fresh" }
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "TES3MP product configuration failed." }

Write-Host "`nBuilding preset: $buildPreset" -ForegroundColor Cyan
cmake --build --preset $buildPreset --parallel
if ($LASTEXITCODE -ne 0) { throw "TES3MP product build failed." }

# Ensure runtime dependencies that lack automatic CMake deployment are present
$productBinDir = "$repoRoot\build\vnext-product"
$myguiCandidate = "$repoRoot\deps\installed\x64-windows\bin\Release\MyGUIEngine.dll"
$vcpkgBin = "$repoRoot\deps\installed\x64-windows\bin"

if (-not (Test-Path "$vcpkgBin\MyGUIEngine.dll") -and (Test-Path $myguiCandidate)) {
    Copy-Item $myguiCandidate "$vcpkgBin\MyGUIEngine.dll" -Force
}
if ((Test-Path $myguiCandidate) -and (Test-Path $productBinDir)) {
    Copy-Item $myguiCandidate "$productBinDir\MyGUIEngine.dll" -Force
}
