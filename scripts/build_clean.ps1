# Full clean rebuild: wipes build\src, re-downloads/patches/builds from scratch,
# then applies branding assets (icons) over the fresh tree.
# ~2-3+ hours. build\downloads_cache is NOT touched (avoids re-downloading).

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

Write-Output "== Cleaning build\src =="
Remove-Item -Recurse -Force (Join-Path $root "build\src") -ErrorAction SilentlyContinue
Remove-Item -Force (Join-Path $root "build\domsubcache.tar.gz") -ErrorAction SilentlyContinue

Write-Output "== Running build.py (clone + patches + build) =="
python3 build.py
if ($LASTEXITCODE -ne 0) {
    Write-Error "build.py failed (exit code $LASTEXITCODE). Skipping icon overlay."
    exit $LASTEXITCODE
}

Write-Output "== Applying branding assets =="
& (Join-Path $PSScriptRoot "apply_branding_assets.ps1")

Write-Output "== Done: clean build + branding assets applied =="
