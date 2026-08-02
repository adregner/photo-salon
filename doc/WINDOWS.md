# Windows Cross-Compilation Setup

`photo-salon.exe` is cross-compiled from macOS or Linux using `clang-cl` targeting the
MSVC ABI. Toolchain: `cmake/toolchains/windows-x86_64-clang-cl.cmake`

The result is a **single standalone executable**. It links the C runtime statically and
imports nothing but Windows' own DLLs, so there is no Visual C++ Redistributable to
install and nothing to ship beside the `.exe`.

Everything it links against, however, has to be produced by MSVC on a Windows machine:
the CRT, the Windows SDK import libraries, a static Qt, and static image codecs. Those
are pre-built, published to S3, and pinned by `windows-deps.lock`.

## macOS / Linux prerequisites

```bash
brew install llvm lld@21                      # macOS
sudo apt install cmake clang-19 lld-19 llvm-19  # Debian/Ubuntu
```

`llvm` provides `clang-cl` (compiler) and `llvm-lib` (archiver); `lld` provides
`lld-link` (PE/COFF linker). The build also needs a host Qt for `moc` and `rcc` —
Homebrew's `qt` on macOS, `/opt/qt-linux/6.11.1/gcc_64` on Linux (the release workflow
installs it with `aqtinstall`).

## Build

```bash
./build-windows.sh
```

This reads `windows-deps.lock`, downloads and verifies any missing bundles, then
configures and compiles. Output: `_build_win/photo-salon.exe`, a PE32+ x86-64 Windows
GUI executable.

To run the CMake steps manually, once the dependencies are present:

```bash
cmake -B _build_win \
  --toolchain cmake/toolchains/windows-x86_64-clang-cl.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build _build_win
```

## `windows/` directory layout

Nothing under these paths is committed. `fetch-windows-deps.sh` populates all of it
from the four bundles named in `windows-deps.lock`, verifying each against the SHA-256
recorded there and caching it locally.

```
windows/
  toolchain/          ← committed — the scripts that BUILD everything below
  msvc/
    include/          ← MSVC C/C++ headers
    lib/              ← CRT libraries (libcmt, libcpmt, libvcruntime, …)
  sdk/
    include/          ← Windows SDK headers (ucrt, shared, um)
    lib/
      um/             ← every x64 import library the SDK ships
      ucrt/           ← libucrt.lib, ucrt.lib
  qt-6.11/
    x64/              ← Qt 6.11.1, static, static CRT
  codecs/
    x64/              ← libheif + libde265 + OpenJPEG, static
```

`windows/SalonViewer.ico`, `windows/photo-salon.rc` and `windows/toolchain/` are the
only committed contents.

## The one-toolset rule

**Every artifact in every bundle is produced by one MSVC toolset, in one scripted run.**

This is not a stylistic preference. Earlier bundles were gathered piecemeal — Qt from
MSVC 14.44, the codecs from 14.51, the CRT import libraries from something older still
— and the mismatches surfaced as link failures a long way from their cause:

- **Undefined `__std_rotate` / `__std_unique_4`.** Newer STL headers call vectorised
  algorithm helpers that live in the CRT. Compiling against 14.51 headers while linking
  a 14.44-era `msvcprt.lib` leaves them unresolved. This was worked around by building
  the codecs with `-D_USE_STD_VECTOR_ALGORITHMS=0` to force the header-only
  implementations. That workaround is gone: the CRT now comes from the same toolset as
  the objects that reference it.
- **`error STL1001: Unexpected compiler version`.** Qt's
  `lib/cmake/Qt6/qt.toolchain.cmake` pins the compiler the static Qt was built with via
  `QT_USE_ORIGINAL_COMPILER`. Building a Qt module with that compiler but a newer
  toolset's STL headers on `INCLUDE` fails outright.

If a component needs a special flag to link against the others, that is the signal that
it was built with the wrong toolset. Fix the toolset, not the symptom.

## Regenerating the bundles

On a Windows machine, with the MSVC toolset and Windows SDK named in
`windows/toolchain/versions.psd1`, Qt's CMake and Ninja (`C:\Qt\Tools`), git, the AWS
CLI, and ~25 GB free:

```powershell
cd windows\toolchain
.\Make-WindowsToolchain.ps1
```

Steps run in order and can be run individually with `-Step`:

| Step | What | Time (8 cores) | Time (2 cores) |
|---|---|---|---|
| `msvc` | MSVC headers + CRT libraries from the pinned toolset | ~1 min | ~1 min |
| `sdk` | Windows SDK headers + all x64 import libraries | ~3 min | ~3 min |
| `qt` | Qt built static, with the static CRT | ~1 h | 3–5 h |
| `codecs` | libde265, libheif, OpenJPEG built static | ~10 min | ~30 min |
| `package` | tar everything, write `BUILDINFO.txt` + `windows-deps.lock` | ~5 min | ~10 min |
| `publish` | upload to S3 | — | — |

