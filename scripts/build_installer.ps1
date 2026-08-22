# Installer build: applies branding assets, then builds mini_installer.

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

& (Join-Path $PSScriptRoot "apply_branding_assets.ps1")

Set-Location (Join-Path $root "build\src")
ninja -C out\Default mini_installer
