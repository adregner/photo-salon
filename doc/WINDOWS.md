# Windows Cross-Compilation Setup

`photo-salon.exe` is cross-compiled from macOS using `clang-cl` targeting the MSVC ABI.  
Toolchain: `cmake/toolchains/windows-x86_64-clang-cl.cmake`

## macOS prerequisites (Homebrew)

```bash
brew install llvm lld@21
```

`llvm` provides `clang-cl` (compiler) and `llvm-lib` (archiver).  
`lld@21` provides `lld-link` (PE/COFF linker).

## `windows/` directory layout

Large build artifacts live under `windows/` and are fetched automatically by `build-windows.sh`
via `fetch-windows-deps.sh` (downloads from S3). The small import libs are committed to the repo.

```
windows/
  sdk/
    include/        ← NOT committed — auto-fetched (140 MB, Windows SDK headers)
      ucrt/
      shared/
      um/
    lib/
      ucrt/         ← committed — ucrt.lib
      um/           ← committed — 37 Windows SDK import libs
  msvc/
    include/        ← NOT committed — auto-fetched (17 MB, MSVC STL headers)
    lib/            ← committed — 5 MSVC runtime import libs
  qt-6.11/
    x64/            ← NOT committed — auto-fetched (330 MB, Qt 6.11.1 static build)
```

The `lib/` subdirectories contain only the files actually referenced at link time.
The `include/` subdirectories and `qt-6.11/` are downloaded on first build and cached locally.

### `windows/qt-6.11/x64/` — Qt static build

Qt 6.11.1 built as a static library on Windows with MSVC.

**Why MSVC is required:** Qt uses the Microsoft C++ ABI (`??`-mangled symbols). A MinGW/Zig
build uses the Itanium ABI (`_Z`-mangled symbols) and cannot link against these libraries.

Build options used:
- `-DCMAKE_BUILD_TYPE=Release`
- `-DBUILD_SHARED_LIBS=OFF`
- `-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF`

The bundle is hosted at `s3://photo-salon/_build/windows/qt-6.11.tar.gz` (~89 MB),
fetched by `fetch-windows-deps.sh` and extracted to `windows/qt-6.11/x64/`. Because it is
a **static** build, image-format plugins are static `.lib`s (under
`plugins/imageformats/`) with their `*_init` objects plus `lib/cmake/Qt6Gui` import
configs; the cross-link picks them up automatically via Qt's static-plugin import, so
adding a format needs **no app code change**.

### Updating the bundle — adding image-format modules (TIFF, WebP, …)

The base bundle is built from **qtbase only**, so out of the box it ships only the
jpeg/gif/ico/bmp/ppm/etc. plugins. TIFF, WebP, TGA, WBMP and ICNS come from the separate
**`qtimageformats`** module, which must be built **statically against this same static
Qt** and installed into the prefix. Do this on a Windows machine with the matching MSVC
toolset (see the gotcha below):

```bash
# 1. Extract the current bundle to a working prefix (e.g. C:\qtwork\qt-6.11\x64)
# 2. Get the matching source
git clone --depth 1 --branch v6.11.1 https://github.com/qt/qtimageformats.git
# 3. Configure + build + install into the prefix, from a build dir:
#    (run inside the original toolset's vcvars64 env; Qt ships cmake/ninja under C:\Qt\Tools)
qt-6.11/x64/bin/qt-configure-module.bat <src>/qtimageformats -- -DCMAKE_BUILD_TYPE=Release
cmake --build .
cmake --install .          # installs the new qtiff/qwebp/… .lib + cmake configs into the prefix
# 4. Re-tar:  tar czf qt-6.11.tar.gz qt-6.11
# 5. Back up the old S3 object, then upload the new one (see § Bundle upload below).
```

**Toolset gotcha (STL1001):** `lib/cmake/Qt6/qt.toolchain.cmake` pins the *original*
compiler the static Qt was built with via `QT_USE_ORIGINAL_COMPILER` (on by default). The
6.11.1 bundle was built with **MSVC 14.44.35207**. If you build a module with that
compiler but a *newer* toolset's STL headers on `INCLUDE` (e.g. 14.51 from a current VS),
you get `error STL1001: Unexpected compiler version, expected MSVC Compiler 19.50 or
newer`. Fix: run the module build under the **original toolset's** `vcvars64.bat` so
compiler and headers match (or pass `-DQT_USE_ORIGINAL_COMPILER=OFF` to use a single
newer toolset consistently — any MSVC v14x is forward binary-compatible).

`JasPer` (JPEG 2000) and `MNG` are **off** — `qtimageformats` doesn't bundle those
third-party libs by default; enabling them needs their sources fetched separately. The
image-format plugins add no new Windows system-DLL dependencies beyond what `Qt6Gui`
already pulls in, so no new `windows/sdk/lib/um/` import libs are required.

