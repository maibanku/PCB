// eda/geometry/measure.cpp
//
// P3 几何内核 · Task 7 实现：测量（length / area / distance / arc_length）。
// 公式与边界见同名 .h。决策对齐 D1（nm 整数坐标）、D2（自由函数 + POD）。

#include "eda/geometry/measure.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace eda::geometry {
namespace {

// 2π 常量。避免依赖 M_PI（MSVC 默认不暴露）。
constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

// 规范化角度跨度到 (0, 2π]（与 arc_tessellate.cpp 同一逻辑）。
// start == end 时返回 2π（整圆）。ccw=false 时按相反方向取负后归一化。
double normalized_span(double start_angle, double end_angle, bool ccw) {
    double span = ccw ? (end_angle - start_angle) : (start_angle - end_angle);
    span = std::fmod(span, kTwoPi);
    if (span <= 0.0) span += kTwoPi;
    return span;
}

// 鞋带公式计算环的签名面积（nm²）。
// Σ (x_i * y_{i+1} - x_{i+1} * y_i) / 2。
// 使用 long double 累加：单环顶点可达 ~10^9 nm，乘积 ~10^18，求和后仍需保精度。
// 注意：MSVC 上 long double == double（53 位尾数），超 ~9×10^15 会掉精度；
// 但面积仅用于相对比较与最终显示，不影响几何拓扑正确性。生产级可换 __int128。
long double ring_signed_area(const std::vector<Point>& ring) {
    const std::size_t n = ring.size();
    if (n < 3) return 0.0L;
    long double sum = 0.0L;
    for (std::size_t i = 0; i < n; ++i) {
        const Point& p = ring[i];
        const Point& q = ring[(i + 1) % n];
        sum += static_cast<long double>(p.x) * static_cast<long double>(q.y) -
               static_cast<long double>(p.y) * static_cast<long double>(q.x);
    }
    return sum / 2.0L;
}

}  // namespace

// ===================== 长度 =====================

Coord length(const Polyline& poly) {
    if (poly.points.size() < 2) return 0;
    long double total = 0.0L;
    for (std::size_t i = 0; i + 1 < poly.points.size(); ++i) {
        const long double dx = static_cast<long double>(poly.points[i + 1].x) -
                               static_cast<long double>(poly.points[i].x);
        const long double dy = static_cast<long double>(poly.points[i + 1].y) -
                               static_cast<long double>(poly.points[i].y);
        total += std::sqrt(dx * dx + dy * dy);
    }
    return static_cast<Coord>(std::llround(static_cast<double>(total)));
}

double arc_length(const Arc& arc) {
    if (arc.radius <= 0) return 0.0;
    const double span = normalized_span(arc.start_angle, arc.end_angle, arc.ccw);
    return static_cast<double>(arc.radius) * span;
}

// ===================== 面积 =====================

double area(const Polygon& poly) {
    long double outer = std::fabs(ring_signed_area(poly.outer));
    if (outer == 0.0L) return 0.0;
    for (const auto& hole : poly.holes) {
        outer -= std::fabs(ring_signed_area(hole));
    }
    return static_cast<double>(outer > 0.0L ? outer : 0.0L);
}

// ===================== 距离 =====================

Coord distance(Point a, Point b) {
    const long double dx = static_cast<long double>(a.x - b.x);
    const long double dy = static_cast<long double>(a.y - b.y);
    return static_cast<Coord>(std::llround(static_cast<double>(std::sqrt(dx * dx + dy * dy))));
}

Coord point_to_segment(Point p, const Segment& s) {
    const long double px = static_cast<long double>(p.x);
    const long double py = static_cast<long double>(p.y);
    const long double ax = static_cast<long double>(s.a.x);
    const long double ay = static_cast<long double>(s.a.y);
    const long double bx = static_cast<long double>(s.b.x);
    const long double by = static_cast<long double>(s.b.y);
    const long double dx = bx - ax;
    const long double dy = by - ay;
    const long double len_sq = dx * dx + dy * dy;

    long double dist_sq;
    if (len_sq <= 0.0L) {
        // 零长线段：退化为点到点 a 距离。
        const long double ex = px - ax;
        const long double ey = py - ay;
        dist_sq = ex * ex + ey * ey;
    } else {
        // 投影参数 t = ((p-a)·(b-a)) / |b-a|²，clamp 到 [0,1]。
        long double t = ((px - ax) * dx + (py - ay) * dy) / len_sq;
        if (t < 0.0L) t = 0.0L;
        else if (t > 1.0L) t = 1.0L;
        const long double cx = ax + t * dx;
        const long double cy = ay + t * dy;
        const long double ex = px - cx;
        const long double ey = py - cy;
        dist_sq = ex * ex + ey * ey;
    }

    return static_cast<Coord>(std::llround(static_cast<double>(std::sqrt(dist_sq))));
}

Coord distance(Point p, const Segment& s) {
    return point_to_segment(p, s);
}

Coord distance(Point p, const Polyline& poly) {
    if (poly.points.empty()) {
        return std::numeric_limits<Coord>::max();  // 空折线：无定义，返回哨兵值。
    }
    if (poly.points.size() == 1) {
        return distance(p, poly.points[0]);  // 单点：点到点距离。
    }
    Coord best = std::numeric_limits<Coord>::max();
    for (std::size_t i = 0; i + 1 < poly.points.size(); ++i) {
        const Segment seg{poly.points[i], poly.points[i + 1]};
        const Coord d = point_to_segment(p, seg);
        if (d < best) best = d;
    }
    return best;
}

}  // namespace eda::geometry
