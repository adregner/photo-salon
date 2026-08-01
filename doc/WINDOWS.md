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
  codecs/
    x64/            ← NOT committed — auto-fetched (49 MB, MSVC libheif + OpenJPEG)
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

## Image codec libraries (HEIC / JPEG 2000)

HEIC and the JPEG 2000 family are decoded by our own static Qt image plugins in
`src/imageformats/`, backed by **libheif** and **OpenJPEG** (see
`doc/ARCHITECTURE.md` § Image format support). macOS and Linux get them from
Homebrew/apt.

They can't come from the host when cross-compiling: `pkg-config` is deliberately not
consulted, or a Windows link would pick up the host's Linux `.so`s. So MSVC builds are
vendored in the same way as the static Qt bundle, as
`s3://photo-salon/_build/windows/codecs.tar.gz` (~8 MB), fetched by
`fetch-windows-deps.sh` into `windows/codecs/x64/`. The toolchain already searches that
prefix, so `./build-windows.sh` picks both codecs up with no further steps.

The bundle's own `BUILDINFO.txt` records the exact versions, commits and flags it was
built with. As shipped:

| Library | Version | Artifacts |
|---|---|---|
| [**libheif**](https://github.com/strukturag/libheif) | 1.23.1 | `lib/heif.lib`, `include/libheif/*.h` |
| [**libde265**](https://github.com/strukturag/libde265) | 1.0.16 | `lib/libde265.lib` — linked explicitly, see below |
| [**OpenJPEG**](https://github.com/uclouvain/openjpeg) | 2.5.3 | `lib/openjp2.lib`, `include/openjpeg.h`, `include/opj_config.h` |

AVIF support (libaom/dav1d) is **not** included — it is a separate libheif back-end, and
the goal here is HEIC.

### Rebuilding the bundle

On a Windows machine with MSVC and CMake+Ninja (Qt ships both under `C:\Qt\Tools`), from
a `vcvars64.bat` shell. Everything x64, **Release**, static (`-DBUILD_SHARED_LIBS=OFF`),
and — to match the static Qt bundle, which links the CRT dynamically — built with
`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL` (`/MD`). Mixing `/MT` and `/MD` in one
link produces the classic duplicate-symbol / heap-mismatch failures.

Build **libde265 first**, into a shared staging prefix, then point libheif at it:

```bash
S=C:/work/stage          # staging prefix all three install into
COMMON="-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DCMAKE_INSTALL_PREFIX=$S"

cmake -G Ninja -S libde265 -B b1 $COMMON -DENABLE_SDL=OFF
cmake --build b1 --target install

# NOTE the LIBDE265_STATIC_BUILD define — see "Static-build defines" below.
cmake -G Ninja -S libheif -B b2 $COMMON -DCMAKE_PREFIX_PATH=$S \
  -DCMAKE_C_FLAGS=/DLIBDE265_STATIC_BUILD -DCMAKE_CXX_FLAGS=/DLIBDE265_STATIC_BUILD \
  -DWITH_EXAMPLES=OFF -DENABLE_PLUGIN_LOADING=OFF -DBUILD_TESTING=OFF \
  -DWITH_GDK_PIXBUF=OFF -DWITH_LIBDE265=ON
  # …plus -DWITH_<everything else>=OFF; confirm the configure summary prints
  #   "Compiling 'libde265' as built-in backend"  and  HEIC decoding YES.
cmake --build b2 --target install

cmake -G Ninja -S openjpeg -B b3 $COMMON -DBUILD_CODEC=OFF -DBUILD_TESTING=OFF
cmake --build b3 --target install
```

`ENABLE_PLUGIN_LOADING=OFF` links the HEVC decoder **in** rather than `dlopen`ing it at
runtime — a plugin DLL would otherwise have to ship and be found next to the `.exe`.
`BUILD_CODEC=OFF` skips `opj_compress` and friends; only the library is needed.

Then assemble the tree:

```bash
X=windows/codecs/x64
mkdir -p $X/include/libheif $X/lib
cp $S/lib/heif.lib $S/lib/libde265.lib $S/lib/openjp2.lib  $X/lib/
cp $S/include/libheif/*.h                                  $X/include/libheif/
cp $S/include/openjpeg-2.5/openjpeg.h $S/include/openjpeg-2.5/opj_config.h $X/include/
```

`libde265.lib` ships **beside** `heif.lib`, and both get linked: libheif references
`de265_*` but records no `/DEFAULTLIB` for it, so nothing would otherwise pull it in.
`photo_salon_find_codec` hands the link one library path per codec, so `CMakeLists.txt`
finds and links libde265 explicitly under `if(WIN32)`.

> Do **not** merge them into a single archive with `lib /OUT:`. An earlier revision did,
> and the result contained two members both named `bitstream.cc.obj` — libheif and
> libde265 each have a `bitstream.cc`. Duplicate member names in an archive are a
> correctness hazard.

Flattening OpenJPEG's versioned `include/openjpeg-2.5/` into `include/` is deliberate,
though `photo_salon_find_codec` would find the versioned directory too.

Verify before publishing:

```bash
dumpbin /directives $X/lib/*.lib | findstr /i defaultlib   # expect only msvcrt/msvcprt/oldnames/uuid
dumpbin /symbols $X/lib/heif.lib | findstr UNDEF | findstr __std_   # see § Toolset below
```

Anything new in the first must be copied into `windows/sdk/lib/um/` per `doc/BUILD.md`
§ Windows SDK import libraries. As built, none is.

### Toolset — the codecs must not outrun the vendored runtime

The bundle is built with MSVC 14.51, but `windows/msvc/lib/msvcprt.lib` predates that
toolset. Newer STL headers call vectorized algorithm helpers that live in `msvcp140.dll`
— `__std_rotate` and `__std_unique_4` are **not** in the vendored import lib, so the
cross-link fails with undefined symbols. The bundle is therefore compiled with
`-D_USE_STD_VECTOR_ALGORITHMS=0`, which makes the STL use its header-only
implementations. That keeps the codecs compatible with the vendored import libs without
raising the VC++ redistributable version end users need.

Check every `__std_*` reference against what is actually vendored:

```bash
dumpbin /linkermember:1 windows/msvc/lib/msvcprt.lib | findstr __std_
```

Building with a toolset matching the static Qt bundle's (**14.44**) would avoid the
mismatch at source, and is the better fix if this box ever gets that toolset installed.

> **Known issue — native MSVC consumers.** An `.exe` compiled with MSVC 14.51 that links
> libheif can hit an access violation during static initialisation, before `main()`. It is
> layout-sensitive: it appears and disappears with unrelated flags on the *consumer*, and
> the faulting read sits a couple of bytes below the module base. libde265 and OpenJPEG
> alone never reproduce it, and it happens with libheif 1.19.8 and 1.23.1 alike — so it
> reads as a libheif/toolset interaction, not a property of this bundle's configuration.
> It has not been seen to affect the shipping path, which cross-compiles with clang-cl and
> lld-link rather than MSVC. Rebuilding under toolset 14.44 is the untested next step.

Then re-tar from the directory *above* `codecs/` so the archive root stays
`codecs/x64/...`, back up the existing object, and publish:

```bash
B=s3://photo-salon/_build/windows
tar czf codecs.tar.gz codecs
aws s3 cp $B/codecs.tar.gz $B/codecs.prebackup.tar.gz   # rollback copy
aws s3 cp codecs.tar.gz    $B/codecs.tar.gz             # publish
```

### Static-build defines

Both libraries' public headers decorate their APIs with `__declspec(dllimport)` on
Windows **unless** told the build is static. Get this wrong and the link fails on
`__imp_heif_*` / `__imp_opj_*` / `__imp_de265_*` symbols:

| Compiling | Needs |
|---|---|
| libheif (against static libde265) | `LIBDE265_STATIC_BUILD` |
| our plugins (against static libheif) | `LIBHEIF_STATIC_BUILD` |
| our plugins (against static OpenJPEG) | `OPJ_STATIC` |

The last two are set for us — `CMakeLists.txt` adds them to the plugin targets under
`if(WIN32)`. They are deliberately **not** set on macOS/Linux, where the codecs are shared
libraries and `OPJ_STATIC` would instead force hidden visibility.

### Why 1.23.1 rather than 1.19.8

libheif **1.19.8** was built first. A release-mode app linking it and OpenJPEG crashed
in every run of the codec tests; the same app against 1.23.1 passed them repeatedly, so
the bundle ships 1.23.1. Note that this is *not* a clean bill of health for 1.23.1 — the
static-init access violation described under § Toolset reproduces on both versions in
some consumer build configurations. Version choice mitigated it; it did not fix it.

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
