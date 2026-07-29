// tests/test_geometry_triangulate.cpp
//
// P3 几何内核 · Task 5 测试：三角剖分（earcut 薄包装）。
//
// 对应 plan「几何与命令栈实现计划.md」Task 5 Step 1：
//   - 矩形：2 三角形 / 4 顶点 / 索引有效性
//   - 凹多边形（L 形）：干净简单多边形的耳切
//   - 含孔多边形（外环 + 方孔）：桥接 + 顶点平铺
//   - 三角形数公式：T = n - 2 + 2*h（n=顶点总数，h=孔数）
//   - 面积守恒：Σ|三角形面积| == |area(Polygon)|
//   - 退化输入：空 / <3 点外环 / 退化孔
//
// ⚠ 公式说明：plan 原文记作 "n-2-2*holes"，但含孔项符号应为正——
//   由 Euler-Poincaré（χ = 1-h）与 earcut 源码（splitPolygon 每孔 +2 桥节点）
//   共同验证，正确公式为 T = n + 2h - 2 = n - 2 + 2*h。本测试按正确公式断言。
//
// 集成时由主循环在 tests/CMakeLists.txt 追加：
//   add_executable(test_geometry_triangulate test_geometry_triangulate.cpp)
//   target_link_libraries(test_geometry_triangulate PRIVATE eda_geometry gtest_main)
//   gtest_discover_tests(test_geometry_triangulate)

#include <gtest/gtest.h>

#include "eda/geometry/types.h"
#include "eda/geometry/triangulate.h"
#include "eda/geometry/measure.h"  // area()，用于面积守恒交叉验证（同 eda_geometry 库）

#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace eda::geometry;

namespace {

// 单三角形面积（叉积取半绝对值）。输入整数 nm，中间量 double。
double triangle_area(const Point& a, const Point& b, const Point& c) {
    const double ax = static_cast<double>(a.x), ay = static_cast<double>(a.y);
    const double bx = static_cast<double>(b.x), by = static_cast<double>(b.y);
    const double cx = static_cast<double>(c.x), cy = static_cast<double>(c.y);
    return std::fabs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay)) / 2.0;
}

// 对剖分结果累加所有三角形面积（与 area(Polygon) 交叉验证用）。
double total_triangle_area(const TriangulationResult& tri) {
    double sum = 0.0;
    for (std::size_t i = 0; i + 2 < tri.indices.size(); i += 3) {
        sum += triangle_area(tri.vertices[tri.indices[i]],
                             tri.vertices[tri.indices[i + 1]],
                             tri.vertices[tri.indices[i + 2]]);
    }
    return sum;
}

// 断言所有索引在 vertices 范围内，且 indices.size() 是 3 的倍数。
void expect_valid_indexed_triangles(const TriangulationResult& tri) {
    EXPECT_EQ(tri.indices.size() % 3u, 0u);
    for (uint32_t idx : tri.indices) {
        EXPECT_LT(idx, tri.vertices.size());
    }
}

}  // namespace

// ===================== 矩形（无孔）=====================

TEST(TriangulateRectangleTest, TwoTrianglesFourVertices) {
    Polygon poly;
    poly.outer = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};  // CCW 方波
    const auto result = triangulate(poly);

    EXPECT_EQ(result.vertices.size(), 4u);
    EXPECT_EQ(result.indices.size(), 6u);  // 2 三角形 × 3
    expect_valid_indexed_triangles(result);
}

TEST(TriangulateRectangleTest, TriangleCountFormulaNoHole) {
    Polygon poly;
    poly.outer = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    const auto result = triangulate(poly);

    const std::size_t n = result.vertices.size();
    const std::size_t h = poly.holes.size();
    // 干净简单多边形：T = n - 2 + 2*h（h=0 时退化为 n-2）
    EXPECT_EQ(result.indices.size() / 3u, n - 2u + 2u * h);
    EXPECT_EQ(result.indices.size() / 3u, 2u);  // 4 - 2 = 2
}

TEST(TriangulateRectangleTest, AreaConserved) {
    Polygon poly;
    poly.outer = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    const auto result = triangulate(poly);

    // 100×100 = 10000 nm²
    EXPECT_NEAR(total_triangle_area(result), 10000.0, 1e-6);
    // 与 measure::area 交叉验证（DoD 关键不变量）
    EXPECT_NEAR(total_triangle_area(result), area(poly), 1e-6);
}

// ===================== 三角形（最小输入）=====================

TEST(TriangulateTriangleTest, OneTriangleThreeVertices) {
    Polygon poly;
    poly.outer = {{0, 0}, {100, 0}, {50, 100}};
    const auto result = triangulate(poly);

    EXPECT_EQ(result.vertices.size(), 3u);
    EXPECT_EQ(result.indices.size(), 3u);  // 1 三角形
    expect_valid_indexed_triangles(result);
    EXPECT_NEAR(total_triangle_area(result), area(poly), 1e-6);
}

// ===================== 凹多边形（L 形）=====================

TEST(TriangulateConcaveTest, LShapeTriangleCount) {
    // L 形（6 顶点，CCW）：
    //   (0,0) → (200,0) → (200,100) → (100,100) → (100,200) → (0,200) → close
    // 凹点：(100,100)。三角形数 = n - 2 = 4。
    Polygon poly;
    poly.outer = {
        {0, 0}, {200, 0}, {200, 100},
        {100, 100}, {100, 200}, {0, 200}
    };
    const auto result = triangulate(poly);

    EXPECT_EQ(result.vertices.size(), 6u);
    EXPECT_EQ(result.indices.size() / 3u, 4u);  // 6 - 2 = 4
    expect_valid_indexed_triangles(result);
}

