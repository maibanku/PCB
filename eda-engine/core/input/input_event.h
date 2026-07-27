#pragma once

#include "core/math/vector2.h"

namespace eda {

// 按键动作（对应 GLFW_PRESS / GLFW_RELEASE / GLFW_REPEAT）
enum class KeyAction { PRESS, RELEASE, REPEAT };

// 鼠标按键（左/右/中，扩展时追加 X1/X2 等）
enum class MouseButton { LEFT, RIGHT, MIDDLE };

// 输入事件（tagged union 风格）：上层（窗口/平台）把原始输入打包成 InputEvent 后 push 进 InputState。
// 不同 Type 使用不同字段：
//   - KEY            : key + action
//   - MOUSE_MOVE     : mouse_pos
//   - MOUSE_BUTTON   : button + action + mouse_pos
//   - MOUSE_SCROLL   : scroll_delta + mouse_pos
struct InputEvent {
    enum class Type { KEY, MOUSE_MOVE, MOUSE_BUTTON, MOUSE_SCROLL } type = Type::KEY;
    int key = 0;                                  // GLFW key code（KEY 事件有效）
    KeyAction action = KeyAction::PRESS;          // KEY/MOUSE_BUTTON 事件有效
    Vector2 mouse_pos{};                          // MOUSE_* 事件的屏幕坐标
    MouseButton button = MouseButton::LEFT;       // MOUSE_BUTTON 事件有效
    float scroll_delta = 0.0f;                    // MOUSE_SCROLL 事件的滚轮偏移（y 轴）
};

}  // namespace eda
