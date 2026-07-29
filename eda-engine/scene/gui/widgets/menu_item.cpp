// MenuItem —— 实现（P4 菜单系统）。
//
// 要点：
//   - set_text / set_shortcut_text / set_separator 触发 update_minimum_size（向父
//     冒泡重排）+ queue_redraw。
//   - activate()：disabled / separator 静默跳过（不 emit activated），否则 emit。
//     真实点击由 Menu 命中检测转发；测试与键盘路径可直调。
//   - _draw：颜色全部走 ThemeManager 查表（hover → highlight / highlight_foreground；
//     默认透明背景；disabled → disabled_foreground；separator → border 横线）。
//     文本左对齐 8px 内边距；快捷键右对齐 8px 内边距。

#include "scene/gui/widgets/menu_item.h"

#include "scene/gui/theme.h"

namespace eda {

MenuItem::MenuItem() = default;

// —— 文本 / 快捷键 / command_id ——

void MenuItem::set_text(const std::string& text) {
    if (text_ == text) return;
    text_ = text;
    update_minimum_size();
    queue_redraw();
}

void MenuItem::set_shortcut_text(const std::string& text) {
    if (shortcut_text_ == text) return;
    shortcut_text_ = text;
    update_minimum_size();
    queue_redraw();
}

void MenuItem::set_command_id(const std::string& id) {
    command_id_ = id;
}

// —— 状态 ——

void MenuItem::set_disabled(bool d) {
    if (disabled_ == d) return;
    disabled_ = d;
    if (disabled_) hover_ = false;  // 禁用时清掉 hover，避免遗留视觉
    queue_redraw();
}

void MenuItem::set_separator(bool s) {
    if (separator_ == s) return;
    separator_ = s;
    update_minimum_size();  // separator 高度退化为 1px
    queue_redraw();
}

void MenuItem::set_hover(bool h) {
    if (disabled_ || separator_) h = false;  // 禁用 / 分隔线不响应 hover
    if (hover_ == h) return;
    hover_ = h;
    queue_redraw();
}

// —— 用户点击 ——

void MenuItem::activate() {
    // 禁用 / 分隔线：静默跳过（不 emit activated）。
    if (disabled_ || separator_) return;
    activated_.emit();
}

// —— 最小尺寸 ——

Vector2 MenuItem::get_minimum_size() const {
    // separator：1px 横线，宽度由容器拉伸。
    if (separator_) {
        return Vector2{0.0f, 1.0f};
    }

    // 字符宽 ≈ 字号 × 0.5（与 Label/Button 一致的经验估值）。
    constexpr int  kFontSize   = 14;
    constexpr float kCharWidth = static_cast<float>(kFontSize) * 0.5f;

    float text_w      = kCharWidth * static_cast<float>(text_.size());
    float shortcut_w  = kCharWidth * static_cast<float>(shortcut_text_.size());
    // 文本 + 快捷键 + 左右内边距 8×2 + 中间 16px 间隔（仅当两者都非空）。
    float width = text_w + 16.0f;
    if (!shortcut_text_.empty()) {
        width += shortcut_w + 16.0f;
    }
    float height = static_cast<float>(kFontSize) + 12.0f;  // 字号 + 上下 6×2 padding
    return Vector2{width, height};
}

// —— 自绘 ——

void MenuItem::_draw() {
    auto* cv = current_canvas();
    if (cv == nullptr) return;

    const auto& theme = ThemeManager::get().get_current();
    Rect2       r = get_rect();
    if (r.size.x <= 0.0f || r.size.y <= 0.0f) return;

    // —— separator：1px 横线，无背景 / 无文本 ——
    if (separator_) {
        Rect2 line;
        line.pos  = {r.pos.x, r.pos.y + r.size.y * 0.5f};
        line.size = {r.size.x, 1.0f};
        cv->draw_rect(line, theme.get_color("border"), /*filled=*/true);
        return;
    }

    // —— 背景：hover 填充 highlight；默认不画（透明，由 popup 底色承载）——
    if (hover_ && !disabled_) {
        cv->draw_rect(r, theme.get_color("highlight"), /*filled=*/true);
    }

    // —— 文本：左对齐 8px 内边距 ——
    if (!text_.empty()) {
        Color fg = disabled_ ? theme.get_color("disabled_foreground")
                             : (hover_ ? theme.get_color("highlight_foreground")
                                       : theme.get_color("foreground"));
        Vector2 text_pos{r.pos.x + 8.0f, r.pos.y + 4.0f};
        cv->draw_text(text_, text_pos, fg);
    }

    // —— 快捷键文本：右对齐 8px 内边距（估值字符宽 × 字数从右往左推算起点）——
    if (!shortcut_text_.empty()) {
        Color  fg = disabled_ ? theme.get_color("disabled_foreground")
                              : (hover_ ? theme.get_color("highlight_foreground")
                                        : theme.get_color("secondary_foreground"));
        constexpr float kCharWidth = 14.0f * 0.5f;
        float sw = kCharWidth * static_cast<float>(shortcut_text_.size());
        Vector2 sc_pos{r.pos.x + r.size.x - sw - 8.0f, r.pos.y + 4.0f};
        cv->draw_text(shortcut_text_, sc_pos, fg);
    }
}

}  // namespace eda