## Image codec libraries (HEIC / JPEG 2000) — **not yet available on Windows**

HEIC and the JPEG 2000 family are decoded by our own static Qt image plugins in
`src/imageformats/`, backed by **libheif** and **OpenJPEG** (see
`doc/ARCHITECTURE.md` § Image format support). Both are found at configure time and
are optional: the Windows cross-build currently finds neither, prints a CMake warning,
and produces an `.exe` that works exactly as before **minus those two formats**. macOS
and Linux get them from Homebrew/apt.

They can't come from the host: `pkg-config` is deliberately not consulted when
cross-compiling, or a Windows link would pick up the host's Linux `.so`s. So the
libraries have to be **built on a Windows machine with MSVC** and vendored in, the same
way the static Qt bundle is.

> **The Windows *release* workflow fails until that is done.** `release-windows.yml` sets
> `PHOTO_SALON_REQUIRE_CODECS=1`, which makes a missing codec a fatal CMake error rather
> than a warning, so no `.exe` can be published without HEIC and JPEG 2000 support. Plain
> CI (`ci.yml`) still cross-compiles fine — it leaves the codecs optional — so the
> Windows build stays verified in the meantime.

### What to build and bring back

All of it x64, **Release**, static (`-DBUILD_SHARED_LIBS=OFF`), and — to match the
static Qt bundle, which links the CRT dynamically — with
`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL` (`/MD`). Mixing `/MT` and `/MD` in one
link is what produces the classic duplicate-symbol / heap-mismatch failures.

