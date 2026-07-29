# eda/eco/CMakeLists.txt
#
# P14 ECO 工程变更订单 · 原理图 ↔ PCB 双向同步骨架（14-01 ~ 14-04）。
#
# 模块能力：
#   * EcoDiff::compute_diff  —— 比较原理图 Netlist 与 Board Net 列表，输出 EcoItem。
#   * EcoApplier::apply      —— 把 EcoItem 列表应用到 Board（命令化可选）。
#
# 链接边界：eda_eco PUBLIC eda_core + eda_schematic + eda_pcb + eda_command。
#   - eda_core     : Object / Variant 基础对象体系（P2）
#   - eda_schematic: Schematic::generate_netlist / Netlist / Net（P7）
#   - eda_pcb      : Board::nets / Net（P8）
#   - eda_command  : UndoStack / Command / MacroCommand（P3）
#
# 集成时由主构建在 eda/CMakeLists.txt 追加：
#   add_subdirectory(eco)
# 并在 tests/CMakeLists.txt 追加 test_eco 目标。

add_library(eda_eco STATIC
    eco_diff.cpp
    eco_applier.cpp
)
target_include_directories(eda_eco PUBLIC ${EDA_ROOT})
target_link_libraries(eda_eco PUBLIC
    eda_core
    eda_schematic
    eda_pcb
    eda_command
)
