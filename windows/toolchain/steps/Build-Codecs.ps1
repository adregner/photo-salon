# Builds the HEIC and JPEG 2000 decoders: libde265, libheif, OpenJPEG.
#
# Static, Release, x64, and with the same CRT linkage as Qt. Mixing /MT and /MD
# in one link produces duplicate-symbol and heap-mismatch failures, so the
# linkage comes from versions.psd1 rather than being written here.
#
# Takes about 10 minutes.

[CmdletBinding()]
param(
    [switch] $Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'lib\Common.ps1')

$cfg     = Get-ToolchainConfig
$prefix  = Join-Path $cfg.Stage 'codec-install'   # shared staging prefix
$dest    = Join-Path $cfg.Stage 'codecs'          # the bundle tree
$commits = @{}

if ($Clean) {
    foreach ($d in @($prefix, $dest)) { if (Test-Path $d) { Remove-Item $d -Recurse -Force } }
    foreach ($c in $cfg.Codecs) {
        $b = Join-Path $cfg.Build $c.Name
        if (Test-Path $b) { Remove-Item $b -Recurse -Force }
    }
}

$common = @(
    '-G Ninja'
    '-DCMAKE_BUILD_TYPE=Release'
    '-DBUILD_SHARED_LIBS=OFF'
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=$($cfg.CrtLinkage)"
    # CMAKE_MSVC_RUNTIME_LIBRARY only takes effect under policy CMP0091 NEW, and
    # a project that declares an old cmake_minimum_required gets CMP0091 OLD --
    # where the CRT flag comes from CMAKE_<LANG>_FLAGS_RELEASE and defaults to
    # /MD. libde265 does exactly that, and came out linked against msvcprt/MSVCRT
    # while libheif next to it was correctly LIBCMT. Forcing the policy is what
    # makes the setting above mean anything for these older projects.
    '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW'
    "-DCMAKE_INSTALL_PREFIX=`"$prefix`""
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"   # these projects predate CMake 4's floor
) -join ' '

# libde265 must be built and installed first: libheif compiles it in as its
# HEVC back-end rather than dlopen-ing a plugin DLL at runtime.
$perCodec = @{
    'libde265' = '-DENABLE_SDL=OFF'
    'libheif'  = (@(
        "-DCMAKE_PREFIX_PATH=`"$prefix`""
        # libde265's headers declare its API __declspec(dllimport) unless told
        # the build is static; without this libheif's link looks for __imp_de265_*.
        '-DCMAKE_C_FLAGS=/DLIBDE265_STATIC_BUILD'
        '-DCMAKE_CXX_FLAGS=/DLIBDE265_STATIC_BUILD'
        '-DWITH_LIBDE265=ON'
        # Linking the decoder in rather than loading it: a plugin DLL would have
        # to ship beside the .exe, which defeats the point of this whole setup.
        '-DENABLE_PLUGIN_LOADING=OFF'
        '-DWITH_EXAMPLES=OFF'
        '-DBUILD_TESTING=OFF'
        '-DWITH_GDK_PIXBUF=OFF'
        # Only HEIC decoding is wanted. Every other back-end off, explicitly, so
        # the result does not depend on what happens to be installed on the box.
        '-DWITH_AOM_DECODER=OFF'
        '-DWITH_AOM_ENCODER=OFF'
        '-DWITH_DAV1D=OFF'
        '-DWITH_FFMPEG_DECODER=OFF'
        '-DWITH_JPEG_DECODER=OFF'
        '-DWITH_JPEG_ENCODER=OFF'
        '-DWITH_KVAZAAR=OFF'
        '-DWITH_OpenJPEG_DECODER=OFF'
        '-DWITH_OpenJPEG_ENCODER=OFF'
        '-DWITH_OPENJPH_ENCODER=OFF'
        '-DWITH_RAV1E=OFF'
        '-DWITH_SvtEnc=OFF'
        '-DWITH_UNCOMPRESSED_CODEC=OFF'
        '-DWITH_VVDEC=OFF'
        '-DWITH_VVENC=OFF'
        '-DWITH_X265=OFF'
    ) -join ' ')
    'openjpeg'  = '-DBUILD_CODEC=OFF -DBUILD_TESTING=OFF'
}

