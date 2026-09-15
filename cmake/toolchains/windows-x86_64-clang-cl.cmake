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

# Platform-specific LLVM tool locations and Qt host path.
#
# The vendored MSVC STL headers hard-require a minimum Clang version (14.51
# wants Clang 20 — see cmake/clang-cl-win.sh), so this floor moves together with
# MsvcToolset in windows/toolchain/versions.psd1.
set(_photo_salon_min_llvm 20)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(_llvm_bin "/opt/homebrew/opt/llvm/bin")
    # Homebrew's lld is keg-only and versioned; prefer whatever llvm ships, then
    # fall back to a versioned formula.
    find_program(_lld_link NAMES lld-link
        PATHS "${_llvm_bin}" /opt/homebrew/opt/lld/bin /opt/homebrew/opt/lld@21/bin
        NO_DEFAULT_PATH)
    set(CMAKE_LINKER "${_lld_link}")
    set(QT_HOST_PATH          "/opt/homebrew/opt/qt"            CACHE PATH "")
    set(QT_HOST_PATH_CMAKE_DIR "/opt/homebrew/opt/qt/lib/cmake" CACHE PATH "")
else()
    # Newest installed LLVM at or above the floor. Distro packages top out below
    # it, so this normally comes from apt.llvm.org — see doc/WINDOWS.md.
    if(DEFINED ENV{PHOTO_SALON_LLVM_BIN})
        set(_llvm_bin "$ENV{PHOTO_SALON_LLVM_BIN}")
    else()
        file(GLOB _llvm_dirs "/usr/lib/llvm-*")
        set(_llvm_bin "")
        set(_best 0)
        foreach(_d ${_llvm_dirs})
            string(REGEX MATCH "llvm-([0-9]+)$" _m "${_d}")
            if(_m AND CMAKE_MATCH_1 GREATER_EQUAL _photo_salon_min_llvm
                  AND CMAKE_MATCH_1 GREATER _best
                  AND EXISTS "${_d}/bin/lld-link")
                set(_best ${CMAKE_MATCH_1})
                set(_llvm_bin "${_d}/bin")
            endif()
        endforeach()
        if(NOT _llvm_bin)
            message(FATAL_ERROR
                "No LLVM >= ${_photo_salon_min_llvm} found under /usr/lib/llvm-*.\n"
                "The vendored MSVC STL headers require Clang ${_photo_salon_min_llvm} or newer "
                "(error STL1000 otherwise).\n"
                "  curl -fsSL https://apt.llvm.org/llvm.sh | sudo bash -s ${_photo_salon_min_llvm}\n"
                "  sudo apt-get install -y lld-${_photo_salon_min_llvm} llvm-${_photo_salon_min_llvm}\n"
                "Or set PHOTO_SALON_LLVM_BIN to a suitable bin directory.")
        endif()
    endif()
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
#
# /SUBSYSTEM is deliberately NOT forced here. CMake derives it per target from
# WIN32_EXECUTABLE, which CMakeLists.txt sets on photo-salon and leaves off the
# tests. Forcing /SUBSYSTEM:WINDOWS globally also made every test binary a GUI
# app with no console, so their output went nowhere and they could not be run.
set(CMAKE_EXE_LINKER_FLAGS_INIT "/MACHINE:X64 /MANIFEST:NO /LIBPATH:${_win_sdk_um} /LIBPATH:${_win_sdk_ucrt} /LIBPATH:${_msvc_lib}")

# Programs (moc, rcc) are host-native — find them outside the sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Packages, libraries, and headers come from the target sysroot only
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

