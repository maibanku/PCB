include(GoogleTest)
add_executable(test_smoke_build test_smoke_build.cpp)
target_include_directories(test_smoke_build PRIVATE ${EDA_ROOT})
target_link_libraries(test_smoke_build PRIVATE gtest_main)
gtest_discover_tests(test_smoke_build)

add_executable(test_math test_math.cpp)
target_link_libraries(test_math PRIVATE eda_core gtest_main)
gtest_discover_tests(test_math)

add_executable(test_input test_input.cpp)
target_link_libraries(test_input PRIVATE eda_core gtest_main)
gtest_discover_tests(test_input)

add_executable(test_canvas_vertex test_canvas_vertex.cpp)
target_link_libraries(test_canvas_vertex PRIVATE eda_servers gtest_main)
gtest_discover_tests(test_canvas_vertex)

add_executable(test_canvas_2d test_canvas_2d.cpp)
target_link_libraries(test_canvas_2d PRIVATE eda_scene gtest_main)
gtest_discover_tests(test_canvas_2d)