TEST(TriangulateConcaveTest, LShapeAreaConserved) {
    Polygon poly;
    poly.outer = {
        {0, 0}, {200, 0}, {200, 100},
        {100, 100}, {100, 200}, {0, 200}
    };
    const auto result = triangulate(poly);

    // L 形面积 = 横臂 200×100 + 竖臂 100×100 = 30000 nm²
    EXPECT_NEAR(total_triangle_area(result), 30000.0, 1e-6);
    EXPECT_NEAR(total_triangle_area(result), area(poly), 1e-6);
}

// ===================== 含孔多边形（外环 + 方孔）=====================

TEST(TriangulateHoleTest, OuterSquareWithSquareHoleTopology) {
    // 外环 200×200，方孔 100×100 居中。
    // 顶点总数 n = 4 + 4 = 8，孔数 h = 1。
    // 三角形数 = n - 2 + 2*h = 8 - 2 + 2 = 8。
    Polygon poly;
    poly.outer = {{0, 0}, {200, 0}, {200, 200}, {0, 200}};
    poly.holes.push_back({{50, 50}, {150, 50}, {150, 150}, {50, 150}});

    const auto result = triangulate(poly);

    EXPECT_EQ(result.vertices.size(), 8u);  // 外环 4 + 孔 4，平铺
    EXPECT_EQ(result.indices.size() / 3u, 8u);  // n - 2 + 2h = 8
    expect_valid_indexed_triangles(result);
}

TEST(TriangulateHoleTest, AreaConservedWithHole) {
    Polygon poly;
    poly.outer = {{0, 0}, {200, 0}, {200, 200}, {0, 200}};
    poly.holes.push_back({{50, 50}, {150, 50}, {150, 150}, {50, 150}});

    const auto result = triangulate(poly);

    // 净面积 = 200×200 - 100×100 = 30000 nm²（孔面积被扣除）
    EXPECT_NEAR(total_triangle_area(result), 30000.0, 1e-6);
    EXPECT_NEAR(total_triangle_area(result), area(poly), 1e-6);
}

TEST(TriangulateHoleTest, VerticesFlattenedOuterThenHole) {
    // 验证顶点平铺顺序：外环在前，孔紧随——与 earcut 编号约定一致。
    Polygon poly;
    poly.outer = {{10, 20}, {30, 40}, {50, 60}, {70, 80}};
    poly.holes.push_back({{90, 100}, {110, 120}, {130, 140}, {150, 160}});

    const auto result = triangulate(poly);

    ASSERT_EQ(result.vertices.size(), 8u);
    for (std::size_t i = 0; i < 4u; ++i) {
        EXPECT_EQ(result.vertices[i], poly.outer[i]);
    }
    for (std::size_t i = 0; i < 4u; ++i) {
        EXPECT_EQ(result.vertices[4u + i], poly.holes[0][i]);
    }
}

// ===================== 多孔：三角形数公式 =====================

TEST(TriangulateFormulaTest, TwoHolesTriangleCount) {
    // 外环 400×400，两个方孔各 100×100。
    // n = 4 + 4 + 4 = 12，h = 2 → T = 12 - 2 + 4 = 14。
    Polygon poly;
    poly.outer = {{0, 0}, {400, 0}, {400, 400}, {0, 400}};
    poly.holes.push_back({{50, 50}, {150, 50}, {150, 150}, {50, 150}});
    poly.holes.push_back({{250, 250}, {350, 250}, {350, 350}, {250, 350}});

    const auto result = triangulate(poly);

    ASSERT_EQ(poly.holes.size(), 2u);
    EXPECT_EQ(result.vertices.size(), 12u);
    EXPECT_EQ(result.indices.size() / 3u, 14u);  // n - 2 + 2h = 14
    expect_valid_indexed_triangles(result);
}

TEST(TriangulateFormulaTest, TwoHolesAreaConserved) {
    Polygon poly;
    poly.outer = {{0, 0}, {400, 0}, {400, 400}, {0, 400}};
    poly.holes.push_back({{50, 50}, {150, 50}, {150, 150}, {50, 150}});
    poly.holes.push_back({{250, 250}, {350, 250}, {350, 350}, {250, 350}});

    const auto result = triangulate(poly);

    // 净面积 = 160000 - 10000 - 10000 = 140000 nm²
    EXPECT_NEAR(total_triangle_area(result), 140000.0, 1e-6);
    EXPECT_NEAR(total_triangle_area(result), area(poly), 1e-6);
}

// ===================== 退化输入 =====================

TEST(TriangulateDegenerateTest, EmptyOuterReturnsEmpty) {
    Polygon poly;
    const auto result = triangulate(poly);
    EXPECT_TRUE(result.vertices.empty());
    EXPECT_TRUE(result.indices.empty());
}

TEST(TriangulateDegenerateTest, TwoPointOuterReturnsEmpty) {
    Polygon poly;
    poly.outer = {{0, 0}, {100, 0}};
    const auto result = triangulate(poly);
    EXPECT_TRUE(result.vertices.empty());
    EXPECT_TRUE(result.indices.empty());
}

TEST(TriangulateDegenerateTest, DegenerateHoleSkipped) {
    // 外环有效，但孔 < 3 点 → 孔被跳过，按无孔矩形剖分。
    Polygon poly;
    poly.outer = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    poly.holes.push_back({{50, 50}, {60, 60}});  // 2 点退化孔
    const auto result = triangulate(poly);

    // 退化孔不进入顶点列表：仅外环 4 顶点。
    EXPECT_EQ(result.vertices.size(), 4u);
    EXPECT_EQ(result.indices.size() / 3u, 2u);  // 按无孔矩形 = 2 三角形
    expect_valid_indexed_triangles(result);
}
