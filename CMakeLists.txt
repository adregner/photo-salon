cmake_minimum_required(VERSION 3.16)
project(photo-salon VERSION 0.1.0 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network)
qt_standard_project_setup()
enable_testing()

# --- Extra image-format decoders -------------------------------------------
# Qt's own image plugins cover JPEG/PNG/TIFF/WebP/… but not HEIF/HEIC or the
# JPEG 2000 family (.jpf/.jpx/.jp2). Those are decoded by libheif and OpenJPEG,
# wrapped in the static Qt image plugins under src/imageformats/. CMake
# auto-imports static plugins into everything that links photo-salon-lib, so
# QImageReader — and every caller of supportedExtensions() — gains the formats
# with no call-site changes.
#
# Both are optional for development: a missing library drops just that format,
# with a warning. Release builds turn that warning into a hard error — see
# PHOTO_SALON_REQUIRE_CODECS below — so a published binary always opens every
# format the app advertises.
find_package(PkgConfig QUIET)

# Set by the release pipelines (`./build` and `build-windows.sh` forward the
# environment variable of the same name; `bundle-macos.sh` always sets it).
option(PHOTO_SALON_REQUIRE_CODECS
    "Fail configuration if an image codec is missing — for release builds" OFF)

# Reports a codec that could not be found: fatal for a release build, a warning
# otherwise, with the same install hints either way.
function(photo_salon_codec_missing name formats hints)
    set(reason "${name} not found — ${formats} files will not open.\n${hints}\
  Windows: see doc/WINDOWS.md § Image codec libraries")
    if(PHOTO_SALON_REQUIRE_CODECS)
        message(FATAL_ERROR "${reason}\n"
            "PHOTO_SALON_REQUIRE_CODECS is ON, so this build refuses to produce a "
            "release binary without ${name}. Unset it to build without the format.")
    endif()
    message(WARNING "${reason}")
endfunction()

# Locates an optional codec — pkg-config first, then a plain header/library
# search. Sets <prefix>_FOUND / _INCLUDE_DIRS / _LIBRARIES in the caller's
# scope, always as *absolute* library paths: the plugins are static libraries,
# and a static target propagates its libraries to the final link but not its
# link directories, so a bare `-lheif` would go looking in the default paths
# only (fine on Linux, "library 'heif' not found" against Homebrew on macOS).
# Any extra arguments are header search path suffixes. Pre-setting
# <prefix>_INCLUDE_DIR and <prefix>_LIBRARY on the command line points it at a
# specific build — the only way in when cross-compiling.
function(photo_salon_find_codec prefix pc_module header library)
    # pkg-config answers for the host, so it must not be consulted when
    # cross-compiling: it would hand a Windows link the host's Linux libraries.
    if(PkgConfig_FOUND AND NOT CMAKE_CROSSCOMPILING)
        pkg_check_modules(PC_${prefix} QUIET ${pc_module})
    endif()
    # _LINK_LIBRARIES is pkg-config's library list already resolved to full paths.
    if(PC_${prefix}_FOUND AND PC_${prefix}_LINK_LIBRARIES)
        set(${prefix}_FOUND        TRUE                             PARENT_SCOPE)
        set(${prefix}_INCLUDE_DIRS "${PC_${prefix}_INCLUDE_DIRS}"    PARENT_SCOPE)
        set(${prefix}_LIBRARIES    "${PC_${prefix}_LINK_LIBRARIES}"  PARENT_SCOPE)
        return()
    endif()
    find_path(${prefix}_INCLUDE_DIR NAMES ${header} PATH_SUFFIXES ${ARGN})
    find_library(${prefix}_LIBRARY NAMES ${library} HINTS ${PC_${prefix}_LIBRARY_DIRS})
    if(${prefix}_INCLUDE_DIR AND ${prefix}_LIBRARY)
        set(${prefix}_FOUND        TRUE                      PARENT_SCOPE)
        set(${prefix}_INCLUDE_DIRS "${${prefix}_INCLUDE_DIR}" PARENT_SCOPE)
        set(${prefix}_LIBRARIES    "${${prefix}_LIBRARY}"     PARENT_SCOPE)
    else()
        set(${prefix}_FOUND FALSE PARENT_SCOPE)
    endif()
endfunction()

