#pragma once

#include "platform/window.h"
#include "servers/rendering_server.h"  // RenderingDriver

struct GLFWwindow;

namespace eda {

// GlfwWindow —— Window 的 GLFW 引导实现。
//
// 构造时创建窗口并按 RenderingDriver 配置 client API：
//   - OPENGL  : GL 3.3 Core Profile 上下文，glfwMakeContextCurrent
//   - VULKAN  : GLFW_NO_API（不创建 GL 上下文，窗口仅作呈现目标）
//
// 析构时销毁窗口并 glfwTerminate（当前假设单窗口场景；多窗口需引用计数化 init/terminate）。
//
// poll_events() 调 glfwPollEvents；期间 GLFW 回调把原始输入打包成 InputEvent，
// 经基类 on_* 成员投递给上层注册的回调。
class GlfwWindow : public Window {
public:
    GlfwWindow(int width, int height, const char* title, RenderingDriver driver);
    ~GlfwWindow() override;

    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;

    bool should_close() const override;
    void poll_events() override;
    void swap_buffers() override;

    Vector2i size() const override;
    Vector2i position() const override;
    void* get_native_handle() const override;

private:
    GLFWwindow* window_ = nullptr;
    Vector2 last_mouse_pos_{};  // 由 cursor pos 回调维护，供 button/scroll 事件附带

    // 注册 GLFW 回调：key / cursor pos / mouse button / scroll / framebuffer size
    void register_callbacks_();
};

}  // namespace eda
