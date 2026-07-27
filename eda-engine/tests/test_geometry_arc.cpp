// tests/test_geometry_arc.cpp
//
// P3 几何内核 · Task 2 测试：圆弧离散化（Arc → Polyline）。
//
// 对应 plan「几何与命令栈实现计划.md」Task 2 Step 1：
//   - 90° 圆弧离散点数 ≥ 下限（验证自适应公式生效）
//   - 每段弦高 ≤ max_chord_error
//   - 整圆闭合首尾重合
//   - ccw vs cw 方向
//   - 零半径退化为点
//   - 固定分段重载端点精确
//
// 集成时由主循环在 tests/CMakeLists.txt 追加：
//   add_executable(test_geometry_arc test_geometry_arc.cpp)
//   target_link_libraries(test_geometry_arc PRIVATE eda_geometry gtest_main)
//   gtest_discover_tests(test_geometry_arc)

#include <gtest/gtest.h>

#include "eda/geometry/types.h"
#include "eda/geometry/units.h"
#include "eda/geometry/arc_tessellate.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eda::geometry;

namespace {

// 半径 r（nm）、半弦长 h（nm）→ 该弦对应的圆弧弦高（sagitta）。
// sagitta = r - sqrt(r² - h²)。h >= r 时返回 r（退化）。
double sagitta_from_half_chord(double r_nm, double half_chord_nm) {
    if (half_chord_nm >= r_nm) return r_nm;
    const double d = std::sqrt(r_nm * r_nm - half_chord_nm * half_chord_nm);
    return r_nm - d;
}

}  // namespace

// ---------- 90° 圆弧：点数下限 + 首尾端点精确 ----------
TEST(ArcTessellateTest, QuarterArcPointCountLowerBound) {
    // 半径 1mm、容差 1μm：理论 n = ceil((π/2) / (2·acos(1 - 0.001))) ≈ 18
    Arc a{Point{0, 0}, mm_to_nm(1.0), 0.0, M_PI / 2, true};
    Polyline pl = arc_to_polyline(a, 0.001);  // 1μm 容差
    // 自适应公式应给出远多于"最小段数"的采样点
    EXPECT_GE(pl.points.size(), 15u);
    // 首点 ≈ 弧起点（角度 0 → (r, 0)）
    EXPECT_NEAR(pl.points.front().x, mm_to_nm(1.0), 1);
    EXPECT_NEAR(pl.points.front().y, 0, 1);
    // 末点 ≈ 弧终点（角度 π/2 → (0, r)）
    EXPECT_NEAR(pl.points.back().x, 0, 1);
    EXPECT_NEAR(pl.points.back().y, mm_to_nm(1.0), 1);
}

// ---------- 每段弦高 ≤ max_chord_error ----------
TEST(ArcTessellateTest, SagittaWithinTolerance) {
    const Coord r = mm_to_nm(1.0);
    const double tol_mm = 0.001;  // 1μm
    Arc a{Point{1000, 2000}, r, 0.0, M_PI / 2, true};
    Polyline pl = arc_to_polyline(a, tol_mm);
    ASSERT_GE(pl.points.size(), 2u);

    const double r_nm = static_cast<double>(r);
    const double tol_nm = tol_mm * 1e6;
    const double slack_nm = 2.0;  // 四舍五入到 nm 引入的额外误差
    for (std::size_t i = 1; i < pl.points.size(); ++i) {
        const double dx = static_cast<double>(pl.points[i].x - pl.points[i - 1].x);
        const double dy = static_cast<double>(pl.points[i].y - pl.points[i - 1].y);
        const double chord_len = std::sqrt(dx * dx + dy * dy);
        const double sagitta = sagitta_from_half_chord(r_nm, chord_len / 2.0);
        EXPECT_LE(sagitta, tol_nm + slack_nm)
            << "segment " << i << " sagitta " << sagitta << "nm exceeds tol "
            << tol_nm << "nm";
    }
}

