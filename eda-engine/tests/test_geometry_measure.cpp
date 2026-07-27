// tests/test_geometry_measure.cpp
//
// P3 几何内核 · Task 7 测试：测量（length / area / distance / arc_length）。
//
// 对应 plan「几何与命令栈实现计划.md」Task 7 Step 3：
//   - polyline 长度（已知长度反算）
//   - 多边形面积（无孔 / 含孔守恒：外环 - Σ孔）
//   - 点到线段距离（投影在线段内 / 投影落在外取端点 / 点在线段上）
//   - 点到折线距离（取最近段）
//   - 弧长 r·Δθ（四分之一圆 / 半圆 / 整圆）
//
// 集成时由主循环在 tests/CMakeLists.txt 追加：
//   add_executable(test_geometry_measure test_geometry_measure.cpp)
//   target_link_libraries(test_geometry_measure PRIVATE eda_geometry gtest_main)
//   gtest_discover_tests(test_geometry_measure)

#include <gtest/gtest.h>

#include "eda/geometry/types.h"
#include "eda/geometry/units.h"
#include "eda/geometry/measure.h"

#include <cmath>
#include <cstdint>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eda::geometry;

// ===================== length(Polyline) =====================

TEST(MeasureLengthTest, StraightHorizontal) {
    Polyline pl{{{0, 0}, {1000, 0}}};
    EXPECT_EQ(length(pl), 1000);
}

TEST(MeasureLengthTest, MultiSegment345) {
    // 3000 + 4000 直角折线，总长 7000
    Polyline pl{{{0, 0}, {3000, 0}, {3000, 4000}}};
    EXPECT_EQ(length(pl), 7000);
}

TEST(MeasureLengthTest, SinglePointReturnsZero) {
    Polyline pl{{{500, 500}}};
    EXPECT_EQ(length(pl), 0);
}

TEST(MeasureLengthTest, EmptyReturnsZero) {
    Polyline pl{};
    EXPECT_EQ(length(pl), 0);
}

TEST(MeasureLengthTest, DiagonalRoundsToNm) {
    // 3-4-5 单段：长度 5000
    Polyline pl{{{0, 0}, {3000, 4000}}};
    EXPECT_EQ(length(pl), 5000);
}

// ===================== area(Polygon) =====================

