add_library(eda_scene STATIC
    2d/canvas_2d.cpp
    main/node.cpp
    main/scene_tree.cpp
    gui/control.cpp
    gui/container.cpp
    gui/theme.cpp
)
target_include_directories(eda_scene PUBLIC ${EDA_ROOT})
target_link_libraries(eda_scene PUBLIC eda_servers eda_core)
