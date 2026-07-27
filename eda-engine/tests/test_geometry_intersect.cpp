// tests/test_geometry_intersect.cpp
//
// P3 几何内核 · Task 7 测试：求交（segment/segment、segment/arc、polyline 自交）。
//
// 对应 plan「几何与命令栈实现计划.md」Task 7 Step 1：
//   - 线段-线段：相交 / 不相交 / 平行 / 共线（退化）/ 端点相切 / T 接
//   - 线段-弧：弦双交点 / 相切单点 / 相离 / 弧角度范围限制
//   - 折线自交：无自交 / 单交（figure-8）/ 相邻段共享端点不算自交 /
//     垂直段处理 / 多个交叉点
//
// 集成时由主循环在 tests/CMakeLists.txt 追加：
//   add_executable(test_geometry_intersect test_geometry_intersect.cpp)
//   target_link_libraries(test_geometry_intersect PRIVATE eda_geometry gtest_main)
//   gtest_discover_tests(test_geometry_intersect)

#include <gtest/gtest.h>

#include "eda/geometry/types.h"
#include "eda/geometry/units.h"
#include "eda/geometry/intersect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eda::geometry;

// ===================== segment_segment_intersect =====================

TEST(SegmentSegmentTest, BasicCrossing) {
    // X 形：两条对角线交于 (5, 5)
    Segment a{{0, 0}, {10, 10}};
    Segment b{{0, 10}, {10, 0}};
    const auto ip = segment_segment_intersect(a, b);
    ASSERT_TRUE(ip.has_value());
    EXPECT_EQ(*ip, (Point{5, 5}));
}

TEST(SegmentSegmentTest, NoIntersectionDisjoint) {
    // 两条不相交的水平线段
    Segment a{{0, 0}, {10, 0}};
    Segment b{{0, 10}, {10, 10}};
    EXPECT_FALSE(segment_segment_intersect(a, b).has_value());
}

TEST(SegmentSegmentTest, ParallelNoIntersection) {
    // 平行（denom == 0）：无交点
    Segment a{{0, 0}, {10, 0}};
    Segment b{{0, 5}, {10, 5}};
    EXPECT_FALSE(segment_segment_intersect(a, b).has_value());
}

TEST(SegmentSegmentTest, CollinearOverlappingDegenerate) {
    // 共线重叠：退化（无唯一交点），返回 nullopt
    Segment a{{0, 0}, {10, 0}};
    Segment b{{5, 0}, {15, 0}};
    EXPECT_FALSE(segment_segment_intersect(a, b).has_value());
}

TEST(SegmentSegmentTest, CollinearDisjointNoIntersection) {
    // 共线但不重叠：无交点
    Segment a{{0, 0}, {5, 0}};
    Segment b{{10, 0}, {15, 0}};
    EXPECT_FALSE(segment_segment_intersect(a, b).has_value());
}

TEST(SegmentSegmentTest, EndpointTangent) {
    // 两段共享端点 (10, 10)：交点为该端点
    Segment a{{0, 0}, {10, 10}};
    Segment b{{10, 10}, {20, 0}};
    const auto ip = segment_segment_intersect(a, b);
    ASSERT_TRUE(ip.has_value());
    EXPECT_EQ(*ip, (Point{10, 10}));
}

TEST(SegmentSegmentTest, TJunction) {
    // 一段端点 (5, -5→5) 落在另一段 (0,0→10,0) 上：T 接，交点 (5, 0)
    Segment a{{0, 0}, {10, 0}};
    Segment b{{5, -5}, {5, 5}};
    const auto ip = segment_segment_intersect(a, b);
    ASSERT_TRUE(ip.has_value());
    EXPECT_EQ(*ip, (Point{5, 0}));
}

TEST(SegmentSegmentTest, SymmetricArguments) {
    // 参数顺序不影响结果
    Segment a{{0, 0}, {10, 10}};
    Segment b{{0, 10}, {10, 0}};
    EXPECT_EQ(*segment_segment_intersect(a, b),
              *segment_segment_intersect(b, a));
}

TEST(SegmentSegmentTest, ZeroLengthSegmentReturnsNullopt) {
    // 零长段（a == b）→ denom == 0 → nullopt
    Segment a{{5, 5}, {5, 5}};
    Segment b{{0, 0}, {10, 10}};
    EXPECT_FALSE(segment_segment_intersect(a, b).has_value());
}

// ===================== segment_arc_intersect =====================

