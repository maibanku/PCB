add_library(eda_geometry STATIC
    geometry/arc_tessellate.cpp
    geometry/index.cpp
    geometry/intersect.cpp
    geometry/measure.cpp
)
target_include_directories(eda_geometry PUBLIC ${EDA_ROOT})
target_link_libraries(eda_geometry PUBLIC eda_core)

add_library(eda_project STATIC
    project/project.cpp
    project/snapshot.cpp
)
target_include_directories(eda_project PUBLIC ${EDA_ROOT})
target_link_libraries(eda_project PUBLIC eda_core)

add_library(eda_command STATIC
    command/command.cpp
    command/undo_stack.cpp
    command/selection.cpp
)
target_include_directories(eda_command PUBLIC ${EDA_ROOT})
target_link_libraries(eda_command PUBLIC eda_core)
