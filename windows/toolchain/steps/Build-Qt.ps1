# Builds Qt statically, with the static CRT, using the pinned MSVC toolset.
#
# This is the long pole: roughly 1-2 hours and ~12 GB of scratch space. It is
# safe to re-run -- an existing configured build tree is reused unless -Clean.

[CmdletBinding()]
param(
    [switch] $Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'lib\Common.ps1')

$cfg = Get-ToolchainConfig
# Install straight into the layout the bundle ships, so packaging is a plain
# tar of $Stage/<QtDirName> with no shuffling of a multi-gigabyte tree.
$prefix = Join-Path $cfg.Stage "$($cfg.QtDirName)\x64"

if ($Clean) {
    foreach ($d in @($prefix, (Join-Path $cfg.Build 'qtbase'), (Join-Path $cfg.Build 'qtimageformats'))) {
        if (Test-Path $d) { Write-Step "Removing $d"; Remove-Item $d -Recurse -Force }
    }
}

# ── Fetch sources ────────────────────────────────────────────────────────────
$sourceHashes = @{}
foreach ($module in $cfg.QtModules) {
    $name    = "$module-everywhere-src-$($cfg.QtVersion)"
    $archive = Join-Path $cfg.Src "$name.zip"
    $srcDir  = Join-Path $cfg.Src $name

    if (-not (Test-Path $archive)) {
        $branch = ($cfg.QtVersion -split '\.')[0..1] -join '.'
        $url = "https://download.qt.io/archive/qt/$branch/$($cfg.QtVersion)/submodules/$name.zip"
        Write-Step "Downloading $name.zip"
        # Invoke-WebRequest's progress bar makes large downloads crawl in 5.1.
        $oldProgress = $ProgressPreference; $ProgressPreference = 'SilentlyContinue'
        try   { Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing }
        finally { $ProgressPreference = $oldProgress }
    }
    $sourceHashes[$module] = Get-Sha256 $archive

    if (-not (Test-Path $srcDir)) {
        Write-Step "Extracting $name.zip"
        & tar.exe -xf $archive -C $cfg.Src
        if ($LASTEXITCODE -ne 0) { throw "Failed to extract $archive" }
    }
}
$sourceHashes.GetEnumerator() | ForEach-Object { "qt-$($_.Key)-src-sha256 $($_.Value)" } |
    Set-Content (Join-Path $cfg.Out 'qt-sources.sha256') -Encoding ascii

# ── Configure + build qtbase ─────────────────────────────────────────────────
$qtbaseSrc   = Join-Path $cfg.Src   "qtbase-everywhere-src-$($cfg.QtVersion)"
$qtbaseBuild = Join-Path $cfg.Build 'qtbase'
if (-not (Test-Path $qtbaseBuild)) { New-Item -ItemType Directory -Path $qtbaseBuild -Force | Out-Null }

