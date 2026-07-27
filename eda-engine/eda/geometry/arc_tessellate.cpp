// eda/geometry/arc_tessellate.cpp
//
// P3 几何内核 · Task 2 实现：圆弧离散化。算法与边界见同名 .h。

#include "eda/geometry/arc_tessellate.h"

#include <cmath>
#include <cstddef>

namespace eda::geometry {
namespace {

// 2π 常量。避免依赖 M_PI（MSVC 默认不暴露，需 _USE_MATH_DEFINES）。
constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

// 默认弦高容差（DoD：1μm），当调用方传入 <=0 时兜底。
constexpr double kDefaultErrorNm = 1000.0;

// 规范化角度跨度到 (0, 2π]。
//   ccw=true ：从 start 到 end 的逆时针有向角
//   ccw=false：从 start 到 end 的顺时针有向角（取负后归一化）
// start == end 时返回 2π（整圆）。
double normalized_span(double start_angle, double end_angle, bool ccw) {
    double span = ccw ? (end_angle - start_angle) : (start_angle - end_angle);
    span = std::fmod(span, kTwoPi);
    if (span <= 0.0) span += kTwoPi;
    return span;
}

// 浮点坐标 → 整数 nm（四舍五入，消除浮点累积漂移）。
Point to_point_nm(double x, double y) {
    return Point{static_cast<Coord>(std::llround(x)),
                 static_cast<Coord>(std::llround(y))};
}

// 已知跨度 span 与分段数 n，执行参数采样并精确化首尾。
// 零半径已由调用方提前处理；调用方保证 n >= 1、span > 0。
Polyline tessellate_impl(const Arc& arc, double span, int n) {
    if (n < 1) n = 1;

    const double r = static_cast<double>(arc.radius);
    const double cx = static_cast<double>(arc.center.x);
    const double cy = static_cast<double>(arc.center.y);
    const double direction = arc.ccw ? 1.0 : -1.0;

    Polyline result;
    result.points.reserve(static_cast<std::size_t>(n) + 1u);

    const double inv_n = 1.0 / static_cast<double>(n);
    for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) * inv_n;
        const double angle = arc.start_angle + direction * span * t;
        result.points.push_back(to_point_nm(
            cx + r * std::cos(angle), cy + r * std::sin(angle)));
    }

    // 端点精确化：n 段累积浮点漂移可能让首尾偏离弧端点几个 nm，
    // 强制覆写为 start_angle / end_angle 处的精确位置。
    // 整圆场景 end_angle == start_angle，末点自然与首点重合（闭合）。
    result.points.front() = to_point_nm(
        cx + r * std::cos(arc.start_angle),
        cy + r * std::sin(arc.start_angle));
    result.points.back() = to_point_nm(
        cx + r * std::cos(arc.end_angle),
        cy + r * std::sin(arc.end_angle));

    return result;
}

}  // namespace

Polyline arc_to_polyline(const Arc& arc, double max_chord_error_mm) {
    // 边界：零半径 → 退化为圆心单点
    if (arc.radius <= 0) {
        Polyline degenerate;
        degenerate.points.push_back(arc.center);
        return degenerate;
    }

    const double span =
        normalized_span(arc.start_angle, arc.end_angle, arc.ccw);

    // 容差 mm → nm（双精度，避免整型截断）；<=0 时取默认 1μm
    double e_nm = max_chord_error_mm * 1e6;
    if (e_nm <= 0.0) e_nm = kDefaultErrorNm;
    const double r_nm = static_cast<double>(arc.radius);

    // Δθ_max = 2·acos(1 - e/r)；clamp ratio 到 [-1, 1] 防止 acos 域外
    double ratio = 1.0 - e_nm / r_nm;
    if (ratio < -1.0) ratio = -1.0;
    if (ratio > 1.0) ratio = 1.0;
    const double max_step = 2.0 * std::acos(ratio);

    // n = ceil(span / Δθ_max)，下界 1
    int n = static_cast<int>(std::ceil(span / max_step));
    if (n < 1) n = 1;
    // 整圆至少 3 段（单段会退化为零长弦，两段只是直径往返，都无法描出圆）
    if (span >= kTwoPi - 1e-9 && n < 3) n = 3;

    return tessellate_impl(arc, span, n);
}

Polyline arc_to_polyline_fixed(const Arc& arc, int segments) {
    // 边界：零半径 → 退化为圆心单点
    if (arc.radius <= 0) {
        Polyline degenerate;
        degenerate.points.push_back(arc.center);
        return degenerate;
    }

    const double span =
        normalized_span(arc.start_angle, arc.end_angle, arc.ccw);
    const int n = segments < 1 ? 1 : segments;
    return tessellate_impl(arc, span, n);
}

}  // namespace eda::geometry
