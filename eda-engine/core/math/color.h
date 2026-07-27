#pragma once

namespace eda {

// 归一化 RGBA 浮点颜色，分量范围 [0.0, 1.0]。
// 默认构造为不透明黑色（a = 1.0f）。
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

}  // namespace eda
