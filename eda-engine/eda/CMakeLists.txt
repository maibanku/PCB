# eda/CMakeLists.txt
#
# P3 EDA 业务核心层入口。承载几何内核；Task 2 起引入首个 .cpp（arc_tessellate），
# eda_geometry 由 INTERFACE 升级为 STATIC。后续 Task 3~7 在下方 STATIC 列表追加源文件。
#
# 约定（D1/D2）：内部坐标统一 int64_t / nm；业务层 include 从仓库根解析，
# 例：#include "eda/geometry/types.h"。
#
# 依赖（仅 P1）：eda_core（类型层暂不实际引用 Vector2/Transform2D，仅链接关系占位）。
# 不依赖 P2 Object/Variant——几何内核与命令栈解耦，可独立开发。

add_library(eda_geometry STATIC
    geometry/arc_tessellate.cpp)
target_include_directories(eda_geometry PUBLIC ${EDA_ROOT})
target_link_libraries(eda_geometry PUBLIC eda_core)

# Task 3+ 追加源文件到上方 STATIC 列表：
#   geometry/boolean.cpp      (Task 3，需链接 Clipper2)
#   geometry/offset.cpp       (Task 4，需链接 Clipper2)
#   geometry/triangulate.cpp  (Task 5，需链接 earcut)
#   geometry/index.cpp        (Task 6)
#   geometry/intersect.cpp    (Task 7)
#   geometry/measure.cpp      (Task 7)
# 届时在 target_link_libraries 追加 Clipper2 / earcut（PUBLIC，透传给业务层）。
