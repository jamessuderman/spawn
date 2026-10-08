# Spawn — debug build helper.
# Adds a SpawnAssets --clean-first step before the regular build to
# sidestep the LNK1236 "corrupt COFF sections" error that hits the
# binary-data target every time a new SVG icon is added. The error is
# a stale .obj in the SpawnAssets intermediate dir; a clean rebuild of
# that target regenerates the obj files and the link succeeds.
#
# Usage (from the repo root):
#   powershell -ExecutionPolicy Bypass -File tools/build-debug.ps1
#
# Equivalent to:
#   cmake --build build/debug-x64 --target SpawnAssets --clean-first
#   cmake --build build/debug-x64

$ErrorActionPreference = "Stop"

$buildDir = "build/debug-x64"
if (-not (Test-Path -LiteralPath $buildDir)) {
    Write-Host "Build directory '$buildDir' does not exist." -ForegroundColor Red
    Write-Host "Configure it first with: cmake --preset debug-x64" -ForegroundColor Yellow
    exit 1
}

Write-Host "[build-debug] Cleaning SpawnAssets target..." -ForegroundColor Cyan
cmake --build $buildDir --target SpawnAssets --clean-first
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[build-debug] Building SpawnStandalone..." -ForegroundColor Cyan
cmake --build $buildDir
exit $LASTEXITCODE
