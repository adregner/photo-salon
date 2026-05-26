cmake_minimum_required(VERSION 3.16)
project(photo-salon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()
enable_testing()

add_library(photo-salon-lib STATIC
    src/MainWindow.cpp
    src/ImageViewer.cpp
    src/HelpOverlay.cpp
    src/ImageFormats.cpp
)
target_include_directories(photo-salon-lib PUBLIC src)
target_link_libraries(photo-salon-lib PUBLIC Qt6::Widgets)

qt_add_executable(photo-salon src/main.cpp)
target_link_libraries(photo-salon PRIVATE photo-salon-lib)
if(WIN32)
    set_target_properties(photo-salon PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

add_subdirectory(tests)
