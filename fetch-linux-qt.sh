#!/usr/bin/env bash
set -euo pipefail

QT_VERSION="${QT_VERSION:-6.11.1}"
DEST="/opt/qt-linux"
QT_PREFIX="$DEST/$QT_VERSION/gcc_64"
QT_CMAKE_DIR="$QT_PREFIX/lib/cmake/Qt6"

echo "Checking Qt $QT_VERSION for Linux..."

if [ -d "$QT_CMAKE_DIR" ]; then
    echo "  skip  Qt $QT_VERSION (already present at $QT_PREFIX)"
    echo "CMAKE_PREFIX_PATH=$QT_PREFIX"
    exit 0
fi

# Ensure aqtinstall is available
if ! command -v aqt &>/dev/null; then
    if ! command -v pip3 &>/dev/null; then
        echo "Error: pip3 not found — install Python 3 and pip3 first." >&2
        exit 1
    fi
    echo "  installing aqtinstall via pip3..."
    pip3 install --break-system-packages --quiet --user aqtinstall
    # pip --user puts scripts here on most distros
    export PATH="$HOME/.local/bin:$PATH"
fi

if ! command -v aqt &>/dev/null; then
    echo "Error: aqt still not found after pip install." >&2
    echo "Add \$HOME/.local/bin to your PATH and retry." >&2
    exit 1
fi

mkdir -p "$DEST"
echo "  fetch Qt $QT_VERSION → $QT_PREFIX"
# qtimageformats adds the TIFF/WebP/etc. plugins on top of qtbase's jpeg/png/gif.
# TIFF is required by the "Open in..." external-editor export.
aqt install-qt --outputdir "$DEST" linux desktop "$QT_VERSION" linux_gcc_64 \
    --archives icu qtbase --modules qtimageformats

# aqt's prebuilt qtiff plugin links the older libtiff.so.5 soname, but modern
# Linux (Ubuntu 24.04+) ships libtiff.so.6. Drop a compat symlink into the Qt
# prefix lib dir — the plugin's RUNPATH is $ORIGIN/../../lib — so qtiff loads and
# the "Open in..." TIFF export works. Skipped if libtiff.so.5 already resolves.
if [ ! -e "$QT_PREFIX/lib/libtiff.so.5" ]; then
    sys_tiff="$(ldconfig -p 2>/dev/null | awk '/libtiff\.so\.6/{print $NF; exit}')"
    if [ -n "$sys_tiff" ]; then
        ln -sf "$sys_tiff" "$QT_PREFIX/lib/libtiff.so.5"
        echo "  link  libtiff.so.5 -> $sys_tiff (qtiff compat shim)"
    fi
fi

echo "Done."
echo ""
echo "To build with this Qt:"
echo "  cmake -B _build -DCMAKE_PREFIX_PATH=$QT_PREFIX"
echo "  cmake --build _build"
