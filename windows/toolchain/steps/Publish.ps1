# Uploads the packaged bundles to S3 under a tag-prefixed, immutable path.
#
# Artifacts are never overwritten: each bundle tag gets its own prefix, so every
# previously published set stays reachable and rolling back is a matter of
# checking out the older windows-deps.lock. That replaces the old
# copy-to-.prebackup.tar.gz-before-overwriting dance.

[CmdletBinding()]
param(
    [switch] $Force   # re-upload even if the key already exists
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'lib\Common.ps1')

$cfg      = Get-ToolchainConfig
$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$lockPath = Join-Path $repoRoot 'windows-deps.lock'

if (-not (Test-Path $lockPath)) { throw "No windows-deps.lock -- run the package step first." }

$lock = @{}
Get-Content $lockPath | Where-Object { $_ -match '^\s*([A-Z0-9_]+)=(.*)$' } | ForEach-Object {
    if ($_ -match '^\s*([A-Z0-9_]+)=(.*)$') { $lock[$Matches[1]] = $Matches[2] }
}

$prefix = "s3://photo-salon/_build/windows/$($lock.BUNDLE_TAG)"
$files  = @('MSVC', 'SDK', 'QT', 'CODECS') | ForEach-Object { $lock["${_}_FILE"] }
$files += 'BUILDINFO.txt'

# Verify locally before anything leaves the machine: a tarball whose hash no
# longer matches the lock means the lock and the artifact disagree, and the
# build host would reject the download after paying for it.
Write-Step 'Verifying artifacts against windows-deps.lock'
foreach ($key in @('MSVC', 'SDK', 'QT', 'CODECS')) {
    $file = Join-Path $cfg.Out $lock["${key}_FILE"]
    if (-not (Test-Path $file)) { throw "Missing artifact: $file" }
    $actual = Get-Sha256 $file
    if ($actual -ne $lock["${key}_SHA256"]) {
        throw ("$($lock["${key}_FILE"]) hashes $actual but windows-deps.lock says " +
               "$($lock["${key}_SHA256"]). Re-run the package step.")
    }
    Write-Host "  ok  $($lock["${key}_FILE"])"
}

Write-Step "Publishing to $prefix"
foreach ($file in $files) {
    $local = Join-Path $cfg.Out $file
    if (-not (Test-Path $local)) { throw "Missing $local" }
    $key = "$prefix/$file"

    if (-not $Force) {
        Invoke-Native -AllowFailure -Script { aws s3 ls $key } | Out-Null
        if ($LASTEXITCODE -eq 0) {
            throw ("$key already exists. Bundle tags are immutable -- bump BundleTag in " +
                   "versions.psd1 and re-package, or pass -Force if you are certain " +
                   "nothing is consuming this tag.")
        }
        $global:LASTEXITCODE = 0
    }

    Write-Host "  uploading $file"
    Invoke-Native -What "upload $file" -Script { aws s3 cp $local $key } | Out-Null
}

Write-Step 'Published'
Write-Host "  Commit windows-deps.lock so the build host fetches this set."
Invoke-Native -Script { aws s3 ls "$prefix/" } | ForEach-Object { Write-Host $_ }
