add_library(eda_editor STATIC
    dock/dock_manager.cpp
    dock/dock_panel.cpp
    inspector/inspector.cpp
    inspector/property_editors.cpp
    command_palette/command_registry.cpp
    command_palette/command_palette.cpp
    shortcuts/input_map.cpp
    # ToolBar 物理位于 scene/gui/widgets/，但依赖 CommandRegistry（eda_editor），
    # 故编译入 eda_editor（与 dock_panel 同构：用 scene/gui 基础控件 + editor 命令系统）。
    ${EDA_ROOT}/scene/gui/widgets/tool_bar.cpp
)
target_include_directories(eda_editor PUBLIC ${EDA_ROOT})
target_link_libraries(eda_editor PUBLIC eda_scene eda_core)
