#pragma once

// eda/geometry/triangulate.h
//
// P3 几何内核 · Task 5：三角剖分（Polygon → 索引化三角形流）。
//
// 包装 earcut（耳切法）把可能含孔的 Polygon 剖分为索引三角形流，
// 为铺铜 GPU 填充、Gerber 输出、碰撞渲染提供三角形数据。
//
// 设计要点（plan「几何与命令栈实现计划.md」Task 5）：
//   - 输入 Polygon 坐标一律 int64 nm（D1）；earcut 内部转 double，
//     nm 量程（±9.2×10⁹）远小于 double 的精确整数范围（2⁵³ ≈ 9×10¹⁵），
//     零精度损失。
//   - earcut 期望"环数组"输入：rings[0]=外环，rings[1..]=孔。
//     顶点按出现顺序编号（外环 0..n_outer-1，孔紧随其后），与结果 indices 对齐。
//   - 业务层只 include 本头，永不直接 include <mapbox/earcut.hpp>（D4 薄包装）。
//     earcut 的坐标访问 traits 适配封在 triangulate.cpp 内。
//
// 不变量：
//   - 顶点数：result.vertices.size() == outer.size() + Σ(有效 hole.size())。
//   - 索引有效性：所有 indices[k] < vertices.size()，indices.size() % 3 == 0。
//   - 三角形数（简单干净多边形，无共线/重合点）：
//         T = n - 2 + 2*h     （n=顶点总数，h=孔数）
//     该公式由 Euler-Poincaré 导出（χ = 1 - h）：含孔区域每孔需 1 座"桥"，
//     桥在链表中复制 2 节点，单环化后边界顶点数 = n + 2h，耳切得 n + 2h - 2。
//   - 面积守恒：Σ|三角形面积| == |area(Polygon)|（与 measure::area 一致）。
//
// 这些纯查询函数无需进命令栈（plan「全局约束」第 5 条）。

#include "eda/geometry/types.h"

#include <cstdint>
#include <vector>

namespace eda::geometry {

// 三角剖分结果：索引化三角形流（GPU/渲染友好的 indexed triangle list）。
struct TriangulationResult {
    // 顶点列表：外环 ++ 各孔按输入顺序平铺。earcut 的 indices 引用此列表；
    // 同一下标在 vertices 与 earcut 内部编号上一一对应（无重排）。
    std::vector<Point> vertices;

    // 三角形索引：每 3 个连续值为一组三角形 (v0, v1, v2)，指向 vertices。
    // 索引类型 uint32_t，适配 GPU 索引缓冲；环绕方向由 earcut 规范化为外环 CCW。
    std::vector<uint32_t> indices;
};

// 对 Polygon（可能含孔）做三角剖分。
//
//   poly 多边形（外环 + 可选孔列表，nm 坐标）
//
// 返回 TriangulationResult：vertices 为输入顶点（外环 ++ 有效孔平铺），
// indices 为三角形顶点索引（uint32_t，每 3 个一组）。
//
// 边界：
//   - 外环 < 3 点：返回空结果（indices 与 vertices 均空）。
//   - 某孔 < 3 点：跳过该退化孔（不进入 vertices / rings，其余正常剖分）。
//   - earcut 对退化输入（自交、零面积环）的清理由其内置 filterPoints 处理，
//     本包装不做额外清洗。
TriangulationResult triangulate(const Polygon& poly);

}  // namespace eda::geometry