| # | Library | Artifacts needed | Notes |
|---|---|---|---|
| 1 | [**libheif**](https://github.com/strukturag/libheif) (≥ 1.17) | `heif.lib`, headers `libheif/heif.h` + `libheif/heif_version.h` | Configure with `-DWITH_EXAMPLES=OFF -DENABLE_PLUGIN_LOADING=OFF` so the HEVC decoder is linked **in** rather than `dlopen`ed at runtime — a plugin DLL would have to be shipped and found next to the `.exe`. |
| 2 | [**libde265**](https://github.com/strukturag/libde265) (≥ 1.0.15) | `libde265.lib` | The HEVC decoder libheif needs to decode HEIC at all. Build it first and point libheif's configure at it. |
| 3 | [**OpenJPEG**](https://github.com/uclouvain/openjpeg) (≥ 2.5) | `openjp2.lib`, headers `openjpeg.h`, `opj_config.h`, `opj_stdint.h` | Configure with `-DBUILD_CODEC=OFF` — only the library is needed, not `opj_compress` and friends. |
| 4 | Windows SDK import libs | *expected: none new* | libheif, libde265 and OpenJPEG use only the CRT and `kernel32`, which are already vendored. Confirm on the built libs with `dumpbin /directives *.lib \| findstr /i defaultlib`; if a `.lib` that isn't already in `windows/sdk/lib/um/` shows up, copy it in per `doc/BUILD.md` § Windows SDK import libraries. |

AVIF support (libaom/dav1d) is **not** required — it is a separate libheif back-end,
and the goal here is HEIC.

### Where to put them

The cross-compile toolchain already searches `windows/codecs/x64` (both as a
`CMAKE_PREFIX_PATH` and a find-root), so the vendored tree just needs the standard
layout:

```
windows/
  codecs/
    x64/
      include/
        libheif/heif.h
        libheif/heif_version.h
        openjpeg.h
        opj_config.h
        opj_stdint.h
      lib/
        heif.lib
        libde265.lib
        openjp2.lib
```

With that in place `./build-windows.sh` picks both up with no further changes — the
CMake warnings disappear and the plugins are compiled into the `.exe`. If a header ends
up under a versioned subdirectory (OpenJPEG's own install uses `include/openjpeg-2.5/`),
either flatten it into `include/` or pass
`-DOPENJPEG_INCLUDE_DIR=<path> -DOPENJPEG_LIBRARY=<path/openjp2.lib>` (and the matching
`HEIF_*` variables) to CMake.

Ship the tree the same way as the Qt bundle: tar it up as `codecs.tar.gz`, upload to
`s3://photo-salon/_build/windows/`, and add a `fetch` line for it in
`fetch-windows-deps.sh` next to the existing ones.

### Bundle upload

Re-tar from the directory *above* `qt-6.11/` so the archive root stays `qt-6.11/x64/...`.
Back up the existing object before overwriting:

```bash
B=s3://photo-salon/_build/windows
aws s3 cp $B/qt-6.11.tar.gz $B/qt-6.11.prebackup.tar.gz   # rollback copy
aws s3 cp qt-6.11.tar.gz    $B/qt-6.11.tar.gz             # publish
```

AWS auth on the build box uses `aws login` (a custom session wrapper; cross-device:
`aws login --remote`), not static keys.

## Build

```bash
./build-windows.sh
```

This fetches any missing Windows dependencies (headers, Qt static libs) from S3, then runs
CMake and compiles. Output: `_build_win/photo-salon.exe` — a PE32+ x86-64 Windows GUI executable.

To run the CMake steps manually (after dependencies are already fetched):

```bash
cmake -B _build_win \
  --toolchain cmake/toolchains/windows-x86_64-clang-cl.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build _build_win
```

The MSVC runtime DLLs (`msvcrt.dll`, `msvcp140.dll`, `vcruntime140.dll`) are not statically
linked; they are provided by Windows or installed via the Visual C++ Redistributable.

## Authenticode Signing (optional)

Signing the `.exe` prevents Windows SmartScreen from blocking it on the target machine.
The build script signs automatically after compilation when the prerequisites below are met.
Two methods are supported; Azure Trusted Signing is tried first.

### Method 1 — Azure Trusted Signing (recommended)

No local certificate file needed. Signing happens in Azure's cloud HSM, which gives
immediate SmartScreen reputation because Microsoft is the root CA.

**Prerequisites:**

```bash
# macOS
brew install jsign azure-cli

# Linux (Debian/Ubuntu) — jsign requires Java 11+ (NOT Java 8)
sudo apt install openjdk-21-jdk
curl -LO https://github.com/ebourg/jsign/releases/latest/download/jsign.jar
sudo install -m755 jsign.jar /usr/local/lib/
echo '#!/bin/sh\nexec java -jar /usr/local/lib/jsign.jar "$@"' | sudo tee /usr/local/bin/jsign
sudo chmod +x /usr/local/bin/jsign

# Linux Azure CLI
curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash
```

**Values needed from Azure Portal:**

| Variable | Where to find it |
|---|---|
| `AZURE_TRUSTED_SIGNING_ENDPOINT` | Trusted Signing account → Overview → Endpoint (hostname only, no `https://`, e.g. `wus2.codesigning.azure.net`) |
| `AZURE_TRUSTED_SIGNING_ACCOUNT` | Trusted Signing account → Overview → Account name |
| `AZURE_TRUSTED_SIGNING_CERT_PROFILE` | Trusted Signing account → Certificate profiles → profile name |

Note: jsign requires the alias in `account/profile` form and a token scoped to
`https://codesigning.azure.net` (not a general Azure management token). The build
script handles both automatically.

**Interactive (local dev):**

```bash
az login
export AZURE_TRUSTED_SIGNING_ENDPOINT="wus2.codesigning.azure.net"
export AZURE_TRUSTED_SIGNING_ACCOUNT="my-signing-account"
export AZURE_TRUSTED_SIGNING_CERT_PROFILE="MyCertProfile"
./build-windows.sh
```

**Service principal (CI / non-interactive):**

```bash
export AZURE_TENANT_ID="<tenant-id>"
export AZURE_CLIENT_ID="<app-client-id>"
export AZURE_CLIENT_SECRET="<app-client-secret>"
export AZURE_TRUSTED_SIGNING_ENDPOINT="wus2.codesigning.azure.net"
export AZURE_TRUSTED_SIGNING_ACCOUNT="my-signing-account"
export AZURE_TRUSTED_SIGNING_CERT_PROFILE="MyCertProfile"
./build-windows.sh
```

The service principal needs the **Trusted Signing Certificate Profile Signer** role on the
Trusted Signing account in Azure Portal (Access control → Add role assignment).

### Method 2 — Local PFX certificate (self-signed or OV cert)

Fallback method if Azure Trusted Signing is not configured. Requires `osslsigncode`.

```bash
# macOS
brew install osslsigncode

# Linux (Debian/Ubuntu)
sudo apt install osslsigncode
```

**Self-signed cert (personal use / known machines only):**

```bash
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 3650 -nodes \
  -subj "/CN=Photo Salon/O=Andrew Regner/C=US"
openssl pkcs12 -export -out codesign.pfx -inkey key.pem -in cert.pem -passout pass:changeme
```

On each target Windows 11 PC (PowerShell as Administrator, one-time):

```powershell
Import-Certificate -FilePath cert.pem -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath cert.pem -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
```

Place `codesign.pfx` in the repo root (gitignored), then build:

```bash
CODESIGN_PASSWORD=changeme ./build-windows.sh
# or: CODESIGN_CERT=/path/to/my.pfx CODESIGN_PASSWORD=secret ./build-windows.sh
```

### Verifying the signature

From Linux/macOS:

```bash
osslsigncode verify _build_win/photo-salon.exe
```

From Windows: right-click the `.exe` → Properties → Digital Signatures tab.

If neither signing method is configured, the unsigned `.exe` is produced as before.
