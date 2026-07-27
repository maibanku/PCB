#pragma once

// eda/geometry/arc_tessellate.h
//
// P3 几何内核 · Task 2：圆弧离散化（Arc → Polyline）。
//
// 把参数化 Arc（中心 + 半径 + 起止角 + 方向）按弦高容差（sagitta）自适应
// 离散为整数 nm 的 Polyline，作为所有含弧输入的布尔 / 求交 / 三角剖分前置——
// Clipper2 / earcut 均不直接支持弧。
//
// 算法（plan「几何与命令栈实现计划.md」Task 2）：
//   1) 由半径 r 与容差 e 反推最大角度步长 Δθ_max = 2·acos(1 - e/r)
//      （弦高公式 sagitta = r·(1 - cos(Δθ/2)) ≤ e 反解）。
//   2) 分段数 n = ceil(angle_span / Δθ_max)，下界 1；整圆下界 3
//      （避免单段退化为零长弦、两段直径往返）。
//   3) 按参数 t∈[0,1] 采样，浮点中间量，输出 Point 四舍五入到 nm。
//   4) 首尾点强制为精确端点（消除 n 段累积浮点漂移）。
//
// 边界：
//   - 零半径：退化为只含圆心的单点 Polyline。
//   - 整圆（start_angle == end_angle，或跨度经规范化等于 2π）：末点等于首点，
//     ccw 方向传递到顶点顺序（首→末的环绕方向）。
//
// 决策对齐：D1（nm 整数内部坐标）、D3（圆弧保留参数，按需离散，不预存离散点）。

#include "eda/geometry/types.h"

namespace eda::geometry {

// 按弦高容差自适应离散 Arc 为 Polyline。
//
//   arc                参数化圆弧（半径单位 nm，角度单位弧度）
//   max_chord_error_mm 弦高容差（单位 mm，>0；<=0 时取默认 1μm = 0.001mm）
//
// 返回 Polyline：顶点为整数 nm，首尾点严格等于弧端点（四舍五入到 nm）。
// 每段实际弦高 <= max_chord_error（容差本身允许的浮点 / 取整误差除外）。
// 零半径弧退化为只含圆心的单点 Polyline。
Polyline arc_to_polyline(const Arc& arc, double max_chord_error_mm);

// 固定分段数离散（等参数采样，不按弦高容差）。便于测试与等弧长细分。
//
//   segments 分段数（<1 时按 1 处理）；输出顶点数 = segments + 1
//
// 零半径弧同样退化为单点。整圆（首末角相等）时末点与首点重合。
Polyline arc_to_polyline_fixed(const Arc& arc, int segments);

}  // namespace eda::geometry
