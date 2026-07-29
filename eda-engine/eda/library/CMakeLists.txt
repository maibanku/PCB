# eda/library/CMakeLists.txt
#
# P6 元件库核心数据模型（Symbol + Footprint + Device 一体化绑定）。
# 对齐 docs/06-元件库 与 REQ-007 EDA Object Model。
#
# 注意：本文件为新建，按需求"不改顶层/tests CMakeLists.txt；不编译"。
# 当前未被顶层 CMakeLists.txt 通过 add_subdirectory 接入构建——
# 在 P6 后续 Task 把 eda_library 加入 eda/CMakeLists.txt 时启用。
#
# 集成方式（参考 eda/geometry、eda/project）：
#   target_include_directories(eda_library PUBLIC ${EDA_ROOT})
#   target_link_libraries(eda_library PUBLIC eda_core eda_geometry eda_project)

add_library(eda_library STATIC
    symbol.cpp
    footprint.cpp
    device.cpp
)
target_include_directories(eda_library PUBLIC ${EDA_ROOT})
target_link_libraries(eda_library PUBLIC eda_core eda_geometry eda_project)
