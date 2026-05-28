#!/usr/bin/env bash
set -euo pipefail

"$(dirname "$0")/fetch-windows-deps.sh"

cmake -B _build_win --toolchain cmake/toolchains/windows-x86_64-clang-cl.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build _build_win

# Optional Authenticode signing.
#
# Method 1 — Azure Trusted Signing (preferred, no local cert needed):
#   Set AZURE_TRUSTED_SIGNING_ENDPOINT and AZURE_TRUSTED_SIGNING_CERT_PROFILE.
#   Authenticate via any Azure DefaultCredential source:
#     - Interactive:   az login
#     - Service principal: AZURE_TENANT_ID + AZURE_CLIENT_ID + AZURE_CLIENT_SECRET
#     - CI/managed identity: AZURE_CLIENT_ID alone (if running on Azure)
#   Requires: jsign (brew install jsign  /  java -jar jsign.jar)
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

if [[ -n "${AZURE_TRUSTED_SIGNING_ENDPOINT:-}" && -n "${AZURE_TRUSTED_SIGNING_CERT_PROFILE:-}" ]] \
   && command -v jsign &>/dev/null; then
  echo "Signing ${EXE} via Azure Trusted Signing..."
  jsign \
    --storetype TRUSTEDSIGNING \
    --keystore "${AZURE_TRUSTED_SIGNING_ENDPOINT}" \
    --alias   "${AZURE_TRUSTED_SIGNING_CERT_PROFILE}" \
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
