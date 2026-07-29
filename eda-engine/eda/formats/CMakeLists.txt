# eda/formats/CMakeLists.txt
#
# P16 格式兼容 · 外部格式导入/导出库（骨架）。
#
# 当前覆盖：
#   - KiCadPcbParser  : .kicad_pcb S-expression → pcb::Board（手写递归下降）
#   - KiCadImporter   : 高层导入入口 + FidelityReport（保真度报告）
#
# 链接边界：
#   - eda_core    : Object / Variant（P2 对象体系）
#   - eda_geometry: Coord / Point / mm_to_nm（P3 几何内核）
#   - eda_pcb     : Board / Track / Pad / Via / Net（P8 PCB 编辑器）
#
# 集成时由主构建在 eda/CMakeLists.txt 追加：
#   add_subdirectory(formats)
# 并在 tests/CMakeLists.txt 追加 test_kicad_import 目标。

add_library(eda_formats STATIC
    kicad_pcb_parser.cpp
    kicad_importer.cpp
)
target_include_directories(eda_formats PUBLIC ${EDA_ROOT})
target_link_libraries(eda_formats PUBLIC eda_core eda_geometry eda_pcb)
