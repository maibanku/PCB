# eda/CMakeLists.txt
#
# P3 EDA 业务核心层入口。当前仅承载几何内核（Task 1）的 header-only / constexpr 工具，
# 故 eda_geometry 先建为 INTERFACE 库；Task 2 引入 arc_tessellate.cpp 后改为 STATIC 并追加源文件。
#
# 约定（D1/D2）：内部坐标统一 int64_t / nm；业务层 include 从仓库根解析，
# 例：#include "eda/geometry/types.h"。
#
# 依赖（仅 P1）：eda_core（类型层暂不实际引用 Vector2/Transform2D，仅链接关系占位）。
# 不依赖 P2 Object/Variant——几何内核与命令栈解耦，可独立开发。

add_library(eda_geometry INTERFACE)
target_include_directories(eda_geometry INTERFACE ${EDA_ROOT})
target_link_libraries(eda_geometry INTERFACE eda_core)

# Task 2+ 引入 .cpp 后切换为 STATIC（届时取消下方注释、删除上方 INTERFACE 块）：
#
# add_library(eda_geometry STATIC
#     geometry/arc_tessellate.cpp
#     geometry/boolean.cpp
#     geometry/offset.cpp
#     geometry/triangulate.cpp
#     geometry/index.cpp
#     geometry/intersect.cpp
#     geometry/measure.cpp)
# target_include_directories(eda_geometry PUBLIC ${EDA_ROOT})
# target_link_libraries(eda_geometry PUBLIC eda_core Clipper2 earcut)
