# Incremental build: applies branding assets + overlay, then builds only changed targets

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
& (Join-Path $PSScriptRoot "apply_branding_assets.ps1")
& (Join-Path $PSScriptRoot "apply_overlay.ps1")
Set-Location (Join-Path $root "build\src")
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"
git checkout -- third_party/win_build_output 2>$null
.\third_party\ninja\ninja.exe -C out\Default chrome

Write-Output "== Done: incremental builded =="
