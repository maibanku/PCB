// StatusBar —— 实现（P4 widgets）。

#include "scene/gui/widgets/status_bar.h"

#include "scene/gui/widgets/label.h"
#include "scene/gui/control.h"  // Control（用作 EXPAND spacer）
#include "scene/gui/theme.h"

#include <string>

namespace eda {

StatusBar::StatusBar() {
    // 状态文本与右侧 DRC/Zoom 之间留空；中间 EXPAND spacer 推右侧两标签到底栏右端。
    set_spacing(16.0f);

    status_label_ = new Label();
    add_child(status_label_);

    // EXPAND spacer：吞掉剩余水平空间，把 DRC / Zoom 标签推到右侧。
    auto* spacer = new Control();
    add_child(spacer);
    set_h_size_flag(spacer, SizeFlag::EXPAND);

    drc_label_ = new Label();
    add_child(drc_label_);

    zoom_label_ = new Label();
    add_child(zoom_label_);

    // 初始填充：依默认 status_/drc_count_/zoom_ 渲染三个标签。
    update_labels_();
}

void StatusBar::set_status(const std::string& text) {
    if (status_ == text) return;
    status_ = text;
    update_labels_();
}

void StatusBar::set_drc_count(int count) {
    if (drc_count_ == count) return;
    drc_count_ = count;
    update_labels_();
}

void StatusBar::set_zoom(float zoom) {
    if (zoom_ == zoom) return;
    zoom_ = zoom;
    update_labels_();
}

void StatusBar::update_labels_() {
    // 状态文本：原样回显。
    status_label_->set_text(status_);

    // DRC："DRC: <N>"（正负皆支持，便于表达未运行 / 出错计数）。
    drc_label_->set_text("DRC: " + std::to_string(drc_count_));

    // Zoom："Zoom: <NN>%" —— 1.0 → 100%。+0.5f 四舍五入到最近整数百分比。
    int pct = static_cast<int>(zoom_ * 100.0f + 0.5f);
    zoom_label_->set_text("Zoom: " + std::to_string(pct) + "%");
}

Vector2 StatusBar::get_minimum_size() const {
    // 高度固定 24px；宽度由父容器决定（0 表示可任意宽）。
    return Vector2{0.0f, kFixedHeight};
}

void StatusBar::_draw() {
    auto* cv = current_canvas();
    if (cv == nullptr) return;

    Vector2 s = get_size();
    if (s.x <= 0.0f || s.y <= 0.0f) return;

    // 背景铺满 secondary_background（任务规范：statusbar 用 secondary_background）。
    const auto& theme = ThemeManager::get().get_current();
    Color bg = theme.get_color("secondary_background");
    cv->draw_rect(Rect2{{0.0f, 0.0f}, s}, bg, /*filled=*/true);
}

}  // namespace eda
