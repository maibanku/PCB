// Label —— 实现（P4 Task 4）。

#include "scene/gui/widgets/label.h"

#include "scene/gui/theme.h"

namespace eda {

Label::Label() = default;

void Label::set_text(const std::string& text) {
    if (text_ == text) return;
    text_ = text;
    update_minimum_size();  // 文本长度变化 → 向父容器冒泡重排
    queue_redraw();
}

void Label::set_font_token(const std::string& token) {
    if (font_token_ == token) return;
    font_token_ = token;
    update_minimum_size();
    queue_redraw();
}

int Label::font_size_for_token_() const {
    // 与 theme.cpp apply_common_tokens 中 font Variant 的 size 字段对齐：
    //   default=14  small=12  heading=18  （其余 mono=13）
    if (font_token_ == "small")   return 12;
    if (font_token_ == "heading") return 18;
    return 14;  // default / 未知 token 退回默认
}

Vector2 Label::get_minimum_size() const {
    // 估值：字符宽 ≈ 字号 × 0.5（无衬线比例字体经验值），行高 ≈ 字号 × 1.4。
    // 真实度量由 font_provider（后续 Task 协同）落地；此处仅保证布局可用。
    int fs = font_size_for_token_();
    float char_w = static_cast<float>(fs) * 0.5f;
    float line_h = static_cast<float>(fs) * 1.4f;
    float w = char_w * static_cast<float>(text_.size());
    return Vector2{w, line_h};
}

void Label::_draw() {
    auto* cv = current_canvas();
    if (cv == nullptr) return;

    const auto& theme = ThemeManager::get().get_current();
    Color bg = theme.get_color("secondary_background");
    Color fg = theme.get_color("foreground");

    Rect2 r = get_rect();

    // 背景：铺满填充矩形。
    cv->draw_rect(r, bg, /*filled=*/true);

    // 文本占位：在左上内边距处下发自绘调用。生产环境由 font_provider 落地真实字形；
    // 测试环境经 RecordingControlCanvas 记录 (text, pos) 断言。
    if (!text_.empty()) {
        Vector2 text_pos{r.pos.x + 4.0f, r.pos.y + 4.0f};
        cv->draw_text(text_, text_pos, fg);
    }
}

}  // namespace eda
