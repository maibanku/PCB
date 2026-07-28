add_library(eda_editor STATIC
    dock/dock_manager.cpp
    dock/dock_panel.cpp
    inspector/inspector.cpp
    inspector/property_editors.cpp
    command_palette/command_registry.cpp
    command_palette/command_palette.cpp
    shortcuts/input_map.cpp
)
target_include_directories(eda_editor PUBLIC ${EDA_ROOT})
target_link_libraries(eda_editor PUBLIC eda_scene eda_core)