foreach ($codec in $cfg.Codecs) {
    $src = Get-GitSource -Config $cfg -Codec $codec
    $commits[$codec.Name] = $src.Commit

    $build = Join-Path $cfg.Build $codec.Name
    $flags = $perCodec[$codec.Name]

    Write-Step "Configuring $($codec.Name) $($codec.Tag)"
    Invoke-VcvarsChecked -Config $cfg -WorkingDirectory $cfg.Build `
        -Command "cmake -S `"$($src.Dir)`" -B `"$build`" $common $flags" `
        -LogFile (Join-Path $cfg.Logs "$($codec.Name)-configure.log") -What "$($codec.Name) configure"

    Write-Step "Building and installing $($codec.Name)"
    Invoke-VcvarsChecked -Config $cfg -WorkingDirectory $cfg.Build `
        -Command "cmake --build `"$build`" --target install" `
        -LogFile (Join-Path $cfg.Logs "$($codec.Name)-build.log") -What "$($codec.Name) build"
}

# libheif's configure summary is the only reliable statement that HEVC decoding
# actually got compiled in. A missing back-end otherwise shows up as a runtime
# "no decoder" error long after this script has claimed success.
#
# The wording has changed between libheif releases -- 1.23 prints a summary
# table row ("libde265 HEVC decoder : + built-in"), older versions printed
# "Compiling 'libde265' as built-in backend". Accept either, and fail on
# neither, rather than silently skipping the check when upstream rewords it.
$heifLog = Get-Content (Join-Path $cfg.Logs 'libheif-configure.log') -Raw
$builtIn = ($heifLog -match "(?im)^\s*libde265[^\r\n]*:\s*\+\s*built-in") -or
           ($heifLog -match "(?i)Compiling 'libde265' as built-in backend")
if (-not $builtIn) {
    Write-Host $heifLog
    throw ("libheif did not compile libde265 in as a built-in backend -- HEIC would not " +
           "decode. If libheif reworded its configure summary again, update this check.")
}
# The summary's HEIC row is "HEIC <decode> <encode>"; decoding is what matters.
if ($heifLog -match "(?im)^\s*HEIC\s+(\S+)") {
    if ($Matches[1] -notmatch '(?i)yes') {
        Write-Host $heifLog
        throw "libheif reports HEIC decoding as '$($Matches[1])', expected YES."
    }
    Write-Host "  libheif: libde265 built-in, HEIC decoding YES"
}

# ── Assemble the bundle tree ─────────────────────────────────────────────────
Write-Step 'Assembling the codec bundle'
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $dest 'x64\include\libheif') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $dest 'x64\lib') -Force | Out-Null

# libde265.lib ships beside heif.lib and both are linked: libheif references
# de265_* but records no /DEFAULTLIB for it, so nothing would otherwise pull it
# in. They are NOT merged into one archive -- both projects have a bitstream.cc,
# so a merged archive would hold two members named bitstream.cc.obj.
foreach ($lib in @('heif.lib', 'libde265.lib', 'openjp2.lib')) {
    $path = Join-Path $prefix "lib\$lib"
    if (-not (Test-Path $path)) { throw "Expected $lib in $prefix\lib after install" }
    Copy-Item $path (Join-Path $dest "x64\lib\$lib") -Force
}
Copy-Item (Join-Path $prefix 'include\libheif\*.h') (Join-Path $dest 'x64\include\libheif') -Force

# OpenJPEG installs into a versioned include dir; flatten it, which is what
# photo_salon_find_codec's plain openjpeg.h lookup expects to find first.
$opjInc = Get-ChildItem (Join-Path $prefix 'include') -Directory -Filter 'openjpeg-*' |
          Select-Object -First 1
if (-not $opjInc) { throw "OpenJPEG headers not found under $prefix\include" }
foreach ($h in @('openjpeg.h', 'opj_config.h')) {
    Copy-Item (Join-Path $opjInc.FullName $h) (Join-Path $dest 'x64\include') -Force
}

# ── Verify ───────────────────────────────────────────────────────────────────
Write-Step 'Verifying the codec libraries'
$dumpbin = Join-Path $cfg.MsvcDir 'bin\Hostx64\x64\dumpbin.exe'

