add_executable(eda_main main.cpp)
target_link_libraries(eda_main PRIVATE
    eda_editor      # dock / inspector / command_palette / shortcuts
    eda_scene       # gui (Control/Container/Theme) + 2d
    eda_platform    # GlfwWindow
    eda_drivers     # RasterizerGL
    eda_core        # math / input / object
)
