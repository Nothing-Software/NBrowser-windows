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
 
Write-Output "== Done: clean build =="
Write-Output "Run build_installer.ps1 next if you need mini_installer.exe / the packaged installer."