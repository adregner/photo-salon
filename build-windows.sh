#!/usr/bin/env bash
set -euo pipefail

"$(dirname "$0")/fetch-windows-deps.sh"

cmake -B _build_win --toolchain cmake/toolchains/windows-x86_64-clang-cl.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build _build_win

# Optional Authenticode signing via osslsigncode.
# Set CODESIGN_CERT to the path of your PFX file (default: codesign.pfx in repo root).
# Set CODESIGN_PASSWORD to the PFX password (default: empty).
# If osslsigncode is not installed or the cert file is absent, signing is skipped silently.
EXE="_build_win/photo-salon.exe"
CERT="${CODESIGN_CERT:-$(dirname "$0")/codesign.pfx}"

if command -v osslsigncode &>/dev/null && [[ -f "$CERT" ]]; then
  echo "Signing ${EXE} with ${CERT}..."
  osslsigncode sign \
    -pkcs12 "$CERT" \
    -pass "${CODESIGN_PASSWORD:-}" \
    -n "Photo Salon" \
    -i "https://github.com/adregner/photo-salon" \
    -ts http://timestamp.digicert.com \
    -in  "$EXE" \
    -out "${EXE}.signed"
  mv "${EXE}.signed" "$EXE"
  echo "Signed: ${EXE}"
else
  echo "Skipping signing (osslsigncode not found or ${CERT} missing)"
fi