photo_salon_find_codec(HEIF libheif libheif/heif.h heif)
photo_salon_find_codec(OPENJPEG libopenjp2 openjpeg.h openjp2
    openjpeg-2.5 openjpeg-2.4 openjpeg-2.3 openjpeg)

if(HEIF_FOUND)
    qt_add_plugin(photo-salon-heif STATIC CLASS_NAME HeifPlugin)
    target_sources(photo-salon-heif PRIVATE
        src/imageformats/HeifPlugin.cpp
        src/imageformats/HeifPlugin.h
    )
    target_include_directories(photo-salon-heif
        PUBLIC  src/imageformats
        PRIVATE ${HEIF_INCLUDE_DIRS})
    target_link_libraries(photo-salon-heif PUBLIC Qt6::Gui PRIVATE ${HEIF_LIBRARIES})
    # On Windows heif.h decorates its API with __declspec(dllimport) unless told
    # the library is static — and the vendored windows/codecs build is static, so
    # without this every heif_* call looks for a non-existent __imp_ thunk.
    #
    # That static libheif also needs libde265 named explicitly: it references
    # de265_* but records no /DEFAULTLIB for it, so nothing would pull it in.
    # Everywhere else libheif is a shared library that carries the dependency itself.
    if(WIN32)
        target_compile_definitions(photo-salon-heif PRIVATE LIBHEIF_STATIC_BUILD)
        find_library(DE265_LIBRARY NAMES libde265 de265)
        if(NOT DE265_LIBRARY)
            message(FATAL_ERROR
                "libheif was found but libde265 was not. The static Windows libheif "
                "cannot decode HEIC without it — see doc/WINDOWS.md § Image codec libraries.")
        endif()
        target_link_libraries(photo-salon-heif PRIVATE ${DE265_LIBRARY})
    endif()
else()
    photo_salon_codec_missing(libheif "HEIF/HEIC"
        "  macOS:  brew install libheif\n  Linux:  apt install libheif-dev  (or libheif-devel)\n")
endif()

if(OPENJPEG_FOUND)
    qt_add_plugin(photo-salon-jpeg2000 STATIC CLASS_NAME Jpeg2000Plugin)
    target_sources(photo-salon-jpeg2000 PRIVATE
        src/imageformats/Jpeg2000Plugin.cpp
        src/imageformats/Jpeg2000Plugin.h
    )
    target_include_directories(photo-salon-jpeg2000
        PUBLIC  src/imageformats
        PRIVATE ${OPENJPEG_INCLUDE_DIRS})
    target_link_libraries(photo-salon-jpeg2000 PUBLIC Qt6::Gui PRIVATE ${OPENJPEG_LIBRARIES})
    # Same story as LIBHEIF_STATIC_BUILD above: openjpeg.h defaults to
    # __declspec(dllimport) on Windows unless OPJ_STATIC says otherwise. Keep it
    # Windows-only — on ELF/Mach-O this macro instead forces hidden visibility,
    # which is wrong for the shared OpenJPEG we link against there.
    if(WIN32)
        target_compile_definitions(photo-salon-jpeg2000 PRIVATE OPJ_STATIC)
    endif()
else()
    photo_salon_codec_missing(OpenJPEG "JPEG 2000 (.jpf/.jpx/.jp2)"
        "  macOS:  brew install openjpeg\n  Linux:  apt install libopenjp2-7-dev  (or openjpeg2-devel)\n")
endif()

if(PHOTO_SALON_REQUIRE_CODECS)
    message(STATUS "Release build: HEIF and JPEG 2000 codecs found, both plugins will ship")
endif()

