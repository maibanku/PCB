// eda/geometry/offset.cpp
//
// P3 几何内核 · Task 4 实现：偏移（Clipper2 ClipperOffset 薄包装）。
// 算法与边界见同名 .h。决策对齐 D1（nm 整数坐标）、D4（Clipper2 薄包装）。
//
// 实现要点：
//   1) to_path64 / from_path64：Polygon <-> Clipper2 Path64 的整数无损转换（nm）。
//      Point64 与我方 Point 均为 int64 二元组，逐点拷贝零精度损失。
//   2) 方向规范化：外环强制 CCW、孔强制 CW。ClipperOffset 依赖环方向识别外 / 孔——
//      delta > 0 在外环上外推、在孔上内缩，一键同时完成"外膨材料"双向偏移。
//      用 Clipper2 Area() 符号判定方向（CCW 为正），不符则 std::reverse。
//   3) 多边形偏移用 PolyTree64 解析输出层级（顶层 = 外环，孩子 = 一级孔），
//      还原为 vector<Polygon>；嵌套岛屿本层一并展平为孔（P3 不出现嵌套岛屿）。
//   4) 线条偏移用 Paths64 扁平输出（开路径的端部成型后是闭合环），逐条转 Polyline。
//   5) miter_limit（默认 2.0）限幅锐角 miter 臂长，避免尖角飞出；
//      arc_tolerance（圆角离散弦高）<=0 时让 Clipper2 按 delta 自适应。

#include "eda/geometry/offset.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

// 薄包装原则（D4）：Clipper2 头**只在 offset.cpp 内 include**，业务层永不接触。
// CMake 中 eda_geometry 需链接 Clipper2 target（由主循环在 eda/CMakeLists.txt 配置）。
#include "clipper2/clipper.h"

