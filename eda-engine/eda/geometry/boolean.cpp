// eda/geometry/boolean.cpp
//
// P3 几何内核 · Task 3 实现：布尔运算（Clipper2 薄包装）。
// 算法与边界见同名 .h。决策对齐 D1（nm 整数坐标）、D4（薄包装隔离）。
//
// 本文件是整个 eda/geometry 中唯一 include <clipper2/clipper.h> 的翻译单元
// （D4）。业务层（eda/schematic、eda/pcb 等）只 include "eda/geometry/boolean.h"，
// 永不直接接触 Clipper2Lib 命名空间——底层实现可替换而不影响业务。

#include "eda/geometry/boolean.h"

#include <clipper2/clipper.h>

#include <utility>
#include <vector>

namespace eda::geometry {
namespace {

// eda::geometry::Point → Clipper2 Point64。
// Coord == int64_t 与 Point64 内部 int64_t 一一对应，零精度损失（D1）。
Clipper2Lib::Point64 to_clipper_point(Point p) {
    return Clipper2Lib::Point64(p.x, p.y);
}

// Clipper2 Point64 → eda::geometry::Point。
Point from_clipper_point(const Clipper2Lib::Point64& p) {
    return Point{p.x, p.y};
}

// 把一条环（vector<Point>）转为 Clipper2 Path64。
Clipper2Lib::Path64 ring_to_path(const std::vector<Point>& ring) {
    Clipper2Lib::Path64 path;
    path.reserve(ring.size());
    for (const auto& pt : ring) path.push_back(to_clipper_point(pt));
    return path;
}

// 把 Clipper2 Path64 转回环（vector<Point>）。
std::vector<Point> path_to_ring(const Clipper2Lib::Path64& path) {
    std::vector<Point> ring;
    ring.reserve(path.size());
    for (const auto& pt : path) ring.push_back(from_clipper_point(pt));
    return ring;
}

// Polygon（outer + holes）→ Clipper2 Paths64（扁平化为路径数组）。
// 外环与孔都追加进同一组 paths：FillRule=NonZero 下，孔与外环反向缠绕时
// 自动作为孔（plan「环方向约定：外环 CCW，孔 CW」）。
// 退化环（<3 点）静默跳过——Clipper2 不接受退化路径。
Clipper2Lib::Paths64 polygon_to_paths(const Polygon& poly) {
    Clipper2Lib::Paths64 paths;
    if (poly.outer.size() >= 3) {
        paths.push_back(ring_to_path(poly.outer));
    }
    paths.reserve(paths.size() + poly.holes.size());
    for (const auto& hole : poly.holes) {
        if (hole.size() < 3) continue;
        paths.push_back(ring_to_path(hole));
    }
    return paths;
}

// 把 PolyTree64 还原为 vector<Polygon>。
// 每个顶层节点（outer）成为一个 Polygon，其直接子节点（holes）填入该 Polygon
// 的 holes；嵌套岛（hole 内的 outer）作为新的独立 Polygon 递归输出，
// 保证任意深度的 outer/hole 嵌套都能被正确摊平、面积守恒。
void collect_polygons(const Clipper2Lib::PolyPath64& node,
                      std::vector<Polygon>& out) {
    Polygon poly;
    poly.outer = path_to_ring(node.Polygon());
    for (const auto& hole : node) {
        poly.holes.push_back(path_to_ring(hole->Polygon()));
        // hole 内若还有 outer（嵌套岛），作为独立 Polygon 递归收集。
        for (const auto& island : *hole) {
            collect_polygons(*island, out);
        }
    }
    out.push_back(std::move(poly));
}

// 执行一次布尔运算，结果以 PolyTree64 取回（保留 outer/hole 层级），
// 再还原为 vector<Polygon>。subjects / clips 为空时跳过对应的 Add 调用
// （Clipper2 对空入参容忍，但显式跳过更稳妥）。
std::vector<Polygon> run_boolean(Clipper2Lib::ClipType clip_type,
                                 const Clipper2Lib::Paths64& subjects,
                                 const Clipper2Lib::Paths64& clips) {
    Clipper2Lib::Clipper64 clipper;
    if (!subjects.empty()) clipper.AddSubject(subjects);
    if (!clips.empty()) clipper.AddClip(clips);

    Clipper2Lib::PolyTree64 solution;
    clipper.Execute(clip_type, Clipper2Lib::FillRule::NonZero, solution);

    std::vector<Polygon> result;
    for (const auto& top : solution) {
        collect_polygons(*top, result);
    }
    return result;
}

}  // namespace

std::vector<Polygon> unite(const Polygon& a, const Polygon& b) {
    // 并集：两多边形都作为 subject，无 clip。
    // 把 b 的路径拼到 subjects 后面，一并送入 Clipper。
    Clipper2Lib::Paths64 subjects = polygon_to_paths(a);
    const Clipper2Lib::Paths64 b_paths = polygon_to_paths(b);
    subjects.insert(subjects.end(), b_paths.begin(), b_paths.end());
    return run_boolean(Clipper2Lib::ClipType::Union, subjects,
                       Clipper2Lib::Paths64{});
}

std::vector<Polygon> subtract(const Polygon& a, const Polygon& b) {
    // 差集：a 为 subject，b 为 clip。
    return run_boolean(Clipper2Lib::ClipType::Difference,
                       polygon_to_paths(a), polygon_to_paths(b));
}

std::vector<Polygon> intersect(const Polygon& a, const Polygon& b) {
    // 交集：a 为 subject，b 为 clip。
    return run_boolean(Clipper2Lib::ClipType::Intersection,
                       polygon_to_paths(a), polygon_to_paths(b));
}

std::vector<Polygon> xor_op(const Polygon& a, const Polygon& b) {
    // 对称差：a 为 subject，b 为 clip。
    return run_boolean(Clipper2Lib::ClipType::Xor,
                       polygon_to_paths(a), polygon_to_paths(b));
}

}  // namespace eda::geometry
