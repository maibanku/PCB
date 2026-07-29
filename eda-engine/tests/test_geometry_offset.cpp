// tests/test_geometry_offset.cpp
//
// P3 几何内核 · Task 4 测试：偏移（Clipper2 ClipperOffset 薄包装）。
//
// 对应 plan「几何与命令栈实现计划.md」Task 4 Step 1：
//   - 单位方形外扩 1mm → 面积增加（精确 9 mm²）
//   - 内缩 → 面积减小（精确 64 mm²）
//   - 含孔内缩（孔扩大 = 材料减少）
//   - 含孔外扩（孔缩小 = 材料增加，对称验证）
//   - polyline 偏移（开路径 → 闭合体育场形，面积近似公式）
//   - courtyard 生成（器件外轮廓 + 间距 → 精确 96 mm²）
//   - 走线 Path → 多边形（path_to_polygon，与 polyline 偏移等价）
//   - 边界：空输入 / 零 delta / 过度内缩塌缩
//
// 集成时由主循环在 tests/CMakeLists.txt 追加：
//   add_executable(test_geometry_offset test_geometry_offset.cpp)
//   target_link_libraries(test_geometry_offset PRIVATE eda_geometry gtest_main)
//   gtest_discover_tests(test_geometry_offset)
// （eda_geometry 需链接 Clipper2，由主循环在 eda/CMakeLists.txt 配置。）

#include <gtest/gtest.h>

#include "eda/geometry/types.h"
#include "eda/geometry/units.h"
#include "eda/geometry/offset.h"
#include "eda/geometry/measure.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace eda::geometry;

namespace {

// 单环绝对面积（nm²）：直接对 outer / hole 单独算鞋带，方向不影响（取绝对值）。
// 用 long double 累加，规避 int64 乘积溢出（板级面积可达 10^16~10^18 nm²）。
double ring_area_abs(const std::vector<Point>& ring) {
    if (ring.size() < 3) return 0.0;
    long double s = 0.0;
    const std::size_t n = ring.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto& a = ring[i];
        const auto& b = ring[(i + 1) % n];
        s += static_cast<long double>(a.x) * static_cast<long double>(b.y);
        s -= static_cast<long double>(b.x) * static_cast<long double>(a.y);
    }
    return std::fabs(static_cast<double>(s)) * 0.5;
}

// 构造 CCW 矩形 Polygon（外环 CCW；用于测试，方向由 offset.cpp 内部校正）。
Polygon make_rect(Coord x0, Coord y0, Coord w, Coord h) {
    Polygon p;
    p.outer = {{x0, y0}, {x0 + w, y0}, {x0 + w, y0 + h}, {x0, y0 + h}};
    return p;
}

}  // namespace

// ===================== inflate（外扩）=====================

// 单位方形（1mm × 1mm）外扩 1mm → 3mm × 3mm = 9 mm²
TEST(OffsetInflateTest, UnitSquareInflateAreaIncreases) {
    Polygon sq = make_rect(0, 0, mm_to_nm(1.0), mm_to_nm(1.0));
    const double orig_area = area(sq);

    auto result = inflate(sq, mm_to_nm(1.0));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_GT(area(result[0]), orig_area);
    // 9 mm² = 9e12 nm²（矩形偏移无离散误差，整数精确）
    EXPECT_NEAR(area(result[0]), 9e12, 1e10);
}

// 外扩后边界外推 1mm：原 (0,0)~(1,1) → (-1,-1)~(2,2)
TEST(OffsetInflateTest, UnitSquareInflateBounds) {
    Polygon sq = make_rect(0, 0, mm_to_nm(1.0), mm_to_nm(1.0));
    auto result = inflate(sq, mm_to_nm(1.0));
    ASSERT_EQ(result.size(), 1u);

    Coord xmin = result[0].outer[0].x, xmax = xmin;
    Coord ymin = result[0].outer[0].y, ymax = ymin;
    for (const auto& p : result[0].outer) {
        xmin = std::min(xmin, p.x);
        xmax = std::max(xmax, p.x);
        ymin = std::min(ymin, p.y);
        ymax = std::max(ymax, p.y);
    }
    EXPECT_EQ(xmax - xmin, mm_to_nm(3.0));  // 3mm 宽
    EXPECT_EQ(ymax - ymin, mm_to_nm(3.0));  // 3mm 高
}

