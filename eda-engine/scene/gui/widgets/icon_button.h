// IconButton —— 方形图标按钮（P4 Task 4 基础 Widget）。
//
// 在 Control 之上叠加：
//   - 图标逻辑名 icon_name_（对应 Theme icon token：close / open / save / undo 等，
//     实际图标图集由后续 Task 落地）。
//   - 状态机：hover_ / pressed_ / disabled_，配色映射与 Button 同构。
//   - clicked 信号（IconButton 通常不显式 emit enter/exit/down/up，保持极简）。
//   - _draw 策略：
//       * draw_rect_filled 画方形底色（状态映射 token）
//       * draw_rect 非填充画 1px 边框（focus_border 当 has_focus_，否则 border）
//       * draw_rect_filled 在中心 12×12 画前景色块占位图标（真实 icon atlas 后续）
//   - get_minimum_size 默认 24×24（图标 16 + padding 4×2），方形建议由调用方
//     set_size 保持等边。
//
// 对齐 Godot scene/gui/button.h 的 IconButton 变体（最小子集：无 toggle / 无 tooltip）。

#pragma once

#include "scene/gui/control.h"
#include "core/input/input_event.h"
#include "core/object/signal.h"

#include <string>

namespace eda {

class IconButton : public Control {
public:
    IconButton();

    IconButton(const IconButton&) = delete;
    IconButton& operator=(const IconButton&) = delete;

    // —— 图标逻辑名（对应 Theme icon token）——
    void set_icon_name(const std::string& name);
    const std::string& get_icon_name() const { return icon_name_; }

    // —— 禁用态 ——
    void set_disabled(bool d);
    bool is_disabled() const { return disabled_; }

    // —— 状态查询 ——
    bool is_hover() const { return hover_; }
    bool is_pressed() const { return pressed_; }

    Vector2 get_minimum_size() const override;

    // —— 信号 ——
    Signal<>& clicked() { return clicked_; }

    // —— 输入事件处理 ——
    void _input(const InputEvent& event) override;

    // —— 类型查询 ——
    static std::string get_class_static() { return "IconButton"; }
    std::string get_class() const override { return "IconButton"; }
    bool is_class(const std::string& name) const override {
        return name == "IconButton" || Control::is_class(name);
    }

protected:
    void _draw() override;

private:
    std::string icon_name_;
    bool hover_    = false;
    bool pressed_  = false;
    bool disabled_ = false;

    Signal<> clicked_;

    bool contains_(Vector2 p) const;
};

}  // namespace eda
