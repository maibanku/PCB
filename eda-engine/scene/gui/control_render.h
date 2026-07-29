// control_render —— Control 子树 → Canvas2D 渲染管线接通（P4 Task 4）。
//
// render_subtree(Control* root, Canvas2D& canvas)：
//   1. 构造 ProductionControlCanvas 绑定到 canvas（适配 ControlCanvas 接口 → Canvas2D）
//   2. ControlCanvasScope RAII 绑定到 thread-local 当前画布（Control._draw 经
//      current_canvas() 取到此画布下发绘制调用）
//   3. DFS 前序遍历 Control 子树，对每个 CanvasItem 调 draw_if_dirty()
//      （dirty 则分派 _draw + 焦点高亮框架逻辑）
//
// 完整数据流（自顶向下）：
//   Control 树
//     → render_subtree DFS 遍历
//     → 每个 Control.draw_if_dirty() → _draw() 经 current_canvas() 下发
//     → ProductionControlCanvas::{draw_rect, draw_text}
//     → Canvas2D::draw_rect_filled / draw_line（顶点缓冲追加 CanvasVertex 三角形）
//     → Canvas2D::flush() 上传 RenderingServer::draw_triangles（帧边界由主循环触发）
//     → RasterizerGL::draw_triangles → GPU
//
// 由主循环每帧调用：
//   canvas.clear();                  // 帧起始清顶点缓冲（或 flush 提交后自动清）
//   render_subtree(root, canvas);    // 遍历 Control 树栅格化
//   canvas.flush();                  // 提交到 RenderingServer / RasterizerGL

#pragma once

namespace eda {

class Control;
class Canvas2D;

// 渲染 Control 子树到 Canvas2D。
// root 为 nullptr 时安全空操作。仅遍历 CanvasItem 类型节点（跳过非 CanvasItem 的
// Node），对每个节点调 draw_if_dirty()；未 queue_redraw 的节点跳过以省功。
void render_subtree(Control* root, Canvas2D& canvas);

}  // namespace eda
