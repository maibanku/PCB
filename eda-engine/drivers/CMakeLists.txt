add_library(eda_drivers STATIC
    opengl/rasterizer_gl.cpp
    vulkan/rasterizer_vk.cpp
)
target_include_directories(eda_drivers PUBLIC ${EDA_ROOT})
target_link_libraries(eda_drivers PUBLIC glfw eda_servers glad)
