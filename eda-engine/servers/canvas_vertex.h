#pragma once

#include "core/math/vector2.h"
#include "core/math/color.h"

namespace eda {

// 2D 画布顶点 —— Canvas2D 栅格化产物，驱动层的最小绘制载荷。
// 布局：Vector2(8) + Color(16) = 24 字节，与 drivers/opengl 及 drivers/vulkan
// 的顶点属性描述（location 0: vec2 pos，location 1: vec4 color）对齐。
struct CanvasVertex {
    Vector2 pos;   // 屏幕空间像素坐标
    Color color;
};

}  // namespace eda
