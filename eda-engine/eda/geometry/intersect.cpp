// eda/geometry/intersect.cpp
//
// P3 几何内核 · Task 7 实现：求交（segment/segment、segment/arc、polyline 自交）。
// 算法与边界见同名 .h。决策对齐 D1（nm 整数坐标）、D2（自由函数 + POD）。
//
// 三块实现：
//   1) segment_segment_intersect：解析公式，整数分子分母 + long double 中间量，
//      denom = 0 视为退化（平行 / 共线）。
//   2) segment_arc_intersect：线段参数方程代入圆方程得二次方程，
//      解 t ∈ [0,1] 后用 atan2 判角度是否落在弧的规范化跨度内。
//   3) polyline_self_intersections：Bentley-Ottmann 扫描线。
//      - 事件优先队列（端点 LEFT/RIGHT + 发现的 CROSS）按 (x, y, kind) 序。
//      - 扫描状态 std::set<size_t, StatusCmp>，比较器按当前 sweep_x 处的 y 排序，
//        同 y 用斜率、最终用段 idx 做 tiebreaker，保证 strict weak ordering。
//      - CROSS 事件：把相交两段移除并以 sweep_x = cross.x ± tiny 重插，
//        让比较器给出 post-cross 顺序（状态线有序性在事件之间不变）。
//      - active_cross_pairs 集合避免同一对段的 CROSS 重复入队 / 重复报告。
//      - 相邻段（polyline idx 差 ≤ 1）共享端点不算自交。

