<#
Runs windeployqt6 against build/OUTPUT/bin/nmpbx.exe, but only if it looks like it
hasn't been done yet. cmake --install never removes files it didn't put there, so
once Qt's DLLs/plugins are deployed once, they stay valid across ordinary rebuilds -
this just avoids re-running windeployqt6 (which re-scans and re-copies everything)
on every single build/debug cycle.

Pass -Force to redeploy anyway - needed after a Qt version bump, or after adding a
QML import that pulls in a plugin that wasn't previously deployed.
#>
param(
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "build"),
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $BuildDir "OUTPUT\bin\nmpbx.exe"
$marker = Join-Path $BuildDir "OUTPUT\bin\Qt6Core.dll"

if (-not $Force -and (Test-Path $marker)) {
    Write-Host "Qt already deployed, skipping (pass -Force to redeploy)." -ForegroundColor DarkGray
    exit 0
}

& "C:\Qt\6.10.3\msvc2022_64\bin\windeployqt6.exe" $exePath --release --qmldir (Join-Path $root "Linphone\view")
exit $LASTEXITCODE
