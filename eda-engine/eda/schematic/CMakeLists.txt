# eda/schematic/CMakeLists.txt
#
# P7 原理图核心数据模型（Schematic + Component + Wire + NetLabel + PowerPort + Netlist）。
# 对齐 docs/07-原理图编辑器/01-原理图图元 + 04-网络表生成 与 REQ-007 EDA Object Model。
#
# 注意：本文件为新建，按需求"不改顶层/tests CMakeLists.txt；不编译"。
# 当前未被顶层 CMakeLists.txt 通过 add_subdirectory 接入构建——
# 在 P7 后续 Task 把 eda_schematic 加入 eda/CMakeLists.txt 时启用。
#
# 集成方式（参考 eda/library、eda/pcb）：
#   target_include_directories(eda_schematic PUBLIC ${EDA_ROOT})
#   target_link_libraries(eda_schematic PUBLIC eda_core eda_geometry eda_library)
#
# 依赖传递：
#   - eda_core    : Object / Variant / ClassDB / RefCounted / Resource（P2 对象体系）
#   - eda_geometry: Coord / Point / Box / Polyline（P3 图元）
#   - eda_library : Symbol / Device / Pin / GraphicsElement（P6 元件库，由 Component 引用）
#   注：eda_library 传递依赖 eda_project，故 generate_uuid 可直接复用。

add_library(eda_schematic STATIC
    component.cpp
    schematic.cpp
)
target_include_directories(eda_schematic PUBLIC ${EDA_ROOT})
target_link_libraries(eda_schematic PUBLIC eda_core eda_geometry eda_library)
