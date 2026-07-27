#pragma once

#include "core/math/vector2.h"

namespace eda {

// 2D 仿射变换（对应 Godot Transform2D）。
// 采用行主序 3x3 矩阵存储（共 9 个分量），仅使用其中 6 个有效分量
// 以表达仿射：[m0 m1 m2 | m3 m4 m5 | 0 0 1]。
struct Transform2D {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    static Transform2D translate(Vector2 t) {
        Transform2D r;
        r.m[2] = t.x;
        r.m[5] = t.y;
        return r;
    }

    static Transform2D scale(Vector2 s) {
        Transform2D r;
        r.m[0] = s.x;
        r.m[4] = s.y;
        return r;
    }

    Vector2 transform_point(Vector2 p) const {
        return {m[0] * p.x + m[1] * p.y + m[2],
                m[3] * p.x + m[4] * p.y + m[5]};
    }
};

}  // namespace eda
