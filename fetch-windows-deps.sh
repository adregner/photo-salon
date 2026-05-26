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
echo "Done."
