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
