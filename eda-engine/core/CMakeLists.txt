add_library(eda_core STATIC
    math/vector2.cpp
    math/color.cpp
    math/rect2.cpp
    math/transform_2d.cpp
    input/input_state.cpp
    object/object.cpp
    object/ref_counted.cpp
    object/variant.cpp
    object/class_db.cpp
    object/signal.cpp
)
target_include_directories(eda_core PUBLIC ${EDA_ROOT})