The `qt` step dominates and scales with core count — it is ~1900 compilation units.
Run it in the background and watch `C:\pst\logs\qtbase-build.log`; a small cloud VM
will take most of a working day.

Scratch space is `C:\pst` (short on purpose — Qt's build tree nests deeply enough that
a long path hits `MAX_PATH` and fails mid-build). Logs land in `C:\pst\logs`, finished
artifacts in `C:\pst\out`.

**Change `versions.psd1`, not the scripts**, for the toolset, SDK, Qt version, codec
versions, CRT linkage, or which CRT libraries get vendored. Bump `BundleTag` whenever
any of them changes.

### What the scripts check

Each step refuses to produce an artifact it can tell is wrong, because every one of
these has been a real failure at some point:

- Qt actually came out **static** (no `Qt6*.dll` in `bin/`) and carries the expected
  image-format plugins, `qtiff` and `qwebp` included.
- `Qt6Core.lib` requests the CRT flavour that was asked for, not the default.
- Every codec library requests that same CRT flavour — a `/MT` and `/MD` mix produces
  duplicate-symbol and heap-mismatch failures.
- libheif's configure summary says it compiled libde265 in as a **built-in backend**; a
  missing HEVC back-end would otherwise only show up as a runtime "no decoder" error.
- Every `__std_*` symbol `heif.lib` references is present in the CRT being vendored.
- Upstream codec tags still resolve to the commits pinned in `versions.psd1`.

None of these run on the cross-compile host, so they prove the bundle is
self-consistent, not that the cross-link works. Always finish by building on the build
host — see § Verifying below.

### Publishing

Artifacts are published under a **tag-prefixed, immutable** path:

```
s3://photo-salon/_build/windows/<BundleTag>/
```

The publish step refuses to overwrite an existing key, so previous bundles stay
reachable and rollback is a matter of checking out an older `windows-deps.lock`. That
replaces the old copy-to-`.prebackup.tar.gz`-before-overwriting routine.

```powershell
.\Make-WindowsToolchain.ps1 -Step publish
```

Then commit `windows-deps.lock` — nothing takes effect until that lands. AWS auth on
the build box uses `aws login` (a custom session wrapper; cross-device:
`aws login --remote`), not static keys.

## Static CRT — what makes the binary standalone

`CMAKE_MSVC_RUNTIME_LIBRARY` is set to `MultiThreaded` (`/MT`) in the cross-compile
toolchain file, and Qt is configured with `-static-runtime` to match. The `/MT` chain is
self-describing and resolves entirely out of the bundles: `libcmt` pulls in `libucrt`,
`libvcruntime` and `kernel32`; `libcpmt` pulls in `advapi32`, `synchronization` and
`uuid`.

Under the default `/MD` the binary instead imports `msvcp140.dll`, `msvcp140_1.dll`,
`vcruntime140.dll` and `vcruntime140_1.dll`, all of which come from the Visual C++
Redistributable and are absent from a stock Windows install.

The two linkages must not be mixed in one binary. `CrtLinkage` in `versions.psd1` and
`CMAKE_MSVC_RUNTIME_LIBRARY` in the toolchain file have to agree; changing one means
regenerating the bundles.

Qt is also configured `-no-icu`. Left alone, configure finds the SDK's `icuuc`/`icuin`
import libraries and the `.exe` ends up importing `icuuc.dll` and `icuin.dll`, which are
only OS components on new enough Windows 10 builds. Without ICU, `QCollator` falls back
to `CompareStringEx` and Windows' own collation — which is what a file-name sort wants
anyway.

### Verifying

```bash
./build-windows.sh
llvm-readobj-19 --coff-imports _build_win/photo-salon.exe | grep 'Name:' | sort -u
```

The list must contain no `MSVCP140*.dll` and no `VCRUNTIME140*.dll`. Everything left
should be a Windows system DLL or an `api-ms-win-*` forwarder.

## What each bundle contains

### Qt

Qt built as a **static** library with the static CRT. Qt uses the Microsoft C++ ABI
(`??`-mangled symbols); a MinGW or Zig build uses the Itanium ABI (`_Z`-mangled) and
cannot be linked against, which is why MSVC is required rather than merely convenient.

`qtimageformats` is built alongside `qtbase` and is **not optional** — it supplies the
`tiff`, `webp`, `tga`, `icns` and `wbmp` plugins. Dropping it silently removes image
formats the app loads on macOS and Linux. Because Qt is static, those plugins are
`.lib` files under `plugins/imageformats/` with their `*_init` objects plus
`lib/cmake/Qt6Gui` import configs; the cross-link picks them up via Qt's static-plugin
import, so adding a format needs **no app code change**.

