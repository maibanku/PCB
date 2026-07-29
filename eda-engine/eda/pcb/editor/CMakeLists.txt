# eda/pcb/editor/CMakeLists.txt
#
# P8 PCB 编辑器 · 交互模块（工具模式 + 鼠标选择/移动/旋转/删除）。
#
# 依赖：
#   - eda_pcb       : Board / Track / Pad / Via / PcbEntity（P8 对象树）
#   - eda_command   : UndoStack / Command（P3 命令栈，所有编辑动作命令化）
#   - eda_geometry  : Coord / Point / Box / Path（P3 几何值类型）
#   - eda_scene     : Canvas2D（P1 立即模式画布，render_overlay 绘制覆盖层）
#   - eda_core      : Object / Signal / Variant / InputEvent / Vector2（P1/P2）
#
# 注意：不依赖 eda_editor（GLFW 键码用本地常量），保持 PCB 编辑器模块独立。

add_library(eda_pcb_editor STATIC
    pcb_editor.cpp
)
target_include_directories(eda_pcb_editor PUBLIC ${EDA_ROOT})
target_link_libraries(eda_pcb_editor PUBLIC
    eda_pcb
    eda_command
    eda_geometry
    eda_scene
    eda_core
)
