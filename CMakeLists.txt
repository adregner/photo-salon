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

# easyexif — single-file JPEG EXIF parser, no external dependencies
include(FetchContent)
FetchContent_Declare(easyexif
    GIT_REPOSITORY https://github.com/mayanklahiri/easyexif.git
    GIT_TAG        cd994a3b6009bc3c1f84062e96bd7f5ad16e85f6
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(easyexif)

add_library(photo-salon-lib STATIC
    src/MainWindow.cpp
    src/ImageViewer.cpp
    src/HelpOverlay.cpp
    src/ImageFormats.cpp
    src/BackgroundColorPicker.cpp
    src/BwConverter.cpp
    src/BwPanel.cpp
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
target_include_directories(photo-salon-lib PUBLIC src)
target_include_directories(photo-salon-lib PRIVATE ${easyexif_SOURCE_DIR})
target_link_libraries(photo-salon-lib PUBLIC Qt6::Widgets Qt6::Concurrent Qt6::Network)

qt_add_executable(photo-salon src/main.cpp)
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
