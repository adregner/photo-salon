# Tars the staged trees, records what went into them, and writes the lock file
# the build host uses to fetch and verify them.

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'lib\Common.ps1')

$cfg      = Get-ToolchainConfig
$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))

# name        stage directory (archive root)   published file name
$components = @(
    @{ Key = 'MSVC';   Dir = 'msvc';            File = "msvc-$($cfg.MsvcToolset).tar.gz";                  Extract = 'msvc' }
    @{ Key = 'SDK';    Dir = 'sdk';             File = "sdk-$($cfg.WindowsSdk).tar.gz";                    Extract = 'sdk' }
    @{ Key = 'QT';     Dir = $cfg.QtDirName;    File = "qt-$($cfg.QtVersion)-$($cfg.MsvcToolset).tar.gz";  Extract = "$($cfg.QtDirName)/x64" }
    @{ Key = 'CODECS'; Dir = 'codecs';          File = "codecs-$($cfg.MsvcToolset).tar.gz";                Extract = 'codecs/x64' }
)

foreach ($c in $components) {
    $path = Join-Path $cfg.Stage $c.Dir
    if (-not (Test-Path $path)) {
        throw ("Nothing staged at $path -- run the matching step first " +
               "(Make-WindowsToolchain.ps1 -Step $($c.Key.ToLower())).")
    }
}

foreach ($c in $components) {
    Write-Step "Packing $($c.File)"
    $out = Join-Path $cfg.Out $c.File
    New-Tarball -SourceParent $cfg.Stage -SourceName $c.Dir -OutFile $out | Out-Null
    $c.Sha  = Get-Sha256 $out
    $c.Size = (Get-Item $out).Length
    Write-Host ("  {0:N1} MB  {1}" -f ($c.Size / 1MB), $c.Sha)
}

# ── BUILDINFO.txt ────────────────────────────────────────────────────────────
$codecCommitsFile = Join-Path $cfg.Out 'codec-commits.txt'
$codecCommits = if (Test-Path $codecCommitsFile) { Get-Content $codecCommitsFile } else { @('(not built this run)') }
$qtSrcFile = Join-Path $cfg.Out 'qt-sources.sha256'
$qtSources = if (Test-Path $qtSrcFile) { Get-Content $qtSrcFile } else { @('(not built this run)') }

$crtNote = if ($cfg.CrtLinkage -eq 'MultiThreaded') {
    'MultiThreaded (/MT) -- the CRT is linked into the .exe. No Visual C++
Redistributable is needed on the target machine, and nothing ships beside the
binary.'
} else {
    'MultiThreadedDLL (/MD) -- the .exe imports msvcp140.dll and vcruntime140.dll,
so the target machine needs the Visual C++ Redistributable installed.'
}

$buildinfo = @"
photo-salon Windows cross-compilation toolchain bundle
======================================================

Bundle tag : $($cfg.BundleTag)
Built      : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss K')
Host       : $((Get-CimInstance Win32_OperatingSystem).Caption) ($env:COMPUTERNAME)

Every artifact below was produced in a single run of
windows/toolchain/Make-WindowsToolchain.ps1 from ONE MSVC toolset. That is the
point of the bundle: the CRT, the Windows SDK import libraries, Qt and the
image codecs cannot drift apart into the ABI and STL-version mismatches that
mixing toolsets produces.

Toolchain
---------
MSVC toolset   $($cfg.MsvcToolset)  (cl $(& (Join-Path $cfg.MsvcDir 'bin\Hostx64\x64\cl.exe') 2>&1 | Select-String -Pattern 'Version ([\d.]+)' | ForEach-Object { $_.Matches[0].Groups[1].Value }))
Windows SDK    $($cfg.WindowsSdk)
CMake          $(& $cfg.CMake --version | Select-Object -First 1)
Ninja          $(& $cfg.Ninja --version)

CRT linkage
-----------
$crtNote

Qt $($cfg.QtVersion)
-------------
Modules   : $($cfg.QtModules -join ', ')
Configure : $($cfg.QtConfigureArgs -join ' ')

qtimageformats supplies the tiff/webp/tga/icns/wbmp plugins. Because Qt is
static, those are .lib files under plugins/imageformats/ with their *_init
objects plus lib/cmake/Qt6Gui import configs; the cross-link picks them up via
Qt's static-plugin import, so adding a format needs no app code change.

