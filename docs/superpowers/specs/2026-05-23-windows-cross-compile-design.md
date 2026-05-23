# Windows Cross-Compilation Design

## Overview

Cross-compile `photo-salon` into a single static Windows `.exe` from macOS or Linux, using Zig as the cross-compiler and a from-source static build of Qt 6.11.1. No DLLs to distribute — the Qt libraries, platform plugin, and C++ runtime are all linked into the binary.

Target: `x86_64-windows-gnu` (MinGW ABI via Zig).

---

## Architecture

Three parts:

1. **One-time Qt build script** — downloads `qtbase` source, cross-compiles it to `x86_64-windows-gnu` using Zig, installs static archives to `~/dev/personal/qt-win-static/6.11.1/mingw64/`.

2. **Zig wrapper scripts + CMake toolchain file** — thin shell wrappers append `-target x86_64-windows-gnu` to every Zig invocation. The toolchain file configures CMake's cross-compile mode and points `find_package(Qt6)` at the static install.

3. **Unchanged app `CMakeLists.txt`** — `qt_add_executable` with a static Qt automatically handles static plugin import (including `qwindows`) via `qt_finalize_executable`. No source changes required.

---

## New Files

```
scripts/
  build-qt-static-windows.sh       # one-time Qt source build
cmake/
  zig-cc.sh                         # wrapper: exec zig cc -target x86_64-windows-gnu "$@"
  zig-cxx.sh                        # wrapper: exec zig c++ -target x86_64-windows-gnu "$@"
  zig-ar.sh                         # wrapper: exec zig ar "$@"
  toolchains/
    windows-x86_64-zig.cmake        # CMake cross-compile toolchain
```

---

## Qt Source Build Script

`scripts/build-qt-static-windows.sh`:

1. Downloads `qtbase-everywhere-src-6.11.1.tar.xz` from `https://download.qt.io/archive/qt/6.11/6.11.1/submodules/` into `_qt_build/` (skips if already present).
2. Extracts the tarball.
3. Configures Qt's CMake build:
   - `--toolchain cmake/toolchains/windows-x86_64-zig.cmake`
   - `CMAKE_INSTALL_PREFIX` → `~/dev/personal/qt-win-static/6.11.1/mingw64`
   - `QT_HOST_PATH` → `$(brew --prefix qt)` for host moc/rcc/qlalr tools
   - `BUILD_SHARED_LIBS=OFF`
   - `CMAKE_BUILD_TYPE=Release`
   - `QT_BUILD_TESTS=OFF`, `QT_BUILD_EXAMPLES=OFF`
   - Disabled: dbus, sql, network, concurrent, xml, accessibility, testlib, printsupport, opengl
4. Builds with all available cores.
5. Installs to the prefix.

The script is idempotent: exits early if `~/dev/personal/qt-win-static/6.11.1/mingw64/include/QtCore` already exists. Prints elapsed time on completion.

`_qt_build/` is added to `.gitignore`.

---

## CMake Toolchain File

`cmake/toolchains/windows-x86_64-zig.cmake`:

- `CMAKE_SYSTEM_NAME Windows` / `CMAKE_SYSTEM_PROCESSOR AMD64`
- `CMAKE_C_COMPILER` / `CMAKE_CXX_COMPILER` — absolute paths to `cmake/zig-cc.sh` and `cmake/zig-cxx.sh` (resolved relative to the toolchain file using `CMAKE_CURRENT_LIST_DIR`)
- `CMAKE_AR` → `cmake/zig-ar.sh`
- `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY` — prevents CMake from attempting to run cross-compiled probe executables on the host
- `CMAKE_PREFIX_PATH` → `~/dev/personal/qt-win-static/6.11.1/mingw64`
- `CMAKE_FIND_ROOT_PATH` → same as prefix; `CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY` — ensures `find_package(Qt6)` finds the Windows static Qt and not Homebrew Qt

---

## App Build

No changes to `CMakeLists.txt` or source files.

```bash
# one-time setup (~30-50 min):
./scripts/build-qt-static-windows.sh

# configure (once per clean build dir):
cmake -B _build_win --toolchain cmake/toolchains/windows-x86_64-zig.cmake

# build:
cmake --build _build_win

# output:
_build_win/photo-salon.exe   # single static binary, no DLLs required
```

---

## Edge Cases

- **Qt build time**: 30–50 minutes first run. Script exits early on subsequent runs if install prefix is populated.
- **Host Qt version mismatch**: Homebrew Qt may differ from 6.11.1. Qt tolerates minor/patch mismatches for host tools. Script prints both versions for visibility.
- **Static plugin auto-import**: Qt 6's `qt_finalize_executable` links `QWindowsIntegrationPlugin` automatically when building against a static Qt. No `Q_IMPORT_PLUGIN` needed in app code.
- **Subsequent app builds**: `cmake -B _build_win --toolchain ...` only needed once; use `cmake --build _build_win` for incremental builds.

---

## Documentation Updates

- `CLAUDE.md`: Add Windows build section documenting the one-time setup and build commands.
- `README.md`: Add Windows cross-compilation instructions.
- `.gitignore`: Add `_qt_build/` and `_build_win/`.
