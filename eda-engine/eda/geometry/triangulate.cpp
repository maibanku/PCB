// eda/geometry/triangulate.cpp
//
// P3 几何内核 · Task 5 实现：三角剖分（earcut 薄包装）。
// 接口语义与不变量见同名 .h。决策对齐 D1（nm 整数）、D2（自由函数 + POD）、
// D4（第三方薄包装——earcut 仅在本文件可见）。

#include "eda/geometry/triangulate.h"

// earcut 是 header-only；仅本 .cpp include 它，业务层（eda_geometry 之外）
// 永不直接依赖 <mapbox/earcut.hpp>（D4 薄包装隔离）。
#include <mapbox/earcut.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

// ============================================================================
// earcut 坐标访问 traits 适配
//
// earcut 通过 mapbox::util::nth<I, Point>::get(p) 取点坐标。其默认实现走
// std::get<I>（仅 tuple-like 类型可用）；我们的 eda::geometry::Point 是
// POD struct { x, y }，必须在此特化 nth 暴露 x/y。
//
// 特化返回 Coord（int64_t），earcut 调用点隐式转 double。nm 量程整数在
// double 中精确表示（2⁵³ ≈ 9×10¹⁵ >> 9.2×10⁹），无精度损失。
//
// 必须在实例化 mapbox::earcut<EarcutRings> 之前定义——故置于 include 之后、调用之前。
// ============================================================================
namespace mapbox {
namespace util {

template <>
struct nth<0, eda::geometry::Point> {
    inline static eda::geometry::Coord get(const eda::geometry::Point& p) {
        return p.x;
    }
};

template <>
struct nth<1, eda::geometry::Point> {
    inline static eda::geometry::Coord get(const eda::geometry::Point& p) {
        return p.y;
    }
};

}  // namespace util
}  // namespace mapbox

namespace eda::geometry {
namespace {

// 喂给 earcut 的环数组：外环在前（rings[0]），孔紧随其后（rings[1..]）。
// 环的 value_type 即 eda::geometry::Point，由上面的 nth 特化适配。
using EarcutRings = std::vector<std::vector<Point>>;

}  // namespace

TriangulationResult triangulate(const Polygon& poly) {
    TriangulationResult result;

    // 边界：外环至少 3 点才有面积意义；否则返回空结果。
    if (poly.outer.size() < 3) {
        return result;
    }

    // 收集有效环：外环 + 顶点数 >= 3 的孔（退化孔跳过）。
    // earcut 要求 rings[0] 为外环，rings[1..] 为孔。
    EarcutRings rings;
    rings.reserve(1 + poly.holes.size());
    rings.push_back(poly.outer);

    std::size_t vertex_count = poly.outer.size();
    for (const auto& hole : poly.holes) {
        if (hole.size() < 3) continue;  // 退化孔无法成环
        rings.push_back(hole);
        vertex_count += hole.size();
    }

    // 平铺顶点：外环 ++ 有效孔，顺序与 rings 一致。
    // earcut 按出现顺序为节点编号（外环 0..n_outer-1，孔紧随其后），
    // 故 result.vertices 的下标与 earcut.indices 一一对齐——无需重映射。
    result.vertices.reserve(vertex_count);
    for (const Point& p : poly.outer) {
        result.vertices.push_back(p);
    }
    for (const auto& hole : poly.holes) {
        if (hole.size() < 3) continue;  // 与上保持一致：退化孔不进 vertices
        for (const Point& p : hole) {
            result.vertices.push_back(p);
        }
    }

    // 执行耳切。mapbox::earcut<N>::indices 类型为 vector<N>；取 N=uint32_t 与
    // TriangulationResult::indices 完全一致，可直接 move（无截断风险）。
    result.indices = mapbox::earcut<uint32_t>(rings);

    return result;
}

}  // namespace eda::geometry