-no-icu is deliberate: left alone, configure finds the SDK's icuuc/icuin import
libraries and the .exe ends up importing icuuc.dll and icuin.dll, which are only
OS components on new enough Windows 10 builds. Without ICU, QCollator uses
CompareStringEx and Windows' own collation.

Source archives (download.qt.io):
$(($qtSources | ForEach-Object { "  $_" }) -join "`n")

Image codecs
------------
$(($codecCommits | ForEach-Object { "  $_" }) -join "`n")

All static, Release, x64, CRT linkage as above.
  libde265  ENABLE_SDL=OFF
  libheif   WITH_LIBDE265=ON, ENABLE_PLUGIN_LOADING=OFF (the HEVC decoder is
            linked in, not dlopen-ed -- a plugin DLL would have to ship beside
            the .exe), every other back-end explicitly OFF, and compiled with
            /DLIBDE265_STATIC_BUILD so libde265's headers do not decorate their
            API __declspec(dllimport).
  openjpeg  BUILD_CODEC=OFF, BUILD_TESTING=OFF

heif.lib and libde265.lib ship separately and BOTH must be linked: libheif
references de265_* but records no /DEFAULTLIB for it. CMakeLists.txt finds and
links libde265 explicitly on Windows. Do not merge them into one archive --
both projects have a bitstream.cc, so the result holds two members named
bitstream.cc.obj.

Consumers of these headers need LIBHEIF_STATIC_BUILD and OPJ_STATIC, which
CMakeLists.txt sets on the plugin targets under if(WIN32). Without them the
headers declare the APIs __declspec(dllimport) and the link fails on __imp_*.

Windows SDK
-----------
Headers: $($cfg.SdkIncludeDirs -join ', ') (winrt/ and cppwinrt/ excluded -- 210 MB
nothing here includes).
Import libraries: ALL of Lib/$($cfg.WindowsSdk)/um/x64, plus $($cfg.UcrtLibs -join ' ') from
ucrt/x64. Taking all of um/x64 retires the old routine of copying one more
import library into the repo every time a link failed with
"lld-link: error: could not open '<name>.lib'".

MSVC runtime
------------
Headers from VC/Tools/MSVC/$($cfg.MsvcToolset)/include, libraries:
$(($cfg.MsvcLibs | ForEach-Object { "  $_" }) -join "`n")

Artifacts
---------
$(($components | ForEach-Object { "{0,-44} {1,9:N1} MB  {2}" -f $_.File, ($_.Size / 1MB), $_.Sha }) -join "`n")
"@

$buildinfoPath = Join-Path $cfg.Out 'BUILDINFO.txt'
Set-Content -Path $buildinfoPath -Value $buildinfo -Encoding utf8
Write-Step "Wrote $buildinfoPath"

# ── windows-deps.lock ────────────────────────────────────────────────────────
# Deliberately KEY=value and nothing else: fetch-windows-deps.sh reads it with
# plain shell, no jq or python on the build host.
$lockLines = @(
    '# Pinned Windows cross-compilation dependencies.'
    '#'
    '# Generated by windows/toolchain/Make-WindowsToolchain.ps1 -- do not hand-edit.'
    '# Regenerate on a Windows machine; see doc/WINDOWS.md.'
    ''
    "BUNDLE_TAG=$($cfg.BundleTag)"
    "BASE_URL=https://photo-salon.s3.amazonaws.com/_build/windows/$($cfg.BundleTag)"
    "MSVC_TOOLSET=$($cfg.MsvcToolset)"
    "WINDOWS_SDK=$($cfg.WindowsSdk)"
    "QT_VERSION=$($cfg.QtVersion)"
    "CRT_LINKAGE=$($cfg.CrtLinkage)"
    ''
)
foreach ($c in $components) {
    $lockLines += "$($c.Key)_FILE=$($c.File)"
    $lockLines += "$($c.Key)_DIR=$($c.Extract)"
    $lockLines += "$($c.Key)_SHA256=$($c.Sha)"
    $lockLines += ''
}
$lockPath = Join-Path $repoRoot 'windows-deps.lock'
Set-Content -Path $lockPath -Value $lockLines -Encoding ascii
Write-Step "Wrote $lockPath"

Write-Host ''
Write-Host 'Next: review the artifacts, then publish with' -ForegroundColor Green
Write-Host '  .\Make-WindowsToolchain.ps1 -Step publish' -ForegroundColor Green
