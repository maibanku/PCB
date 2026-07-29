add_executable(eda_main main.cpp)
target_link_libraries(eda_main PRIVATE
    eda_editor          # dock / inspector / command_palette / shortcuts
    eda_scene           # gui (Control/Container/Theme) + 2d
    eda_platform        # GlfwWindow
    eda_drivers         # RasterizerGL
    eda_core            # math / input / object / signal / ref
    # —— P8 编辑器交互 + P11 实时 DRC + P13 制造输出 + P5 工程命令 ——
    eda_pcb_editor      # PcbEditor（PUBLIC 拉 eda_pcb / eda_command / eda_geometry）
    eda_validation      # DrcLiveController（PUBLIC 拉 eda_pcb / eda_geometry）
    eda_manufacturing   # GerberExporter（gerber_export.cpp 内调 Board 方法，需显式 eda_pcb）
    eda_pcb             # Board / Track / Pad / Via（gerber_export 符号解析）
    eda_project         # ProjectTree / register_project_commands
    eda_command         # UndoStack（显式；eda_pcb_editor 已传递）
)
