# Copies custom branding assets (icons etc.) over build\src.
# Paths are built relative to this script's own location - works from any repo path.
##############################################################
# !!A some code implementation -> Credit to Aerium Browser!! #
# https://github.com/aerium-browser/aerium-browser-windows   #
##############################################################

$BrandName   = "NBrowser"
$CompanyName = "Nothing Software"

$root = Split-Path $PSScriptRoot -Parent
$src  = Join-Path $root "nbrowser\branding-assets"
$dst  = Join-Path $root "build\src"

if (-not (Test-Path $dst)) {
    Write-Error "build\src not found - run build_clean.ps1 first."
    exit 1
}

# --- 1. Copy binary assets (icons, logos) ---
if (Test-Path $src) {
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
    Write-Output "Done: $($files.Count) branding asset files applied."
} else {
    Write-Output "No branding-assets directory found at $src - skipping binary asset copy."
}

# --- 2. Rewrite BRANDING file (product/company/copyright lines) ---
$brandingFile = Join-Path $dst "chrome\app\theme\chromium\BRANDING"
if (Test-Path $brandingFile) {
    $lines = Get-Content -Path $brandingFile -Encoding UTF8
    $newLines = foreach ($line in $lines) {
        if ($line.StartsWith("PRODUCT_")) {
            $line -replace "Chromium", $BrandName
        } elseif ($line.StartsWith("COMPANY_FULLNAME=") -or $line.StartsWith("COMPANY_SHORTNAME=")) {
            ($line -split "=", 2)[0] + "=" + $CompanyName
        } elseif ($line.StartsWith("COPYRIGHT=")) {
            "COPYRIGHT=Copyright @LASTCHANGE_YEAR@ $CompanyName. All rights reserved."
        } else {
            $line
        }
    }
    [System.IO.File]::WriteAllText($brandingFile, ($newLines -join "`n") + "`n", [System.Text.UTF8Encoding]::new($false))
    Write-Output "Rewrote BRANDING file."
} else {
    Write-Output "BRANDING file not found at $brandingFile - skipping."
}
 
# --- 3. Sweep all .grd/.grdp/.xtb strings for "Chromium", keeping .xtb
#        translations linked to their message (see fix_branding_translations.py
#        for why this can't be a blind text substitution). ---
$fixScript = Join-Path $PSScriptRoot "fix_branding_translations.py"
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { $python = Get-Command python3 -ErrorAction SilentlyContinue }
if (-not $python) {
    Write-Error "python not found on PATH - required to run fix_branding_translations.py."
    exit 1
}
& $python.Source $fixScript $dst
if ($LASTEXITCODE -ne 0) {
    Write-Error "fix_branding_translations.py failed with exit code $LASTEXITCODE."
    exit 1
}

Write-Output "Branding fully applied over build\src."