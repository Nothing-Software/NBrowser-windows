# Copies overlay files (new/fully-rewritten source files) over build\src.
# Mirrors apply_branding_assets.ps1's copy logic, without the BRANDING-file
# rewrite or the .grd/.xtb string sweep - overlay content is already final
# NBrowser text, and running that sweep over it after the fact would mangle
# any literal "Chromium" substring it contains.
#
# Run this AFTER apply_branding_assets.ps1, not before - see nbrowser\overlay
# usage notes in the first-run page implementation plan for why the order
# matters.

$root = Split-Path $PSScriptRoot -Parent
$src  = Join-Path $root "nbrowser\overlay"
$dst  = Join-Path $root "build\src"

if (-not (Test-Path $dst)) {
    Write-Error "build\src not found - run build_clean.ps1 first."
    exit 1
}

if (-not (Test-Path $src)) {
    Write-Output "No overlay directory found at $src - skipping."
    exit 0
}

$files = Get-ChildItem -Path $src -Recurse -File
foreach ($file in $files) {
    $relativePath = $file.FullName.Substring($src.Length + 1)
    $destPath = Join-Path $dst $relativePath
    $destDir = Split-Path $destPath -Parent
    if (-not (Test-Path $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }
    Copy-Item $file.FullName -Destination $destPath -Force
    Write-Output "Copied: $relativePath"
}
Write-Output "Done: $($files.Count) overlay files applied."
