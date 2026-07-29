# Clipper2（直接编译源——FetchContent 顶层 CMakeLists 为空）
add_library(eda_clipper2 STATIC
    ${clipper2_SOURCE_DIR}/CPP/Clipper2Lib/src/clipper.engine.cpp
    ${clipper2_SOURCE_DIR}/CPP/Clipper2Lib/src/clipper.offset.cpp
    ${clipper2_SOURCE_DIR}/CPP/Clipper2Lib/src/clipper.rectclip.cpp
)
target_include_directories(eda_clipper2 PUBLIC ${clipper2_SOURCE_DIR}/CPP/Clipper2Lib/include)

# eda_geometry（含 Clipper2 布尔/偏移 + earcut 剖分）
add_library(eda_geometry STATIC
    geometry/arc_tessellate.cpp
    geometry/index.cpp
    geometry/intersect.cpp
    geometry/measure.cpp
    geometry/boolean.cpp
    geometry/offset.cpp
    geometry/triangulate.cpp
)
target_include_directories(eda_geometry PUBLIC ${EDA_ROOT})
target_link_libraries(eda_geometry PUBLIC eda_core eda_clipper2)
target_include_directories(eda_geometry PRIVATE ${CMAKE_SOURCE_DIR}/third_party/earcut/include)

# eda_project（含 attachment + archive）
add_library(eda_project STATIC
    project/project.cpp
    project/snapshot.cpp
    project/template_def.cpp
    project/template_library.cpp
    project/archive.cpp
    project/archive_manifest.cpp
    project/attachment.cpp
    project/attachment_manager.cpp
)
target_include_directories(eda_project PUBLIC ${EDA_ROOT})
target_link_libraries(eda_project PUBLIC eda_core)

# eda_command
add_library(eda_command STATIC
    command/command.cpp
    command/undo_stack.cpp
    command/selection.cpp
)
target_include_directories(eda_command PUBLIC ${EDA_ROOT})
target_link_libraries(eda_command PUBLIC eda_core)

# 业务子域（各自 CMakeLists）
add_subdirectory(library)
add_subdirectory(schematic)
add_subdirectory(pcb)
add_subdirectory(validation)
add_subdirectory(manufacturing)
