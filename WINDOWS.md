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

All files copied from Windows live under `windows/`, mirroring the SDK/MSVC structure:

```
windows/
  sdk/
    include/        ← NOT committed (140 MB) — copy from Windows SDK
      ucrt/
      shared/
      um/
    lib/
      ucrt/         ← committed — ucrt.lib
      um/           ← committed — 37 Windows SDK import libs
  msvc/
    include/        ← NOT committed (17 MB) — copy from Visual Studio
    lib/            ← committed — 5 MSVC runtime import libs
```

The `lib/` subdirectories contain only the files actually referenced at link time and are
committed to the repo. The `include/` subdirectories are large and must be copied manually
from a Windows machine (see below).

## Files to copy from Windows

### `windows/sdk/include/` — Windows SDK headers

**Source (Windows):**

| Subdirectory | Windows path |
|---|---|
| `ucrt/` | `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt\` |
| `shared/` | `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared\` |
| `um/` | `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um\` |

### `windows/msvc/include/` — MSVC C++ Standard Library headers

**Source (Windows):**
```
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35227\include\
```

### `windows/qt-6.11/x64/` — Qt static build

Qt 6.11.1 built as a static library on Windows with MSVC, installed to this directory.
Not committed (330 MB). Must be built or copied from a Windows machine.

**Why MSVC is required:** Qt uses the Microsoft C++ ABI (`??`-mangled symbols). A MinGW/Zig
build uses the Itanium ABI (`_Z`-mangled symbols) and cannot link against these libraries.

Build options used (see `scripts/build-qt-windows-static.bat`):
- `-DCMAKE_BUILD_TYPE=Release`
- `-DBUILD_SHARED_LIBS=OFF`
- `-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF`

## Build

```bash
cmake -B _build_win \
  --toolchain cmake/toolchains/windows-x86_64-clang-cl.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build _build_win
```

Output: `_build_win/photo-salon.exe` — a PE32+ x86-64 Windows GUI executable.

The MSVC runtime DLLs (`msvcrt.dll`, `msvcp140.dll`, `vcruntime140.dll`) are not statically
linked; they are provided by Windows or installed via the Visual C++ Redistributable.

## Authenticode Signing (optional)

Signing the `.exe` prevents Windows SmartScreen from blocking it on the target machine.
The build script automatically signs after compilation when `osslsigncode` is installed
and a PFX certificate file is present.

### Prerequisites

```bash
# macOS
brew install osslsigncode

# Linux (Debian/Ubuntu)
sudo apt install osslsigncode
```

### Certificate options

| Option | Cost | SmartScreen result |
|---|---|---|
| Self-signed | Free | Silent on machines that trust the cert manually |
| OV code signing cert (Sectigo, DigiCert, etc.) | ~$100/yr | Warning reducible over time as reputation builds |
| Azure Trusted Signing | ~$10/mo | No warning — Microsoft-backed, immediate reputation |
| EV code signing cert | ~$300–500/yr | No warning — immediate reputation, requires USB HSM |

### Self-signed certificate (personal use / known machines)

```bash
# Generate key + self-signed cert (valid 10 years)
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 3650 -nodes \
  -subj "/CN=Photo Salon/O=Andrew Regner/C=US"

# Bundle into a PFX file
openssl pkcs12 -export -out codesign.pfx -inkey key.pem -in cert.pem -passout pass:changeme
```

Then on each target Windows 11 PC (run PowerShell as Administrator, one-time):

```powershell
Import-Certificate -FilePath cert.pem -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath cert.pem -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
```

### Running the build with signing

Place `codesign.pfx` in the repo root (it is gitignored), then:

```bash
CODESIGN_PASSWORD=changeme ./build-windows.sh
```

Or point to a cert in another location:

```bash
CODESIGN_CERT=/path/to/my.pfx CODESIGN_PASSWORD=secret ./build-windows.sh
```

If neither `osslsigncode` nor the cert file is found, signing is skipped and the unsigned
`.exe` is produced as before.
