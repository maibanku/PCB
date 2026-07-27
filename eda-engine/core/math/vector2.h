#pragma once
#include <cmath>

namespace eda {

struct Vector2 {
    float x = 0.0f, y = 0.0f;

    Vector2() = default;
    Vector2(float x_, float y_) : x(x_), y(y_) {}

    Vector2 operator+(const Vector2& o) const { return {x + o.x, y + o.y}; }
    Vector2 operator-(const Vector2& o) const { return {x - o.x, y - o.y}; }
    Vector2 operator*(float s) const { return {x * s, y * s}; }
    Vector2 operator/(float s) const { return {x / s, y / s}; }

    bool operator==(const Vector2& o) const { return x == o.x && y == o.y; }

    float length() const { return std::sqrt(x * x + y * y); }

    Vector2 normalized() const {
        float l = length();
        return l > 0 ? Vector2{x / l, y / l} : Vector2{};
    }
};

struct Vector2i {
    int x = 0, y = 0;
};

}  // namespace eda
