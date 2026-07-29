// tests/test_geometry_boolean.cpp
//
// P3 几何内核 · Task 3 测试：布尔运算（unite / subtract / intersect / xor_op）。
//
// 对应 plan「几何与命令栈实现计划.md」Task 3 Step 2：
//   - 两单位方形并集（重叠 → 单结果，面积守恒）
//   - 差集（A \ B 面积正确；被完全包含时返回空）
//   - 交集（A ∩ B 面积正确；不相交时返回空）
//   - 异或（重叠区域扣除后断成两块）
//   - 含孔多边形布尔后面积守恒（外环 + 孔结构保留）
//   - 不相交图形并集 → 两个独立结果
//
// 集成时由主循环在 tests/CMakeLists.txt 追加：
//   add_executable(test_geometry_boolean test_geometry_boolean.cpp)
//   target_link_libraries(test_geometry_boolean PRIVATE eda_geometry gtest_main)
//   gtest_discover_tests(test_geometry_boolean)

#include <gtest/gtest.h>

#include "eda/geometry/types.h"
#include "eda/geometry/boolean.h"

#include <cmath>
#include <cstddef>
#include <vector>

using namespace eda::geometry;

namespace {

// 构造一个矩形外环（无孔），坐标 (x0,y0)-(x1,y1)。
// 外环顺序 (x0,y0)→(x1,y0)→(x1,y1)→(x0,y1) 为 CCW（与 types.h 约定一致）。
Polygon make_square(Coord x0, Coord y0, Coord x1, Coord y1) {
    Polygon p;
    p.outer = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    return p;
}

// 构造一个矩形外环 + 一个矩形孔。
// 外环 CCW；孔 CW（与外环反向，NonZero 下成为真孔）。
// 孔顺序 (hx0,hy0)→(hx0,hy1)→(hx1,hy1)→(hx1,hy0) 为 CW。
Polygon make_square_with_hole(Coord x0, Coord y0, Coord x1, Coord y1,
                              Coord hx0, Coord hy0, Coord hx1, Coord hy1) {
    Polygon p;
    p.outer = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    p.holes.push_back({{hx0, hy0}, {hx0, hy1}, {hx1, hy1}, {hx1, hy0}});
    return p;
}

// 鞋带公式计算单环面积（绝对值，double）。
// 用 long double 累加：10^4 nm 级坐标的乘积约 10^8，求和仍处 double 安全区间。
double ring_area(const std::vector<Point>& ring) {
    const std::size_t n = ring.size();
    if (n < 3) return 0.0;
    long double sum = 0.0L;
    for (std::size_t i = 0; i < n; ++i) {
        const Point& p = ring[i];
        const Point& q = ring[(i + 1) % n];
        sum += static_cast<long double>(p.x) * static_cast<long double>(q.y) -
               static_cast<long double>(p.y) * static_cast<long double>(q.x);
    }
    return std::fabs(static_cast<double>(sum / 2.0L));
}

// Polygon 面积 = |外环面积| - Σ|孔面积|（下界 0）。
double polygon_area(const Polygon& p) {
    double a = ring_area(p.outer);
    for (const auto& hole : p.holes) a -= ring_area(hole);
    return a < 0.0 ? 0.0 : a;
}

// 一组多边形总面积（结果合并后的总面积）。
double total_area(const std::vector<Polygon>& polys) {
    double a = 0.0;
    for (const auto& p : polys) a += polygon_area(p);
    return a;
}

}  // namespace

// ---------- 两单位方形并集 ----------
TEST(BooleanTest, UnionOfOverlappingSquares) {
    const Polygon a = make_square(0, 0, 10, 10);     // 面积 100
    const Polygon b = make_square(5, 0, 15, 10);     // 面积 100，与 a 重叠 5×10=50
    auto result = unite(a, b);
    ASSERT_EQ(result.size(), 1u);                    // 重叠 → 合并为单个矩形
    EXPECT_EQ(result[0].holes.size(), 0u);
    EXPECT_NEAR(polygon_area(result[0]), 150.0, 1e-6);
}

