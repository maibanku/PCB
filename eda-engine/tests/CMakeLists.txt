include(GoogleTest)
add_executable(test_smoke_build test_smoke_build.cpp)
target_include_directories(test_smoke_build PRIVATE ${EDA_ROOT})
target_link_libraries(test_smoke_build PRIVATE gtest_main)
gtest_discover_tests(test_smoke_build)

add_executable(test_math test_math.cpp)
target_link_libraries(test_math PRIVATE eda_core gtest_main)
gtest_discover_tests(test_math)

add_executable(test_input test_input.cpp)
target_link_libraries(test_input PRIVATE eda_core gtest_main)
gtest_discover_tests(test_input)

add_executable(test_canvas_vertex test_canvas_vertex.cpp)
target_link_libraries(test_canvas_vertex PRIVATE eda_servers gtest_main)
gtest_discover_tests(test_canvas_vertex)

add_executable(test_canvas_2d test_canvas_2d.cpp)
target_link_libraries(test_canvas_2d PRIVATE eda_scene gtest_main)
gtest_discover_tests(test_canvas_2d)

add_executable(test_object test_object.cpp)
target_link_libraries(test_object PRIVATE eda_core gtest_main)
gtest_discover_tests(test_object)

add_executable(test_class_db test_class_db.cpp)
target_link_libraries(test_class_db PRIVATE eda_core gtest_main)
gtest_discover_tests(test_class_db)

add_executable(test_signal test_signal.cpp)
target_link_libraries(test_signal PRIVATE eda_core gtest_main)
gtest_discover_tests(test_signal)

add_executable(test_geometry_types test_geometry_types.cpp)
target_link_libraries(test_geometry_types PRIVATE eda_geometry gtest_main)
gtest_discover_tests(test_geometry_types)

add_executable(test_variant test_variant.cpp)
target_link_libraries(test_variant PRIVATE eda_core gtest_main)
gtest_discover_tests(test_variant)

add_executable(test_resource test_resource.cpp)
target_link_libraries(test_resource PRIVATE eda_core gtest_main)
gtest_discover_tests(test_resource)

add_executable(test_node_scene_tree test_node_scene_tree.cpp)
target_link_libraries(test_node_scene_tree PRIVATE eda_scene gtest_main)
gtest_discover_tests(test_node_scene_tree)

add_executable(test_geometry_arc test_geometry_arc.cpp)
target_link_libraries(test_geometry_arc PRIVATE eda_geometry gtest_main)
gtest_discover_tests(test_geometry_arc)

add_executable(test_geometry_index test_geometry_index.cpp)
target_link_libraries(test_geometry_index PRIVATE eda_geometry gtest_main)
gtest_discover_tests(test_geometry_index)

add_executable(test_geometry_intersect test_geometry_intersect.cpp)
target_link_libraries(test_geometry_intersect PRIVATE eda_geometry gtest_main)
gtest_discover_tests(test_geometry_intersect)

add_executable(test_geometry_measure test_geometry_measure.cpp)
target_link_libraries(test_geometry_measure PRIVATE eda_geometry gtest_main)
gtest_discover_tests(test_geometry_measure)

add_executable(test_control test_control.cpp)
target_link_libraries(test_control PRIVATE eda_scene gtest_main)
gtest_discover_tests(test_control)

add_executable(test_project_tree test_project_tree.cpp)
target_link_libraries(test_project_tree PRIVATE eda_project gtest_main)
gtest_discover_tests(test_project_tree)

add_executable(test_command test_command.cpp)
target_link_libraries(test_command PRIVATE eda_command gtest_main)
gtest_discover_tests(test_command)

add_executable(test_container test_container.cpp)
target_link_libraries(test_container PRIVATE eda_scene gtest_main)
gtest_discover_tests(test_container)

add_executable(test_theme test_theme.cpp)
target_link_libraries(test_theme PRIVATE eda_scene gtest_main)
gtest_discover_tests(test_theme)

# P4 文本渲染首版：FontProvider（stb_truetype 集成）+ TextRenderer（Canvas2D 占位渲染）
add_executable(test_font test_font.cpp)
target_link_libraries(test_font PRIVATE eda_scene gtest_main)
gtest_discover_tests(test_font)

add_executable(test_snapshot test_snapshot.cpp)
target_link_libraries(test_snapshot PRIVATE eda_project gtest_main)
gtest_discover_tests(test_snapshot)

add_executable(test_dock test_dock.cpp)
target_link_libraries(test_dock PRIVATE eda_editor gtest_main)
gtest_discover_tests(test_dock)

add_executable(test_inspector test_inspector.cpp)
target_link_libraries(test_inspector PRIVATE eda_editor gtest_main)
gtest_discover_tests(test_inspector)

add_executable(test_command_palette test_command_palette.cpp)
target_link_libraries(test_command_palette PRIVATE eda_editor gtest_main)
gtest_discover_tests(test_command_palette)

