# eda/schematic/editor/CMakeLists.txt
#
# P7 原理图编辑器 · 交互模块（工具模式 + 鼠标放置器件 / 画导线 / 放网络标号 / 移动 / 旋转 / 删除）。
#
# 依赖：
#   - eda_schematic : Schematic / Component / Wire / NetLabel（P7 原理图核心数据）
#   - eda_library   : Symbol（P6 符号，Component 引用源）
#   - eda_command   : UndoStack / Command（P3 命令栈，所有编辑动作命令化）
#   - eda_geometry  : Coord / Point / Box（P3 几何值类型）
#   - eda_scene     : Canvas2D（P1 立即模式画布，render_overlay 绘制覆盖层）
#   - eda_core      : Object / Signal / Variant / InputEvent / Vector2 / Color（P1/P2）
#   - eda_project   : generate_uuid（经 eda_schematic → eda_library → eda_project 传递）
#
# 注意：不依赖 eda_editor（GLFW 键码用本地常量），保持原理图编辑器模块独立。

add_library(eda_schematic_editor STATIC
    schematic_editor.cpp
)
target_include_directories(eda_schematic_editor PUBLIC ${EDA_ROOT})
target_link_libraries(eda_schematic_editor PUBLIC
    eda_schematic
    eda_library
    eda_command
    eda_geometry
    eda_scene
    eda_core
)
