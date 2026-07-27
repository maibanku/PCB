#pragma once

#include "core/math/color.h"
#include "core/math/rect2.h"
#include "core/math/vector2.h"
#include "servers/canvas_vertex.h"

#include <vector>

namespace eda {

class RenderingServer;  // 前向声明：flush() 的可选消费方，测试模式可为空

// Canvas2D —— 立即模式矢量画布（对齐 Godot scene/2d 的精简版）。
//
// 职责：在 CPU 侧把图元（线段 / 矩形 / 圆）栅格化为 CanvasVertex 三角形顶点列表，
// 供 RenderingServer::draw_triangles 消费。本类只懂"图元 → 三角形"，不懂 GPU 细节；
// 驱动层（drivers/opengl、drivers/vulkan）只懂"三角形 → 像素"，不懂图元语义。
//
// 立即模式语义（状态机）：
//   - set_color()           : 设置当前颜色（状态量，影响后续所有图元的顶点颜色）
//   - set_line_thickness()  : 设置当前线宽（影响 draw_line 生成的四边形厚度）
//   - draw_*()              : 仅向内部顶点缓冲追加三角形顶点
//   - vertices()            : 只读访问顶点缓冲（供 driver 上传 / 测试断言）
//   - flush()               : 若绑定 RenderingServer，则上传并清空缓冲；未绑定也清空
//   - clear()               : 仅清空顶点缓冲（保留颜色 / 线宽状态，便于跨帧复用）
//
// EDA 风格 Demo 用法（网格 / 焊盘 / 走线 / 过孔）：
//   canvas.set_color(grid_color);          canvas.draw_line(a, b);
//   canvas.set_color(pad_color);           canvas.draw_rect_filled(Rect2{pos, size});
//   canvas.set_color(trace_color);         canvas.set_line_thickness(w); canvas.draw_line(a, b);
//   canvas.set_color(via_color);           canvas.draw_circle_filled(center, via_radius);
class Canvas2D {
public:
    // server 可空：测试与无 GPU 场景下仅做 CPU 栅格化，由调用方自行读 vertices()。
    explicit Canvas2D(RenderingServer* server = nullptr);

    // —— 状态 ——
    void set_color(Color color);
    void set_line_thickness(float thickness);

    // —— 图元（每个图元仅向 vertices_ 追加三角形顶点）——

    // 线段栅格化为厚度 = line_thickness_ 的四边形（2 三角形 / 6 顶点）。
    void draw_line(Vector2 a, Vector2 b);

    // 实心矩形：2 三角形 / 6 顶点。
    void draw_rect_filled(Rect2 rect);

    // 实心圆：中心扇形三角剖分，segments 个三角形（segments × 3 顶点）。
    // segments < 3 时钳制为 3，保证至少生成一个三角形。
    void draw_circle_filled(Vector2 center, float radius, int segments = 32);

    // —— 缓冲访问 / 提交 ——
    const std::vector<CanvasVertex>& vertices() const { return vertices_; }

    // 提交：若绑定 RenderingServer 则上传顶点；随后无条件清空缓冲（帧边界语义）。
    void flush();

    // 仅清空顶点缓冲，保留颜色 / 线宽状态。
    void clear();

private:
    RenderingServer* server_;
    std::vector<CanvasVertex> vertices_;
    Color current_color_{1.0f, 1.0f, 1.0f, 1.0f};  // 默认不透明白色
    float line_thickness_ = 1.0f;

    // 内部图元发射器：使用 current_color_ 着色。
    void push_triangle(Vector2 a, Vector2 b, Vector2 c);
    void push_quad(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3);
};

}  // namespace eda
