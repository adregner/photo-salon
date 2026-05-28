#!/usr/bin/env bash
set -euo pipefail

"$(dirname "$0")/fetch-windows-deps.sh"

for dir in windows/msvc/lib windows/sdk/lib/ucrt windows/sdk/lib/um; do
  for f in "$dir"/*.lib; do
    base=$(basename "$f")
    upper=$(echo "$base" | tr '[:lower:]' '[:upper:]')
    if [ "$base" != "$upper" ] && [ ! -e "$dir/$upper" ]; then
      ln -s "$base" "$dir/$upper"
      echo "Created: $dir/$upper -> $base"
    fi
  done
done

cmake -B _build_win --toolchain cmake/toolchains/windows-x86_64-clang-cl.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build _build_win

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
