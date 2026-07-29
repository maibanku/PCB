// ProductionControlCanvas —— 生产环境 ControlCanvas（P4 Task 4 渲染管线接通）。
//
// 把 Control._draw() 经 ControlCanvas 接口下发的自绘调用适配到 Canvas2D 栅格化器：
//   - ControlCanvas 是 per-call color 接口（draw_rect(rect, color, filled) 等）
//   - Canvas2D 是状态机（set_color + draw_*）
//   - 本类做适配：每次调用先 set_color(color) 再 draw_*，将立即模式的自绘画布调用
//     转为 Canvas2D 的三角形顶点缓冲。
//
// 接通后的渲染管线（与 control_render.cpp render_subtree 协同）：
//     Control 树 → render_subtree DFS 遍历 → 每个 Control._draw(ProductionControlCanvas)
//     → ProductionControlCanvas 转 Canvas2D::draw_rect_filled / draw_line
//     → Canvas2D 栅格化为 CanvasVertex 三角形 → RasterizerGL::draw_triangles → GPU
//
// draw_text 当前为 no-op：真实字形由 font_provider（后续 Task 协同）落地；逻辑调用
// 经此进入，测试经 RecordingControlCanvas 记录 (text, pos) 断言。

#pragma once

#include "scene/gui/canvas_item.h"  // ControlCanvas
#include "scene/2d/canvas_2d.h"

#include <string>

namespace eda {

class ProductionControlCanvas : public ControlCanvas {
public:
    explicit ProductionControlCanvas(Canvas2D& canvas) : canvas_(canvas) {}

    // —— ControlCanvas 接口实现 ——

    void draw_rect(Rect2 rect, Color color, bool filled) override {
        // 适配：先设色再画。filled 走矩形栅格化；非 filled 画 4 条 1px 边框矩形。
        canvas_.set_color(color);
        if (filled) {
            canvas_.draw_rect_filled(rect);
            return;
        }
        draw_rect_border_(rect);
    }

    void draw_text(const std::string& /*text*/, Vector2 /*pos*/,
                   Color /*color*/) override {
        // 文本栅格化由 font_provider（后续 Task）落地；当前无字体 atlas，暂为 no-op。
        // 文本逻辑调用经此进入，保证 _draw 的 draw_text 调用有真实落点（即便暂不渲染）。
    }

private:
    Canvas2D& canvas_;

    // 非填充矩形 = 4 条 1px 填充矩形（上下左右边）。复用当前已设的 color 状态。
    void draw_rect_border_(Rect2 rect) {
        // 上边
        canvas_.draw_rect_filled(Rect2{rect.pos, {rect.size.x, 1.0f}});
        // 下边
        canvas_.draw_rect_filled(
            Rect2{{rect.pos.x, rect.pos.y + rect.size.y - 1.0f},
                  {rect.size.x, 1.0f}});
        // 左边
        canvas_.draw_rect_filled(Rect2{rect.pos, {1.0f, rect.size.y}});
        // 右边
        canvas_.draw_rect_filled(
            Rect2{{rect.pos.x + rect.size.x - 1.0f, rect.pos.y},
                  {1.0f, rect.size.y}});
    }
};

}  // namespace eda
