// TitleBar —— 实现（P4 widgets）。

#include "scene/gui/widgets/title_bar.h"

#include "scene/gui/widgets/button.h"
#include "scene/gui/widgets/label.h"
#include "scene/gui/control.h"  // Control（用作 EXPAND spacer）
#include "scene/gui/theme.h"

namespace eda {

TitleBar::TitleBar() {
    // 标签与按钮组之间留空；中间 EXPAND spacer 推按钮到右侧。
    set_spacing(8.0f);

    // 左一：固定应用名。
    title_label_ = new Label();
    title_label_->set_text("EDA Engine");
    add_child(title_label_);

    // 左二：项目名（默认未命名）。
    project_label_ = new Label();
    project_label_->set_text("(untitled)");
    add_child(project_label_);

    // EXPAND spacer：吞掉剩余水平空间，把后续按钮推到右侧。
    auto* spacer = new Control();
    add_child(spacer);
    set_h_size_flag(spacer, SizeFlag::EXPAND);

    // 窗口按钮文本使用 UTF-8 字面量（源文件 /utf-8，直接写字形即可）：
    //   ─ (U+2500) □ (U+25A1) × (U+00D7)
    minimize_btn_ = new Button();
    minimize_btn_->set_text("\xE2\x94\x80");  // ─
    minimize_btn_->clicked().connect([this]() { minimize_requested_.emit(); });
    add_child(minimize_btn_);

    maximize_btn_ = new Button();
    maximize_btn_->set_text("\xE2\x96\xA1");  // □
    maximize_btn_->clicked().connect([this]() { maximize_requested_.emit(); });
    add_child(maximize_btn_);

    close_btn_ = new Button();
    close_btn_->set_text("\xC3\x97");  // ×
    close_btn_->clicked().connect([this]() { close_requested_.emit(); });
    add_child(close_btn_);
}

void TitleBar::set_project_name(const std::string& name) {
    if (project_name_ == name) return;
    project_name_ = name;
    // 空项目名回退到占位文案，避免标签塌缩为 0 宽。
    project_label_->set_text(name.empty() ? std::string("(untitled)") : name);
}

Vector2 TitleBar::get_minimum_size() const {
    // 高度固定 32px；宽度由父容器决定（0 表示可任意宽）。
    return Vector2{0.0f, kFixedHeight};
}

void TitleBar::_draw() {
    auto* cv = current_canvas();
    if (cv == nullptr) return;

    Vector2 s = get_size();
    if (s.x <= 0.0f || s.y <= 0.0f) return;

    // 背景铺满 background token（任务规范：titlebar 用 background）。
    const auto& theme = ThemeManager::get().get_current();
    Color bg = theme.get_color("background");
    cv->draw_rect(Rect2{{0.0f, 0.0f}, s}, bg, /*filled=*/true);
}

}  // namespace eda
