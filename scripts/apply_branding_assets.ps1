# Copies custom branding assets (icons etc.) over build\src.
# Paths are built relative to this script's own location - works from any repo path.

$root = Split-Path $PSScriptRoot -Parent
$src  = Join-Path $root "nbrowser\branding-assets"
$dst  = Join-Path $root "build\src"

if (-not (Test-Path $src)) {
    Write-Error "Assets directory not found: $src"
    exit 1
}
if (-not (Test-Path $dst)) {
    Write-Error "build\src not found - run build_clean.ps1 first."
    exit 1
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

Write-Output "Done: $($files.Count) branding files applied over build\src."
