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
 
# --- 3. Sweep all .grd/.grdp/.xtb strings for "Chromium" ---
$stringRoots = @("chrome", "components", "extensions", "ui", "content")
$stringSuffixes = @(".grd", ".grdp", ".xtb")
$replacedCount = 0
 
foreach ($rootName in $stringRoots) {
    $rootPath = Join-Path $dst $rootName
    if (-not (Test-Path $rootPath)) { continue }
 
    $targetFiles = Get-ChildItem -Path $rootPath -Recurse -File | Where-Object {
        $stringSuffixes -contains $_.Extension
    }
 
    foreach ($file in $targetFiles) {
        $text = $null
        try {
            $text = [System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::UTF8)
        } catch {
            continue
        }
        if ($text -notmatch "Chromium" -and $text -notmatch "ungoogled-chromium") {
            continue
        }
 
        $newText = $text
        # Order matters: most specific first (case-sensitive, like the
        # original Python: 'Chromium' and 'chromium' never collide).
        $newText = $newText -creplace "The Chromium Authors", $CompanyName
        $newText = $newText -creplace "Chromium", $BrandName
        $newText = $newText -creplace "ungoogled-chromium", "$BrandName by $CompanyName"
 
        if ($newText -ne $text) {
            [System.IO.File]::WriteAllText($file.FullName, $newText, [System.Text.UTF8Encoding]::new($false))
            $replacedCount++
        }
    }
}
 
Write-Output "Renamed product in $replacedCount string files (.grd/.grdp/.xtb)."
Write-Output "Branding fully applied over build\src."