# Extracts the Windows SDK headers and x64 import libraries.
#
# Every um/x64 import library is taken, not a hand-picked subset. The old
# curated list grew one entry at a time, each time a link failed with
# "lld-link: error: could not open '<name>.lib'", and it carried no record of
# which SDK the files came from. Import libraries hold no code -- only the
# symbol-to-DLL mapping -- so taking all of them costs archive size and removes
# a whole class of maintenance.

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'lib\Common.ps1')

$cfg  = Get-ToolchainConfig
$dest = Join-Path $cfg.Stage 'sdk'
$ver  = $cfg.WindowsSdk

if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }

# ── Headers ──────────────────────────────────────────────────────────────────
# winrt/ and cppwinrt/ are skipped: 210 MB that nothing here includes, and
# cppwinrt's headers do not compile cleanly under clang-cl anyway.
foreach ($dir in $cfg.SdkIncludeDirs) {
    $from = Join-Path $cfg.SdkRoot "Include\$ver\$dir"
    if (-not (Test-Path $from)) { throw "SDK include directory not found: $from" }
    Write-Step "Copying SDK $ver headers: $dir"
    Copy-Tree -From $from -To (Join-Path $dest "include\$dir")
}

# ── Import libraries ─────────────────────────────────────────────────────────
# Names keep their on-disk SDK casing (the SDK itself is inconsistent --
# icuin.Lib next to icuuc.lib). fetch-windows-deps.sh adds lowercase and
# uppercase aliases on the case-sensitive build host, because the /DEFAULTLIB
# directives MSVC embeds in object files use whatever casing the source wrote.
$umFrom = Join-Path $cfg.SdkRoot "Lib\$ver\um\x64"
$umTo   = Join-Path $dest 'lib\um'
New-Item -ItemType Directory -Path $umTo -Force | Out-Null
Write-Step "Copying SDK $ver um/x64 import libraries"
$umLibs = Get-ChildItem $umFrom -File -Filter '*.lib'
foreach ($f in $umLibs) { Copy-Item $f.FullName (Join-Path $umTo $f.Name) -Force }

$ucrtFrom = Join-Path $cfg.SdkRoot "Lib\$ver\ucrt\x64"
$ucrtTo   = Join-Path $dest 'lib\ucrt'
New-Item -ItemType Directory -Path $ucrtTo -Force | Out-Null
Write-Step "Copying SDK $ver ucrt/x64 libraries"
$missing = @()
foreach ($lib in $cfg.UcrtLibs) {
    $path = Join-Path $ucrtFrom $lib
    if (-not (Test-Path $path)) { $missing += $lib; continue }
    Copy-Item $path (Join-Path $ucrtTo $lib.ToLower()) -Force
}
if ($missing) { throw "UCRT libraries missing from $ucrtFrom : $($missing -join ', ')" }

# A case-insensitive filesystem cannot hold two libs whose names differ only in
# case, but a case-sensitive one can -- and the fetch script's aliasing would
# then have two candidates for the same directive. Catch it here, not there.
$collisions = $umLibs | Group-Object { $_.Name.ToLower() } | Where-Object { $_.Count -gt 1 }
if ($collisions) { throw "Case-colliding import libs in um/x64: $($collisions.Name -join ', ')" }

Write-Host ("  um:   {0} libs, {1:N0} MB" -f $umLibs.Count,
            (($umLibs | Measure-Object Length -Sum).Sum / 1MB))
Write-Host ("  ucrt: {0} libs, {1:N0} MB" -f $cfg.UcrtLibs.Count,
            ((Get-ChildItem $ucrtTo -File | Measure-Object Length -Sum).Sum / 1MB))
Write-Host ("  headers: {0:N0} MB" -f ((Get-ChildItem (Join-Path $dest 'include') -Recurse -File |
                                        Measure-Object Length -Sum).Sum / 1MB))
