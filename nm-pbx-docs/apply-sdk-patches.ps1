# Applies our local patches to the linphone-sdk submodules.
#
# The SDK is a git submodule and its components are nested submodules, so our edits
# live in working trees that nothing tracks. They are lost by a fresh clone, by
# `git submodule update --force`, and by any SDK version bump. The patches are kept
# in nm-pbx-docs\sdk-patches so they can be put back.
#
# Safe to run more than once. A patch that is already applied is left alone.
#
# Run this after any `git submodule update`, and after every SDK version bump.
#
# Usage, from anywhere:
#   powershell -ExecutionPolicy Bypass -File <repo>\nm-pbx-docs\apply-sdk-patches.ps1

$ErrorActionPreference = "Stop"

# Windows PowerShell wraps a native command's stderr in error records, which under
# ErrorActionPreference=Stop would abort the script on git's ordinary "does not apply"
# diagnostics. Run git through here instead: it swallows both streams and reports only
# the exit code, which is the only thing we care about.
function Invoke-Git {
    $old = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & git @args 2>&1 | Out-Null
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $old
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$patchDir = Join-Path $PSScriptRoot "sdk-patches"

# Each patch, and the submodule working tree it applies to.
$patches = @(
    @{ Submodule = "external\linphone-sdk\mediastreamer2"; Patch = "mediastreamer2-msvc-libm.patch" }
    @{ Submodule = "external\linphone-sdk\liblinphone";    Patch = "liblinphone-carddav-auth-username.patch" }
    @{ Submodule = "external\linphone-sdk\liblinphone";    Patch = "liblinphone-log-collection.patch" }
    @{ Submodule = "external\linphone-sdk\mediastreamer2"; Patch = "mediastreamer2-preprocess-timing.patch" }
    @{ Submodule = "external\linphone-sdk\mswasapi";       Patch = "mswasapi-activate-timing.patch" }
)

if (-not (Test-Path $patchDir)) {
    Write-Host "Not found: $patchDir" -ForegroundColor Red
    Write-Host "Expected this script to live in nm-pbx-docs inside the repository."
    exit 1
}

$applied = 0
$already = 0
$failed = @()

foreach ($entry in $patches) {
    $target = Join-Path $repoRoot $entry.Submodule
    $patch = Join-Path $patchDir $entry.Patch

    Write-Host "$($entry.Patch)"

    if (-not (Test-Path $patch)) {
        Write-Host "  MISSING  patch file not found at $patch" -ForegroundColor Red
        $failed += $entry.Patch
        continue
    }
    if (-not (Test-Path $target)) {
        Write-Host "  MISSING  submodule not checked out at $target" -ForegroundColor Red
        Write-Host "           run: git submodule update --init --recursive"
        $failed += $entry.Patch
        continue
    }

    # --reverse --check succeeds only when the change is already present.
    if ((Invoke-Git -C $target apply --reverse --check $patch) -eq 0) {
        Write-Host "  already applied" -ForegroundColor DarkGray
        $already++
        continue
    }

    # Confirm it would apply cleanly before touching anything.
    if ((Invoke-Git -C $target apply --check $patch) -ne 0) {
        Write-Host "  FAILED   does not apply cleanly" -ForegroundColor Red
        Write-Host "           the SDK has probably moved. Fix it by hand in the submodule, then"
        Write-Host "           regenerate the patch. See nm-pbx-docs\sdk-patches\README.md."
        $failed += $entry.Patch
        continue
    }

    if ((Invoke-Git -C $target apply $patch) -eq 0) {
        Write-Host "  applied" -ForegroundColor Green
        $applied++
    } else {
        Write-Host "  FAILED   apply reported an error" -ForegroundColor Red
        $failed += $entry.Patch
    }
}

Write-Host ""
Write-Host "$applied applied, $already already in place, $($failed.Count) failed"

if ($failed) {
    Write-Host ""
    Write-Host "Do not build until these are resolved:" -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  $_" }
    exit 1
}

Write-Host "All SDK patches are in place." -ForegroundColor Green
