#include <gtest/gtest.h>

#include "servers/canvas_vertex.h"
#include "servers/rendering_server.h"

using namespace eda;

// ===== CanvasVertex 默认值 =====

TEST(CanvasVertexTest, DefaultPositionIsZero) {
    CanvasVertex v;
    EXPECT_FLOAT_EQ(v.pos.x, 0.0f);
    EXPECT_FLOAT_EQ(v.pos.y, 0.0f);
}

TEST(CanvasVertexTest, DefaultColorIsOpaqueBlack) {
    CanvasVertex v;
    EXPECT_FLOAT_EQ(v.color.r, 0.0f);
    EXPECT_FLOAT_EQ(v.color.g, 0.0f);
    EXPECT_FLOAT_EQ(v.color.b, 0.0f);
    EXPECT_FLOAT_EQ(v.color.a, 1.0f);
}

// ===== CanvasVertex pos/color 字段 =====

TEST(CanvasVertexTest, PositionFieldIsAssignable) {
    CanvasVertex v;
    v.pos = Vector2{3.0f, 4.0f};
    EXPECT_FLOAT_EQ(v.pos.x, 3.0f);
    EXPECT_FLOAT_EQ(v.pos.y, 4.0f);
}

TEST(CanvasVertexTest, ColorFieldIsAssignable) {
    CanvasVertex v;
    v.color = Color{1.0f, 0.2f, 0.4f, 0.8f};
    EXPECT_FLOAT_EQ(v.color.r, 1.0f);
    EXPECT_FLOAT_EQ(v.color.g, 0.2f);
    EXPECT_FLOAT_EQ(v.color.b, 0.4f);
    EXPECT_FLOAT_EQ(v.color.a, 0.8f);
}

TEST(CanvasVertexTest, AggregateInitialization) {
    CanvasVertex v{Vector2{7.0f, 8.0f}, Color{0.1f, 0.2f, 0.3f, 1.0f}};
    EXPECT_FLOAT_EQ(v.pos.x, 7.0f);
    EXPECT_FLOAT_EQ(v.pos.y, 8.0f);
    EXPECT_FLOAT_EQ(v.color.r, 0.1f);
    EXPECT_FLOAT_EQ(v.color.b, 0.3f);
}

// ===== RenderingDriver 枚举值 =====

TEST(RenderingDriverTest, ExposesOpenGlValue) {
    constexpr RenderingDriver driver = RenderingDriver::OPENGL;
    EXPECT_EQ(driver, RenderingDriver::OPENGL);
}

TEST(RenderingDriverTest, ExposesVulkanValue) {
    constexpr RenderingDriver driver = RenderingDriver::VULKAN;
    EXPECT_EQ(driver, RenderingDriver::VULKAN);
}

TEST(RenderingDriverTest, OpenGlAndVulkanAreDistinct) {
    EXPECT_NE(RenderingDriver::OPENGL, RenderingDriver::VULKAN);
}