# 1. Every library must request the CRT flavour the rest of the bundle uses.
$wantCrt = if ($cfg.CrtLinkage -eq 'MultiThreaded') { 'libcmt' } else { 'msvcrt' }
$banned  = if ($cfg.CrtLinkage -eq 'MultiThreaded') { 'msvcrt|msvcprt' } else { 'libcmt|libcpmt' }
foreach ($lib in Get-ChildItem (Join-Path $dest 'x64\lib') -Filter *.lib) {
    $directives = Invoke-Native -What 'dumpbin /directives' -Script { & $dumpbin /directives $lib.FullName } |
                  Select-String -Pattern '/DEFAULTLIB:"?([^"\s]+)' -AllMatches |
                  ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value } |
                  Sort-Object -Unique
    $bad = $directives | Where-Object { $_ -match "(?i)^($banned)" }
    if ($bad) { throw "$($lib.Name) requests the wrong CRT: $($bad -join ', '). Expected $wantCrt." }
    Write-Host "  $($lib.Name): $($directives -join ' ')"
}

# 2. The compiler emits calls to __std_* CRT helpers -- vectorised algorithms
#    (__std_rotate), exception plumbing (__std_exception_copy), RTTI
#    (__std_type_info_name). A CRT older than the compiler that produced the
#    objects will not have the newest of them; that is the failure earlier
#    revisions papered over with -D_USE_STD_VECTOR_ALGORITHMS=0. Prove the whole
#    set resolves here rather than discovering it at cross-link.
#
#    They are spread across several libraries -- the C++ ones in libcpmt, the
#    exception and RTTI ones in libvcruntime -- so the search has to cover every
#    CRT library that ships for this linkage, and only that linkage's set: a
#    symbol found only in the /MD libraries would not save a /MT link.
$crtLibPaths = @()
if ($cfg.CrtLinkage -eq 'MultiThreaded') {
    foreach ($n in @('libcmt.lib', 'libcpmt.lib', 'libvcruntime.lib', 'libconcrt.lib', 'libvcmath-mt.lib')) {
        $crtLibPaths += Join-Path $cfg.MsvcDir "lib\x64\$n"
    }
    $crtLibPaths += Join-Path $cfg.SdkRoot "Lib\$($cfg.WindowsSdk)\ucrt\x64\libucrt.lib"
} else {
    foreach ($n in @('msvcrt.lib', 'msvcprt.lib', 'vcruntime.lib', 'concrt.lib', 'libvcmath-md.lib')) {
        $crtLibPaths += Join-Path $cfg.MsvcDir "lib\x64\$n"
    }
    $crtLibPaths += Join-Path $cfg.SdkRoot "Lib\$($cfg.WindowsSdk)\ucrt\x64\ucrt.lib"
}

$available = @()
foreach ($crtLib in $crtLibPaths) {
    if (-not (Test-Path $crtLib)) { continue }
    $available += Invoke-Native -What 'dumpbin /linkermember' -Script { & $dumpbin /linkermember:1 $crtLib } |
                  Select-String -Pattern '\b(__std_\w+)' -AllMatches |
                  ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value }
}
$available = $available | Sort-Object -Unique

foreach ($lib in Get-ChildItem (Join-Path $dest 'x64\lib') -Filter *.lib) {
    $needed = Invoke-Native -What 'dumpbin /symbols' -Script { & $dumpbin /symbols $lib.FullName } |
              Select-String -Pattern 'UNDEF.*\b(__std_\w+)' -AllMatches |
              ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value } |
              Sort-Object -Unique
    $unresolved = $needed | Where-Object { $available -notcontains $_ }
    if ($unresolved) {
        throw ("$($lib.Name) references CRT helpers absent from the $($cfg.CrtLinkage) " +
               "libraries this bundle ships: $($unresolved -join ', '). The cross-link would " +
               "fail with undefined symbols. Build the codecs with the same toolset the CRT " +
               "was taken from.")
    }
    Write-Host "  $($lib.Name) __std_*: $(if ($needed) { $needed -join ' ' } else { '(none)' })"
}

# ── Record what was built ────────────────────────────────────────────────────
$commits.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" } |
    Set-Content (Join-Path $cfg.Out 'codec-commits.txt') -Encoding ascii

Write-Step "Codec bundle ready at $dest"
