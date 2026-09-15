<#
.SYNOPSIS
    Regenerates every Windows artifact the photo-salon cross-build links against.

.DESCRIPTION
    photo-salon.exe is cross-compiled on macOS or Linux with clang-cl, but the
    things it links -- the MSVC CRT, the Windows SDK import libraries, a static
    Qt, and static image codecs -- can only come from a Windows machine. This
    script produces all of them in one run from one MSVC toolset and publishes
    them to S3.

    One toolset for everything is the whole design. Earlier bundles were
    gathered piecemeal: Qt from MSVC 14.44, the codecs from 14.51, the CRT
    import libraries from something older still. The result was undefined
    __std_rotate / __std_unique_4 symbols at cross-link, worked around by
    compiling the codecs with -D_USE_STD_VECTOR_ALGORITHMS=0. Building
    everything together removes the cause instead of the symptom.

    Inputs are pinned in versions.psd1. Outputs land in C:\pst\out and are
    described by windows-deps.lock, which is committed to the repo and tells the
    build host what to fetch and what each artifact must hash to.

.PARAMETER Step
    Which stage to run. Defaults to 'all'.

      msvc     MSVC headers + CRT libraries from the pinned toolset   (~1 min)
      sdk      Windows SDK headers + all x64 import libraries         (~3 min)
      qt       Qt built static, with the static CRT                   (1-2 hours)
      codecs   libde265, libheif, OpenJPEG built static               (~10 min)
      package  tar everything, write BUILDINFO.txt + windows-deps.lock
      publish  upload to S3 (asks for nothing -- review first)
      all      everything except publish

.PARAMETER Clean
    Discard existing build trees for the qt and codecs steps instead of
    resuming them.

.EXAMPLE
    .\Make-WindowsToolchain.ps1
    Full regeneration, stopping short of publishing.

.EXAMPLE
    .\Make-WindowsToolchain.ps1 -Step codecs
    Rebuild just the image codecs, then re-run -Step package.

.NOTES
    Prerequisites on the Windows machine:
      * Visual Studio with the MSVC toolset named in versions.psd1
      * the Windows SDK version named in versions.psd1
      * Qt's CMake and Ninja (C:\Qt\Tools) -- the Qt online installer's
        "CMake" and "Ninja" components, no Qt libraries required
      * git, and the AWS CLI authenticated for the publish step
      * ~25 GB free on C:
#>

[CmdletBinding()]
param(
    [ValidateSet('all', 'msvc', 'sdk', 'qt', 'codecs', 'package', 'publish')]
    [string] $Step = 'all',
    [switch] $Clean,
    [switch] $Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\Common.ps1')

$cfg = Get-ToolchainConfig
Write-Host ''
Write-Host "photo-salon Windows toolchain -- bundle tag $($cfg.BundleTag)" -ForegroundColor Green
Write-Host "  MSVC toolset  $($cfg.MsvcToolset)"
Write-Host "  Windows SDK   $($cfg.WindowsSdk)"
Write-Host "  Qt            $($cfg.QtVersion) ($($cfg.QtModules -join ', '))"
Write-Host "  CRT linkage   $($cfg.CrtLinkage)$(if ($cfg.CrtLinkage -eq 'MultiThreaded') { ' (/MT -- standalone .exe)' } else { ' (/MD -- needs the VC++ Redistributable)' })"
Write-Host "  Work root     $($cfg.Work)"
Write-Host ''

$started = Get-Date
$steps = if ($Step -eq 'all') { @('msvc', 'sdk', 'qt', 'codecs', 'package') } else { @($Step) }

foreach ($s in $steps) {
    $stepStart = Get-Date
    Write-Host ''
    Write-Host ('=' * 72) -ForegroundColor DarkGray
    Write-Host "STEP: $s" -ForegroundColor Green
    Write-Host ('=' * 72) -ForegroundColor DarkGray

    switch ($s) {
        'msvc'    { & (Join-Path $PSScriptRoot 'steps\Export-Msvc.ps1') }
        'sdk'     { & (Join-Path $PSScriptRoot 'steps\Export-Sdk.ps1') }
        'qt'      { & (Join-Path $PSScriptRoot 'steps\Build-Qt.ps1')     -Clean:$Clean }
        'codecs'  { & (Join-Path $PSScriptRoot 'steps\Build-Codecs.ps1') -Clean:$Clean }
        'package' { & (Join-Path $PSScriptRoot 'steps\Package.ps1') }
        'publish' { & (Join-Path $PSScriptRoot 'steps\Publish.ps1')      -Force:$Force }
    }
    Write-Host ("  [$s done in {0:hh\:mm\:ss}]" -f ((Get-Date) - $stepStart)) -ForegroundColor DarkGray
}

Write-Host ''
Write-Host ("All steps completed in {0:hh\:mm\:ss}" -f ((Get-Date) - $started)) -ForegroundColor Green
if ($steps -contains 'package') {
    Write-Host ''
    Write-Host 'Review, then publish and commit:' -ForegroundColor Green
    Write-Host "  Get-Content $(Join-Path $cfg.Out 'BUILDINFO.txt')"
    Write-Host '  .\Make-WindowsToolchain.ps1 -Step publish'
    Write-Host '  git add windows-deps.lock && git commit'
}