#include "eda/geometry/intersect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <queue>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eda::geometry {
namespace {

// 2π 常量。避免依赖 M_PI（MSVC 默认不暴露）。
constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

// CROSS 事件重排时给 sweep_x 加的微小偏移，让比较器给出严格的 post/pre-cross 顺序。
// 1e-6 nm 远低于浮点精度（坐标量级 ~10^9 nm 时 double 绝对精度 ~10^-7 nm）。
constexpr double kTiny = 1e-6;

// "交点严格在 sweep_x 左侧" 的判定容差。必须 << kTiny，以保证 CROSS 重排后
// 再检查新邻居时，刚处理过的交点（ix ≈ cross.x）能被 sweep_x = cross.x + kTiny
// 屏蔽掉，避免重复入队。
constexpr double kSchedTol = 1e-9;

// 角度落在弧内的容差（弧度）。
constexpr double kArcTol = 1e-9;

// long double → int64 nm（四舍五入，half away from zero）。
inline int64_t round_nm(long double v) {
    return static_cast<int64_t>(std::llround(static_cast<double>(v)));
}

// double → int64 nm（四舍五入）。
inline int64_t round_nm_d(double v) {
    return static_cast<int64_t>(std::llround(v));
}

// 规范化角度跨度到 (0, 2π]（与 arc_tessellate.cpp 同一逻辑）。
double normalized_span(double start_angle, double end_angle, bool ccw) {
    double span = ccw ? (end_angle - start_angle) : (start_angle - end_angle);
    span = std::fmod(span, kTwoPi);
    if (span <= 0.0) span += kTwoPi;
    return span;
}

// 角度 θ 是否落在弧的规范化跨度内（含端点，带容差）。
bool angle_on_arc(double theta, const Arc& arc) {
    const double span = normalized_span(arc.start_angle, arc.end_angle, arc.ccw);
    double diff = arc.ccw ? (theta - arc.start_angle) : (arc.start_angle - theta);
    diff = std::fmod(diff, kTwoPi);
    if (diff < 0.0) diff += kTwoPi;
    return diff <= span + kArcTol;
}

// ===================== Bentley-Ottmann 内部类型 =====================

// 扫描段：left.x <= right.x；垂直段（left.x == right.x）left.y < right.y。
struct BoSeg {
    std::size_t idx;        // polyline 段索引（原始）
    Point left, right;
    bool is_vertical;
};

BoSeg make_bo_seg(std::size_t idx, Point a, Point b) {
    BoSeg s;
    s.idx = idx;
    if (a.x < b.x || (a.x == b.x && a.y <= b.y)) {
        s.left = a;
        s.right = b;
    } else {
        s.left = b;
        s.right = a;
    }
    s.is_vertical = (a.x == b.x);
    return s;
}

// 段在 x 处的 y 坐标（垂直段返回 left.y，认为垂直段在该 x 处覆盖 y ∈ [left.y, right.y]）。
double y_at_x(const BoSeg& s, double x) {
    if (s.is_vertical) return static_cast<double>(s.left.y);
    const double dx = static_cast<double>(s.right.x - s.left.x);
    const double t = (x - static_cast<double>(s.left.x)) / dx;
    return static_cast<double>(s.left.y) + t * static_cast<double>(s.right.y - s.left.y);
}

// 段的斜率（dy/dx）。垂直段返回 ±1e18 作为哨兵。
double slope_of(const BoSeg& s) {
    if (s.is_vertical) {
        return (s.right.y > s.left.y) ? 1e18 : -1e18;
    }
    const double dx = static_cast<double>(s.right.x - s.left.x);
    return static_cast<double>(s.right.y - s.left.y) / dx;
}

// 事件类型。数值越小优先级越高（同点处 LEFT 先于 CROSS 先于 RIGHT）。
struct BoEvent {
    enum class Kind : unsigned char { kLeft = 0, kCross = 1, kRight = 2 };
    Kind kind;
    double x;
    double y;
    std::size_t s1;   // LEFT/RIGHT：段索引；CROSS：较低段（pre-cross）
    std::size_t s2;   // CROSS：较高段；其它未用
};

// 优先队列：小顶堆（x、y、kind、s1、s2 字典序）。
struct BoEventGreater {
    bool operator()(const BoEvent& a, const BoEvent& b) const {
        if (a.x != b.x) return a.x > b.x;
        if (a.y != b.y) return a.y > b.y;
        const unsigned ka = static_cast<unsigned>(a.kind);
        const unsigned kb = static_cast<unsigned>(b.kind);
        if (ka != kb) return ka > kb;
        if (a.s1 != b.s1) return a.s1 > b.s1;
        return a.s2 > b.s2;
    }
};

// 扫描状态共享上下文（比较器持有指针，sweep_x 可变）。
struct SweepState {
    double sweep_x = 0.0;
    const std::vector<BoSeg>* segs = nullptr;
};

// 状态线比较器：按当前 sweep_x 处的 y 升序；同 y 用斜率；最终用段 idx。
struct StatusCmp {
    const SweepState* state;
    bool operator()(std::size_t a, std::size_t b) const {
        const BoSeg& sa = (*state->segs)[a];
        const BoSeg& sb = (*state->segs)[b];
        const double ya = y_at_x(sa, state->sweep_x);
        const double yb = y_at_x(sb, state->sweep_x);
        if (ya < yb) return true;
        if (ya > yb) return false;
        const double ma = slope_of(sa);
        const double mb = slope_of(sb);
        if (ma < mb) return true;
        if (ma > mb) return false;
        return sa.idx < sb.idx;
    }
};

// 构造 (lo, hi) 键，避免 std::minmax 的引用悬挂。
inline std::pair<std::size_t, std::size_t>
make_key(std::size_t a, std::size_t b) {
    return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
}

}  // namespace

// ===================== segment_segment_intersect =====================

