#include <gtest/gtest.h>

#include "core/math/vector2.h"
#include "core/math/color.h"
#include "core/math/rect2.h"
#include "core/math/transform_2d.h"

using namespace eda;

TEST(Vector2Test, AddAndLength) {
    Vector2 a{3.0f, 4.0f}, b{0.0f, 0.0f};
    EXPECT_FLOAT_EQ(a.length(), 5.0f);
    EXPECT_EQ(a + b, (Vector2{3, 4}));
}

TEST(Vector2Test, Normalize) {
    Vector2 v{3, 4};
    EXPECT_NEAR(v.normalized().length(), 1.0f, 1e-5f);
}

TEST(ColorTest, Construct) {
    Color c{1.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_FLOAT_EQ(c.r, 1.0f);
}

TEST(Transform2DTest, Translate) {
    Transform2D t = Transform2D::translate({5.0f, 0.0f});
    Vector2 p = t.transform_point({0.0f, 0.0f});
    EXPECT_NEAR(p.x, 5.0f, 1e-5f);
    EXPECT_NEAR(p.y, 0.0f, 1e-5f);
}

TEST(Transform2DTest, Scale) {
    Transform2D s = Transform2D::scale({2.0f, 3.0f});
    Vector2 p = s.transform_point({1.0f, 1.0f});
    EXPECT_NEAR(p.x, 2.0f, 1e-5f);
    EXPECT_NEAR(p.y, 3.0f, 1e-5f);
}
