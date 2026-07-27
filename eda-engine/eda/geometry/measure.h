#pragma once

// eda/geometry/measure.h
//
// P3 几何内核 · Task 7：测量（length / area / distance / arc_length）。
//
// 对应 plan「几何与命令栈实现计划.md」Task 7 与子功能 README「06-求交与测量」。
//
// 设计要点：
//   - 输入图元坐标一律 int64 nm（D1），结果在边界处按需转 double。
//   - length / distance 用 double 中间量累加，最终四舍五入回 nm（Coord），
//     与 transform.h / arc_tessellate.cpp 保持同一精度约定。
//   - area 用鞋带（shoelace）公式，外环减孔；多环面积在 long double 上累加，
//     规避 int64 乘积溢出（板级面积可达 10^16~10^18 nm²）。
//   - 含孔 Polygon：外环面积 - Σ孔面积；结果非负（孔大于外环时 clamp 到 0）。
//
// 量纲：所有长度返回 nm（Coord），面积返回 nm²（double），弧长返回 nm（double）。
//
// 这些纯查询函数无需进命令栈（plan「全局约束」第 5 条）。

#include "eda/geometry/types.h"

#include <cstdint>
#include <vector>

namespace eda::geometry {

// ---------- 长度 ----------

// Polyline 长度（nm）：Σ各段欧氏长度，四舍五入到整数 nm。
// 少于 2 点返回 0。
Coord length(const Polyline& poly);

// Arc 弧长（nm，double 保留精度）：r · Δθ，Δθ 为规范化跨度（0, 2π]。
// 零半径返回 0。
double arc_length(const Arc& arc);

// ---------- 面积 ----------

// Polygon 面积（nm²，含孔）：|外环签名面积| - Σ|孔签名面积|。
// 环方向不影响结果（取绝对值）；孔总和大于外环时 clamp 到 0。
// 外环 < 3 点返回 0。
double area(const Polygon& poly);

// ---------- 距离 ----------

// 点到点距离（nm），四舍五入。
Coord distance(Point a, Point b);

// 点到线段距离（nm）：投影落在线段上取垂直距离，否则取到较近端点距离。
// 零长线段退化为点到点距离。
Coord point_to_segment(Point p, const Segment& s);

// 点到线段距离（nm），point_to_segment 的别名重载。
Coord distance(Point p, const Segment& s);

// 点到折线距离（nm）：min over 各线段。空折线返回 Coord 最大值。
Coord distance(Point p, const Polyline& poly);

}  // namespace eda::geometry
