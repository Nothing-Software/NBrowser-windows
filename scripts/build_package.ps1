# Package build + applies branding assets

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
& (Join-Path $PSScriptRoot "apply_branding_assets.ps1")
& (Join-Path $PSScriptRoot "apply_overlay.ps1")

Write-Output "== Build mini_installer =="
Set-Location (Join-Path $root "build\src")
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"
git checkout -- third_party/win_build_output 2>$null
.\third_party\ninja\ninja.exe -C out\Default mini_installer

Write-Output "== Packaging via package.py =="
Set-Location $root
python3 package.py
if ($LASTEXITCODE -ne 0) {
    Write-Error "build.py failed (exit code $LASTEXITCODE). Skipping icon overlay."
    exit $LASTEXITCODE
}

Write-Output "== Done: package builded =="