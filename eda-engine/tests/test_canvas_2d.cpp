#include <gtest/gtest.h>

#include "scene/2d/canvas_2d.h"

#include <cstddef>

using namespace eda;

// ===== draw_line：线段栅格化为 2 个三角形（四边形） =====

TEST(Canvas2DTest, DrawLineProducesTwoTriangles) {
    Canvas2D canvas;
    canvas.draw_line({0.0f, 0.0f}, {10.0f, 0.0f});
    EXPECT_EQ(canvas.vertices().size(), 6u);  // 2 三角形 = 6 顶点
}

TEST(Canvas2DTest, DrawLineWithThicknessLiesWithinQuad) {
    Canvas2D canvas;
    canvas.set_line_thickness(2.0f);
    canvas.draw_line({0.0f, 0.0f}, {10.0f, 0.0f});
    // 水平线段、厚度 2 → 四边形 y ∈ [-1, 1]、x ∈ [0, 10]
    for (const auto& v : canvas.vertices()) {
        EXPECT_GE(v.pos.y, -1.0f);
        EXPECT_LE(v.pos.y, 1.0f);
        EXPECT_GE(v.pos.x, -1.0f);
        EXPECT_LE(v.pos.x, 11.0f);
    }
}

// ===== draw_rect_filled：实心矩形 = 2 三角形 =====

TEST(Canvas2DTest, DrawRectFilledProducesTwoTriangles) {
    Canvas2D canvas;
    canvas.draw_rect_filled(Rect2{{10.0f, 10.0f}, {20.0f, 30.0f}});
    EXPECT_EQ(canvas.vertices().size(), 6u);
}

TEST(Canvas2DTest, DrawRectFilledCoversFourCorners) {
    Canvas2D canvas;
    canvas.draw_rect_filled(Rect2{{0.0f, 0.0f}, {10.0f, 20.0f}});
    // 四个角必须出现在顶点列表中（整数值浮点加法精确可比较）
    bool has_00 = false, has_10_0 = false, has_10_20 = false, has_0_20 = false;
    for (const auto& v : canvas.vertices()) {
        if (v.pos == Vector2{0.0f, 0.0f}) has_00 = true;
        if (v.pos == Vector2{10.0f, 0.0f}) has_10_0 = true;
        if (v.pos == Vector2{10.0f, 20.0f}) has_10_20 = true;
        if (v.pos == Vector2{0.0f, 20.0f}) has_0_20 = true;
    }
    EXPECT_TRUE(has_00);
    EXPECT_TRUE(has_10_0);
    EXPECT_TRUE(has_10_20);
    EXPECT_TRUE(has_0_20);
}

// ===== draw_circle_filled：中心扇形，segments × 3 顶点 =====

TEST(Canvas2DTest, DrawCircleFilledProducesSegmentsTimesThree) {
    Canvas2D canvas;
    canvas.draw_circle_filled({50.0f, 50.0f}, 10.0f, /*segments=*/16);
    EXPECT_EQ(canvas.vertices().size(), 48u);  // 16 × 3
}

TEST(Canvas2DTest, DrawCircleFilledDefaultSegmentsIsThirtyTwo) {
    Canvas2D canvas;
    canvas.draw_circle_filled({0.0f, 0.0f}, 5.0f);  // 默认 segments = 32
    EXPECT_EQ(canvas.vertices().size(), 96u);  // 32 × 3
}

TEST(Canvas2DTest, DrawCircleFilledClampsLowSegmentCount) {
    Canvas2D canvas;
    canvas.draw_circle_filled({0.0f, 0.0f}, 5.0f, /*segments=*/0);
    EXPECT_EQ(canvas.vertices().size(), 9u);  // 钳制到 3 → 9 顶点
}

TEST(Canvas2DTest, DrawCircleFilledVerticesWithinRadius) {
    Canvas2D canvas;
    constexpr int kSegments = 16;
    constexpr float kRadius = 10.0f;
    canvas.draw_circle_filled({0.0f, 0.0f}, kRadius, kSegments);
    // 中心点距离 0；圆周边点距离 ≈ kRadius。所有顶点都不应超出半径（含浮点误差）。
    for (const auto& v : canvas.vertices()) {
        EXPECT_LE(v.pos.length(), kRadius + 1e-4f);
    }
}

