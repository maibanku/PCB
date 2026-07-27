cmake_minimum_required(VERSION 3.20)
project(eda_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 所有 target 从仓库根解析 include：#include "core/math/vector2.h" 等
set(EDA_ROOT ${CMAKE_SOURCE_DIR})

include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0)
set(gtest_force_shared_runtime ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)


# GLFW（Task 5/7 窗口与 GL 上下文）
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# glad（本地预生成，GL 3.3 core loader）
add_library(glad STATIC ${CMAKE_SOURCE_DIR}/third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC ${CMAKE_SOURCE_DIR}/third_party/glad/include)
enable_testing()

# 模块（逐 Task 解锁）
add_subdirectory(core)        # Task 2/3
add_subdirectory(servers)     # Task 4
add_subdirectory(platform)    # Task 5
add_subdirectory(drivers)     # Task 6/7
add_subdirectory(scene)       # Task 8
add_subdirectory(main)        # Task 9

add_subdirectory(eda)
add_subdirectory(editor)
add_subdirectory(tests)
