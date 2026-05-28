set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Override link rules to bypass vs_link_exe manifest embedding (mt.exe / rc
# not available on macOS). Must be set before project() is called.
set(CMAKE_USER_MAKE_RULES_OVERRIDE
    "${CMAKE_CURRENT_LIST_DIR}/../WinCrossLinkRules.cmake" CACHE FILEPATH "")

set(_cmake_dir "${CMAKE_CURRENT_LIST_DIR}/..")

# clang-cl wrapper handles: target, C++17, and all Windows/MSVC include paths
set(CMAKE_C_COMPILER   "${_cmake_dir}/clang-cl-win.sh")
set(CMAKE_CXX_COMPILER "${_cmake_dir}/clang-cl-win.sh")

# Platform-specific LLVM tool locations and Qt host path
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(_llvm_bin "/opt/homebrew/opt/llvm/bin")
    set(CMAKE_LINKER "/opt/homebrew/opt/lld@21/bin/lld-link")
    set(QT_HOST_PATH          "/opt/homebrew/opt/qt"            CACHE PATH "")
    set(QT_HOST_PATH_CMAKE_DIR "/opt/homebrew/opt/qt/lib/cmake" CACHE PATH "")
else()
    # Linux CI — LLVM 18 from apt (clang-18 lld-18 llvm-18)
    set(_llvm_bin "/usr/lib/llvm-18/bin")
    set(CMAKE_LINKER "${_llvm_bin}/lld-link")
    set(QT_HOST_PATH          "/opt/qt-linux/6.11.1/gcc_64"            CACHE PATH "")
    set(QT_HOST_PATH_CMAKE_DIR "/opt/qt-linux/6.11.1/gcc_64/lib/cmake" CACHE PATH "")
endif()
set(CMAKE_AR "${_llvm_bin}/llvm-lib")

# Prevent CMake from trying to run cross-compiled test executables on the host
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Qt 6.11.1 static Windows install — pre-compiled on Windows, in-tree
get_filename_component(_project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_qt_prefix "${_project_root}/windows/qt-6.11/x64")
set(CMAKE_PREFIX_PATH    "${_qt_prefix}")
set(CMAKE_FIND_ROOT_PATH "${_qt_prefix}")

# lld-link flags: Windows SDK UM libraries, MSVC runtime libs, x64 machine, subsystem
set(_win_sdk_um   "${_project_root}/windows/sdk/lib/um")
set(_win_sdk_ucrt "${_project_root}/windows/sdk/lib/ucrt")
set(_msvc_lib     "${_project_root}/windows/msvc/lib")
set(CMAKE_EXE_LINKER_FLAGS_INIT "/MACHINE:X64 /SUBSYSTEM:WINDOWS /MANIFEST:NO /LIBPATH:${_win_sdk_um} /LIBPATH:${_win_sdk_ucrt} /LIBPATH:${_msvc_lib}")

# Programs (moc, rcc) are host-native — find them outside the sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Packages, libraries, and headers come from the target sysroot only
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

