#include "scene/2d/canvas_2d.h"

#include "servers/rendering_server.h"

#include <cmath>

namespace eda {

namespace {
// 2π，圆周剖分用。用 double 字面量再截断到 float，减少三角函数入参误差。
constexpr float kTau = 6.28318530717958647692f;
}  // namespace

Canvas2D::Canvas2D(RenderingServer* server) : server_(server) {}

void Canvas2D::set_color(Color color) { current_color_ = color; }

void Canvas2D::set_line_thickness(float thickness) { line_thickness_ = thickness; }

void Canvas2D::push_triangle(Vector2 a, Vector2 b, Vector2 c) {
    // 三角形顶点按当前颜色着色；屏幕空间坐标直接透传给 driver。
    vertices_.push_back({a, current_color_});
    vertices_.push_back({b, current_color_});
    vertices_.push_back({c, current_color_});
}

void Canvas2D::push_quad(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3) {
    // 四边形拆为两个三角形，共享对角线 p0→p2，无缝无重叠。
    push_triangle(p0, p1, p2);
    push_triangle(p0, p2, p3);
}

void Canvas2D::draw_line(Vector2 a, Vector2 b) {
    // 把线段扩展为厚度 line_thickness_ 的轴对齐四边形：
    //   d = 沿线段方向单位向量；n = 旋转 90° 的法线；h = n × 半厚。
    // 零长度线段（a == b）的 normalized() 返回零向量，自然退化为无面积三角形。
    Vector2 d = (b - a).normalized();
    Vector2 n{d.y, -d.x};
    Vector2 h = n * (line_thickness_ * 0.5f);
    push_quad(a - h, a + h, b + h, b - h);
}

void Canvas2D::draw_rect_filled(Rect2 rect) {
    // 矩形四角：A(左上) B(右上) C(右下) D(左下)，拆两个三角形共享对角线 A→C。
    Vector2 a = rect.pos;
    Vector2 b{rect.pos.x + rect.size.x, rect.pos.y};
    Vector2 c{rect.pos.x + rect.size.x, rect.pos.y + rect.size.y};
    Vector2 d{rect.pos.x, rect.pos.y + rect.size.y};
    push_quad(a, b, c, d);
}

void Canvas2D::draw_circle_filled(Vector2 center, float radius, int segments) {
    // 中心扇形三角剖分：圆心 + 相邻圆周点构成一个三角形。
    // segments 段 → segments 个三角形 → segments × 3 顶点。
    if (segments < 3) {
        segments = 3;
    }
    for (int i = 0; i < segments; ++i) {
        float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * kTau;
        float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * kTau;
        Vector2 p0{center.x + radius * std::cos(a0), center.y + radius * std::sin(a0)};
        Vector2 p1{center.x + radius * std::cos(a1), center.y + radius * std::sin(a1)};
        push_triangle(center, p0, p1);
    }
}

void Canvas2D::flush() {
    // 已绑定 server 且缓冲非空时上传；无论是否上传，帧边界都清空缓冲。
    if (server_ != nullptr && !vertices_.empty()) {
        server_->draw_triangles(vertices_);
    }
    vertices_.clear();
}

void Canvas2D::clear() { vertices_.clear(); }

}  // namespace eda
