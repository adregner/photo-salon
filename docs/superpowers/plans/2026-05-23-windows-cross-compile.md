# Windows Cross-Compilation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable cross-compilation of a single static `photo-salon.exe` from macOS using Zig and a from-source static Qt 6.11.1 build.

**Architecture:** Thin shell wrappers route all compiler invocations through `zig c++ -target x86_64-windows-gnu`. A CMake toolchain file sets cross-compile mode and points `find_package(Qt6)` at a locally-built static Qt. A one-time setup script downloads and compiles Qt's `qtbase` module statically. The app `CMakeLists.txt` is unchanged — Qt 6's `qt_finalize_executable` auto-imports the static Windows platform plugin.

**Tech Stack:** CMake 3.16+, Zig 0.16+, Qt 6.11.1 (compiled from source), Bash

**Important:** The Qt build script passes compiler flags directly (not via the app toolchain file). The app toolchain sets `CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY` which would interfere with Qt's own internal `find_package` calls during its build.

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `.gitignore` | Modify | ignore `_qt_build/` and `_build_win/` |
| `cmake/zig-cc.sh` | Create | `exec zig cc -target x86_64-windows-gnu "$@"` |
| `cmake/zig-cxx.sh` | Create | `exec zig c++ -target x86_64-windows-gnu "$@"` |
| `cmake/zig-ar.sh` | Create | `exec zig ar "$@"` |
| `cmake/zig-ranlib.sh` | Create | `exec zig ar s "$@"` (ranlib equivalent) |
| `cmake/toolchains/windows-x86_64-zig.cmake` | Create | CMake cross-compile toolchain for app builds |
| `scripts/build-qt-static-windows.sh` | Create | one-time Qt source download, configure, build, install |
| `CLAUDE.md` | Modify | add Windows build section |
| `README.md` | Modify | add Windows cross-compilation instructions |

---

### Task 1: Add .gitignore entries

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: Append scratch directories to .gitignore**

Add these two lines at the end of `.gitignore`:
```
_qt_build/
_build_win/
```

- [ ] **Step 2: Commit**

```bash
git add .gitignore
git commit -m "chore: ignore qt build scratch dir and windows build output"
```

---

### Task 2: Create Zig compiler wrapper scripts

**Files:**
- Create: `cmake/zig-cc.sh`
- Create: `cmake/zig-cxx.sh`
- Create: `cmake/zig-ar.sh`
- Create: `cmake/zig-ranlib.sh`

- [ ] **Step 1: Create cmake/zig-cc.sh**

```bash
#!/bin/bash
exec zig cc -target x86_64-windows-gnu "$@"
```

- [ ] **Step 2: Create cmake/zig-cxx.sh**

```bash
#!/bin/bash
exec zig c++ -target x86_64-windows-gnu "$@"
```

- [ ] **Step 3: Create cmake/zig-ar.sh**

```bash
#!/bin/bash
exec zig ar "$@"
```

- [ ] **Step 4: Create cmake/zig-ranlib.sh**

CMake invokes ranlib as `ranlib <archive>`. This wrapper translates that into `zig ar s <archive>` which updates the archive's symbol table — the equivalent operation.

```bash
#!/bin/bash
exec zig ar s "$@"
```

- [ ] **Step 5: Make all four scripts executable**

```bash
chmod +x cmake/zig-cc.sh cmake/zig-cxx.sh cmake/zig-ar.sh cmake/zig-ranlib.sh
```

- [ ] **Step 6: Smoke-test each wrapper**

```bash
./cmake/zig-cc.sh --version
./cmake/zig-cxx.sh --version
./cmake/zig-ar.sh --version
```

Expected: each prints a Zig version string. `zig-ranlib.sh` is not tested here because `zig ar s` requires an existing archive file.

- [ ] **Step 7: Commit**

```bash
git add cmake/zig-cc.sh cmake/zig-cxx.sh cmake/zig-ar.sh cmake/zig-ranlib.sh
git commit -m "build: add zig cross-compiler wrapper scripts for x86_64-windows-gnu"
```

---

### Task 3: Create CMake toolchain file