// ===== set_color：影响后续顶点颜色 =====

TEST(Canvas2DTest, SetColorAffectsSubsequentVertices) {
    Canvas2D canvas;
    canvas.set_color({1.0f, 0.0f, 0.0f, 1.0f});  // 红
    canvas.draw_rect_filled(Rect2{{0.0f, 0.0f}, {10.0f, 10.0f}});
    for (const auto& v : canvas.vertices()) {
        EXPECT_FLOAT_EQ(v.color.r, 1.0f);
        EXPECT_FLOAT_EQ(v.color.g, 0.0f);
        EXPECT_FLOAT_EQ(v.color.b, 0.0f);
        EXPECT_FLOAT_EQ(v.color.a, 1.0f);
    }
}

TEST(Canvas2DTest, SetColorAppliesOnlyToSubsequentPrimitives) {
    Canvas2D canvas;
    canvas.set_color({1.0f, 0.0f, 0.0f, 1.0f});  // 红
    canvas.draw_rect_filled(Rect2{{0.0f, 0.0f}, {1.0f, 1.0f}});  // 6 红顶点
    canvas.set_color({0.0f, 1.0f, 0.0f, 1.0f});  // 绿
    canvas.draw_rect_filled(Rect2{{0.0f, 0.0f}, {1.0f, 1.0f}});  // 6 绿顶点

    ASSERT_EQ(canvas.vertices().size(), 12u);
    for (std::size_t i = 0; i < 6u; ++i) {
        EXPECT_FLOAT_EQ(canvas.vertices()[i].color.r, 1.0f);
        EXPECT_FLOAT_EQ(canvas.vertices()[i].color.g, 0.0f);
    }
    for (std::size_t i = 6u; i < 12u; ++i) {
        EXPECT_FLOAT_EQ(canvas.vertices()[i].color.r, 0.0f);
        EXPECT_FLOAT_EQ(canvas.vertices()[i].color.g, 1.0f);
    }
}

TEST(Canvas2DTest, DefaultColorIsOpaqueWhite) {
    Canvas2D canvas;  // 未调用 set_color
    canvas.draw_rect_filled(Rect2{{0.0f, 0.0f}, {1.0f, 1.0f}});
    ASSERT_FALSE(canvas.vertices().empty());
    const auto& c = canvas.vertices().front().color;
    EXPECT_FLOAT_EQ(c.r, 1.0f);
    EXPECT_FLOAT_EQ(c.g, 1.0f);
    EXPECT_FLOAT_EQ(c.b, 1.0f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

// ===== clear / flush：缓冲管理 =====

TEST(Canvas2DTest, ClearEmptiesVertexBuffer) {
    Canvas2D canvas;
    canvas.draw_rect_filled(Rect2{{0.0f, 0.0f}, {1.0f, 1.0f}});
    ASSERT_FALSE(canvas.vertices().empty());
    canvas.clear();
    EXPECT_TRUE(canvas.vertices().empty());
}

TEST(Canvas2DTest, ClearPreservesColorState) {
    Canvas2D canvas;
    canvas.set_color({1.0f, 0.0f, 0.0f, 1.0f});  // 红
    canvas.draw_rect_filled(Rect2{{0.0f, 0.0f}, {1.0f, 1.0f}});
    canvas.clear();
    ASSERT_TRUE(canvas.vertices().empty());
    canvas.draw_rect_filled(Rect2{{0.0f, 0.0f}, {1.0f, 1.0f}});
    ASSERT_EQ(canvas.vertices().size(), 6u);
    // clear 不重置颜色状态：后续顶点仍是红
    EXPECT_FLOAT_EQ(canvas.vertices().front().color.r, 1.0f);
    EXPECT_FLOAT_EQ(canvas.vertices().front().color.g, 0.0f);
}

TEST(Canvas2DTest, FlushWithNullServerIsSafeAndClears) {
    Canvas2D canvas;  // server = nullptr
    canvas.draw_rect_filled(Rect2{{0.0f, 0.0f}, {1.0f, 1.0f}});
    ASSERT_EQ(canvas.vertices().size(), 6u);
    canvas.flush();  // 不应解引用空 server
    EXPECT_TRUE(canvas.vertices().empty());
}
