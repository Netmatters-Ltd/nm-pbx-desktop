<#
Builds, installs, and deploys Qt for NMPBX. Mirrors the Build -> Install -> Deploy Qt
task chain used by .vscode/tasks.json, so it can be run from a plain terminal instead
of through VS Code.

Deploying Qt (windeployqt6) is skipped once build/OUTPUT/bin already has Qt's DLLs,
since a normal rebuild doesn't change what Qt libraries/plugins are needed. Pass
-ForceQtDeploy after a Qt version bump or a QML change that needs a new plugin.

Usage:
    .\scripts\dev.ps1                  # build, install, deploy Qt (if needed)
    .\scripts\dev.ps1 -Debug           # same, then launch nmpbx.exe under the Visual Studio debugger
    .\scripts\dev.ps1 -ForceQtDeploy   # force windeployqt6 to run even if already deployed
#>
param(
    [string]$Config = "RelWithDebInfo",
    [switch]$Debug,
    [switch]$ForceQtDeploy
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build"
$exePath = Join-Path $buildDir "OUTPUT\bin\nmpbx.exe"

$cmake = "C:\msys64\mingw64\bin\cmake.exe"
$deployQtScript = Join-Path $PSScriptRoot "deploy-qt-if-needed.ps1"
$devenv = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\devenv.exe"

function Invoke-Step {
    param([string]$Name, [string]$Exe, [string[]]$Arguments)
    Write-Host "==> $Name" -ForegroundColor Cyan
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

Invoke-Step "Build" $cmake @("--build", $buildDir, "--config", $Config, "--parallel", "10")
Invoke-Step "Install" $cmake @("--install", $buildDir, "--config", $Config)

Write-Host "==> Deploy Qt" -ForegroundColor Cyan
& $deployQtScript -BuildDir $buildDir @(if ($ForceQtDeploy) { "-Force" })
if ($LASTEXITCODE -ne 0) {
    throw "Deploy Qt failed with exit code $LASTEXITCODE"
}

Write-Host "Build, install and Qt deploy complete." -ForegroundColor Green

if ($Debug) {
    Write-Host "==> Launching under the Visual Studio debugger" -ForegroundColor Cyan
    & $devenv "/debugexe" $exePath
}
