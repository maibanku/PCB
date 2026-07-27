// DockPanel 实现（P4 Task 4）。
//
// 要点：
//   - set_content：reparent 新内容到本面板（CanvasItem::set_parent 自动先从旧
//     父剥离再挂入新父），旧内容若仍是本面板子节点则置其 parent=nullptr 剥离。
//   - layout_content_：anchors 全置 0 + set_position/set_size 把内容锁定到标题栏
//     下方的绝对像素矩形（对齐 Container::fit_child_in_rect 语义；set_position/
//     set_size 触发 NOTIFICATION_RESIZED，嵌套容器内容可借此重排）。
//   - _notification(NOTIFICATION_RESIZED)：自身尺寸变化 → 重排内容。
//   - _draw：标题栏背景 / 标题文本 / 边框三元素；颜色全部经 ThemeManager 查表，
//     缺失键回退到命名常量（DEFAULT_TITLEBAR_BG 等，不污染业务调色板）。

#include "editor/dock/dock_panel.h"

#include <algorithm>

#include "scene/gui/theme.h"

namespace eda {

namespace {

// 标题栏 / 边框回退色（仅当主题缺失对应键时使用；正常路径走 ThemeManager）。
// 命名常量而非魔法数字，便于审阅与未来替换为 theme token。
constexpr Color kFallbackTitlebarBg{0.18f, 0.20f, 0.24f, 1.0f};
constexpr Color kFallbackTitlebarFg{0.85f, 0.87f, 0.90f, 1.0f};
constexpr Color kFallbackBorder{0.10f, 0.12f, 0.14f, 1.0f};

// Theme::get_color 缺键返回 Color{0,0,0,1}（黑），无法与"显式设置黑色"区分；
// 对 dock 标题栏需要的几个键，未设置时退回 fallback。这里以 has_* 判定。
Color lookup_color_or(const char* key, Color fallback) {
    const Theme& t = ThemeManager::get().get_current();
    if (t.has_color(key)) return t.get_color(key);
    return fallback;
}

}  // namespace

DockPanel::DockPanel() = default;

// —— 标题 / 图标 ——

void DockPanel::set_title(const std::string& title) {
    if (title_ == title) return;
    title_ = title;
    queue_redraw();
}

void DockPanel::set_icon(const std::string& icon) {
    if (icon_ == icon) return;
    icon_ = icon;
    queue_redraw();
}

// —— 内容托管 ——

void DockPanel::set_content(Control* c) {
    if (content_ == c) return;
    // 剥离旧内容：若仍是本面板子节点，detach（不 delete；调用方拥有生命周期）。
    if (content_ != nullptr && content_->get_parent() == this) {
        content_->set_parent(nullptr);
    }
    content_ = c;
    // 挂入新内容（CanvasItem::set_parent 内部先从旧父 remove_child 再 add_child）。
    if (c != nullptr && c->get_parent() != this) {
        c->set_parent(this);
    }
    layout_content_();
    queue_redraw();
}

void DockPanel::set_visible(bool v) {
    if (visible_ == v) return;
    visible_ = v;
    queue_redraw();
}

// —— 关闭 / 浮动按钮请求 ——

void DockPanel::request_close() {
    close_requested_ = true;
    queue_redraw();
}

void DockPanel::request_float() {
    float_requested_ = true;
    queue_redraw();
}

// —— 标题栏几何 ——

void DockPanel::set_title_bar_height(float h) {
    if (title_bar_height_ == h) return;
    title_bar_height_ = h;
    layout_content_();
    update_minimum_size();
    queue_redraw();
}

// —— 最小尺寸 ——

Vector2 DockPanel::get_minimum_size() const {
    // 宽：内容最小宽（若无内容则 0）；高：标题栏 + 内容最小高。
    Vector2 content_min{0, 0};
    if (content_ != nullptr) {
        content_min = content_->get_combined_minimum_size();
    }
    return Vector2{content_min.x, title_bar_height_ + content_min.y};
}

// —— 通知 ——

void DockPanel::_notification(int what) {
    Control::_notification(what);
    if (what == NOTIFICATION_RESIZED) {
        layout_content_();
    }
}

// —— 自绘 ——

void DockPanel::_draw() {
    auto* canvas = current_canvas();
    if (!canvas) return;

    Vector2 s = get_size();
    if (s.x <= 0.0f || s.y <= 0.0f) return;

    const Color titlebar_bg = lookup_color_or("secondary_background", kFallbackTitlebarBg);
    const Color titlebar_fg = lookup_color_or("foreground", kFallbackTitlebarFg);
    const Color border_clr   = lookup_color_or("border", kFallbackBorder);

    // 标题栏背景。
    Rect2 title_bar{{0.0f, 0.0f}, {s.x, title_bar_height_}};
    canvas->draw_rect(title_bar, titlebar_bg, /*filled=*/true);

    // 标题文本（左对齐，留 6px 内边距；y 取基线近似 4）。
    if (!title_.empty()) {
        canvas->draw_text(title_, {6.0f, 4.0f}, titlebar_fg);
    }

    // 外框（1px 非填充，框住整个面板）。
    canvas->draw_rect(Rect2{{0.0f, 0.0f}, s}, border_clr, /*filled=*/false);
}

// —— 内部：内容布局 ——

void DockPanel::layout_content_() {
    if (content_ == nullptr) return;
    Vector2 s = get_size();
    // 内容区：标题栏下方，宽度铺满，高度 = 总高 - 标题栏（向下截断到 0）。
    float content_h = std::max(0.0f, s.y - title_bar_height_);
    Rect2  content_rect{{0.0f, title_bar_height_}, {s.x, content_h}};

    // 锚点全置 0：使内容几何脱离父尺寸（绝对像素，不被面板尺寸二次拉伸）。
    // 必须在 set_position/set_size 之前完成，否则旧锚点会混入父尺寸项。
    content_->set_anchor(Side::LEFT, 0.0f);
    content_->set_anchor(Side::TOP, 0.0f);
    content_->set_anchor(Side::RIGHT, 0.0f);
    content_->set_anchor(Side::BOTTOM, 0.0f);
    content_->set_position(content_rect.pos);
    content_->set_size(content_rect.size);
}

}  // namespace eda