TEST(SegmentArcTest, ChordTwoPoints) {
    // 上半圆弧（start=0, end=π, ccw），水平弦 y=500 穿过：两个交点
    const Coord r = 1000;
    Arc arc{Point{0, 0}, r, 0.0, M_PI, true};
    Segment s{{-1000, 500}, {1000, 500}};
    const auto pts = segment_arc_intersect(s, arc);
    ASSERT_EQ(pts.size(), 2u);
    // 圆方程 x² + 500² = 1000² → x = ±sqrt(750000) ≈ ±866
    const auto smaller_x = std::min(pts[0].x, pts[1].x);
    const auto larger_x = std::max(pts[0].x, pts[1].x);
    EXPECT_NEAR(smaller_x, -866, 2);
    EXPECT_NEAR(larger_x, 866, 2);
    EXPECT_NEAR(pts[0].y, 500, 2);
    EXPECT_NEAR(pts[1].y, 500, 2);
}

TEST(SegmentArcTest, TangentOnePoint) {
    // 整圆，y = r 的切线：单点 (0, r)
    const Coord r = 1000;
    Arc arc{Point{0, 0}, r, 0.0, 0.0, true};  // start==end → 整圆
    Segment s{{-1000, r}, {1000, r}};
    const auto pts = segment_arc_intersect(s, arc);
    ASSERT_EQ(pts.size(), 1u);
    EXPECT_NEAR(pts[0].x, 0, 2);
    EXPECT_NEAR(pts[0].y, r, 2);
}

TEST(SegmentArcTest, LineMissesCircle) {
    // 线段所在直线与圆无交：disc < 0
    Arc arc{Point{0, 0}, 1000, 0.0, 2.0 * M_PI, true};
    Segment s{{-1000, 5000}, {1000, 5000}};  // y = 5000，远离半径 1000 圆
    const auto pts = segment_arc_intersect(s, arc);
    EXPECT_TRUE(pts.empty());
}

TEST(SegmentArcTest, ArcSectorRestrictsHits) {
    // 上半圆弧；下半平面的弦虽与圆相交，但交点角度不在弧跨度内
    Arc arc{Point{0, 0}, 1000, 0.0, M_PI, true};  // 上半圆
    Segment s{{-1000, -500}, {1000, -500}};       // 下半平面水平弦
    const auto pts = segment_arc_intersect(s, arc);
    EXPECT_TRUE(pts.empty());
}

TEST(SegmentArcTest, SegmentEndpointOnArcStart) {
    // 弧起点正好落在线段上：应报告该点
    Arc arc{Point{0, 0}, 1000, 0.0, M_PI / 2, true};  // 起点 (1000, 0)
    Segment s{{2000, 0}, {0, 0}};                      // 水平线段，过 (1000, 0)
    const auto pts = segment_arc_intersect(s, arc);
    ASSERT_EQ(pts.size(), 1u);
    EXPECT_NEAR(pts[0].x, 1000, 2);
    EXPECT_NEAR(pts[0].y, 0, 2);
}

TEST(SegmentArcTest, FullCircleChordTwoPoints) {
    // 整圆 + 穿心水平弦：两个对径点
    Arc arc{Point{0, 0}, 1000, 0.0, 0.0, true};
    Segment s{{-2000, 0}, {2000, 0}};  // y=0，过圆心
    const auto pts = segment_arc_intersect(s, arc);
    ASSERT_EQ(pts.size(), 2u);
    const auto smaller_x = std::min(pts[0].x, pts[1].x);
    const auto larger_x = std::max(pts[0].x, pts[1].x);
    EXPECT_NEAR(smaller_x, -1000, 2);
    EXPECT_NEAR(larger_x, 1000, 2);
}

// ===================== polyline_self_intersections =====================

// 辅助：在结果集中查找指定段索引对的交点。
static bool has_pair(
    const std::vector<std::pair<Point, std::pair<std::size_t, std::size_t>>>& v,
    std::size_t i, std::size_t j) {
    return std::any_of(v.begin(), v.end(),
        [&](const auto& r) { return r.second.first == i && r.second.second == j; });
}

TEST(PolylineSelfTest, NoSelfIntersectionSimpleZ) {
    // Z 形：3 段，s_0 与 s_2 不相邻但不交
    Polyline pl{{{0, 0}, {1000, 0}, {1000, 1000}, {2000, 1000}}};
    const auto r = polyline_self_intersections(pl);
    EXPECT_TRUE(r.empty());
}

