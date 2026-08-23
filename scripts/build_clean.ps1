# Full clean rebuild: wipes build\src, re-downloads/patches/builds from scratch
# via build.py, then delegates to build_incremental.ps1 (branding + relink) so
# the final chrome.exe actually carries NBrowser branding.
#
# Note: build.py's own internal ninja pass compiles a THROWAWAY vanilla-
# Chromium-branded chrome.exe first (it has no hook to stop before building).
# That pass cannot be skipped without forking build.py itself, which we
# avoid to keep upstream merges conflict-free. The delegated incremental
# pass below fixes the final binary; it's fast relative to the full compile
# since only branding-affected objects change.
#
# ~2-3+ hours for the full build.py pass. build\downloads_cache is NOT
# touched (avoids re-downloading).

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

Write-Output "== Applying branding and relinking (delegating to build_incremental.ps1) =="
& (Join-Path $PSScriptRoot "build_incremental.ps1")
 
Write-Output "== Done: clean build + branding applied + relinked =="
Write-Output "Run build_installer.ps1 next if you need mini_installer.exe / the packaged installer."