# eda/pcb/CMakeLists.txt
#
# P8 PCB 编辑器 · 对象树骨架（REQ-008）。
#
# 链接边界：eda_pcb PUBLIC eda_core + eda_geometry + eda_command。
#   - eda_core   : Object / Variant / ClassDB（P2 对象体系）
#   - eda_geometry: Coord / Point / Box / Path / Polygon / Polyline（P3 图元）
#   - eda_command : UndoStack / Command（P3 命令栈，PcbEntity::history 挂点）
#
# 注：本骨架暂不链接 eda_project——UUID 生成在 eda::pcb 命名空间内自建（generate_uuid），
#     避免 PCB 模块对工程树模块的反向依赖。集成阶段由顶层 CMakeLists 通过
#     add_subdirectory(eda/pcb) 挂接（不在本骨架改顶层 CMakeLists）。
#
# 集成时由主构建在 eda/CMakeLists.txt 追加：
#   add_subdirectory(pcb)
# 并在 tests/CMakeLists.txt 追加 test_pcb 目标。

add_library(eda_pcb STATIC
    board.cpp
)
target_include_directories(eda_pcb PUBLIC ${EDA_ROOT})
target_link_libraries(eda_pcb PUBLIC eda_core eda_geometry eda_command)
