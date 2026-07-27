#pragma once

#include "core/input/input_event.h"
#include "core/math/vector2.h"

#include <functional>

namespace eda {

// 窗口抽象（对齐 Godot platform/）—— 业务层（main）只持有 Window*，
// 永不直接碰 GLFWwindow* / HWND 等实现细节。GLFW 实现见 platform/glfw。
//
// 职责：
//   - 生命周期与帧驱动：should_close / poll_events / swap_buffers
//   - 窗口几何查询：size / position / get_native_handle
//   - 输入与窗口事件回调：由上层（main / InputState）注册；
//     GlfwWindow 在 poll_events() 期间把底层 GLFW 回调打包成 InputEvent 后投递。
//
// 设计说明：回调统一存储于基类 protected 成员，GlfwWindow 的 GLFW 回调内联读取并分发；
// 当前面向单窗口场景，多窗口需扩展为按窗口隔离的回调表。
class Window {
public:
    using KeyCallback = std::function<void(const InputEvent&)>;
    using MouseButtonCallback = std::function<void(const InputEvent&)>;
    using MouseMoveCallback = std::function<void(const InputEvent&)>;
    using ScrollCallback = std::function<void(const InputEvent&)>;
    using ResizeCallback = std::function<void(int width, int height)>;

    virtual ~Window() = default;

    // —— 生命周期与帧 ——
    virtual bool should_close() const = 0;
    virtual void poll_events() = 0;
    virtual void swap_buffers() = 0;

    // —— 窗口几何 ——
    virtual Vector2i size() const = 0;
    virtual Vector2i position() const = 0;
    // 实现相关原生句柄（GLFW 实现下为 GLFWwindow*）；仅供 RenderingServer::initialize 使用
    virtual void* get_native_handle() const = 0;

    // —— 回调注册（上层投递点）——
    // 按键：GLFW key code 直接透传为 InputEvent::key；GLFW_PRESS/RELEASE/REPEAT → KeyAction
    void set_on_key(KeyCallback cb) { on_key_ = std::move(cb); }
    // 鼠标按键：GLFW_MOUSE_BUTTON_* → MouseButton；action → KeyAction
    void set_on_mouse_button(MouseButtonCallback cb) { on_mouse_button_ = std::move(cb); }
    // 鼠标移动：cursor pos → InputEvent::mouse_pos
    void set_on_mouse_move(MouseMoveCallback cb) { on_mouse_move_ = std::move(cb); }
    // 滚轮：y offset → InputEvent::scroll_delta
    void set_on_scroll(ScrollCallback cb) { on_scroll_ = std::move(cb); }
    // 窗口尺寸变化（framebuffer pixels）
    void set_on_resize(ResizeCallback cb) { on_resize_ = std::move(cb); }

protected:
    KeyCallback on_key_;
    MouseButtonCallback on_mouse_button_;
    MouseMoveCallback on_mouse_move_;
    ScrollCallback on_scroll_;
    ResizeCallback on_resize_;
};

}  // namespace eda
