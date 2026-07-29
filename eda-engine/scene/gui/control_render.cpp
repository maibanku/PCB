// control_render —— 实现（P4 Task 4）。

#include "scene/gui/control_render.h"

#include "scene/gui/control.h"
#include "scene/gui/canvas_item.h"             // CanvasItem / ControlCanvasScope
#include "scene/gui/control_canvas_production.h"  // ProductionControlCanvas
#include "scene/2d/canvas_2d.h"

namespace eda {

void render_subtree(Control* root, Canvas2D& canvas) {
    if (root == nullptr) return;

    // 1. 绑定 ProductionControlCanvas 到 Canvas2D（ControlCanvas 接口 → Canvas2D 栅格化）
    ProductionControlCanvas prod(canvas);
    // 2. RAII 绑定 thread-local 当前画布：Control._draw 内 current_canvas() 取到此画布
    ControlCanvasScope scope(prod);

    // 3. DFS 前序遍历：父先画，子后画（子覆盖父，符合 GUI 合成顺序）。
    //    对每个 CanvasItem 调 draw_if_dirty()；dirty 则分派 _draw + 焦点高亮。
    root->for_each_in_subtree([](Node* n) {
        if (auto* ci = dynamic_cast<CanvasItem*>(n)) {
            ci->draw_if_dirty();
        }
    });
}

}  // namespace eda
