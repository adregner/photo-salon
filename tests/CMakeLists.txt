find_package(Qt6 REQUIRED COMPONENTS Test Widgets)

qt_add_executable(test_zoom test_zoom.cpp)
target_link_libraries(test_zoom PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_zoom COMMAND test_zoom)
set_tests_properties(test_zoom PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_help_overlay test_help_overlay.cpp)
target_link_libraries(test_help_overlay PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_help_overlay COMMAND test_help_overlay)
set_tests_properties(test_help_overlay PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_image_formats test_image_formats.cpp)
target_link_libraries(test_image_formats PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_image_formats COMMAND test_image_formats)
set_tests_properties(test_image_formats PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_folder_navigation test_folder_navigation.cpp)
target_link_libraries(test_folder_navigation PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_folder_navigation COMMAND test_folder_navigation)
set_tests_properties(test_folder_navigation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_open_folder test_open_folder.cpp)
target_link_libraries(test_open_folder PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_open_folder COMMAND test_open_folder)
set_tests_properties(test_open_folder PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_fullscreen test_fullscreen.cpp)
target_link_libraries(test_fullscreen PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_fullscreen COMMAND test_fullscreen)
set_tests_properties(test_fullscreen PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_background_color test_background_color.cpp)
target_link_libraries(test_background_color PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_background_color COMMAND test_background_color)
set_tests_properties(test_background_color PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_crop_tool test_crop_tool.cpp)
target_link_libraries(test_crop_tool PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_crop_tool COMMAND test_crop_tool)
set_tests_properties(test_crop_tool PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
