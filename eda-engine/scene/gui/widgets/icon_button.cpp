// IconButton —— 实现（P4 Task 4）。

#include "scene/gui/widgets/icon_button.h"

#include "scene/gui/theme.h"

namespace eda {

IconButton::IconButton() {
    set_focus_mode(FocusMode::CLICK);
}

void IconButton::set_icon_name(const std::string& name) {
    if (icon_name_ == name) return;
    icon_name_ = name;
    queue_redraw();
}

void IconButton::set_disabled(bool d) {
    if (disabled_ == d) return;
    disabled_ = d;
    if (disabled_) {
        hover_   = false;
        pressed_ = false;
    }
    queue_redraw();
}

bool IconButton::contains_(Vector2 p) const {
    Rect2 r = get_rect();
    return p.x >= r.pos.x && p.x <= r.pos.x + r.size.x &&
           p.y >= r.pos.y && p.y <= r.pos.y + r.size.y;
}

void IconButton::_input(const InputEvent& event) {
    if (disabled_) return;

    if (event.type == InputEvent::Type::MOUSE_MOVE) {
        bool inside = contains_(event.mouse_pos);
        if (inside != hover_) {
            hover_ = inside;
            queue_redraw();
        }
        return;
    }

    if (event.type == InputEvent::Type::MOUSE_BUTTON &&
        event.button == MouseButton::LEFT) {
        if (event.action == KeyAction::PRESS) {
            if (hover_) {
                pressed_ = true;
                queue_redraw();
            }
        } else if (event.action == KeyAction::RELEASE) {
            bool was_pressed = pressed_;
            if (pressed_) {
                pressed_ = false;
                queue_redraw();
            }
            if (was_pressed && hover_) clicked_.emit();
        }
    }
}

Vector2 IconButton::get_minimum_size() const {
    // 方形 24×24：图标 16 + 水平/垂直各 4 padding。
    return Vector2{24.0f, 24.0f};
}

void IconButton::_draw() {
    auto* cv = current_canvas();
    if (cv == nullptr) return;

    const auto& theme = ThemeManager::get().get_current();

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

    // 方形底色。
    cv->draw_rect(r, bg, /*filled=*/true);

    // 边框。
    Color border_color = has_focus() ? theme.get_color("focus_border")
                                     : theme.get_color("border");
    cv->draw_rect(r, border_color, /*filled=*/false);

    // 中心 12×12 色块占位图标（真实 icon atlas 由后续 Task 接入）。
    if (!icon_name_.empty()) {
        float cx = r.pos.x + r.size.x * 0.5f;
        float cy = r.pos.y + r.size.y * 0.5f;
        Rect2 icon_rect{{cx - 6.0f, cy - 6.0f}, {12.0f, 12.0f}};
        cv->draw_rect(icon_rect, fg, /*filled=*/true);
    }
}

}  // namespace eda
