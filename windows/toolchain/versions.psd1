# Pinned inputs for the Windows cross-compilation toolchain bundles.
#
# Everything the cross-build links against is produced from THIS file in one
# scripted run on one Windows machine, so the MSVC toolset is identical across
# the CRT, the Windows SDK import libs, Qt, and the image codecs. Mixing
# toolsets is what produced the __std_rotate / __std_unique_4 link failures
# documented in earlier revisions of doc/WINDOWS.md.
#
# Bump BundleTag whenever any other value here changes. Artifacts are published
# under a tag-prefixed S3 path and are never overwritten, so every published
# bundle stays reachable for rollback.

@{
    BundleTag = '2026.08-mt1'

    # ── Host toolchain (must be installed on the Windows machine) ────────────
    # Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC'
    MsvcToolset = '14.51.36231'
    # Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Lib'
    WindowsSdk  = '10.0.26100.0'

    # ── CRT linkage ──────────────────────────────────────────────────────────
    # MultiThreaded    => /MT, static CRT, no VC++ Redistributable on the target.
    # MultiThreadedDLL => /MD, needs msvcp140.dll + vcruntime140.dll installed.
    # The whole point of this bundle set is a standalone .exe, so: MultiThreaded.
    CrtLinkage = 'MultiThreaded'

    # ── Qt ───────────────────────────────────────────────────────────────────
    # qtimageformats is NOT optional: it supplies the tiff/webp/tga/icns/wbmp
    # plugins the shipping bundle already carries. Dropping it silently removes
    # image formats the app loads on macOS and Linux.
    QtVersion = '6.11.1'
    QtModules = @('qtbase', 'qtimageformats')

    # -static-runtime is what makes the .exe standalone; it is only meaningful
    # together with -static.
    #
    # -no-icu is deliberate. Left to itself Qt's configure finds the Windows
    # SDK's icuuc/icuin import libs and links them, which makes the .exe import
    # icuuc.dll and icuin.dll. Those are OS components only on new enough
    # Windows 10 builds, so linking them undercuts the "runs anywhere" goal for
    # no benefit here: without ICU, QCollator falls back to CompareStringEx and
    # Windows' own collation, which is what a file-name sort wants anyway.
    QtConfigureArgs = @(
        '-static'
        '-static-runtime'
        '-release'
        '-opensource'
        '-confirm-license'
        '-no-icu'
        '-nomake', 'examples'
        '-nomake', 'tests'
    )

    # ── Image codecs (git tags; the commit each resolves to is recorded in
    #    BUILDINFO.txt at build time and verified on rebuild) ────────────────
    Codecs = @(
        @{ Name = 'libde265'; Repo = 'https://github.com/strukturag/libde265.git'; Tag = 'v1.0.16'
           Commit = '7ba65889d3d6d8a0d99b5360b028243ba843be3a' }
        @{ Name = 'libheif';  Repo = 'https://github.com/strukturag/libheif.git';  Tag = 'v1.23.1'
           Commit = '2c4bbb54c2738d4a5efbbe3e5fa1d5d76bb88eb0' }
        @{ Name = 'openjpeg'; Repo = 'https://github.com/uclouvain/openjpeg.git';  Tag = 'v2.5.3'
           Commit = '210a8a5690d0da66f02d49420d7176a21ef409dc' }
    )

    # ── MSVC CRT libraries to vendor out of VC\Tools\MSVC\<toolset>\lib\x64 ──
    # Static-CRT (/MT) set first; the /MD import libs follow so the linkage can
    # be flipped back without re-gathering. Same toolset either way, so there is
    # no mismatch risk in shipping both. The script fails if any is missing.
    MsvcLibs = @(
        # /MT — static CRT
        'libcmt.lib'                 # C runtime
        'libcpmt.lib'                # C++ standard library
        'libvcruntime.lib'           # compiler support routines
        'libconcrt.lib'              # Concurrency Runtime
        'libvcmath-mt.lib'           # vectorised math, split out in MSVC 14.51
        # /MD — import libs, kept for fallback
        'msvcrt.lib'
        'msvcprt.lib'
        'vcruntime.lib'
        'concrt.lib'
        'libvcmath-md.lib'
        # linkage-independent
        'oldnames.lib'               # POSIX-style aliases (_open -> open)
        'comsuppw.lib'               # _com_ptr_t support, pulled in by Qt
        'delayimp.lib'               # /DELAYLOAD thunks
        'legacy_stdio_definitions.lib'
    )

    # ── Windows SDK x64 ucrt libraries ──────────────────────────────────────
    UcrtLibs = @(
        'libucrt.lib'                # /MT
        'ucrt.lib'                   # /MD
    )

    # SDK header directories to vendor. winrt/ and cppwinrt/ are deliberately
    # excluded: 210 MB that nothing in this project includes.
    SdkIncludeDirs = @('ucrt', 'shared', 'um')
}