TEST(PolylineSelfTest, Figure8SingleCrossing) {
    // 蝶形：s_0 (0,0)→(2000,2000) 与 s_2 (2000,0)→(0,2000) 交于 (1000, 1000)
    Polyline pl{{{0, 0}, {2000, 2000}, {2000, 0}, {0, 2000}}};
    const auto r = polyline_self_intersections(pl);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].first, (Point{1000, 1000}));
    EXPECT_EQ(r[0].second.first, 0u);
    EXPECT_EQ(r[0].second.second, 2u);
}

TEST(PolylineSelfTest, AdjacentSharedEndpointNotFlagged) {
    // 相邻段共享端点 (1000, 1000)：不算自交
    Polyline pl{{{0, 0}, {1000, 1000}, {2000, 0}}};
    const auto r = polyline_self_intersections(pl);
    EXPECT_TRUE(r.empty());
}

TEST(PolylineSelfTest, VerticalSegmentsNoFalsePositive) {
    // 含两段垂直、两段水平的阶梯折线：无非相邻相交
    Polyline pl{{{0, 0}, {0, 1000}, {1000, 1000}, {1000, 0}, {2000, 0}}};
    const auto r = polyline_self_intersections(pl);
    EXPECT_TRUE(r.empty());
}

TEST(PolylineSelfTest, MultipleCrossings) {
    // 5 点折线，s_0 与 s_2、s_0 与 s_3 各有一个交点
    Polyline pl{{{0, 0},
                 {4000, 4000},
                 {4000, 0},
                 {0, 4000},
                 {2000, -1000}}};
    const auto r = polyline_self_intersections(pl);
    EXPECT_EQ(r.size(), 2u);
    // s_0 vs s_2 交于 (2000, 2000)
    ASSERT_TRUE(has_pair(r, 0, 2));
    for (const auto& res : r) {
        if (res.second == std::make_pair(std::size_t{0}, std::size_t{2})) {
            EXPECT_EQ(res.first, (Point{2000, 2000}));
        }
        if (res.second == std::make_pair(std::size_t{0}, std::size_t{3})) {
            // s_0 vs s_3：8000/7 ≈ 1142.857 → 四舍五入 1143
            EXPECT_NEAR(res.first.x, 1143, 1);
            EXPECT_NEAR(res.first.y, 1143, 1);
        }
    }
    EXPECT_TRUE(has_pair(r, 0, 3));
}

TEST(PolylineSelfTest, TooFewPointsReturnsEmpty) {
    Polyline pl{{{0, 0}, {1000, 0}, {1000, 1000}}};  // 3 点 → 2 段
    const auto r = polyline_self_intersections(pl);
    EXPECT_TRUE(r.empty());
}

TEST(PolylineSelfTest, TJunctionEndpointOnSegment) {
    // s_2 端点 (1500,0) 落在 s_0 内部：T 接，唯一交点，应报告
    Polyline pl{{{0, 0}, {2000, 0}, {500, 500}, {1500, 0}}};
    const auto r = polyline_self_intersections(pl);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].first, (Point{1500, 0}));
    EXPECT_EQ(r[0].second.first, 0u);
    EXPECT_EQ(r[0].second.second, 2u);
}

TEST(PolylineSelfTest, SharedEndpointNonAdjacent) {
    // 折线回到已访问点：s_0 端点 (5,5) 与 s_2 端点 (5,5) 重合，非相邻（|0-2|=2）
    // 共享端点应被报告为自交（捕获"折线回到自身"的短路）
    Polyline pl{{{0, 0}, {5, 5}, {10, 0}, {5, 5}}};
    const auto r = polyline_self_intersections(pl);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].first, (Point{5, 5}));
    EXPECT_EQ(r[0].second.first, 0u);
    EXPECT_EQ(r[0].second.second, 2u);
}

TEST(PolylineSelfTest, ZeroLengthSegmentsSkipped) {
    // 含重复点（零长段）：算法跳过，不影响其余段的判定
    Polyline pl{{{0, 0}, {0, 0}, {2000, 2000}, {2000, 0}, {0, 2000}}};
    // 重复点 (0,0)→(0,0) 跳过后，s_0: (0,0)→(2000,2000), s_2: (2000,0)→(0,2000)
    const auto r = polyline_self_intersections(pl);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].first, (Point{1000, 1000}));
}
