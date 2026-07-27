#pragma once

#include "core/math/vector2.h"

namespace eda {

// 轴对齐矩形（对应 Godot Rect2）：左上角 pos + 尺寸 size。
struct Rect2 {
    Vector2 pos;
    Vector2 size;
};

}  // namespace eda
