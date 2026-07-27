add_library(eda_servers INTERFACE)
target_include_directories(eda_servers INTERFACE ${EDA_ROOT})
target_link_libraries(eda_servers INTERFACE eda_core)
