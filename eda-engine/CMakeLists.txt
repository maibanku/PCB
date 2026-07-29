cmake_minimum_required(VERSION 3.20)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(eda_engine LANGUAGES CXX)

# 统一 MSVC runtime（Debug=MDd / Release=MD）
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# ===== 强制 out-of-source build：禁止在源码根目录直接 cmake =====
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR
        "In-source build is forbidden.\n"
        "  cmake -B build              # configure\n"
        "  cmake --build build         # build\n"
        "  All artifacts go to build/bin and build/lib.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ===== 统一产物输出目录：build/<bin|lib> =====
# 可执行文件 → build/bin/eda_main.exe
# 静态库     → build/lib/*.lib
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
# VS 生成器每个配置单独输出(Debug/Release)，强制收敛到同一目录：
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg} "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg} "${CMAKE_BINARY_DIR}/lib")
endforeach()

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

# Clipper2（布尔/偏移，P3 T3/T4）
FetchContent_Declare(clipper2
    GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
    GIT_TAG Clipper2_1.3.0)
set(CLIPPER2_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLIPPER2_TESTS OFF CACHE BOOL "" FORCE)
set(CLIPPER2_USINGZ OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(clipper2)

enable_testing()

# 模块（逐 Task 解锁）
add_subdirectory(core)        # Task 2/3
add_subdirectory(servers)     # Task 4
add_subdirectory(platform)    # Task 5
add_subdirectory(drivers)     # Task 6/7
add_subdirectory(scene)       # Task 8
add_subdirectory(main)        # Task 9

# editor 先于 eda 处理：eda_project 经 project_commands.cpp 接入 CommandRegistry
# （eda_editor），需 eda_editor 目标在前。eda_editor 自身仅依赖 eda_scene + eda_core，
# 不依赖 eda/ 任何子目录，前置无循环。
add_subdirectory(editor)
add_subdirectory(eda)
add_subdirectory(tests)
