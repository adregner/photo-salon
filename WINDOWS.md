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
The build script signs automatically after compilation when the prerequisites below are met.
Two methods are supported; Azure Trusted Signing is tried first.

### Method 1 — Azure Trusted Signing (recommended)

No local certificate file needed. Signing happens in Azure's cloud HSM, which gives
immediate SmartScreen reputation because Microsoft is the root CA.

**Prerequisites:**

```bash
# macOS
brew install jsign azure-cli

# Linux (Debian/Ubuntu) — jsign requires Java 11+
sudo apt install default-jre
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
| `AZURE_TRUSTED_SIGNING_ENDPOINT` | Trusted Signing account → Overview (e.g. `https://myaccount.eus.codesigning.azure.net`) |
| `AZURE_TRUSTED_SIGNING_CERT_PROFILE` | Trusted Signing account → Certificate profiles → profile name |

**Interactive (local dev):**

```bash
az login
export AZURE_TRUSTED_SIGNING_ENDPOINT="https://myaccount.eus.codesigning.azure.net"
export AZURE_TRUSTED_SIGNING_CERT_PROFILE="MyCertProfile"
./build-windows.sh
```

**Service principal (CI / non-interactive):**

```bash
export AZURE_TENANT_ID="<tenant-id>"
export AZURE_CLIENT_ID="<app-client-id>"
export AZURE_CLIENT_SECRET="<app-client-secret>"
export AZURE_TRUSTED_SIGNING_ENDPOINT="https://myaccount.eus.codesigning.azure.net"
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
