#!/usr/bin/env bash
set -euo pipefail

BASE_URL="https://photo-salon.s3.amazonaws.com/_build/windows"
DEST="$(cd "$(dirname "$0")" && pwd)/windows"

fetch() {
    local name="$1" marker="$2"
    if [ -d "$DEST/$marker" ]; then
        echo "  skip  windows/$marker (already present)"
        return
    fi
    echo "  fetch $name → windows/$marker"
    curl -fL --progress-bar "$BASE_URL/$name" | tar -xz -C "$DEST"
}

echo "Checking Windows cross-compilation dependencies..."
fetch msvc-include.tar.gz msvc/include
fetch sdk-include.tar.gz  sdk/include
fetch qt-6.11.tar.gz      qt-6.11/x64

# Generate import stubs for Windows SDK DLLs not included in the bundled SDK libs.
# These are required by Qt6Network and the Schannel TLS backend.
if command -v llvm-dlltool &>/dev/null; then
    DLLTOOL=llvm-dlltool
elif [ -x /opt/homebrew/opt/llvm/bin/llvm-dlltool ]; then
    DLLTOOL=/opt/homebrew/opt/llvm/bin/llvm-dlltool
elif [ -x /usr/lib/llvm-19/bin/llvm-dlltool ]; then
    DLLTOOL=/usr/lib/llvm-19/bin/llvm-dlltool
else
    echo "  warn  llvm-dlltool not found — SDK stubs not generated (Windows build may fail)"
    DLLTOOL=""
fi

if [ -n "$DLLTOOL" ]; then
    mkstub() {
        local dll="$1"; shift
        local lib="$DEST/sdk/lib/um/${dll}.lib"
        [ -f "$lib" ] && return
        local def
        def="$(mktemp /tmp/${dll}.XXXXXX.def)"
        printf 'LIBRARY %s.dll\nEXPORTS\n' "${dll}" > "$def"
        for sym in "$@"; do printf '    %s\n' "$sym" >> "$def"; done
        "$DLLTOOL" -m i386:x86-64 -D "${dll}.dll" -d "$def" -l "$lib"
        rm -f "$def"
        echo "  stub  windows/sdk/lib/um/${dll}.lib"
    }

    mkstub dnsapi   # linked as dep by Qt6Network (no direct symbol refs needed)
    mkstub bcrypt \
        BCryptCloseAlgorithmProvider BCryptDecrypt BCryptDestroyKey BCryptEncrypt \
        BCryptEnumContextFunctions BCryptFreeBuffer BCryptGenerateSymmetricKey \
        BCryptOpenAlgorithmProvider BCryptSetProperty
    mkstub ncrypt \
        NCryptExportKey NCryptFreeObject NCryptImportKey \
        NCryptOpenStorageProvider NCryptSetProperty
    mkstub iphlpapi \
        ConvertInterfaceIndexToLuid ConvertInterfaceLuidToIndex \
        ConvertInterfaceLuidToNameW ConvertInterfaceNameToLuidW \
        GetAdaptersAddresses GetNetworkParams
    mkstub winhttp \
        WinHttpCloseHandle WinHttpGetDefaultProxyConfiguration \
        WinHttpGetIEProxyConfigForCurrentUser WinHttpGetProxyForUrl WinHttpOpen
    mkstub crypt32 \
        CertAddStoreToCollection CertCloseStore CertCompareCertificate \
        CertDuplicateCertificateContext CertFindCertificateInStore \
        CertFreeCertificateChain CertFreeCertificateContext \
        CertGetCertificateChain CertGetCertificateContextProperty \
        CertOpenStore CertSetCertificateContextProperty CertVerifyTimeValidity \
        CryptAcquireCertificatePrivateKey CryptEncodeObject PFXImportCertStore
    mkstub secur32 \
        AcceptSecurityContext AcquireCredentialsHandleW ApplyControlToken \
        DecryptMessage DeleteSecurityContext EncryptMessage FreeContextBuffer \
        FreeCredentialsHandle InitializeSecurityContextW InitSecurityInterfaceW \
        QueryContextAttributesW
fi

echo "Done."
