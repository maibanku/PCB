# eda/manufacturing/CMakeLists.txt
#
# P13 制造输出 · Gerber/Excellon/Pick&Place 写入器骨架。
#
# 三大写入器消费 pcb::Board 的图元（Track/Pad/Via/FootprintInstance），输出
# RS-274X Gerber、Excellon 钻孔、Pick&Place CSV 文本。
#
# 链接边界（按 P13 任务规范）：
#   - eda_core    : Object / Variant 基础对象体系（P2）
#   - eda_geometry: Coord / Point / units(nm→mm)（P3）
#
# 注：实现引用 eda::pcb::Board（eda/pcb/board.h），其方法符号定义在 eda_pcb。
#     集成时由主构建在 eda/CMakeLists.txt 追加：
#       add_subdirectory(manufacturing)
#     若需要在 manufacturing 内解析 Board 符号，应在 target_link_libraries
#     追加 eda_pcb（当前 P13 任务规范仅声明 eda_core + eda_geometry，
#     Board 符号解析延后到集成阶段统一处理——本骨架暂不编译，仅落源码）。

add_library(eda_manufacturing STATIC
    gerber_writer.cpp
    excellon_writer.cpp
    pick_place_writer.cpp
)
target_include_directories(eda_manufacturing PUBLIC ${EDA_ROOT})
target_link_libraries(eda_manufacturing PUBLIC eda_core eda_geometry)