namespace eda::geometry {
namespace {

// ===================== Clipper2 类型适配 =====================

Clipper2Lib::Path64 to_path64(const std::vector<Point>& pts) {
    Clipper2Lib::Path64 path;
    path.reserve(pts.size());
    for (const auto& p : pts) {
        path.push_back(Clipper2Lib::Point64(p.x, p.y));
    }
    return path;
}

std::vector<Point> from_path64(const Clipper2Lib::Path64& path) {
    std::vector<Point> pts;
    pts.reserve(path.size());
    for (const auto& p : path) {
        pts.push_back(Point{p.x, p.y});
    }
    return pts;
}

Clipper2Lib::JoinType to_clipper_join(JoinType jt) {
    switch (jt) {
        case JoinType::Miter: return Clipper2Lib::JoinType::Miter;
        case JoinType::Round: return Clipper2Lib::JoinType::Round;
        case JoinType::Square: return Clipper2Lib::JoinType::Square;
    }
    return Clipper2Lib::JoinType::Miter;
}

Clipper2Lib::EndType to_clipper_end(EndType et) {
    switch (et) {
        case EndType::Polygon: return Clipper2Lib::EndType::Polygon;
        case EndType::Butt:    return Clipper2Lib::EndType::Butt;
        case EndType::Square:  return Clipper2Lib::EndType::Square;
        case EndType::Round:   return Clipper2Lib::EndType::Round;
    }
    return Clipper2Lib::EndType::Polygon;
}

// ===================== 方向规范化 =====================
// ClipperOffset 依赖环方向识别外环 / 孔：CCW = 外环、CW = 孔。
// delta > 0 在外环上外推、在孔上内缩（一键同时双向偏移）。
// 即便调用方传入不一致方向，这里按 Area 符号强制校正。

Clipper2Lib::Path64 normalized_outer(const std::vector<Point>& outer) {
    Clipper2Lib::Path64 path = to_path64(outer);
    // Clipper2 Area：CCW 为正、CW 为负。外环要求 CCW（正面积）。
    if (Clipper2Lib::Area(path) < 0) {
        std::reverse(path.begin(), path.end());
    }
    return path;
}

Clipper2Lib::Path64 normalized_hole(const std::vector<Point>& hole) {
    Clipper2Lib::Path64 path = to_path64(hole);
    // 孔要求 CW（负面积）。
    if (Clipper2Lib::Area(path) > 0) {
        std::reverse(path.begin(), path.end());
    }
    return path;
}

// ===================== PolyTree → vector<Polygon> =====================
// PolyTree64 顶层 Childs = 外环；每个外环节点的 Childs = 一级孔。
// 嵌套更深的岛屿（孔内再出现外环）在 P3 范围内不会出现，此处不递归展开。

std::vector<Polygon> polytree_to_polygons(const Clipper2Lib::PolyTree64& tree) {
    std::vector<Polygon> result;
    for (const auto& outer_ptr : tree) {
        const auto* outer_node = outer_ptr.get();
        Polygon pg;
        pg.outer = from_path64(outer_node->Polygon());
        for (const auto& hole_ptr : *outer_node) {
            const auto* hole_node = hole_ptr.get();
            pg.holes.push_back(from_path64(hole_node->Polygon()));
        }
        result.push_back(std::move(pg));
    }
    return result;
}

// 多边形偏移执行：已规范方向的 Paths + 有符号 delta。
std::vector<Polygon> run_polygon_offset(Clipper2Lib::Paths64& input,
                                        Coord delta_nm,
                                        const OffsetOptions& opts) {
    Clipper2Lib::PolyTree64 tree;
    const double arc_tol_nm =
        opts.arc_tolerance_mm > 0.0 ? opts.arc_tolerance_mm * 1e6 : 0.0;
    Clipper2Lib::ClipperOffset co(opts.miter_limit, arc_tol_nm);
    co.AddPaths(input, to_clipper_join(opts.join_type),
                Clipper2Lib::EndType::Polygon);
    co.Execute(static_cast<double>(delta_nm), tree);
    return polytree_to_polygons(tree);
}

}  // namespace

// ===================== 公共 API =====================

std::vector<Polygon> offset_polygon(const Polygon& polygon, Coord delta_nm,
                                    const OffsetOptions& opts) {
    std::vector<Polygon> result;
    if (polygon.outer.size() < 3u) {
        return result;  // 退化外环：无面积，不偏移
    }
    if (delta_nm == 0) {
        result.push_back(polygon);  // 零偏移：原样返回副本
        return result;
    }

    Clipper2Lib::Paths64 input;
    input.reserve(1u + polygon.holes.size());
    input.push_back(normalized_outer(polygon.outer));
    for (const auto& hole : polygon.holes) {
        if (hole.size() >= 3u) {
            input.push_back(normalized_hole(hole));
        }
    }
    return run_polygon_offset(input, delta_nm, opts);
}

std::vector<Polygon> inflate(const Polygon& polygon, Coord delta_nm,
                             const OffsetOptions& opts) {
    if (delta_nm <= 0) {
        // 非正 delta 退化：外扩语义不成立，原样返回副本
        std::vector<Polygon> result;
        if (polygon.outer.size() >= 3u) result.push_back(polygon);
        return result;
    }
    return offset_polygon(polygon, delta_nm, opts);
}

std::vector<Polygon> shrink(const Polygon& polygon, Coord delta_nm,
                            const OffsetOptions& opts) {
    if (delta_nm <= 0) {
        std::vector<Polygon> result;
        if (polygon.outer.size() >= 3u) result.push_back(polygon);
        return result;
    }
    // 内缩 = 负方向偏移（外环内收、孔外扩）
    return offset_polygon(polygon, -delta_nm, opts);
}

std::vector<Polyline> offset(const Polyline& polyline, Coord delta_nm,
                             JoinType join_type, EndType end_type) {
    std::vector<Polyline> result;
    if (polyline.points.size() < 2u || delta_nm == 0) {
        return result;
    }

    Clipper2Lib::Paths64 solution;
    Clipper2Lib::ClipperOffset co;  // 默认 miter_limit=2.0、arc_tolerance 自适应
    co.AddPath(to_path64(polyline.points), to_clipper_join(join_type),
               to_clipper_end(end_type));
    co.Execute(static_cast<double>(delta_nm), solution);

    result.reserve(solution.size());
    for (const auto& path : solution) {
        Polyline pl;
        pl.points = from_path64(path);
        result.push_back(std::move(pl));
    }
    return result;
}

std::vector<Polygon> courtyard(const Polygon& device_outline, Coord gap_nm,
                               const OffsetOptions& opts) {
    return inflate(device_outline, gap_nm, opts);
}

std::vector<Polygon> path_to_polygon(const Path& path,
                                     const OffsetOptions& opts) {
    std::vector<Polygon> result;
    if (path.points.size() < 2u || path.width <= 0) {
        return result;
    }

    // 走线偏移：中心线两侧各 width/2，拐角按 opts.join_type，端部强制圆头
    //（PCB 走线焊盘端标准；如需其它端部，调用方可改用底层 offset(Polyline, ...)）。
    Clipper2Lib::Paths64 input;
    input.push_back(to_path64(path.points));

    Clipper2Lib::PolyTree64 tree;
    const double arc_tol_nm =
        opts.arc_tolerance_mm > 0.0 ? opts.arc_tolerance_mm * 1e6 : 0.0;
    Clipper2Lib::ClipperOffset co(opts.miter_limit, arc_tol_nm);
    co.AddPaths(input, to_clipper_join(opts.join_type),
                Clipper2Lib::EndType::Round);
    co.Execute(static_cast<double>(path.width) / 2.0, tree);
    return polytree_to_polygons(tree);
}

}  // namespace eda::geometry