// ---------- 差集 ----------
TEST(BooleanTest, DifferenceOfOverlappingSquares) {
    const Polygon a = make_square(0, 0, 10, 10);
    const Polygon b = make_square(5, 0, 15, 10);
    auto result = subtract(a, b);                    // 期望 [0,5]×[0,10]
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NEAR(polygon_area(result[0]), 50.0, 1e-6);
}

// ---------- 差集：被完全包含时返回空 ----------
TEST(BooleanTest, DifferenceWhenFullyCoveredIsEmpty) {
    const Polygon big = make_square(0, 0, 100, 100);
    const Polygon small = make_square(10, 10, 20, 20);
    auto result = subtract(small, big);              // small 完全在 big 内 → 空
    EXPECT_TRUE(result.empty());
}

// ---------- 交集 ----------
TEST(BooleanTest, IntersectionOfOverlappingSquares) {
    const Polygon a = make_square(0, 0, 10, 10);
    const Polygon b = make_square(5, 0, 15, 10);
    auto result = intersect(a, b);                   // 期望 [5,10]×[0,10]
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NEAR(polygon_area(result[0]), 50.0, 1e-6);
}

// ---------- 交集：不相交时返回空 ----------
TEST(BooleanTest, IntersectionOfDisjointIsEmpty) {
    const Polygon a = make_square(0, 0, 10, 10);
    const Polygon b = make_square(20, 20, 30, 30);   // 与 a 完全不相交
    auto result = intersect(a, b);
    EXPECT_TRUE(result.empty());
}

// ---------- 异或 ----------
TEST(BooleanTest, XorOfOverlappingSquares) {
    const Polygon a = make_square(0, 0, 10, 10);
    const Polygon b = make_square(5, 0, 15, 10);
    auto result = xor_op(a, b);
    // 重叠部分 [5,10]×[0,10] 被扣除 → 两块独立：
    //   [0,5]×[0,10]（a 独有）+ [10,15]×[0,10]（b 独有）
    EXPECT_EQ(result.size(), 2u);
    EXPECT_NEAR(total_area(result), 100.0, 1e-6);    // 各 50，合计 100
    for (const auto& p : result) {
        EXPECT_EQ(p.holes.size(), 0u);
    }
}

// ---------- 含孔多边形并集：孔保留 + 面积守恒 ----------
TEST(BooleanTest, UnionWithHolePreservesHoleAndArea) {
    // 外环 0..100（面积 10000），中央孔 30..70（面积 1600）→ 净面积 8400。
    // 两个相同含孔多边形合并 → 形状不变，孔保留。
    const Polygon a = make_square_with_hole(0, 0, 100, 100, 30, 30, 70, 70);
    const Polygon b = make_square_with_hole(0, 0, 100, 100, 30, 30, 70, 70);
    auto result = unite(a, b);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].holes.size(), 1u);           // 孔仍在
    EXPECT_NEAR(polygon_area(result[0]), 8400.0, 1e-6);
}

// ---------- 含孔多边形交集：孔保留 ----------
TEST(BooleanTest, IntersectWithHolePreservesHoleAndArea) {
    // a = 0..100 外环 + 孔 30..70（净 8400）
    // c = 0..100 实心矩形，完全覆盖 a 外环
    // a ∩ c = a（含孔原样保留），面积守恒。
    const Polygon a = make_square_with_hole(0, 0, 100, 100, 30, 30, 70, 70);
    const Polygon c = make_square(0, 0, 100, 100);
    auto result = intersect(a, c);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].holes.size(), 1u);
    EXPECT_NEAR(polygon_area(result[0]), 8400.0, 1e-6);
}

// ---------- 不相交图形并集 = 两个独立结果 ----------
TEST(BooleanTest, UnionOfDisjointReturnsTwo) {
    const Polygon a = make_square(0, 0, 10, 10);
    const Polygon b = make_square(20, 0, 30, 10);    // 与 a 完全不相交
    auto result = unite(a, b);
    EXPECT_EQ(result.size(), 2u);
    EXPECT_NEAR(total_area(result), 200.0, 1e-6);
    for (const auto& p : result) {
        EXPECT_EQ(p.holes.size(), 0u);
    }
}
