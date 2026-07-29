// Button —— 实现（P4 Task 4）。

#include "scene/gui/widgets/button.h"

#include "scene/gui/theme.h"

namespace eda {

Button::Button() {
    // 默认可聚焦：键盘 Tab/Shift+Tab 也可触发焦点高亮（无障碍 WCAG 2.1 前置）。
    set_focus_mode(FocusMode::CLICK);
}

void Button::set_text(const std::string& text) {
    if (text_ == text) return;
    text_ = text;
    update_minimum_size();
    queue_redraw();
}

void Button::set_disabled(bool d) {
    if (disabled_ == d) return;
    disabled_ = d;
    if (disabled_) {
        // 禁用时清干净中间态，避免遗留 hover/pressed 视觉。
        hover_   = false;
        pressed_ = false;
    }
    queue_redraw();
}

bool Button::contains_(Vector2 p) const {
    Rect2 r = get_rect();
    return p.x >= r.pos.x && p.x <= r.pos.x + r.size.x &&
           p.y >= r.pos.y && p.y <= r.pos.y + r.size.y;
}

void Button::set_hover_(bool h) {
    if (hover_ == h) return;
    hover_ = h;
    queue_redraw();
    (h ? mouse_entered_ : mouse_exited_).emit();
}

void Button::set_pressed_(bool p) {
    if (pressed_ == p) return;
    pressed_ = p;
    queue_redraw();
    (p ? mouse_down_ : mouse_up_).emit();
}

void Button::_input(const InputEvent& event) {
    if (disabled_) return;  // 禁用态拒绝所有输入

    if (event.type == InputEvent::Type::MOUSE_MOVE) {
        set_hover_(contains_(event.mouse_pos));
        return;
    }

    if (event.type == InputEvent::Type::MOUSE_BUTTON &&
        event.button == MouseButton::LEFT) {
        if (event.action == KeyAction::PRESS) {
            // 仅当光标在按钮内按下时进入 pressed 态（允许外部按下拖入后不触发）。
            if (hover_) set_pressed_(true);
        } else if (event.action == KeyAction::RELEASE) {
            // 释放：先记下是否 pressed，再清态；若仍在按钮内则触发 clicked。
            bool was_pressed = pressed_;
            if (pressed_) set_pressed_(false);
            if (was_pressed && hover_) clicked_.emit();
        }
    }
}

Vector2 Button::get_minimum_size() const {
    // 估值：text 宽 + 水平 2×8 padding；高 = 14 字号 + 垂直 2×6 padding。
    int fs = 14;
    float char_w = static_cast<float>(fs) * 0.5f;
    float text_w = char_w * static_cast<float>(text_.size());
    return Vector2{text_w + 16.0f, static_cast<float>(fs) + 12.0f};
}

void Button::_draw() {
    auto* cv = current_canvas();
    if (cv == nullptr) return;

    const auto& theme = ThemeManager::get().get_current();

    // 4 态 → 4 套配色 token（换肤零业务改动，全部查表）。
    Color bg, fg;
    if (disabled_) {
        bg = theme.get_color("disabled");
        fg = theme.get_color("disabled_foreground");
    } else if (pressed_) {
        bg = theme.get_color("accent");
        fg = theme.get_color("accent_foreground");
    } else if (hover_) {
        bg = theme.get_color("highlight");
        fg = theme.get_color("highlight_foreground");
    } else {
        bg = theme.get_color("secondary_background");
        fg = theme.get_color("foreground");
    }

    Rect2 r = get_rect();

    // 状态底色（填充矩形；圆角为后续 Task，当前用矩形）。
    cv->draw_rect(r, bg, /*filled=*/true);

    // 边框：聚焦时 focus_border（无障碍可视指示），否则普通 border。
    Color border_color = has_focus() ? theme.get_color("focus_border")
                                     : theme.get_color("border");
    cv->draw_rect(r, border_color, /*filled=*/false);

    // 文本占位（生产由 font_provider 落地字形）。
    if (!text_.empty()) {
        Vector2 text_pos{r.pos.x + 8.0f, r.pos.y + 8.0f};
        cv->draw_text(text_, text_pos, fg);
    }
}

}  // namespace eda
