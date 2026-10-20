# Extracts the MSVC C/C++ headers and CRT libraries the cross-build links against.
#
# These come from the SAME toolset that builds Qt and the codecs. Vendoring a
# CRT older than the compiler that produced the object files is exactly what
# caused the __std_rotate / __std_unique_4 undefined-symbol failures that
# earlier revisions worked around with -D_USE_STD_VECTOR_ALGORITHMS=0.

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'lib\Common.ps1')

$cfg  = Get-ToolchainConfig
$dest = Join-Path $cfg.Stage 'msvc'

if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $dest 'lib') -Force | Out-Null

Write-Step "Copying MSVC $($cfg.MsvcToolset) headers"
Copy-Tree -From (Join-Path $cfg.MsvcDir 'include') -To (Join-Path $dest 'include')

Write-Step "Copying MSVC $($cfg.MsvcToolset) CRT libraries"
$libSrc = Join-Path $cfg.MsvcDir 'lib\x64'
$missing = @()
foreach ($lib in $cfg.MsvcLibs) {
    $path = Join-Path $libSrc $lib
    if (-not (Test-Path $path)) { $missing += $lib; continue }
    # Lowercase on disk; the fetch script adds the other casings as symlinks.
    Copy-Item $path (Join-Path $dest "lib\$($lib.ToLower())") -Force
}
if ($missing) {
    throw ("These libraries are named in versions.psd1 but absent from $libSrc :`n  " +
           ($missing -join "`n  ") + "`n" +
           "MSVC renames CRT libraries between toolsets (libvcmath-mt.lib, for one, is new " +
           "in 14.51). Reconcile MsvcLibs with what this toolset actually ships.")
}

$files = Get-ChildItem (Join-Path $dest 'lib') -File
Write-Host ("  {0} libraries, {1:N0} MB" -f $files.Count, (($files | Measure-Object Length -Sum).Sum / 1MB))
Write-Host ("  headers: {0:N0} MB" -f ((Get-ChildItem (Join-Path $dest 'include') -Recurse -File |
                                        Measure-Object Length -Sum).Sum / 1MB))
