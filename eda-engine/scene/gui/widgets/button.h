// Button —— 可点击按钮控件（P4 Task 4 基础 Widget）。
//
// 在 Control 之上叠加：
//   - 状态机：hover_ / pressed_ / disabled_；_draw 按 4 态映射不同 token 配色
//     （disabled / accent / highlight / secondary_background），换肤零业务改动。
//   - 文本 text_ + 最小尺寸估值（字号 × 字符数 + 水平/垂直 padding）。
//   - 事件处理（_input override）：
//       * MOUSE_MOVE  → hover_ = contains(pos)；hover 边沿触发 mouse_entered/exited
//       * MOUSE_BUTTON LEFT：
//           PRESS    → 若 hover 则 pressed_=true，emit mouse_down
//           RELEASE  → 若 pressed 则 pressed_=false，emit mouse_up；
//                      若仍 hover 则 emit clicked（完整 click 语义）
//       * disabled_ 拒绝所有事件并清干净 hover/pressed
//   - clicked / mouse_entered / mouse_exited / mouse_down / mouse_up 五个 Signal<>。
//   - _draw 策略：
//       * draw_rect_filled 画状态底色（圆角为后续 Task；当前用矩形）
//       * draw_rect 非填充画 1px 边框（focus_border 当 has_focus_，否则 border）
//       * draw_text 在左上内边距处占位写文本
//   - 构造默认 focus_mode=CLICK（键盘 Tab/Shift+Tab 也可聚焦，无障碍前置）。
//
// 对齐 Godot scene/gui/button.h（最小子集：无 shortcut / action / toggle 模式）。

#pragma once

#include "scene/gui/control.h"
#include "core/input/input_event.h"
#include "core/object/signal.h"

#include <string>

namespace eda {

class Button : public Control {
public:
    Button();

    Button(const Button&) = delete;
    Button& operator=(const Button&) = delete;

    // —— 文本 ——
    void set_text(const std::string& text);
    const std::string& get_text() const { return text_; }

    // —— 禁用态 ——
    void set_disabled(bool d);
    bool is_disabled() const { return disabled_; }

    // —— 状态查询（测试 / 外部观察用）——
    bool is_hover() const { return hover_; }
    bool is_pressed() const { return pressed_; }

    Vector2 get_minimum_size() const override;

    // —— 信号 ——
    Signal<>& clicked()        { return clicked_; }
    Signal<>& mouse_entered()  { return mouse_entered_; }
    Signal<>& mouse_exited()   { return mouse_exited_; }
    Signal<>& mouse_down()     { return mouse_down_; }
    Signal<>& mouse_up()       { return mouse_up_; }

    // —— 输入事件处理（override Node::_input）——
    void _input(const InputEvent& event) override;

    // —— 类型查询 ——
    static std::string get_class_static() { return "Button"; }
    std::string get_class() const override { return "Button"; }
    bool is_class(const std::string& name) const override {
        return name == "Button" || Control::is_class(name);
    }

protected:
    void _draw() override;

private:
    std::string text_;
    bool hover_    = false;
    bool pressed_  = false;
    bool disabled_ = false;

    Signal<> clicked_;
    Signal<> mouse_entered_;
    Signal<> mouse_exited_;
    Signal<> mouse_down_;
    Signal<> mouse_up_;

    // 屏幕坐标是否落在控件矩形内。
    bool contains_(Vector2 p) const;
    // 设置 hover 态（变化时 queue_redraw + emit 对应 enter/exit 信号）。
    void set_hover_(bool h);
    // 设置 pressed 态（变化时 queue_redraw + emit 对应 down/up 信号）。
    void set_pressed_(bool p);
};

}  // namespace eda
