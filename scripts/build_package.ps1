# Package build + applies branding assets

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
& (Join-Path $PSScriptRoot "apply_branding_assets.ps1")
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"
git checkout -- third_party/win_build_output 2>$null
python3 package.py
if ($LASTEXITCODE -ne 0) {
    Write-Error "build.py failed (exit code $LASTEXITCODE). Skipping icon overlay."
    exit $LASTEXITCODE
}
