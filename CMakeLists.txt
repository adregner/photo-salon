cmake_minimum_required(VERSION 3.16)
project(photo-salon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(photo-salon
    src/main.cpp
    src/MainWindow.cpp
    src/ImageViewer.cpp
)

target_include_directories(photo-salon PRIVATE src)
target_link_libraries(photo-salon PRIVATE Qt6::Widgets)
