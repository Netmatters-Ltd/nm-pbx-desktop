<#
Builds a Windows installer for NMPBX (NSIS via CPack, per nm-pbx-docs/windows-installer-guide.md)
and drops the result at the repo root.

Reuses the normal build/ directory rather than a separate tree - a separate tree would
mean rebuilding the entire external SDK (linphone-sdk and its many ExternalProject_Add
sub-builds: VPX, crashpad, dav1d, ...) from scratch, which is very expensive. Instead this
just reconfigures build/ with ENABLE_APP_PACKAGING=YES (a fast, incremental reconfigure -
already-built external dependencies aren't rebuilt), packages, then flips the flag back to
OFF afterwards (even on failure) so your normal F5 debug loop goes straight back to its
usual fast, non-packaging Install behaviour.

Don't run this while a debug session is active - Install will fail to overwrite files
still open in the running nmpbx.exe.

Runs from inside build/ itself (not the repo root) because packaging.cmake.in invokes
cpack without an explicit WORKING_DIRECTORY, so cpack looks for build/CPackConfig.cmake
relative to whatever directory the `cmake --install` call was itself run from - matching
the project's own documented workflow of cd-ing into the build dir first.

CPack names the installer itself (NMPBX-<version>-win64.exe); this script doesn't rename
anything, it only relocates the output. If exactly one file comes out of
OUTPUT/Packages, that file is copied to the repo root as-is. If more than one file comes
out (a genuine bundle - e.g. an extra symbols package or archive), the whole Packages
folder is copied to ./NMPBX-release/ instead, so a bundle doesn't get scattered as loose
files across the repo root.
#>
param(
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "build"),
    [string]$Config = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$cmake = "C:\msys64\mingw64\bin\cmake.exe"

function Invoke-Step {
    param([string]$Name, [string]$Exe, [string[]]$Arguments)
    Write-Host "==> $Name" -ForegroundColor Cyan
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

function Set-PackagingFlag {
    param([string]$Value)
    # CMAKE_BUILD_TYPE is otherwise irrelevant for this multi-config (Visual Studio) generator -
    # --config on build/install is what actually selects RelWithDebInfo. But packaging.cmake.in
    # bakes @CMAKE_BUILD_TYPE@ straight into the `cpack -C` flag it runs, so it still needs to be
    # set explicitly or that step fails with "Invalid value used with -C".
    Invoke-Step "Configure (ENABLE_APP_PACKAGING=$Value)" $cmake @(
        "-S", $root, "-B", $BuildDir,
        "-DENABLE_APP_PACKAGING=$Value",
        "-DCMAKE_BUILD_TYPE=$Config"
    )
}

Set-PackagingFlag "YES"
Push-Location $BuildDir
try {
    Invoke-Step "Build" $cmake @("--build", $BuildDir, "--config", $Config, "--parallel", "10")
    Invoke-Step "Install (produces the package)" $cmake @("--install", $BuildDir, "--config", $Config)

    $packagesDir = Join-Path $BuildDir "OUTPUT\Packages"
    if (-not (Test-Path $packagesDir)) {
        throw "No packages produced - expected '$packagesDir' to exist."
    }

    $files = @(Get-ChildItem -Path $packagesDir -File)
    if ($files.Count -eq 0) {
        throw "No packages produced - '$packagesDir' is empty."
    } elseif ($files.Count -eq 1) {
        $dest = Join-Path $root $files[0].Name
        Copy-Item -Path $files[0].FullName -Destination $dest -Force
        Write-Host "Installer dropped at $dest" -ForegroundColor Green
    } else {
        $dest = Join-Path $root "NMPBX-release"
        if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
        Copy-Item -Path $packagesDir -Destination $dest -Recurse -Force
        Write-Host "Package is bundled with $($files.Count) files - copied the whole bundle to $dest" -ForegroundColor Yellow
    }
} finally {
    Pop-Location
    Set-PackagingFlag "OFF"
}
