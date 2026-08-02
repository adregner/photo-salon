---
name: windows-toolchain
description: Regenerate or republish the pre-built Windows artifacts the photo-salon cross-compile links against — the MSVC CRT, Windows SDK import libraries, static Qt, and the libheif/libde265/OpenJPEG codecs. Use when a Qt or codec version needs bumping, when a link fails with a missing .lib or an undefined __std_* symbol, when the bundles must move to a new MSVC toolset, or when the published S3 artifacts are missing or suspect. Requires a Windows machine.
---

# Regenerating the Windows toolchain bundles

`photo-salon.exe` is cross-compiled on macOS or Linux with `clang-cl`, but everything
it links must be produced on Windows by MSVC. Those artifacts are pre-built, published
to S3, and pinned by `windows-deps.lock`.

**The invariant this whole setup exists to hold: one MSVC toolset produces all of it.**
The CRT, the SDK import libraries, Qt and the codecs are gathered in a single scripted
run. Gathering them piecemeal is what previously produced undefined `__std_rotate` /
`__std_unique_4` symbols at cross-link time. If you find yourself adding a workaround
flag to one component, stop — that is the signal that something was built with the
wrong toolset.

## Where things are

| | |
|---|---|
| Scripts | `windows/toolchain/` (run on Windows, PowerShell 5.1) |
| Pinned inputs | `windows/toolchain/versions.psd1` |
| Pinned outputs | `windows-deps.lock` (committed; generated, never hand-edited) |
| Full reference | `doc/WINDOWS.md` |

## Running it

Needs a Windows machine with the MSVC toolset and Windows SDK named in
`versions.psd1`, Qt's CMake and Ninja (`C:\Qt\Tools`), git, the AWS CLI, and ~25 GB
free. Confirm the toolset is present before starting — the scripts fail fast if not,
but a missing toolset means a trip through the Visual Studio Installer.

```powershell
cd windows\toolchain
.\Make-WindowsToolchain.ps1              # all steps except publish
```

The Qt step is ~1900 compilation units and dominates everything else: about an hour on
eight cores, most of a working day on a two-core VM. Check the machine's core count
before promising a timeline.

Individual steps, for when only one thing changed:

```powershell
.\Make-WindowsToolchain.ps1 -Step codecs   # then always re-run -Step package
.\Make-WindowsToolchain.ps1 -Step package
```

Always run the Qt step in the background and poll `C:\pst\logs\qtbase-build.log` — it is
far too long for a foreground call. The `codecs` step can run alongside it; they use
separate build trees.

## Before you change anything

Edit `versions.psd1`, never the scripts, for: toolset, SDK, Qt version, codec
versions, CRT linkage, which CRT libraries get vendored. **Bump `BundleTag` whenever
any of those change** — published artifacts are immutable and the publish step refuses
to overwrite an existing tag.

**Changing `MsvcToolset` can break every build host.** The MSVC STL hard-asserts on the
cross-compiler version (`error STL1000: Unexpected compiler version, expected Clang N or
newer`) — 14.51 requires Clang 20. If a new toolset raises that floor, the minimum has to
move in all four places together: `_min_llvm` in `cmake/clang-cl-win.sh`,
`_photo_salon_min_llvm` in `cmake/toolchains/windows-x86_64-clang-cl.cmake`, and the
LLVM install step in both CI workflows. Check the new headers' requirement before
committing to a toolset bump.

## Publishing

`-Step publish` uploads to S3 and is outward-facing. **Ask the user before running
it.** Show them `C:\pst\out\BUILDINFO.txt` and the artifact hashes first. Because each
tag gets its own prefix, publishing never destroys a previous bundle — rollback is
checking out an older `windows-deps.lock`.

After publishing, commit `windows-deps.lock`. Nothing takes effect until that lands.

## Verifying

The scripts self-check (static Qt, expected image-format plugins, consistent CRT
directives, `__std_*` symbols resolvable). Those checks run on Windows and do not
prove the cross-link works. Always finish on the Linux/macOS build host:

```bash
./build-windows.sh
llvm-readobj-19 --coff-imports _build_win/photo-salon.exe | grep 'Name:' | sort -u
```

Under the `/MT` linkage that list must contain **no** `MSVCP140*.dll` or
`VCRUNTIME140*.dll` — those come from the Visual C++ Redistributable, and their
absence is the point of the static CRT. Everything remaining should be a Windows
system DLL or an `api-ms-win-*` forwarder.

## Common failures

| Symptom | Cause |
|---|---|
| `error STL1000: Unexpected compiler version` | Cross-compiler older than the vendored MSVC STL headers require. Install a newer LLVM; see § Before you change anything. Do not reach for `_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH`. |
| `lld-link: could not open '<name>.lib'` | Import lib genuinely absent from the SDK, or a casing the alias step missed — check `fetch-windows-deps.sh`'s `add_case_aliases`. The bundle ships all of `um/x64`, so a truly missing lib is unusual. |
| `CMAKE_MSVC_RUNTIME_LIBRARY` ignored by a codec | That project declares an old `cmake_minimum_required`, so policy CMP0091 defaults to OLD and the CRT flag comes from `CMAKE_<LANG>_FLAGS_RELEASE` instead. The configure passes `-DCMAKE_POLICY_DEFAULT_CMP0091=NEW` for exactly this. |
| undefined `__std_*` at cross-link | A component was built with a newer toolset than the vendored CRT. Rebuild everything from one toolset rather than adding `-D_USE_STD_VECTOR_ALGORITHMS=0`. |
| `error STL1001: Unexpected compiler version` | A Qt module built under a different `vcvars_ver` than qtbase. `Invoke-Vcvars` pins it; suspect a manual build outside the scripts. |
| unresolved `__imp_heif_*` / `__imp_opj_*` / `__imp_de265_*` | Static-build defines missing. `LIBHEIF_STATIC_BUILD` and `OPJ_STATIC` are set by `CMakeLists.txt` under `if(WIN32)`; `LIBDE265_STATIC_BUILD` is set when compiling libheif. |
| duplicate symbols / heap mismatch | `/MT` and `/MD` mixed. `CrtLinkage` in `versions.psd1` and `CMAKE_MSVC_RUNTIME_LIBRARY` in the toolchain file must agree. |
