#!/usr/bin/env bash
set -euo pipefail

# Downloads and verifies the pinned bundles, and creates the linker's
# case-variant library aliases. See doc/WINDOWS.md.
"$(dirname "$0")/fetch-windows-deps.sh"

# Set PHOTO_SALON_REQUIRE_CODECS=1 (the release workflow does) to refuse to build
# an .exe without the HEIC / JPEG 2000 plugins. The MSVC builds of libheif and
# OpenJPEG they need come from the codecs bundle fetched above into
# windows/codecs/x64 — see doc/WINDOWS.md § Image codecs.
REQUIRE_CODECS="OFF"
[[ -n "${PHOTO_SALON_REQUIRE_CODECS:-}" && "${PHOTO_SALON_REQUIRE_CODECS}" != "0" ]] \
    && REQUIRE_CODECS="ON"

cmake -B _build_win --toolchain cmake/toolchains/windows-x86_64-clang-cl.cmake \
  -DCMAKE_BUILD_TYPE=Release -DPHOTO_SALON_REQUIRE_CODECS="$REQUIRE_CODECS"
cmake --build _build_win

# The .exe must be standalone: the CRT is linked statically (/MT), so it must not
# import the Visual C++ Redistributable DLLs. Nothing about a /MD regression is
# obvious at build time — it links fine and only fails on a machine without the
# redistributable installed — so check the import table before shipping.
verify_standalone() {
    local exe="$1" readobj="" candidate bad d
    # Look beside the compiler first — that is the LLVM this build actually
    # used. The versioned fallbacks are ordered newest-first.
    #
    # This list was pinned to llvm-readobj-19 and silently skipped the whole
    # check on a runner that only had LLVM 20, reporting nothing at all. A
    # guard that goes quiet when it cannot run is worse than no guard, so a
    # missing tool is now an error rather than a shrug.
    for d in ${PHOTO_SALON_LLVM_BIN:-} $(ls -d /usr/lib/llvm-* 2>/dev/null | sort -t- -k2 -n -r) \
             /opt/homebrew/opt/llvm; do
        if [ -x "$d/bin/llvm-readobj" ]; then readobj="$d/bin/llvm-readobj"; break; fi
        if [ -x "$d/llvm-readobj" ]; then readobj="$d/llvm-readobj"; break; fi
    done
    if [ -z "$readobj" ]; then
        for candidate in llvm-readobj llvm-readobj-22 llvm-readobj-21 llvm-readobj-20; do
            if command -v "$candidate" >/dev/null 2>&1; then readobj="$candidate"; break; fi
        done
    fi
    if [ -z "$readobj" ]; then
        echo "error: llvm-readobj not found — cannot verify $exe is standalone." >&2
        echo "       It ships with the LLVM that provides clang-cl. Set PHOTO_SALON_LLVM_BIN," >&2
        echo "       or set PHOTO_SALON_SKIP_STANDALONE_CHECK=1 to build without the check." >&2
        return 1
    fi

    bad="$("$readobj" --coff-imports "$exe" \
           | sed -n 's/^ *Name: //p' | sort -u \
           | grep -Ei '^(msvcp[0-9]|msvcr[0-9]|vcruntime[0-9])' || true)"
    if [ -n "$bad" ]; then
        echo "error: $exe imports Visual C++ Redistributable DLLs:" >&2
        printf '  %s\n' $bad >&2
        echo "       Expected a static CRT. Check CMAKE_MSVC_RUNTIME_LIBRARY in the" >&2
        echo "       toolchain file and CrtLinkage in windows/toolchain/versions.psd1." >&2
        return 1
    fi
    echo "Standalone check: no Visual C++ Redistributable imports (via $readobj)."
}

if [[ -n "${PHOTO_SALON_SKIP_STANDALONE_CHECK:-}" ]]; then
    echo "Standalone check: skipped by PHOTO_SALON_SKIP_STANDALONE_CHECK"
else
    verify_standalone "_build_win/photo-salon.exe"
fi

# Optional Authenticode signing.
#
# Method 1 — Azure Trusted Signing (preferred, no local cert needed):
#   Required env vars:
#     AZURE_TRUSTED_SIGNING_ENDPOINT     — e.g. wus2.codesigning.azure.net (no https://)
#     AZURE_TRUSTED_SIGNING_ACCOUNT      — Trusted Signing account name in Azure Portal
#     AZURE_TRUSTED_SIGNING_CERT_PROFILE — certificate profile name within that account
#   Authentication: run `az login` first (or set AZURE_TENANT_ID + AZURE_CLIENT_ID +
#   AZURE_CLIENT_SECRET for non-interactive/CI use).
#   Requires: jsign (brew install jsign), azure-cli (brew install azure-cli), Java 11+
#
# Method 2 — Local PFX certificate (self-signed or OV cert):
#   Set CODESIGN_CERT to the PFX path (default: codesign.pfx in repo root).
#   Set CODESIGN_PASSWORD to the PFX password (default: empty).
#   Requires: osslsigncode
#
# If neither method is available, signing is skipped silently.

EXE="_build_win/photo-salon.exe"
SIGNING_NAME="Photo Salon"
SIGNING_URL="https://github.com/adregner/photo-salon"

if [[ -n "${AZURE_TRUSTED_SIGNING_ENDPOINT:-}" \
   && -n "${AZURE_TRUSTED_SIGNING_ACCOUNT:-}" \
   && -n "${AZURE_TRUSTED_SIGNING_CERT_PROFILE:-}" ]] \
   && command -v jsign &>/dev/null && command -v az &>/dev/null; then
  echo "Signing ${EXE} via Azure Trusted Signing..."
  # jsign requires a token scoped specifically to the Trusted Signing resource
  AZ_TOKEN="$(az account get-access-token \
    --resource https://codesigning.azure.net \
    --query accessToken -o tsv)"
  jsign \
    --storetype TRUSTEDSIGNING \
    --keystore "${AZURE_TRUSTED_SIGNING_ENDPOINT}" \
    --alias   "${AZURE_TRUSTED_SIGNING_ACCOUNT}/${AZURE_TRUSTED_SIGNING_CERT_PROFILE}" \
    --storepass "${AZ_TOKEN}" \
    --tsaurl  "http://timestamp.acs.microsoft.com" \
    --name    "$SIGNING_NAME" \
    --url     "$SIGNING_URL" \
    "$EXE"
  echo "Signed: ${EXE}"
else
  CERT="${CODESIGN_CERT:-$(dirname "$0")/codesign.pfx}"
  if command -v osslsigncode &>/dev/null && [[ -f "$CERT" ]]; then
    echo "Signing ${EXE} with ${CERT}..."
    osslsigncode sign \
      -pkcs12 "$CERT" \
      -pass   "${CODESIGN_PASSWORD:-}" \
      -n      "$SIGNING_NAME" \
      -i      "$SIGNING_URL" \
      -ts     "http://timestamp.digicert.com" \
      -in     "$EXE" \
      -out    "${EXE}.signed"
    mv "${EXE}.signed" "$EXE"
    echo "Signed: ${EXE}"
  else
    echo "Skipping signing (no Azure Trusted Signing config or PFX cert found)"
  fi
fi