// 非正 delta 退化：原样返回副本（不外扩）
TEST(OffsetInflateTest, NonPositiveDeltaIsNoOp) {
    Polygon sq = make_rect(0, 0, mm_to_nm(2.0), mm_to_nm(2.0));
    auto result = inflate(sq, 0);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_DOUBLE_EQ(area(result[0]), area(sq));
}

// ===================== shrink（内缩）=====================

// 10mm × 10mm 方形内缩 1mm → 8mm × 8mm = 64 mm²
TEST(OffsetShrinkTest, SquareShrinkAreaDecreases) {
    Polygon sq = make_rect(0, 0, mm_to_nm(10.0), mm_to_nm(10.0));
    const double orig_area = area(sq);

    auto result = shrink(sq, mm_to_nm(1.0));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_LT(area(result[0]), orig_area);
    EXPECT_NEAR(area(result[0]), 64e12, 1e10);  // 8² = 64 mm²
}

// 过度内缩塌缩：2mm 方形内缩 1.5mm（> 半边 1mm）→ 结果为空
TEST(OffsetShrinkTest, OverShrinkCollapsesToEmpty) {
    Polygon sq = make_rect(0, 0, mm_to_nm(2.0), mm_to_nm(2.0));
    auto result = shrink(sq, mm_to_nm(1.5));
    EXPECT_TRUE(result.empty());
}

// ===================== 含孔内缩（孔扩大）=====================

// 20mm 外环 + 4mm 中心孔，内缩 1mm：
//   外环 → 18mm（面积 400 → 324 mm²）
//   孔   → 6mm（面积 16 → 36 mm²，孔扩大 = 材料减少）
TEST(OffsetShrinkTest, SquareWithHoleShrinkHoleGrows) {
    Polygon pg = make_rect(0, 0, mm_to_nm(20.0), mm_to_nm(20.0));
    pg.holes.push_back({
        {mm_to_nm(8),  mm_to_nm(8)},
        {mm_to_nm(12), mm_to_nm(8)},
        {mm_to_nm(12), mm_to_nm(12)},
        {mm_to_nm(8),  mm_to_nm(12)},
    });
    const double outer_before = ring_area_abs(pg.outer);
    const double hole_before = ring_area_abs(pg.holes[0]);

    auto result = shrink(pg, mm_to_nm(1.0));
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].holes.size(), 1u);

    const double outer_after = ring_area_abs(result[0].outer);
    const double hole_after = ring_area_abs(result[0].holes[0]);

    EXPECT_LT(outer_after, outer_before);     // 外环缩小
    EXPECT_GT(hole_after, hole_before);       // 孔扩大
    EXPECT_NEAR(outer_after, 324e12, 1e10);   // 18² = 324 mm²
    EXPECT_NEAR(hole_after, 36e12, 1e10);     // 6² = 36 mm²
}

// ===================== 含孔外扩（孔缩小，对称验证）=====================

// 20mm 外环 + 6mm 中心孔，外扩 1mm：
//   外环 → 22mm（面积 484 mm²）
//   孔   → 4mm（面积 16 mm²，孔缩小 = 材料增加）
TEST(OffsetInflateTest, SquareWithHoleInflateHoleShrinks) {
    Polygon pg = make_rect(0, 0, mm_to_nm(20.0), mm_to_nm(20.0));
    pg.holes.push_back({
        {mm_to_nm(7),  mm_to_nm(7)},
        {mm_to_nm(13), mm_to_nm(7)},
        {mm_to_nm(13), mm_to_nm(13)},
        {mm_to_nm(7),  mm_to_nm(13)},
    });
    const double outer_before = ring_area_abs(pg.outer);
    const double hole_before = ring_area_abs(pg.holes[0]);

    auto result = inflate(pg, mm_to_nm(1.0));
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].holes.size(), 1u);

    EXPECT_GT(ring_area_abs(result[0].outer), outer_before);   // 外环扩张
    EXPECT_LT(ring_area_abs(result[0].holes[0]), hole_before); // 孔缩小
    EXPECT_NEAR(ring_area_abs(result[0].outer), 484e12, 1e10); // 22² = 484 mm²
    EXPECT_NEAR(ring_area_abs(result[0].holes[0]), 16e12, 1e10); // 4² = 16 mm²
}

