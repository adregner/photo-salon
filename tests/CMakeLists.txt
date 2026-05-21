find_package(Qt6 REQUIRED COMPONENTS Test Widgets)

qt_add_executable(test_zoom test_zoom.cpp)
target_link_libraries(test_zoom PRIVATE Qt6::Test photo-salon-lib)

add_test(NAME test_zoom COMMAND test_zoom)
set_tests_properties(test_zoom PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
