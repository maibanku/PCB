add_library(eda_scene STATIC
    2d/canvas_2d.cpp
    main/node.cpp
    main/scene_tree.cpp
    gui/control.cpp
    gui/container.cpp
    gui/theme.cpp
    gui/control_render.cpp
    gui/widgets/label.cpp
    gui/widgets/button.cpp
    gui/widgets/icon_button.cpp
    gui/widgets/separator.cpp
    gui/widgets/title_bar.cpp
    gui/widgets/status_bar.cpp
    gui/widgets/menu_item.cpp
    gui/widgets/menu.cpp
    gui/widgets/menu_bar.cpp
    gui/i18n/translation.cpp
    gui/i18n/po_loader.cpp
    gui/i18n/po_extractor.cpp
    gui/fonts/font_provider.cpp
    gui/fonts/text_renderer.cpp
)
target_include_directories(eda_scene PUBLIC ${EDA_ROOT})
# stb_truetype.h：font_provider.cpp 以 STB_TRUETYPE_IMPLEMENTATION 私有包含，
# 仅本模块需要其头路径（不污染 PUBLIC 下游）。
target_include_directories(eda_scene PRIVATE ${EDA_ROOT}/third_party/stb)
target_link_libraries(eda_scene PUBLIC eda_servers eda_core)