// ---------- 整圆闭合：首尾重合 ----------
TEST(ArcTessellateTest, FullCircleClosed) {
    Arc a{Point{0, 0}, mm_to_nm(1.0), 0.0, 0.0, true};  // start == end → 整圆
    Polyline pl = arc_to_polyline(a, 0.001);
    ASSERT_GE(pl.points.size(), 4u);
    EXPECT_EQ(pl.points.front(), pl.points.back());
}

// ---------- ccw vs cw 方向 ----------
TEST(ArcTessellateTest, DirectionCcwVsCw) {
    const Coord r = mm_to_nm(1.0);
    const Point origin{0, 0};

    // CCW: 0 → π/2，短弧（span = π/2）穿过第一象限，中点 y > 0
    Arc a_ccw{origin, r, 0.0, M_PI / 2, true};
    Polyline pl_ccw = arc_to_polyline(a_ccw, 0.001);
    ASSERT_GE(pl_ccw.points.size(), 3u);
    const Point mid_ccw = pl_ccw.points[pl_ccw.points.size() / 2];
    EXPECT_GT(mid_ccw.y, 0);

    // CW: 0 → π/2，长弧（span = 3π/2，顺时针绕远路）穿过第四象限，中点 y < 0
    Arc a_cw{origin, r, 0.0, M_PI / 2, false};
    Polyline pl_cw = arc_to_polyline(a_cw, 0.001);
    ASSERT_GE(pl_cw.points.size(), 3u);
    const Point mid_cw = pl_cw.points[pl_cw.points.size() / 2];
    EXPECT_LT(mid_cw.y, 0);
}

// ---------- 零半径退化为点 ----------
TEST(ArcTessellateTest, ZeroRadiusDegeneratesToPoint) {
    Arc a{Point{5, 7}, 0, 0.0, M_PI / 2, true};
    Polyline pl = arc_to_polyline(a, 0.001);
    ASSERT_EQ(pl.points.size(), 1u);
    EXPECT_EQ(pl.points.front(), (Point{5, 7}));
}

// ---------- 固定分段重载：点数 + 首尾精确 ----------
TEST(ArcTessellateTest, FixedSegments) {
    Arc a{Point{0, 0}, mm_to_nm(1.0), 0.0, M_PI / 2, true};
    Polyline pl = arc_to_polyline_fixed(a, 4);
    EXPECT_EQ(pl.points.size(), 5u);  // 4 段 → 5 点
    EXPECT_EQ(pl.points.front(), (Point{mm_to_nm(1.0), 0}));
    EXPECT_EQ(pl.points.back(), (Point{0, mm_to_nm(1.0)}));
}

// ---------- 固定分段重载：<1 时钳为 1 ----------
TEST(ArcTessellateTest, FixedSegmentsClampedToOne) {
    Arc a{Point{0, 0}, mm_to_nm(1.0), 0.0, M_PI / 2, true};
    Polyline pl = arc_to_polyline_fixed(a, 0);
    EXPECT_EQ(pl.points.size(), 2u);
}

// ---------- ccw 跨 2π 归一化：start=π/2 → end=0（span=3π/2），首尾精确 ----------
TEST(ArcTessellateTest, CcwLongArcWraparound) {
    const Coord r = mm_to_nm(1.0);
    Arc a{Point{0, 0}, r, M_PI / 2, 0.0, true};  // ccw 跨过 2π 回到 0
    Polyline pl = arc_to_polyline(a, 0.001);
    ASSERT_GE(pl.points.size(), 2u);
    // 首点 ≈ (0, r)，末点 ≈ (r, 0)
    EXPECT_NEAR(pl.points.front().x, 0, 1);
    EXPECT_NEAR(pl.points.front().y, r, 1);
    EXPECT_NEAR(pl.points.back().x, r, 1);
    EXPECT_NEAR(pl.points.back().y, 0, 1);
}
