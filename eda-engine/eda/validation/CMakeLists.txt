# eda/validation/CMakeLists.txt
#
# P11 统一校验框架 · 骨架（11-01 统一校验中心 / D1）。
#
# 链接边界：eda_validation PUBLIC eda_core。
#   - eda_core : Object / Variant / Signal（P2 对象体系 + 信号系统）
#   - 不链接 eda_pcb / eda_schematic —— ValidationContext 以 const Object* 泛化持有
#     board/schematic，规则经 Object abstract API 查询（解耦，与 eda/pcb 同样的
#     "反向依赖隔离"原则）。
#   - 集成阶段若派生规则需直查 pcb::Board，由该规则在 .cpp 内 include pcb 头并以
#     is_class + static_cast 下转，新增独立链接关系，不污染本骨架。
#
# 集成时由主构建在 eda/CMakeLists.txt 追加：
#   add_subdirectory(validation)
# 并在 tests/CMakeLists.txt 追加 test_validation 目标。

add_library(eda_validation STATIC
    validation.cpp
    erc.cpp
)
target_include_directories(eda_validation PUBLIC ${EDA_ROOT})
target_link_libraries(eda_validation PUBLIC eda_core)
