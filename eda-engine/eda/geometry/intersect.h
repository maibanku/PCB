#pragma once

// eda/geometry/intersect.h
//
// P3 几何内核 · Task 7：求交（segment/segment、segment/arc、polyline 自交）。
//
// 对应 plan「几何与命令栈实现计划.md」Task 7 与子功能 README「06-求交与测量」。
//
// 设计要点：
//   - 坐标基础 D1：所有输入为 int64 nm，求交判定优先用整数 / long double 运算
//     避免浮点误差（与 index.cpp 同一约定）。
//   - segment_segment：denom = 0（平行 / 共线）视为退化，返回 nullopt；
//     共线重叠也归退化，不返回多点集。
//   - segment_arc：圆弧参数（center/radius/起止角/ccw）为浮点；求交解二次方程，
//     结果按需四舍五入到 nm Point。弧角度范围用规范化 span 严格判定。
//   - polyline_self：Bentley-Ottmann 扫描线（事件队列 + 状态线 + CROSS 事件
//     维护顺序）。非相邻段（ polyline idx 差 > 1）的相交才报告；相邻段共享
//     端点不算自交。
//
// 返回值约定：
//   - segment_segment_intersect：唯一交点 -> optional<Point>；无交 / 退化 -> nullopt。
//   - segment_arc_intersect：0/1/2 个交点，已去重，已按 nm 取整。
//   - polyline_self_intersections：vector<{点, (段 idx i, 段 idx j)}>，i < j，
//     已按 (i, j, point) 去重排序。
//
// 这些纯查询函数无需进命令栈（plan「全局约束」第 5 条）。

#include "eda/geometry/types.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace eda::geometry {

// 两线段交点。退化（平行/共线/零长）或不交返回 nullopt。
// 端点相切（共享端点 / T 接）算交点，返回该点。
std::optional<Point> segment_segment_intersect(const Segment& a, const Segment& b);

// 线段与圆弧交点。返回 0、1（相切 / 端点）或 2 个点，按 nm 取整并去重。
// 弧角度范围外的交点被过滤；线段长度为 0 时返回空。
std::vector<Point> segment_arc_intersect(const Segment& s, const Arc& arc);

// Polyline 自相交检测（Bentley-Ottmann 扫描线）。
// 报告所有非相邻段（polyline 段索引差 |i - j| > 1）的交点。
// 相邻段共享端点不算自交；共线重叠（无唯一交点）不报告。
// 返回元素为 {交点, {段索引 i, 段索引 j}}，i < j，结果已去重并按 (i, j, point) 排序。
std::vector<std::pair<Point, std::pair<std::size_t, std::size_t>>>
polyline_self_intersections(const Polyline& poly);

}  // namespace eda::geometry
