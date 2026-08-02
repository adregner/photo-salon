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
    # Linux CI — LLVM 19 from apt (clang-19 lld-19 llvm-19)
    set(_llvm_bin "/usr/lib/llvm-19/bin")
    set(CMAKE_LINKER "${_llvm_bin}/lld-link")
    set(QT_HOST_PATH          "/opt/qt-linux/6.11.1/gcc_64"            CACHE PATH "")
    set(QT_HOST_PATH_CMAKE_DIR "/opt/qt-linux/6.11.1/gcc_64/lib/cmake" CACHE PATH "")
endif()
set(CMAKE_AR "${_llvm_bin}/llvm-lib")

# Prevent CMake from trying to run cross-compiled test executables on the host
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Qt 6.11.1 static Windows install — pre-compiled on Windows, fetched into the
# tree by fetch-windows-deps.sh. The major.minor is part of the path so a Qt
# bump lands in a new directory rather than silently reusing the old one; bump
# it here and in windows/toolchain/versions.psd1 together.
get_filename_component(_project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_qt_prefix "${_project_root}/windows/qt-6.11/x64")

# MSVC builds of libheif / libde265 / OpenJPEG, in the usual include+lib layout.
# See doc/WINDOWS.md § Image codecs.
set(_codec_prefix "${_project_root}/windows/codecs/x64")

set(CMAKE_PREFIX_PATH    "${_qt_prefix}" "${_codec_prefix}")
set(CMAKE_FIND_ROOT_PATH "${_qt_prefix}" "${_codec_prefix}")

# Static CRT. This is what makes photo-salon.exe standalone: with the default
# MultiThreadedDLL the binary imports msvcp140.dll and vcruntime140.dll, which
# come from the Visual C++ Redistributable and are not present on a stock
# Windows install. /MT links libcmt + libcpmt + libvcruntime + libucrt in
# instead, so nothing ships beside the .exe.
#
# The Qt in windows/qt-6.11 is built with -static-runtime to match. Mixing the
# two linkages in one binary produces duplicate-symbol and heap-mismatch
# failures, so this must stay in step with CrtLinkage in
# windows/toolchain/versions.psd1.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "")

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

