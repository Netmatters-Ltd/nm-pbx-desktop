# Fixes the "You have uncommitted changes" error from gclient sync during cmake configure.
#
# Cause: Git for Windows has core.autocrlf=true in its system config, so these
# gclient-managed checkouts get CRLF on disk while the committed content is LF.
# depot_tools uses its own git, which doesn't see that setting, compares raw bytes,
# and refuses to sync what it thinks is a dirty tree.
#
# This sets core.autocrlf=false locally on each checkout and forces a fresh
# checkout so the files land as LF. It only touches third-party code that gclient
# manages, never the linphone-desktop repository itself.
#
# Don't run this while a build or cmake configure is in progress.
#
# Usage, from anywhere:
#   powershell -ExecutionPolicy Bypass -File <repo>\nm-pbx-docs\fix-gclient-eol.ps1

$ErrorActionPreference = "Stop"

$root = Join-Path (Split-Path -Parent $PSScriptRoot) "external\google"

if (-not (Test-Path $root)) {
    Write-Host "Not found: $root" -ForegroundColor Red
    Write-Host "Expected this script to live in nm-pbx-docs inside the repository."
    exit 1
}

# Discover every checkout under external\google, so this keeps working if a future
# gclient sync pulls in new dependencies. depot_tools manages its own updates, so
# it is left alone.
$repos = Get-ChildItem -Path $root -Recurse -Force -Directory -Filter ".git" |
    ForEach-Object { $_.Parent.FullName } |
    Where-Object { $_ -notlike "*chromium-depot-tools*" } |
    Sort-Object

if (-not $repos) {
    Write-Host "No checkouts found under $root" -ForegroundColor Yellow
    Write-Host "Run the cmake configure step first, so gclient fetches them."
    exit 1
}

Write-Host "Found $($repos.Count) checkout(s)`n"

# Safety check: stop if anything has edits worth keeping.
#
# Forcing core.autocrlf=true makes git normalise CRLF in the working tree back to
# LF before comparing against the committed content, so differences that are purely
# line endings disappear. Whatever is still listed is a real content change.
$suspect = @{}
foreach ($repo in $repos) {
    $genuine = git -C $repo -c core.autocrlf=true diff --name-only HEAD
    if ($genuine) { $suspect[$repo] = $genuine }
}

if ($suspect.Count -gt 0) {
    Write-Host "Real content changes found, not just line endings:" -ForegroundColor Yellow
    foreach ($repo in $suspect.Keys) {
        Write-Host "  $repo"
        $suspect[$repo] | ForEach-Object { Write-Host "      $_" }
    }
    Write-Host "`nNothing has been changed. Review these, then re-run."
    exit 1
}

foreach ($repo in $repos) {
    Write-Host "Fixing $repo"
    git -C $repo config core.autocrlf false
    # Clears git's cached file stats, otherwise the reset below decides the files
    # are already correct and leaves them as CRLF.
    if (git -C $repo ls-files) {
        git -C $repo rm --cached -r . -q
    }
    git -C $repo reset --hard HEAD -q
}

Write-Host "`nVerifying, using the same check gclient runs`n"

$failed = @()
foreach ($repo in $repos) {
    $dirty = git -C $repo -c core.autocrlf=false status --porcelain --untracked-files=no --ignore-submodules
    if ($dirty) {
        $failed += $repo
        Write-Host ("  DIRTY  " + $repo) -ForegroundColor Red
    } else {
        Write-Host ("  clean  " + $repo) -ForegroundColor Green
    }
}

Write-Host ""
if ($failed) {
    Write-Host "$($failed.Count) checkout(s) still dirty. Inspect those by hand." -ForegroundColor Red
    exit 1
}

Write-Host "All clean. Re-run your cmake configure step." -ForegroundColor Green
