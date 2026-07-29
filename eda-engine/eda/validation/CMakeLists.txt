# eda/validation/CMakeLists.txt
#
# P11 统一校验框架（11-01 统一校验中心 / D1 + DRC 集成 + 11-02 实时 DRC 联动）。
#
# 链接边界：eda_validation PUBLIC eda_core；DRC 规则额外链接 eda_pcb + eda_geometry。
#   - eda_core    : Object / Variant / Signal（P2 对象体系 + 信号系统）
#   - eda_pcb     : Board / Track / Pad / Via / Net（DRC 规则在 drc.cpp 内下转访问）
#   - eda_geometry: RTree / segment_segment_intersect / measure（DRC 邻域 + 精确距离）
#
# 解耦说明：
#   - 骨架（validation.cpp / erc.cpp）经 Object abstract API 查询，不依赖 pcb/sch 头。
#   - DRC 规则（drc.cpp）按 validation.h 顶部预留的集成路径，在 .cpp 内 include pcb 头并
#     is_class + static_cast 下转 Board。eda_pcb 自身 PUBLIC 链接 eda_geometry，故 DRC 规则
#     同时获得几何内核访问——这是"派生规则新增独立链接关系，不污染骨架"的落地。
#
# 集成时由主构建在 eda/CMakeLists.txt 追加：
#   add_subdirectory(validation)
# 并在 tests/CMakeLists.txt 追加 test_validation / test_drc / test_drc_live 目标。

add_library(eda_validation STATIC
    validation.cpp
    erc.cpp
    drc.cpp
    drc_live.cpp
)
target_include_directories(eda_validation PUBLIC ${EDA_ROOT})
target_link_libraries(eda_validation PUBLIC eda_core)
# DRC 规则直查 pcb::Board + geometry 内核（仅 drc.cpp 需要，PUBLIC 暴露给下游测试）。
target_link_libraries(eda_validation PUBLIC eda_pcb eda_geometry)