# easyexif — single-file JPEG EXIF parser, no external dependencies
include(FetchContent)
FetchContent_Declare(easyexif
    GIT_REPOSITORY https://github.com/mayanklahiri/easyexif.git
    GIT_TAG        cd994a3b6009bc3c1f84062e96bd7f5ad16e85f6
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(easyexif)

# Build-time version info — generates version.h in the cmake binary dir
find_package(Git QUIET)
set(GIT_COMMIT "")
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
set(RELEASE_TAG "")
if(DEFINED ENV{GITHUB_ACTIONS} AND "$ENV{GITHUB_REF_TYPE}" STREQUAL "tag")
    set(RELEASE_TAG "$ENV{GITHUB_REF_NAME}")
endif()
string(TIMESTAMP BUILD_EPOCH "%s" UTC)
configure_file(src/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)

add_library(photo-salon-lib STATIC
    src/MainWindow.cpp
    src/ImageViewer.cpp
    src/ImagePane.cpp
    src/CompareTabBar.cpp
    src/HelpOverlay.cpp
    src/ImageFormats.cpp
    src/BackgroundColorPicker.cpp
    src/BwConverter.cpp
    src/BwPanel.cpp
    src/ImageAdjust.cpp
    src/AdjustPanel.cpp
    src/RotateGeometry.cpp
    src/RotatePanel.cpp
    src/ImageEdit.cpp
    src/EditManifest.cpp
    src/ExitOverlay.cpp
    src/ExifReader.cpp
    src/ExifOverlay.cpp
    ${easyexif_SOURCE_DIR}/exif.cpp
)
if(APPLE)
    target_sources(photo-salon-lib PRIVATE src/OpenDialog.mm)
    target_link_libraries(photo-salon-lib PUBLIC "-framework AppKit" "-framework UniformTypeIdentifiers")
else()
    target_sources(photo-salon-lib PRIVATE src/OpenDialog.cpp)
endif()

if(APPLE)
    target_sources(photo-salon-lib PRIVATE src/ExternalLauncher.mm)
else()
    target_sources(photo-salon-lib PRIVATE src/ExternalLauncher.cpp)
endif()

target_include_directories(photo-salon-lib PUBLIC src)
target_include_directories(photo-salon-lib PRIVATE ${easyexif_SOURCE_DIR})
target_link_libraries(photo-salon-lib PUBLIC Qt6::Widgets Qt6::Concurrent Qt6::Network)

# Linking the static image plugins here pulls their auto-generated Q_IMPORT_PLUGIN
# initializers into the app and every test binary.
if(HEIF_FOUND)
    target_link_libraries(photo-salon-lib PUBLIC photo-salon-heif)
    target_compile_definitions(photo-salon-lib PUBLIC PHOTO_SALON_HAVE_HEIF)
endif()
if(OPENJPEG_FOUND)
    target_link_libraries(photo-salon-lib PUBLIC photo-salon-jpeg2000)
    target_compile_definitions(photo-salon-lib PUBLIC PHOTO_SALON_HAVE_JPEG2000)
endif()

qt_add_executable(photo-salon src/main.cpp)
target_include_directories(photo-salon PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(photo-salon PRIVATE photo-salon-lib)
if(WIN32)
    set_target_properties(photo-salon PROPERTIES WIN32_EXECUTABLE TRUE)
    if(CMAKE_CROSSCOMPILING)
        # llvm-rc compiles the .rc on macOS/Linux for the Windows target.
        # lld-link accepts .res files directly as linker input.
        find_program(LLVM_RC NAMES llvm-rc llvm-rc-19 llvm-rc-18
            HINTS /opt/homebrew/opt/llvm/bin /usr/lib/llvm-19/bin /usr/bin)
        if(LLVM_RC)
            set(_win_rc  "${CMAKE_CURRENT_SOURCE_DIR}/windows/photo-salon.rc")
            set(_win_res "${CMAKE_CURRENT_BINARY_DIR}/photo-salon.res")
            add_custom_command(
                OUTPUT  "${_win_res}"
                COMMAND "${LLVM_RC}"
                        "/I${CMAKE_CURRENT_SOURCE_DIR}/windows"
                        "/FO${_win_res}"
                        -- "${_win_rc}"
                DEPENDS "${_win_rc}" "${CMAKE_CURRENT_SOURCE_DIR}/windows/SalonViewer.ico"
                COMMENT "Compiling Windows resources"
            )
            target_sources(photo-salon PRIVATE "${_win_res}")
        endif()
    else()
        target_sources(photo-salon PRIVATE windows/photo-salon.rc)
    endif()
endif()
if(APPLE)
    target_sources(photo-salon PRIVATE macos/SalonViewer.icns)
    set_source_files_properties(macos/SalonViewer.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(photo-salon PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Photo Salon"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.aregner.photo-salon"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © 2024 Andrew Regner"
        MACOSX_BUNDLE_ICON_FILE "SalonViewer"
    )
endif()

add_subdirectory(tests)
