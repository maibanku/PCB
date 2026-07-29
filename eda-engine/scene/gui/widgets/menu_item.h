// MenuItem —— 菜单项控件（P4 菜单系统）。
//
// 在 Control（P4 Task 1：锚点 / 几何 / 最小尺寸 / 焦点 / 自绘）之上叠加"菜单单项"：
//   - 文本 text_ + 右侧快捷键文本 shortcut_text_（如 "Ctrl+O"）。
//   - command_id_：绑定 CommandRegistry 中的命令 id；Menu 在 activated 信号槽里
//     经此 id 查 handler 执行。MenuItem 自身不持有 registry，保持"哑控件"。
//   - disabled_：禁用态（灰显 + activate 静默跳过）。
//   - separator_：分隔线模式（画一条 1px 横线，无文本 / 不响应点击）。
//   - hover_：鼠标悬停标志（由外层 Menu 的命中检测或测试驱动；_draw 据此切换背景）。
//   - Signal<> activated：用户"点击"（activate()）且非 disabled / 非 separator 时触发。
//
// 自绘策略（颜色全部走 ThemeManager 查表，禁硬编码 RGB，换肤零业务改动）：
//   - separator_ ：1px 横线（border token），无背景 / 无文本。
//   - hover_     ：填充矩形 = highlight token；文本 = highlight_foreground。
//   - 默认       ：不画背景（透明，由 Menu popup 底色承载）；文本 = foreground。
//   - disabled_  ：文本改 disabled_foreground，不响应 hover 高亮。
//   - 文本左对齐（左留 8px 内边距）；快捷键右对齐（右留 8px 内边距）。
//
// 对齐 Godot scene/gui/menu_button.h 中 MenuItem 内部结构（精简：无 checkable /
// submenu / icon，留待后续 Task）。

#pragma once

#include "scene/gui/control.h"
#include "core/object/signal.h"

#include <string>

namespace eda {

class MenuItem : public Control {
public:
    MenuItem();

    MenuItem(const MenuItem&) = delete;
    MenuItem& operator=(const MenuItem&) = delete;

    // —— 文本（菜单项标签）——
    void              set_text(const std::string& text);
    const std::string& get_text() const { return text_; }

    // —— 右侧快捷键文本（如 "Ctrl+O"；仅显示，绑定逻辑在 InputMap）——
    void              set_shortcut_text(const std::string& text);
    const std::string& get_shortcut_text() const { return shortcut_text_; }

    // —— 绑定 CommandRegistry 命令 id（Menu 据此查 handler 执行）——
    void              set_command_id(const std::string& id);
    const std::string& get_command_id() const { return command_id_; }

    // —— 禁用态（灰显 + activate 静默跳过）——
    void  set_disabled(bool d);
    bool  is_disabled() const { return disabled_; }

    // —— 分隔线模式（1px 横线，无文本 / 不响应）——
    void  set_separator(bool s);
    bool  is_separator() const { return separator_; }

    // —— 悬停态（外层 Menu 命中检测写入；驱动 _draw 背景切换）——
    void  set_hover(bool h);
    bool  is_hover() const { return hover_; }

    // —— 用户"点击"该菜单项 ——
    // disabled / separator → 静默跳过（不 emit）。否则 emit activated。
    // 真实鼠标点击由 Menu 的命中检测路由到此；键盘 / 测试可直调。
    void activate();

    // —— activated 信号（Menu 连接此信号，在其槽里查 registry 执行 handler 并 close）——
    Signal<>& activated() { return activated_; }

    // —— 最小尺寸（容器布局消费；separator 退化为 1px 高）——
    Vector2 get_minimum_size() const override;

    // —— 类型查询（基于类名字符串，无 RTTI）——
    static std::string get_class_static() { return "MenuItem"; }
    std::string        get_class() const override { return "MenuItem"; }
    bool               is_class(const std::string& name) const override {
        return name == "MenuItem" || Control::is_class(name);
    }

protected:
    void _draw() override;

private:
    std::string text_;
    std::string shortcut_text_;
    std::string command_id_;
    bool        disabled_  = false;
    bool        separator_ = false;
    bool        hover_     = false;

    Signal<> activated_;
};

}  // namespace eda
