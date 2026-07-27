// tests/test_geometry_types.cpp
//
// P3 几何内核 · Task 1 测试：图元类型 + 单位 + 变换 + 吸附。
//
// 对应 plan「几何与命令栈实现计划.md」Task 1 Step 1 的 17 个 GoogleTest 用例：
//   3 Units + 1 Point + 3 Box + 1 Arc + 1 Polygon + 1 Path + 2 Snap + 5 Transform = 17
//
// 集成时由主循环在 tests/CMakeLists.txt 追加：
//   add_executable(test_geometry_types test_geometry_types.cpp)
//   target_include_directories(test_geometry_types PRIVATE ${EDA_ROOT})
//   target_link_libraries(test_geometry_types PRIVATE eda_geometry gtest_main)
//   gtest_discover_tests(test_geometry_types)

#include <gtest/gtest.h>

#include "eda/geometry/types.h"
#include "eda/geometry/units.h"
#include "eda/geometry/transform.h"
#include "eda/geometry/snap.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eda::geometry;

// ---------- 单位转换（D1：nm 整数，无损往返） ----------
TEST(UnitsTest, MmRoundTripZeroError) {
    Coord nm = mm_to_nm(1.0);                 // 1 mm → 1_000_000 nm
    EXPECT_EQ(nm, 1'000'000);
    EXPECT_NEAR(nm_to_mm(nm), 1.0, 1e-9);      // nm → mm 误差为 0
}
TEST(UnitsTest, MilRoundTrip) {
    Coord nm = mil_to_nm(1.0);                 // 1 mil = 25_400 nm
    EXPECT_EQ(nm, 25'400);
    EXPECT_NEAR(nm_to_mil(nm), 1.0, 1e-9);
}
TEST(UnitsTest, InchConsistency) {
    // 1 inch = 1000 mil = 25.4 mm，三者转 nm 必须相等
    EXPECT_EQ(inch_to_nm(1.0), mil_to_nm(1000));
    EXPECT_EQ(inch_to_nm(1.0), mm_to_nm(25.4));
    EXPECT_NEAR(nm_to_inch(inch_to_nm(1.0)), 1.0, 1e-9);
}

// ---------- Point ----------
TEST(PointTest, EqualityAndDefault) {
    Point a{1, 2}, b{1, 2}, c{1, 3};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    Point d;                                   // 默认构造 = 原点
    EXPECT_EQ(d, (Point{0, 0}));
}

// ---------- Box ----------
TEST(BoxTest, FromPoints) {
    Point pts[] = {{1, 2}, {3, 4}, {0, 5}};
    Box b = Box::from_points(pts);
    EXPECT_EQ(b.min, (Point{0, 2}));
    EXPECT_EQ(b.max, (Point{3, 5}));
}
TEST(BoxTest, IntersectsAndContains) {
    Box a{{0, 0}, {10, 10}};
    Box b{{5, 5}, {15, 15}};
    EXPECT_TRUE(a.intersects(b));
    Box c{{20, 20}, {30, 30}};
    EXPECT_FALSE(a.intersects(c));
    EXPECT_TRUE(a.contains({5, 5}));
    EXPECT_FALSE(a.contains({10, 11}));        // 上界不含
}
TEST(BoxTest, Merge) {
    Box a{{0, 0}, {10, 10}}, b{{5, 5}, {20, 5}};
    Box m = a.merge(b);
    EXPECT_EQ(m.min, (Point{0, 0}));
    EXPECT_EQ(m.max, (Point{20, 10}));
}

// ---------- Arc / Polygon / Path 结构 ----------
TEST(ArcTest, Fields) {
    Arc a{Point{0, 0}, 1000, 0.0, M_PI / 2, true};
    EXPECT_EQ(a.center, (Point{0, 0}));
    EXPECT_EQ(a.radius, 1000);
    EXPECT_DOUBLE_EQ(a.start_angle, 0.0);
    EXPECT_TRUE(a.ccw);
}
TEST(PolygonTest, WithHoles) {
    Polygon p;
    p.outer = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    p.holes.push_back({{20, 20}, {80, 20}, {80, 80}, {20, 80}});
    EXPECT_EQ(p.outer.size(), 4u);
    EXPECT_EQ(p.holes.size(), 1u);
    EXPECT_EQ(p.holes[0].size(), 4u);
}
TEST(PathTest, CenterlineAndWidth) {
    Path p{{{0, 0}, {1000, 0}}, 200};          // 中心线 + 200nm 宽
    EXPECT_EQ(p.points.size(), 2u);
    EXPECT_EQ(p.width, 200);
}

// ---------- 网格吸附 ----------
TEST(SnapTest, SnapToGrid1mm) {
    Coord grid = mm_to_nm(1.0);                 // 1 mm 栅格
    Point p{mm_to_nm(0.4), mm_to_nm(0.6)};
    Point s = snap_to_grid(p, grid);
    EXPECT_EQ(s, (Point{0, mm_to_nm(1.0)}));
}
TEST(SnapTest, SnapToGridNegativeAxis) {
    Coord grid = mm_to_nm(0.5);
    Point p{mm_to_nm(-0.3), mm_to_nm(-0.7)};
    Point s = snap_to_grid(p, grid);
    EXPECT_EQ(s, (Point{mm_to_nm(-0.5), mm_to_nm(-0.5)}));
}

// ---------- 变换 ----------
TEST(TransformTest, TranslatePoint) {
    EXPECT_EQ(translate(Point{1, 2}, 10, 20), (Point{11, 22}));
}
TEST(TransformTest, TranslatePolyline) {
    Polyline pl{{{0, 0}, {100, 0}}};
    auto t = translate(pl, 5, 7);
    EXPECT_EQ(t.points[0], (Point{5, 7}));
    EXPECT_EQ(t.points[1], (Point{105, 7}));
}
TEST(TransformTest, Rotate90AroundOrigin) {
    Point r = rotate(Point{1000, 0}, M_PI / 2, Point{0, 0});
    EXPECT_NEAR(r.x, 0, 1);
    EXPECT_NEAR(r.y, 1000, 1);
}
TEST(TransformTest, MirrorAxes) {
    EXPECT_EQ(mirror(Point{5, 9}, MirrorAxis::Y), (Point{-5, 9}));
    EXPECT_EQ(mirror(Point{5, 9}, MirrorAxis::X), (Point{5, -9}));
    EXPECT_EQ(mirror(Point{5, 9}, MirrorAxis::ORIGIN), (Point{-5, -9}));
}
TEST(TransformTest, ScaleUniform) {
    Point r = scale(Point{100, 0}, 2.0, 2.0, Point{0, 0});
    EXPECT_EQ(r, (Point{200, 0}));
}