# P4 菜单系统：MenuBar + Menu + MenuItem（scene/gui/widgets/menu*）。
# menu.cpp 内查 CommandRegistry（eda_editor 符号），故 link eda_editor（PUBLIC 带 eda_scene）。
add_executable(test_menu test_menu.cpp)
target_link_libraries(test_menu PRIVATE eda_editor gtest_main)
gtest_discover_tests(test_menu)

add_executable(test_template test_template.cpp)
target_link_libraries(test_template PRIVATE eda_project gtest_main)
gtest_discover_tests(test_template)

add_executable(test_shortcuts test_shortcuts.cpp)
target_link_libraries(test_shortcuts PRIVATE eda_editor gtest_main)
gtest_discover_tests(test_shortcuts)

add_executable(test_i18n test_i18n.cpp)
target_link_libraries(test_i18n PRIVATE eda_scene gtest_main)
gtest_discover_tests(test_i18n)

add_executable(test_archive test_archive.cpp)
target_link_libraries(test_archive PRIVATE eda_project gtest_main)
gtest_discover_tests(test_archive)

add_executable(test_geometry_boolean test_geometry_boolean.cpp)
target_link_libraries(test_geometry_boolean PRIVATE eda_geometry gtest_main)
gtest_discover_tests(test_geometry_boolean)

add_executable(test_geometry_offset test_geometry_offset.cpp)
target_link_libraries(test_geometry_offset PRIVATE eda_geometry gtest_main)
gtest_discover_tests(test_geometry_offset)

add_executable(test_geometry_triangulate test_geometry_triangulate.cpp)
target_link_libraries(test_geometry_triangulate PRIVATE eda_geometry gtest_main)
gtest_discover_tests(test_geometry_triangulate)

add_executable(test_attachment test_attachment.cpp)
target_link_libraries(test_attachment PRIVATE eda_project gtest_main)
gtest_discover_tests(test_attachment)




add_executable(test_validation test_validation.cpp)
target_link_libraries(test_validation PRIVATE eda_validation gtest_main)
gtest_discover_tests(test_validation)

# P11 DRC（设计规则检查）：ClearanceRule / MinWidthRule / ShortCircuitRule / UnroutedNetRule。
# eda_validation 已 PUBLIC 链接 eda_pcb + eda_geometry（drc.cpp 下转 Board + 用 R-tree/intersect）。
add_executable(test_drc test_drc.cpp)
target_link_libraries(test_drc PRIVATE eda_validation gtest_main)
gtest_discover_tests(test_drc)


add_executable(test_library test_library.cpp)
target_link_libraries(test_library PRIVATE eda_library gtest_main)
gtest_discover_tests(test_library)

add_executable(test_schematic test_schematic.cpp)
target_link_libraries(test_schematic PRIVATE eda_schematic gtest_main)
gtest_discover_tests(test_schematic)

add_executable(test_pcb test_pcb.cpp)
target_link_libraries(test_pcb PRIVATE eda_pcb gtest_main)
gtest_discover_tests(test_pcb)

add_executable(test_eco test_eco.cpp)
target_link_libraries(test_eco PRIVATE eda_eco gtest_main)
gtest_discover_tests(test_eco)

add_executable(test_gerber test_gerber.cpp)
target_link_libraries(test_gerber PRIVATE eda_manufacturing eda_pcb gtest_main)
gtest_discover_tests(test_gerber)

# P16 格式兼容 · KiCad .kicad_pcb 导入（eda_formats 库）
add_executable(test_kicad_import test_kicad_import.cpp)
target_link_libraries(test_kicad_import PRIVATE eda_formats gtest_main)
gtest_discover_tests(test_kicad_import)

# P3 Task 9：集成验收（跨模块交叉验证）+ 性能基准（独立 main，不入 ctest）
add_executable(test_geometry_integration test_geometry_integration.cpp)
target_link_libraries(test_geometry_integration PRIVATE eda_geometry eda_command gtest_main)
gtest_discover_tests(test_geometry_integration)

add_executable(test_geometry_benchmark test_geometry_benchmark.cpp)
target_link_libraries(test_geometry_benchmark PRIVATE eda_geometry eda_command)

# P4 Task 4：基础 Widget（Label / Button / IconButton / Separator）_draw 输出 +
# 渲染管线接通（render_subtree → Canvas2D 顶点缓冲）。
add_executable(test_widget test_widget.cpp)
target_link_libraries(test_widget PRIVATE eda_scene gtest_main)
gtest_discover_tests(test_widget)

# P4 widgets：工具栏 / 标题栏 / 状态栏。ToolBar 依赖 CommandRegistry（eda_editor），
# 故链接 eda_editor（PUBLIC 拉入 eda_scene → Label/Button/HBox 皆可用）。
add_executable(test_tool_bar test_tool_bar.cpp)
target_link_libraries(test_tool_bar PRIVATE eda_editor gtest_main)
gtest_discover_tests(test_tool_bar)