if (-not (Test-Path (Join-Path $qtbaseBuild 'CMakeCache.txt'))) {
    # Not $args — that is an automatic variable.
    $configureArgs = ($cfg.QtConfigureArgs -join ' ')
    Write-Step "Configuring qtbase ($configureArgs)"
    Invoke-VcvarsChecked -Config $cfg -WorkingDirectory $qtbaseBuild `
        -Command "`"$qtbaseSrc\configure.bat`" -prefix `"$prefix`" $configureArgs" `
        -LogFile (Join-Path $cfg.Logs 'qtbase-configure.log') -What 'qtbase configure'
}

Write-Step 'Building qtbase (this is the slow part)'
Invoke-VcvarsChecked -Config $cfg -WorkingDirectory $qtbaseBuild `
    -Command 'cmake --build . --parallel' `
    -LogFile (Join-Path $cfg.Logs 'qtbase-build.log') -What 'qtbase build'

Write-Step 'Installing qtbase'
Invoke-VcvarsChecked -Config $cfg -WorkingDirectory $qtbaseBuild `
    -Command 'cmake --install .' `
    -LogFile (Join-Path $cfg.Logs 'qtbase-install.log') -What 'qtbase install'

# ── Configure + build the extra modules against that prefix ──────────────────
#
# qt-configure-module.bat drives the module's build with the just-installed
# Qt's own toolchain file. That file pins the compiler qtbase was built with
# (QT_USE_ORIGINAL_COMPILER), so running under the same vcvars_ver keeps the
# STL headers on INCLUDE in step with it -- mismatched, MSVC raises
# "error STL1001: Unexpected compiler version".
foreach ($module in $cfg.QtModules | Where-Object { $_ -ne 'qtbase' }) {
    $modSrc   = Join-Path $cfg.Src   "$module-everywhere-src-$($cfg.QtVersion)"
    $modBuild = Join-Path $cfg.Build $module
    if (-not (Test-Path $modBuild)) { New-Item -ItemType Directory -Path $modBuild -Force | Out-Null }

    if (-not (Test-Path (Join-Path $modBuild 'CMakeCache.txt'))) {
        Write-Step "Configuring $module"
        Invoke-VcvarsChecked -Config $cfg -WorkingDirectory $modBuild `
            -Command "`"$prefix\bin\qt-configure-module.bat`" `"$modSrc`" -- -DCMAKE_BUILD_TYPE=Release" `
            -LogFile (Join-Path $cfg.Logs "$module-configure.log") -What "$module configure"
    }

    Write-Step "Building $module"
    Invoke-VcvarsChecked -Config $cfg -WorkingDirectory $modBuild `
        -Command 'cmake --build . --parallel' `
        -LogFile (Join-Path $cfg.Logs "$module-build.log") -What "$module build"

    Write-Step "Installing $module"
    Invoke-VcvarsChecked -Config $cfg -WorkingDirectory $modBuild `
        -Command 'cmake --install .' `
        -LogFile (Join-Path $cfg.Logs "$module-install.log") -What "$module install"
}

# ── Sanity checks ────────────────────────────────────────────────────────────
# A Qt that silently came out shared, or without the image-format plugins the
# app relies on, must not reach a bundle.
Write-Step 'Verifying the install'

$dlls = Get-ChildItem (Join-Path $prefix 'bin') -Filter 'Qt6*.dll' -ErrorAction SilentlyContinue
if ($dlls) { throw "Qt built as shared libraries: found $($dlls.Count) Qt6*.dll in bin/. Expected a static build." }

$expectedPlugins = @('qjpeg', 'qgif', 'qico', 'qtiff', 'qwebp')
$plugins = Get-ChildItem (Join-Path $prefix 'plugins\imageformats') -Filter '*.lib' -ErrorAction SilentlyContinue |
           ForEach-Object { $_.BaseName }
$missing = $expectedPlugins | Where-Object { $plugins -notcontains $_ }
if ($missing) {
    throw ("Image-format plugins missing from the Qt install: $($missing -join ', ').`n" +
           "Present: $($plugins -join ', ').`n" +
           "qtiff/qwebp come from qtimageformats -- check that module's build log.")
}

# Every object in a /MT build must ask for libcmt, not msvcrt. One stray /MD
# object produces duplicate-symbol or heap-mismatch failures much later, in the
# app's link, where the cause is far from obvious.
if ($cfg.CrtLinkage -eq 'MultiThreaded') {
    $dumpbin = Join-Path $cfg.MsvcDir 'bin\Hostx64\x64\dumpbin.exe'
    $core    = Join-Path $prefix 'lib\Qt6Core.lib'
    $directives = Invoke-Native -What 'dumpbin /directives' -Script { & $dumpbin /directives $core } |
                  Select-String -Pattern '/DEFAULTLIB:\S+' -AllMatches |
                  ForEach-Object { $_.Matches.Value } | Sort-Object -Unique
    $bad = $directives | Where-Object { $_ -match '(?i)/DEFAULTLIB:"?msvcrt' }
    if ($bad) {
        throw ("Qt6Core.lib requests the dynamic CRT ($($bad -join ', ')) despite " +
               "-static-runtime. The .exe would need the VC++ Redistributable.")
    }
    Write-Host "  Qt6Core.lib CRT directives: $(($directives | Where-Object { $_ -match '(?i)cmt|ucrt|vcruntime' }) -join ' ')"
}

Write-Step "Qt static install ready at $prefix"
$size = (Get-ChildItem $prefix -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("  {0:N0} MB, {1} libraries" -f $size, (Get-ChildItem (Join-Path $prefix 'lib') -Filter *.lib).Count)
