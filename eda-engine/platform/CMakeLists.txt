add_library(eda_platform STATIC
    glfw/glfw_window.cpp
)
target_include_directories(eda_platform PUBLIC ${EDA_ROOT})
target_link_libraries(eda_platform PUBLIC glfw eda_core)
