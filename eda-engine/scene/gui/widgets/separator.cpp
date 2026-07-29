// Separator —— 实现（P4 Task 4）。

#include "scene/gui/widgets/separator.h"

#include "scene/gui/theme.h"

namespace eda {

Separator::Separator() = default;

void Separator::set_orientation(Orientation o) {
    if (orientation_ == o) return;
    orientation_ = o;
    update_minimum_size();
    queue_redraw();
}

Vector2 Separator::get_minimum_size() const {
    // 方向轴上 0（容器拉伸填充），交叉轴 1px 厚度。
    if (orientation_ == Orientation::HORIZONTAL) {
        return Vector2{0.0f, 1.0f};
    }
    return Vector2{1.0f, 0.0f};
}

void Separator::_draw() {
    auto* cv = current_canvas();
    if (cv == nullptr) return;

    const auto& theme = ThemeManager::get().get_current();
    Color line_color = theme.get_color("border");

    Rect2 r = get_rect();
    Rect2 line;

    if (orientation_ == Orientation::HORIZONTAL) {
        // 水平 1px 线：铺满 width，垂直居中（即使 height > 1 也画在中线）。
        line.pos   = {r.pos.x, r.pos.y + r.size.y * 0.5f};
        line.size  = {r.size.x, 1.0f};
    } else {
        // 垂直 1px 线：铺满 height，水平居中。
        line.pos   = {r.pos.x + r.size.x * 0.5f, r.pos.y};
        line.size  = {1.0f, r.size.y};
    }

    cv->draw_rect(line, line_color, /*filled=*/true);
}

}  // namespace eda