JasPer and MNG stay off — `qtimageformats` does not bundle those third-party libraries,
and JPEG 2000 is handled by our own OpenJPEG-backed plugin instead.

### Image codecs (HEIC / JPEG 2000)

HEIC and the JPEG 2000 family are decoded by our own static Qt image plugins in
`src/imageformats/`, backed by **libheif** and **OpenJPEG** (see `doc/ARCHITECTURE.md`
§ Image format support). macOS and Linux get these from Homebrew and apt; a
cross-compile cannot, because `pkg-config` is deliberately not consulted — it would
hand a Windows link the host's Linux `.so`s.

| Library | Artifacts |
|---|---|
| [libheif](https://github.com/strukturag/libheif) | `lib/heif.lib`, `include/libheif/*.h` |
| [libde265](https://github.com/strukturag/libde265) | `lib/libde265.lib` |
| [OpenJPEG](https://github.com/uclouvain/openjpeg) | `lib/openjp2.lib`, `include/openjpeg.h`, `include/opj_config.h` |

Versions and the exact commits are pinned in `versions.psd1` and recorded in the
bundle's `BUILDINFO.txt`. A moved upstream tag is treated as an error, not a warning.

AVIF (libaom/dav1d) is **not** included — it is a separate libheif back-end, and the
goal here is HEIC.

Three things about this bundle are load-bearing:

- **`ENABLE_PLUGIN_LOADING=OFF`** links the HEVC decoder in rather than `dlopen`-ing it
  at runtime. A plugin DLL would otherwise have to ship and be found next to the `.exe`.
- **`libde265.lib` ships beside `heif.lib` and both are linked.** libheif references
  `de265_*` but records no `/DEFAULTLIB` for it, so nothing would otherwise pull it in.
  `CMakeLists.txt` finds and links libde265 explicitly under `if(WIN32)`. They are *not*
  merged into one archive: both projects have a `bitstream.cc`, so a merged archive
  holds two members named `bitstream.cc.obj`.
- **Static-build defines.** Both libraries' public headers decorate their APIs with
  `__declspec(dllimport)` on Windows unless told the build is static. Get this wrong and
  the link fails on `__imp_heif_*` / `__imp_opj_*` / `__imp_de265_*`:

  | Compiling | Needs |
  |---|---|
  | libheif (against static libde265) | `LIBDE265_STATIC_BUILD` |
  | our plugins (against static libheif) | `LIBHEIF_STATIC_BUILD` |
  | our plugins (against static OpenJPEG) | `OPJ_STATIC` |

  The last two are set for us — `CMakeLists.txt` adds them to the plugin targets under
  `if(WIN32)`. They are deliberately **not** set on macOS/Linux, where the codecs are
  shared libraries and `OPJ_STATIC` would instead force hidden visibility.

> **Known issue — native MSVC consumers.** An `.exe` compiled with MSVC 14.51 that
> links libheif can hit an access violation during static initialisation, before
> `main()`. It is layout-sensitive: it appears and disappears with unrelated flags on
> the *consumer*, and the faulting read sits a couple of bytes below the module base.
> libde265 and OpenJPEG alone never reproduce it, and it happens with libheif 1.19.8
> and 1.23.1 alike, so it reads as a libheif/toolset interaction rather than a property
> of this bundle's configuration. It has not been seen to affect the shipping path,
> which cross-compiles with `clang-cl` and `lld-link` rather than MSVC.

### Windows SDK

**Every** x64 import library the SDK ships is vendored, not a hand-picked subset. The
old curated list grew one entry at a time, each time a link failed with `lld-link:
error: could not open '<name>.lib'`, and carried no record of which SDK the files came
from. Import libraries contain no code — only the symbol-to-DLL mapping the linker
records in the PE import table — so taking all of them costs archive size and retires a
whole class of maintenance.

`winrt/` and `cppwinrt/` headers are excluded: 210 MB that nothing here includes.

Library filenames keep their on-disk SDK casing, which is inconsistent (`icuin.Lib` sits
next to `icuuc.lib`). The linker resolves a `/DEFAULTLIB` directive by opening a file
with exactly the name the directive carries, and those names use whatever casing the
source wrote. That is harmless on Windows and on macOS's case-insensitive default, but
on a case-sensitive filesystem every variant has to exist — so `fetch-windows-deps.sh`
creates lowercase and uppercase symlinks after extracting.

### MSVC runtime

Headers from `VC/Tools/MSVC/<toolset>/include`, and the CRT libraries listed in
`versions.psd1`. Both the `/MT` and `/MD` sets are vendored so the linkage can be
flipped without re-gathering; since they come from one toolset there is no mismatch risk
in shipping both.

> Earlier revisions generated a few import libs as minimal symbol stubs via
> `llvm-dlltool`. That was replaced with real SDK import libs: stubs covered only
> hand-listed symbols and broke whenever a new symbol was referenced.

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
