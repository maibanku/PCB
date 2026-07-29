// ToolBar —— 实现（P4 widgets）。

#include "scene/gui/widgets/tool_bar.h"

#include "scene/gui/widgets/button.h"
#include "scene/gui/theme.h"
#include "editor/command_palette/command_registry.h"

namespace eda {

ToolBar::ToolBar() {
    // 项间紧凑间距：按钮自身已有水平 padding，2px 项间距足够视觉分组。
    set_spacing(2.0f);

    // 构造期注册常用工具：[New][Open][Save] │ [Undo][Redo] │ [Zoom+][Zoom-][Fit]
    // command_id 与 command_registry.h namespace builtin 同名常量一致。
    add_button("New",   "file.new");
    add_button("Open",  "file.open");
    add_button("Save",  "file.save");
    add_separator();
    add_button("Undo",  "edit.undo");
    add_button("Redo",  "edit.redo");
    add_separator();
    add_button("Zoom+", "view.zoom_in");
    add_button("Zoom-", "view.zoom_out");
    add_button("Fit",   "view.zoom_fit");
}

Button* ToolBar::add_button(const std::string& text, const std::string& command_id) {
    auto* btn = new Button();
    btn->set_text(text);

    // 捕获 command_id 副本（command_id 是 const&，可能指向调用方临时对象）。
    // 点击时刻查 CommandRegistry 单例 → 找到则调 handler（缺命令 / 空 handler 安全跳过）。
    std::string id = command_id;
    btn->clicked().connect([id]() {
        const Command* cmd = CommandRegistry::get().get(id);
        if (cmd != nullptr && cmd->handler) {
            cmd->handler();
        }
    });

    add_child(btn);
    buttons_.push_back(btn);
    by_command_[command_id] = btn;
    return btn;
}

Separator* ToolBar::add_separator() {
    auto* sep = new Separator();
    sep->set_orientation(Orientation::VERTICAL);  // 工具栏水平排列 → 垂直分隔线
    add_child(sep);
    separators_.push_back(sep);
    return sep;
}

Button* ToolBar::get_button(int index) const {
    if (index < 0 || index >= static_cast<int>(buttons_.size())) return nullptr;
    return buttons_[static_cast<size_t>(index)];
}

Button* ToolBar::find_button(const std::string& command_id) const {
    auto it = by_command_.find(command_id);
    return it == by_command_.end() ? nullptr : it->second;
}

void ToolBar::_draw() {
    auto* cv = current_canvas();
    if (cv == nullptr) return;

    Vector2 s = get_size();
    if (s.x <= 0.0f || s.y <= 0.0f) return;

    // 背景铺满 secondary_background（任务配色规范：toolbar 用 secondary_background）。
    const auto& theme = ThemeManager::get().get_current();
    Color bg = theme.get_color("secondary_background");
    cv->draw_rect(Rect2{{0.0f, 0.0f}, s}, bg, /*filled=*/true);
}

}  // namespace eda