**Files:**
- Create: `cmake/toolchains/windows-x86_64-zig.cmake`

- [ ] **Step 1: Create the toolchains directory**

```bash
mkdir -p cmake/toolchains
```

- [ ] **Step 2: Create cmake/toolchains/windows-x86_64-zig.cmake**

`CMAKE_CURRENT_LIST_DIR` resolves to the directory containing this file (`cmake/toolchains/`). The `..` navigates up to `cmake/` where the wrapper scripts live.

```cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Wrapper scripts live one level up from this toolchain file (cmake/)
set(_zig_dir "${CMAKE_CURRENT_LIST_DIR}/..")

set(CMAKE_C_COMPILER   "${_zig_dir}/zig-cc.sh")
set(CMAKE_CXX_COMPILER "${_zig_dir}/zig-cxx.sh")
set(CMAKE_AR           "${_zig_dir}/zig-ar.sh")
set(CMAKE_RANLIB       "${_zig_dir}/zig-ranlib.sh")

# Prevent CMake from trying to run cross-compiled test executables on the host
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Qt 6.11.1 static Windows install
set(_qt_prefix "$ENV{HOME}/dev/personal/qt-win-static/6.11.1/mingw64")
set(CMAKE_PREFIX_PATH   "${_qt_prefix}")
set(CMAKE_FIND_ROOT_PATH "${_qt_prefix}")

# Programs (moc, rcc) are host-native — find them outside the sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Packages, libraries, and headers come from the target sysroot only
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

- [ ] **Step 3: Commit**

```bash
git add cmake/toolchains/windows-x86_64-zig.cmake
git commit -m "build: add CMake toolchain for Windows cross-compilation via Zig"
```

---

### Task 4: Create the Qt static build script

**Files:**
- Create: `scripts/build-qt-static-windows.sh`

- [ ] **Step 1: Create the scripts directory**

```bash
mkdir -p scripts
```

- [ ] **Step 2: Create scripts/build-qt-static-windows.sh**

Note: compiler flags are passed directly here (not via the app toolchain file) to avoid `CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY` interfering with Qt's internal `find_package` calls.

```bash
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
    -DBUILD_SHARED_LIBS=OFF \
    -DQT_BUILD_TESTS=OFF \
    -DQT_BUILD_EXAMPLES=OFF \
    -DQT_HOST_PATH="$HOST_QT_PATH" \
    -DQT_FEATURE_dbus=OFF \
    -DQT_FEATURE_sql=OFF \
    -DQT_FEATURE_network=OFF \
    -DQT_FEATURE_concurrent=OFF \
    -DQT_FEATURE_xml=OFF \
    -DQT_FEATURE_accessibility=OFF \
    -DQT_FEATURE_testlib=OFF \
    -DQT_FEATURE_printsupport=OFF \
    -DQT_FEATURE_opengl=OFF

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
```

- [ ] **Step 3: Make the script executable**

```bash
chmod +x scripts/build-qt-static-windows.sh
```

- [ ] **Step 4: Commit**

```bash
git add scripts/build-qt-static-windows.sh
git commit -m "build: add one-time Qt static Windows build script"
```

---

### Task 5: Run the Qt build (one-time setup)

**Files:** none — writes to `~/dev/personal/qt-win-static/` (external to repo)

This task takes 30-50 minutes. It cannot be skipped — Task 6 depends on the Qt install being present.

- [ ] **Step 1: Run the Qt build script**

```bash
./scripts/build-qt-static-windows.sh
```

Expected: script prints host/target Qt versions, downloads the tarball (~60 MB), configures, builds, installs, then prints elapsed time. Final line: `Done. Qt installed to ~/dev/personal/qt-win-static/6.11.1/mingw64`.

**If CMake configure fails:**
- "Could not find toolchain file" → verify `cmake/toolchains/windows-x86_64-zig.cmake` exists.
- Compiler detection failure before any `.cpp` is compiled → run `./cmake/zig-cxx.sh --version` from the project root to verify the wrapper works.
- "host Qt tools version mismatch" → the Homebrew Qt version is too far from 6.11.1. Install a matching version or remove `-DQT_HOST_PATH` and let Qt build its own host tools (adds ~15 min).

**If compilation fails mid-build:**
- Note the failing source file and error. Zig 0.16 / C++17 incompatibilities in Qt internals may require additional `-DQT_FEATURE_xxx=OFF` flags. Check Qt's bug tracker for the specific file.

- [ ] **Step 2: Verify the install**

```bash
ls ~/dev/personal/qt-win-static/6.11.1/mingw64/include/QtCore/qobject.h
ls ~/dev/personal/qt-win-static/6.11.1/mingw64/lib/libQt6Core.a
ls ~/dev/personal/qt-win-static/6.11.1/mingw64/plugins/platforms/
```

Expected:
- `qobject.h` exists.
- `libQt6Core.a` exists and is a static archive (not `libQt6Core.dll.a`).
- `platforms/` contains `libqwindows.a`.

---

### Task 6: Build photo-salon.exe

**Files:** none — no source or CMake changes needed.

- [ ] **Step 1: Configure CMake for the Windows target**

From the project root:
```bash
cmake -B _build_win --toolchain cmake/toolchains/windows-x86_64-zig.cmake
```

Expected: CMake prints the standard configuration summary including `Qt6 found` and the install prefix pointing to `~/dev/personal/qt-win-static/6.11.1/mingw64`.

**If `find_package(Qt6)` fails:** verify `~/dev/personal/qt-win-static/6.11.1/mingw64/lib/cmake/Qt6/Qt6Config.cmake` exists. If missing, the Qt install step did not complete — re-run `./scripts/build-qt-static-windows.sh`.

- [ ] **Step 2: Build**

```bash
cmake --build _build_win
```

Expected: compiles all sources and links `_build_win/photo-salon.exe`.

- [ ] **Step 3: Verify the output binary**

```bash
file _build_win/photo-salon.exe
```

Expected output contains: `PE32+ executable (GUI) x86-64, for MS Windows`

- [ ] **Step 4: Confirm no DLL dependencies on Qt**

```bash
# zig objdump is always available since zig is a build prerequisite
zig objdump -p _build_win/photo-salon.exe | grep -i "dll name" | grep -i qt || echo "No Qt DLL imports found — static link confirmed"
```

Expected: `No Qt DLL imports found — static link confirmed`

---

### Task 7: Update documentation

**Files:**
- Modify: `CLAUDE.md`
- Modify: `README.md`

- [ ] **Step 1: Add Windows build section to CLAUDE.md**

Add after the existing `## Build` section:

