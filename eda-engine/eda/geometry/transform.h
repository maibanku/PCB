#pragma once

// eda/geometry/transform.h
//
// P3 几何内核 · 2D 变换（Task 1，header-only）
//
// - translate / mirror：整数精确（nm 量纲下加减、符号翻转）。
// - rotate / scale：浮点中间量，四舍五入回 nm（llround）。
//
// 对 Point / Polyline / Polygon 三类图元提供重载。

#include "eda/geometry/types.h"

#include <cmath>

namespace eda::geometry {

// 镜像轴枚举（D2：枚举值 SCREAMING_CASE 由编译器默认值隐含；这里用 X/Y/ORIGIN 三态）。
enum class MirrorAxis {
    X,        // 沿 X 轴翻转：y → -y
    Y,        // 沿 Y 轴翻转：x → -x
    ORIGIN,   // 关于原点翻转：x → -x, y → -y
};

// ---------- 平移（整数精确） ----------

inline Point translate(Point p, Coord dx, Coord dy) {
    return {p.x + dx, p.y + dy};
}

inline Polyline translate(const Polyline& pl, Coord dx, Coord dy) {
    Polyline r = pl;
    for (auto& pt : r.points) pt = translate(pt, dx, dy);
    return r;
}

inline Polygon translate(const Polygon& pg, Coord dx, Coord dy) {
    Polygon r = pg;
    for (auto& pt : r.outer) pt = translate(pt, dx, dy);
    for (auto& hole : r.holes) {
        for (auto& pt : hole) pt = translate(pt, dx, dy);
    }
    return r;
}

// ---------- 旋转（弧度；浮点中间量四舍五入回 nm） ----------

inline Point rotate(Point p, double angle_rad, Point center) {
    const double c = std::cos(angle_rad);
    const double s = std::sin(angle_rad);
    const double dx = static_cast<double>(p.x - center.x);
    const double dy = static_cast<double>(p.y - center.y);
    return Point{
        static_cast<Coord>(std::llround(center.x + dx * c - dy * s)),
        static_cast<Coord>(std::llround(center.y + dx * s + dy * c))};
}

// ---------- 镜像（整数精确） ----------

inline Point mirror(Point p, MirrorAxis axis) {
    switch (axis) {
        case MirrorAxis::X:      return { p.x, -p.y};
        case MirrorAxis::Y:      return {-p.x,  p.y};
        case MirrorAxis::ORIGIN: return {-p.x, -p.y};
    }
    return p;  // 不可达；保留以安抚旧编译器的"控制到达非 void 函数末尾"告警
}

// ---------- 缩放（以 center 为锚；浮点中间量四舍五入） ----------

inline Point scale(Point p, double sx, double sy, Point center) {
    return Point{
        static_cast<Coord>(std::llround(center.x + static_cast<double>(p.x - center.x) * sx)),
        static_cast<Coord>(std::llround(center.y + static_cast<double>(p.y - center.y) * sy))};
}

}  // namespace eda::geometry