TEST(MeasureAreaTest, UnitSquareNoHoles) {
    Polygon pg;
    pg.outer = {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
    EXPECT_DOUBLE_EQ(area(pg), 1'000'000.0);  // 10^6 nm²
}

TEST(MeasureAreaTest, RightTriangle) {
    Polygon pg;
    pg.outer = {{0, 0}, {3000, 0}, {0, 4000}};
    EXPECT_DOUBLE_EQ(area(pg), 6'000'000.0);  // 0.5 * 3000 * 4000
}

TEST(MeasureAreaTest, SquareWithOneHoleConservation) {
    // 外 1000×1000，孔 200×200 居中：面积 = 10^6 - 40000
    Polygon pg;
    pg.outer = {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
    pg.holes.push_back({{400, 400}, {600, 400}, {600, 600}, {400, 600}});
    EXPECT_DOUBLE_EQ(area(pg), 1'000'000.0 - 40'000.0);  // 960000
}

TEST(MeasureAreaTest, SquareWithTwoHoles) {
    // 外 1000×1000，两个 100×100 孔：面积 = 10^6 - 2*10^4
    Polygon pg;
    pg.outer = {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
    pg.holes.push_back({{100, 100}, {200, 100}, {200, 200}, {100, 200}});
    pg.holes.push_back({{800, 800}, {900, 800}, {900, 900}, {800, 900}});
    EXPECT_DOUBLE_EQ(area(pg), 1'000'000.0 - 2.0 * 10'000.0);  // 980000
}

TEST(MeasureAreaTest, RingOrientationIndependence) {
    // CW 外环 vs CCW 外环：取绝对值，结果一致
    Polygon ccw;
    ccw.outer = {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
    Polygon cw;
    cw.outer = {{0, 0}, {0, 1000}, {1000, 1000}, {1000, 0}};
    EXPECT_DOUBLE_EQ(area(ccw), area(cw));
    EXPECT_DOUBLE_EQ(area(ccw), 1'000'000.0);
}

TEST(MeasureAreaTest, HoleLargerThanOuterClampedToZero) {
    // 孔大于外环：clamp 到 0（不应返回负值）
    Polygon pg;
    pg.outer = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    pg.holes.push_back({{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}});
    EXPECT_DOUBLE_EQ(area(pg), 0.0);
}

TEST(MeasureAreaTest, DegenerateOuterReturnsZero) {
    Polygon pg;
    pg.outer = {{0, 0}, {100, 0}};  // 少于 3 点
    EXPECT_DOUBLE_EQ(area(pg), 0.0);
}

// ===================== distance(Point, Segment) / point_to_segment =====================

TEST(MeasureDistanceTest, PointSegmentPerpendicular) {
    Segment s{{0, 0}, {1000, 0}};
    EXPECT_EQ(point_to_segment({500, 300}, s), 300);
    EXPECT_EQ(distance(Point{500, 300}, s), 300);  // 别名重载
}

TEST(MeasureDistanceTest, PointSegmentProjectionOutsideA) {
    // 投影落在线段外（左侧）：取到端点 a 的距离
    Segment s{{0, 0}, {1000, 0}};
    Point p{-300, 400};
    // 到 (0,0) 距离 = sqrt(300² + 400²) = 500
    EXPECT_EQ(point_to_segment(p, s), 500);
}

TEST(MeasureDistanceTest, PointSegmentProjectionOutsideB) {
    // 投影落在线段外（右侧）：取到端点 b 的距离
    Segment s{{0, 0}, {1000, 0}};
    Point p{2000, 500};
    // 到 (1000,0) 距离 = sqrt(1000² + 500²) = sqrt(1250000) ≈ 1118.034 → 1118
    EXPECT_EQ(point_to_segment(p, s), 1118);
}

TEST(MeasureDistanceTest, PointOnSegmentReturnsZero) {
    Segment s{{0, 0}, {1000, 0}};
    EXPECT_EQ(point_to_segment({500, 0}, s), 0);
    EXPECT_EQ(point_to_segment({0, 0}, s), 0);     // 端点 a
    EXPECT_EQ(point_to_segment({1000, 0}, s), 0);  // 端点 b
}

TEST(MeasureDistanceTest, ZeroLengthSegmentDegeneratesToPoint) {
    Segment s{{500, 500}, {500, 500}};
    EXPECT_EQ(point_to_segment({500, 800}, s), 300);
}

TEST(MeasureDistanceTest, PointPointDistance) {
    EXPECT_EQ(distance(Point{0, 0}, Point{3000, 4000}), 5000);
    EXPECT_EQ(distance(Point{-3, -4}, Point{0, 0}), 5);
}

// ===================== distance(Point, Polyline) =====================

TEST(MeasureDistanceTest, PointPolylineNearestSegment) {
    // L 形折线：(0,0)→(1000,0)→(1000,1000)
    Polyline pl{{{0, 0}, {1000, 0}, {1000, 1000}}};
    // 点 (500, 100)：最近第一段，距离 100
    EXPECT_EQ(distance(Point{500, 100}, pl), 100);
    // 点 (1500, 500)：最近第二段（x=1000），距离 500
    EXPECT_EQ(distance(Point{1500, 500}, pl), 500);
}

TEST(MeasureDistanceTest, PointPolylineEmptyReturnsSentinel) {
    Polyline pl{};
    EXPECT_EQ(distance(Point{0, 0}, pl), std::numeric_limits<Coord>::max());
}

// ===================== arc_length(Arc) =====================

TEST(MeasureArcLengthTest, QuarterCircle) {
    // r=1000，π/2 跨度：弧长 = 1000·π/2 ≈ 1570.796
    Arc a{Point{0, 0}, 1000, 0.0, M_PI / 2, true};
    EXPECT_NEAR(arc_length(a), 1000.0 * M_PI / 2.0, 1e-6);
}

TEST(MeasureArcLengthTest, Semicircle) {
    Arc a{Point{0, 0}, 1000, 0.0, M_PI, true};
    EXPECT_NEAR(arc_length(a), 1000.0 * M_PI, 1e-6);
}

TEST(MeasureArcLengthTest, FullCircle) {
    // start == end → 规范化跨度 2π → 整圆周长 2πr
    Arc a{Point{0, 0}, 1000, 0.0, 0.0, true};
    EXPECT_NEAR(arc_length(a), 2.0 * M_PI * 1000.0, 1e-6);
}

TEST(MeasureArcLengthTest, CwDirectionMatchesCcw) {
    // 同一起止角的 cw 与 ccw：ccw 取短弧（span=π/2），cw 取长弧（span=3π/2）
    Arc ccw{Point{0, 0}, 1000, 0.0, M_PI / 2, true};
    Arc cw{Point{0, 0}, 1000, 0.0, M_PI / 2, false};
    EXPECT_NEAR(arc_length(ccw), 1000.0 * M_PI / 2.0, 1e-6);
    EXPECT_NEAR(arc_length(cw), 1000.0 * 3.0 * M_PI / 2.0, 1e-6);
}

TEST(MeasureArcLengthTest, ZeroRadiusReturnsZero) {
    Arc a{Point{5, 5}, 0, 0.0, M_PI, true};
    EXPECT_DOUBLE_EQ(arc_length(a), 0.0);
}