```markdown
## Windows Cross-Compilation

Produces a single static `photo-salon.exe` with no DLL dependencies. Requires [Zig](https://ziglang.org) and a one-time Qt build step (~40 min).

**One-time setup:**
```bash
./scripts/build-qt-static-windows.sh
```

Qt static libraries are installed to `~/dev/personal/qt-win-static/6.11.1/mingw64/`. Re-running the script is a no-op if the install already exists.

**Build:**
```bash
cmake -B _build_win --toolchain cmake/toolchains/windows-x86_64-zig.cmake
cmake --build _build_win
# → _build_win/photo-salon.exe  (single static binary, no DLLs required)
```

`_qt_build/` (Qt source/build scratch) and `_build_win/` (Windows output) are git-ignored.
```

- [ ] **Step 2: Add Windows section to README.md**

Read `README.md` first to find the right insertion point (after the existing build/run instructions). Add:

```markdown
## Building for Windows

Cross-compile a single static `photo-salon.exe` from macOS or Linux using [Zig](https://ziglang.org):

```bash
# One-time setup (~40 min — downloads and compiles Qt 6.11.1 statically):
./scripts/build-qt-static-windows.sh

# Build:
cmake -B _build_win --toolchain cmake/toolchains/windows-x86_64-zig.cmake
cmake --build _build_win
```

The result is `_build_win/photo-salon.exe` — a self-contained binary with no DLL dependencies.
```

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md README.md
git commit -m "docs: document Windows cross-compilation build process"
```