std::optional<Point> segment_segment_intersect(const Segment& a, const Segment& b) {
    const Point p1 = a.a, p2 = a.b, p3 = b.a, p4 = b.b;

    // 方向向量（long double 规避 int64 乘积溢出；板级坐标差 ~10^9，乘积 ~10^18）。
    const long double dx1 = static_cast<long double>(p2.x - p1.x);
    const long double dy1 = static_cast<long double>(p2.y - p1.y);
    const long double dx2 = static_cast<long double>(p4.x - p3.x);
    const long double dy2 = static_cast<long double>(p4.y - p3.y);

    // denom = (p2-p1) × (p4-p3)。0 ⇒ 平行 / 共线（退化）。
    const long double denom = dx1 * dy2 - dy1 * dx2;
    if (denom == 0.0L) return std::nullopt;

    const long double dx3 = static_cast<long double>(p3.x - p1.x);
    const long double dy3 = static_cast<long double>(p3.y - p1.y);

    // t = ((p3-p1) × (p4-p3)) / denom；u = ((p3-p1) × (p2-p1)) / denom
    const long double t_num = dx3 * dy2 - dy3 * dx2;
    const long double u_num = dx3 * dy1 - dy3 * dx1;

    // t, u ∈ [0, 1] 的整数比较（避免除法）。
    const auto in_range = [](long double num, long double den) -> bool {
        if (den > 0.0L) return num >= 0.0L && num <= den;
        return num <= 0.0L && num >= den;
    };
    if (!in_range(t_num, denom) || !in_range(u_num, denom)) {
        return std::nullopt;
    }

    // 交点 = p1 + (t_num / denom) * (p2 - p1)
    const long double x = static_cast<long double>(p1.x) + t_num * dx1 / denom;
    const long double y = static_cast<long double>(p1.y) + t_num * dy1 / denom;

    return Point{round_nm(x), round_nm(y)};
}

// ===================== segment_arc_intersect =====================

std::vector<Point> segment_arc_intersect(const Segment& s, const Arc& arc) {
    std::vector<Point> result;
    if (arc.radius <= 0) return result;  // 退化弧（零半径）

    const double ax = static_cast<double>(s.a.x);
    const double ay = static_cast<double>(s.a.y);
    const double bx = static_cast<double>(s.b.x);
    const double by = static_cast<double>(s.b.y);
    const double cx = static_cast<double>(arc.center.x);
    const double cy = static_cast<double>(arc.center.y);
    const double r = static_cast<double>(arc.radius);

    // 线段参数方程 P(t) = a + t·(b-a)，代入 |P - c|² = r² 得：
    //   A·t² + B·t + C = 0，A = |b-a|²，B = 2·(a-c)·(b-a)，C = |a-c|² - r²
    const double fx = ax - cx, fy = ay - cy;
    const double dx = bx - ax, dy = by - ay;
    const double A = dx * dx + dy * dy;
    if (A <= 0.0) return result;  // 零长线段

    const double B = 2.0 * (fx * dx + fy * dy);
    const double C = fx * fx + fy * fy - r * r;
    const double disc = B * B - 4.0 * A * C;

    if (disc < -1e-9) return result;  // 无实根：线段所在直线不切圆

    double roots[2];
    int n_roots;
    if (disc <= 1e-9) {
        // 判别式 ≈ 0：相切，单根
        roots[0] = -B / (2.0 * A);
        n_roots = 1;
    } else {
        const double sq = std::sqrt(disc);
        roots[0] = (-B - sq) / (2.0 * A);
        roots[1] = (-B + sq) / (2.0 * A);
        n_roots = 2;
    }

    for (int i = 0; i < n_roots; ++i) {
        const double t = roots[i];
        if (t < -1e-9 || t > 1.0 + 1e-9) continue;  // 落在线段参数范围外
        const double px = ax + t * dx;
        const double py = ay + t * dy;
        const double theta = std::atan2(py - cy, px - cx);
        if (!angle_on_arc(theta, arc)) continue;  // 落在弧角度范围外
        result.push_back(Point{round_nm_d(px), round_nm_d(py)});
    }

    // 去重（相切单点 / 端点重合）
    std::sort(result.begin(), result.end(), [](const Point& p, const Point& q) {
        if (p.x != q.x) return p.x < q.x;
        return p.y < q.y;
    });
    result.erase(std::unique(result.begin(), result.end()), result.end());

    return result;
}

