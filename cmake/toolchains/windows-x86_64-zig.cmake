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