// ===================== polyline 偏移（线条 / courtyard 生成）=====================

// 10mm 水平开线 + 1mm Round/Round → 体育场形（矩形 + 两半圆）
// 面积 ≈ length × 2δ + π × δ² = 10×2 + π ≈ 23.14 mm²
TEST(OffsetPolylineTest, StraightLineOffsetProducesStadium) {
    Polyline line{{{0, 0}, {mm_to_nm(10.0), 0}}};
    auto result = offset(line, mm_to_nm(1.0), JoinType::Round, EndType::Round);
    ASSERT_EQ(result.size(), 1u);
    const double a_nm2 = ring_area_abs(result[0].points);
    EXPECT_NEAR(a_nm2 / 1e12, 23.14, 0.5);  // ~23.14 mm²（容差 0.5mm² 吸收离散误差）
}

// 不足 2 点 / 零 delta → 返回空
TEST(OffsetPolylineTest, DegenerateInputReturnsEmpty) {
    Polyline single{{{0, 0}}};
    EXPECT_TRUE(offset(single, mm_to_nm(1.0)).empty());

    Polyline line{{{0, 0}, {mm_to_nm(10.0), 0}}};
    EXPECT_TRUE(offset(line, 0).empty());
}

// ===================== courtyard 生成（器件外轮廓 + 间距）=====================

// 10mm × 6mm 器件外轮廓 + 1mm 间距 → 12mm × 8mm courtyard = 96 mm²
TEST(OffsetCourtyardTest, CourtyardFromDevicePolygon) {
    Polygon device = make_rect(0, 0, mm_to_nm(10.0), mm_to_nm(6.0));
    auto cy = courtyard(device, mm_to_nm(1.0));
    ASSERT_EQ(cy.size(), 1u);
    EXPECT_NEAR(area(cy[0]), 96e12, 1e10);  // 12 × 8 = 96 mm²
}

// courtyard 即 inflate 的语义化别名：结果面积一致
TEST(OffsetCourtyardTest, CourtyardEqualsInflate) {
    Polygon device = make_rect(0, 0, mm_to_nm(5.0), mm_to_nm(5.0));
    auto cy = courtyard(device, mm_to_nm(0.5));
    auto inf = inflate(device, mm_to_nm(0.5));
    ASSERT_EQ(cy.size(), 1u);
    ASSERT_EQ(inf.size(), 1u);
    EXPECT_DOUBLE_EQ(area(cy[0]), area(inf[0]));
}

// ===================== bonus: Path → Polygon（走线展开）=====================

// 走线：中心线 (0,0)→(10mm,0)，宽度 2mm → 体育场形多边形 ≈ 23.14 mm²
TEST(OffsetPathTest, TracePathToPolygon) {
    Path trace{{{0, 0}, {mm_to_nm(10.0), 0}}, mm_to_nm(2.0)};
    auto result = path_to_polygon(trace);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NEAR(area(result[0]) / 1e12, 23.14, 0.5);
}

// 退化 Path（中心线过短 / width 非正）→ 返回空
TEST(OffsetPathTest, DegeneratePathReturnsEmpty) {
    Path short_path{{{0, 0}}, mm_to_nm(2.0)};
    EXPECT_TRUE(path_to_polygon(short_path).empty());

    Path zero_width{{{0, 0}, {mm_to_nm(10.0), 0}}, 0};
    EXPECT_TRUE(path_to_polygon(zero_width).empty());
}