// ===================== polyline_self_intersections (Bentley-Ottmann) =====================

std::vector<std::pair<Point, std::pair<std::size_t, std::size_t>>>
polyline_self_intersections(const Polyline& poly) {
    using Result = std::pair<Point, std::pair<std::size_t, std::size_t>>;
    std::vector<Result> results;

    // < 4 点 ⇒ < 3 段 ⇒ 无非相邻段对。
    if (poly.points.size() < 4) return results;

    // 紧凑段列表（跳过零长段）。idx 保留原 polyline 段索引，用于相邻判定。
    std::vector<BoSeg> segs;
    for (std::size_t i = 0; i + 1 < poly.points.size(); ++i) {
        const Point a = poly.points[i];
        const Point b = poly.points[i + 1];
        if (a == b) continue;
        segs.push_back(make_bo_seg(i, a, b));
    }
    if (segs.size() < 2) return results;

    // 两段在 polyline 中相邻 ⇔ 原段 idx 差 ≤ 1。
    const auto is_poly_adjacent = [&](std::size_t a, std::size_t b) -> bool {
        const std::size_t ia = segs[a].idx;
        const std::size_t ib = segs[b].idx;
        const std::size_t d = (ia > ib) ? (ia - ib) : (ib - ia);
        return d <= 1;
    };

    // 初始事件（端点 LEFT/RIGHT）
    std::priority_queue<BoEvent, std::vector<BoEvent>, BoEventGreater> pq;
    for (std::size_t i = 0; i < segs.size(); ++i) {
        pq.push(BoEvent{BoEvent::Kind::kLeft,
                        static_cast<double>(segs[i].left.x),
                        static_cast<double>(segs[i].left.y), i, 0});
        pq.push(BoEvent{BoEvent::Kind::kRight,
                        static_cast<double>(segs[i].right.x),
                        static_cast<double>(segs[i].right.y), i, 0});
    }

    SweepState state;
    state.segs = &segs;
    StatusCmp cmp{&state};
    std::set<std::size_t, StatusCmp> status(cmp);
    std::unordered_map<std::size_t, std::set<std::size_t>::iterator> pos;

    // 已调度但尚未处理的 CROSS 对，避免重复入队 / 重复报告。
    std::set<std::pair<std::size_t, std::size_t>> active_cross_pairs;

    // 检查两段是否相交；若交点严格在 sweep_x 右侧，调度 CROSS 事件。
    const auto schedule_cross = [&](std::size_t a, std::size_t b) -> void {
        if (is_poly_adjacent(a, b)) return;
        const auto key = make_key(a, b);
        if (active_cross_pairs.count(key) != 0) return;

        const Segment sa{segs[a].left, segs[a].right};
        const Segment sb{segs[b].left, segs[b].right};
        const auto ip = segment_segment_intersect(sa, sb);
        if (!ip) return;

        const double ix = static_cast<double>(ip->x);
        const double iy = static_cast<double>(ip->y);
        // 仅当交点不在 sweep_x 严格左侧时调度。允许 ix == sweep_x（共享端点的非相邻段），
        // 以便捕获"折线回到已访问点"形成的自交。
        if (ix < state.sweep_x - kSchedTol) return;

        active_cross_pairs.insert(key);

        // 确定调度时刻的 lower/upper（pre-cross）
        const double ya = y_at_x(segs[a], state.sweep_x);
        const double yb = y_at_x(segs[b], state.sweep_x);
        std::size_t lower, upper;
        if (ya <= yb) { lower = a; upper = b; }
        else          { lower = b; upper = a; }

        pq.push(BoEvent{BoEvent::Kind::kCross, ix, iy, lower, upper});
    };

    constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    while (!pq.empty()) {
        const BoEvent e = pq.top();
        pq.pop();

        if (e.kind == BoEvent::Kind::kLeft) {
            state.sweep_x = e.x;
            const auto ins = status.insert(e.s1);
            pos[e.s1] = ins.first;
            const auto it = ins.first;
            if (it != status.begin()) {
                schedule_cross(*std::prev(it), e.s1);
            }
            const auto nit = std::next(it);
            if (nit != status.end()) {
                schedule_cross(e.s1, *nit);
            }
        } else if (e.kind == BoEvent::Kind::kRight) {
            state.sweep_x = e.x;
            const auto it = pos[e.s1];
            std::size_t pred = kNone, succ = kNone;
            if (it != status.begin()) pred = *std::prev(it);
            const auto nit = std::next(it);
            if (nit != status.end()) succ = *nit;

            status.erase(it);
            pos.erase(e.s1);

            if (pred != kNone && succ != kNone) {
                schedule_cross(pred, succ);
            }
        } else {  // CROSS
            // 先验证 CROSS 仍有效（两段仍在状态线中且未被处理过）
            const auto it1 = pos.find(e.s1);
            const auto it2 = pos.find(e.s2);
            if (it1 == pos.end() || it2 == pos.end()) continue;
            if (active_cross_pairs.erase(make_key(e.s1, e.s2)) == 0) continue;

            // 确认 pre-cross 顺序下二者相邻：lower 后紧跟 upper
            std::set<std::size_t>::iterator lower_it, upper_it;
            const auto n1 = std::next(it1->second);
            if (n1 != status.end() && *n1 == e.s2) {
                lower_it = it1->second;
                upper_it = n1;
            } else {
                const auto n2 = std::next(it2->second);
                if (n2 != status.end() && *n2 == e.s1) {
                    lower_it = it2->second;
                    upper_it = n2;
                } else {
                    continue;  // 已不相邻：被其它事件分开，CROSS 失效
                }
            }

            // 设 sweep_x 为交叉点 - tiny：保持 pre-cross 顺序一致性
            state.sweep_x = e.x - kTiny;

            // 记录交点
            const Point ip{round_nm_d(e.x), round_nm_d(e.y)};
            const std::size_t ia = segs[e.s1].idx;
            const std::size_t ib = segs[e.s2].idx;
            results.push_back(
                {ip, {std::min(ia, ib), std::max(ia, ib)}});

            // 记录 pred / succ（重排前的邻居）
            std::size_t pred = kNone, succ = kNone;
            if (lower_it != status.begin()) pred = *std::prev(lower_it);
            const auto succ_it = std::next(upper_it);
            if (succ_it != status.end()) succ = *succ_it;

            // 移除相交两段（按迭代器，不触发比较器）
            status.erase(lower_it);
            status.erase(upper_it);
            pos.erase(e.s1);
            pos.erase(e.s2);

            // 设 sweep_x 为交叉点 + tiny：post-cross 顺序
            state.sweep_x = e.x + kTiny;

            // 重插：比较器把 lower 段放到上方、upper 段放到下方（已交换）
            const auto i1 = status.insert(e.s1);
            pos[e.s1] = i1.first;
            const auto i2 = status.insert(e.s2);
            pos[e.s2] = i2.first;

            // 交换后顺序：pred, e.s2（原 upper，现在下）, e.s1（原 lower，现在上）, succ
            if (pred != kNone) schedule_cross(e.s2, pred);
            if (succ != kNone) schedule_cross(e.s1, succ);
        }
    }

    // 去重并按 (段 idx i, 段 idx j, point) 排序
    std::sort(results.begin(), results.end(),
              [](const Result& a, const Result& b) {
                  if (a.second.first != b.second.first)
                      return a.second.first < b.second.first;
                  if (a.second.second != b.second.second)
                      return a.second.second < b.second.second;
                  if (a.first.x != b.first.x) return a.first.x < b.first.x;
                  return a.first.y < b.first.y;
              });
    results.erase(std::unique(results.begin(), results.end(),
                              [](const Result& a, const Result& b) {
                                  return a.second == b.second && a.first == b.first;
                              }),
                  results.end());

    return results;
}

}  // namespace eda::geometry
