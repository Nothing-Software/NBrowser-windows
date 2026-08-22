# Incremental build: applies branding assets, then builds only changed targets
# (ninja decides what needs rebuilding).

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

& (Join-Path $PSScriptRoot "apply_branding_assets.ps1")

Set-Location (Join-Path $root "build\src")
ninja -C out\Default chrome
