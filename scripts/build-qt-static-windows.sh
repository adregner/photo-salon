#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

QT_VERSION="6.11.1"
QT_MAJOR_MINOR="6.11"
QT_INSTALL_PREFIX="$HOME/dev/personal/qt-win-static/$QT_VERSION/mingw64"
QT_SOURCE_URL="https://download.qt.io/archive/qt/$QT_MAJOR_MINOR/$QT_VERSION/submodules/qtbase-everywhere-src-$QT_VERSION.tar.xz"

SCRATCH_DIR="$PROJECT_DIR/_qt_build"
TARBALL="$SCRATCH_DIR/qtbase-everywhere-src-$QT_VERSION.tar.xz"
SOURCE_DIR="$SCRATCH_DIR/qtbase-everywhere-src-$QT_VERSION"
QT_BUILD_DIR="$SCRATCH_DIR/qtbase-build"

# --- Idempotency check ---
if [ -d "$QT_INSTALL_PREFIX/include/QtCore" ]; then
    echo "Static Qt already installed at $QT_INSTALL_PREFIX — skipping."
    exit 0
fi

# --- Prerequisites ---
command -v zig   >/dev/null 2>&1 || { echo "Error: zig not found in PATH" >&2; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "Error: cmake not found in PATH" >&2; exit 1; }
command -v curl  >/dev/null 2>&1 || { echo "Error: curl not found in PATH" >&2; exit 1; }

HOST_QT_PATH="$(brew --prefix qt 2>/dev/null || true)"
if [ -z "$HOST_QT_PATH" ] || [ ! -d "$HOST_QT_PATH" ]; then
    echo "Error: host Qt not found via 'brew --prefix qt'. Install with: brew install qt" >&2
    exit 1
fi

# Try qmake6 first, fall back to qmake
HOST_QT_VERSION="$("$HOST_QT_PATH/bin/qmake6" -query QT_VERSION 2>/dev/null \
    || "$HOST_QT_PATH/bin/qmake" -query QT_VERSION 2>/dev/null \
    || echo "unknown")"

echo "Host Qt:   $HOST_QT_PATH ($HOST_QT_VERSION)"
echo "Target Qt: $QT_VERSION"
if [ "$HOST_QT_VERSION" != "$QT_VERSION" ]; then
    echo "Warning: host/target Qt version mismatch. Minor/patch differences are usually fine."
    echo "         If the build fails with 'host Qt version' errors, install Qt $QT_VERSION via Homebrew."
fi

NPROC="$(sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

# --- Download ---
mkdir -p "$SCRATCH_DIR"
if [ ! -f "$TARBALL" ]; then
    echo "Downloading qtbase $QT_VERSION (~60 MB)..."
    curl -L --progress-bar -o "$TARBALL" "$QT_SOURCE_URL"
fi

# --- Extract ---
if [ ! -d "$SOURCE_DIR" ]; then
    echo "Extracting..."
    tar -xf "$TARBALL" -C "$SCRATCH_DIR"
fi

# --- Configure ---
echo "Configuring Qt static Windows build..."
START_TIME=$SECONDS

cmake -B "$QT_BUILD_DIR" -S "$SOURCE_DIR" \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=AMD64 \
    -DCMAKE_C_COMPILER="$PROJECT_DIR/cmake/zig-cc.sh" \
    -DCMAKE_CXX_COMPILER="$PROJECT_DIR/cmake/zig-cxx.sh" \
    -DCMAKE_AR="$PROJECT_DIR/cmake/zig-ar.sh" \
    -DCMAKE_RANLIB="$PROJECT_DIR/cmake/zig-ranlib.sh" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_INSTALL_PREFIX="$QT_INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew \
    -DBUILD_SHARED_LIBS=OFF \
    -DQT_BUILD_TESTS=OFF \
    -DQT_BUILD_EXAMPLES=OFF \
    -DQT_HOST_PATH="$HOST_QT_PATH" \
    -DFEATURE_force_bundled_libs=ON \
    -DQT_FEATURE_dbus=OFF \
    -DQT_FEATURE_sql=OFF \
    -DQT_FEATURE_network=OFF \
    -DQT_FEATURE_concurrent=OFF \
    -DQT_FEATURE_xml=OFF \
    -DQT_FEATURE_accessibility=OFF \
    -DQT_FEATURE_testlib=OFF \
    -DQT_FEATURE_printsupport=OFF \
    -DQT_FEATURE_direct2d=OFF \
    -DQT_FEATURE_zstd=OFF

# --- Build ---
echo "Building Qt (30-50 minutes on first run)..."
cmake --build "$QT_BUILD_DIR" --parallel "$NPROC"

# --- Install ---
echo "Installing..."
cmake --install "$QT_BUILD_DIR"

ELAPSED=$((SECONDS - START_TIME))
echo ""
echo "Done. Qt installed to $QT_INSTALL_PREFIX"
printf "Elapsed: %dm %ds\n" $((ELAPSED / 60)) $((ELAPSED % 60))
