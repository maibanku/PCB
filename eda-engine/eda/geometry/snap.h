#pragma once

// eda/geometry/snap.h
//
// P3 几何内核 · 吸附工具（Task 1，header-only）
//
// - snap_to_grid：把点拉到最近的栅格交点。
// - snap_angle ：把角度拉到最近的步长倍数（弧度）。
// - snap_point ：把点吸附到目标点（容差 nm 内才吸附，否则原样返回）。

#include "eda/geometry/types.h"

#include <cmath>

namespace eda::geometry {

// 网格吸附：grid_nm <= 0 时原样返回（防御退化输入）。
inline Point snap_to_grid(Point p, Coord grid_nm) {
    if (grid_nm <= 0) return p;
    const auto snap1 = [](Coord v, Coord g) -> Coord {
        return static_cast<Coord>(std::llround(static_cast<double>(v) / static_cast<double>(g)) *
                                  static_cast<double>(g));
    };
    return {snap1(p.x, grid_nm), snap1(p.y, grid_nm)};
}

// 角度吸附（弧度）；step_rad <= 0 时原样返回。
inline double snap_angle(double angle_rad, double step_rad) {
    if (step_rad <= 0) return angle_rad;
    return std::round(angle_rad / step_rad) * step_rad;
}

// 点吸附到目标点：距离 <= tolerance_nm 才吸附，否则原样返回。
inline Point snap_point(Point p, Point target, Coord tolerance_nm) {
    const Coord dx = p.x - target.x;
    const Coord dy = p.y - target.y;
    if (dx * dx + dy * dy <= tolerance_nm * tolerance_nm) return target;
    return p;
}

}  // namespace eda::geometry
