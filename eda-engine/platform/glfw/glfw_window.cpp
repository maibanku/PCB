#include "platform/glfw/glfw_window.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <stdexcept>

namespace eda {

namespace {

// GLFW 诊断回调 —— 引擎日志系统尚未接入（Plan 后续），当前 stderr 兜底以避免静默吞错。
void on_glfw_error(int code, const char* desc) {
    std::fprintf(stderr, "[eda::GlfwWindow] GLFW error %d: %s\n", code, desc ? desc : "<null>");
}

// GLFW action → KeyAction（GLFW_PRESS/REPEAT/RELEASE → PRESS/REPEAT/RELEASE）
KeyAction to_key_action(int glfw_action) {
    switch (glfw_action) {
        case GLFW_PRESS:   return KeyAction::PRESS;
        case GLFW_REPEAT:  return KeyAction::REPEAT;
        case GLFW_RELEASE: return KeyAction::RELEASE;
        default:           return KeyAction::RELEASE;
    }
}

// GLFW button → MouseButton（LEFT/RIGHT/MIDDLE；X1/X2 等扩展时追加）
MouseButton to_mouse_button(int glfw_button) {
    switch (glfw_button) {
        case GLFW_MOUSE_BUTTON_LEFT:   return MouseButton::LEFT;
        case GLFW_MOUSE_BUTTON_RIGHT:  return MouseButton::RIGHT;
        case GLFW_MOUSE_BUTTON_MIDDLE: return MouseButton::MIDDLE;
        default:                       return MouseButton::LEFT;
    }
}

}  // namespace

GlfwWindow::GlfwWindow(int width, int height, const char* title, RenderingDriver driver) {
    glfwSetErrorCallback(on_glfw_error);
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("eda::GlfwWindow: glfwInit failed");
    }

    if (driver == RenderingDriver::OPENGL) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    } else {
        // Vulkan：窗口不创建 GL 上下文，仅作呈现目标
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (window_ == nullptr) {
        glfwTerminate();
        throw std::runtime_error("eda::GlfwWindow: glfwCreateWindow failed");
    }

    if (driver == RenderingDriver::OPENGL) {
        glfwMakeContextCurrent(window_);
    }

    glfwSetWindowUserPointer(window_, this);
    register_callbacks_();
}

GlfwWindow::~GlfwWindow() {
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

bool GlfwWindow::should_close() const {
    return glfwWindowShouldClose(window_) == GLFW_TRUE;
}

void GlfwWindow::poll_events() {
    glfwPollEvents();
}

void GlfwWindow::swap_buffers() {
    glfwSwapBuffers(window_);
}

Vector2i GlfwWindow::size() const {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    return {w, h};
}

Vector2i GlfwWindow::position() const {
    int x = 0, y = 0;
    glfwGetWindowPos(window_, &x, &y);
    return {x, y};
}

void* GlfwWindow::get_native_handle() const {
    return window_;
}

void GlfwWindow::register_callbacks_() {
    // 按键：GLFW key code 直接透传为 InputEvent::key；action 映射到 KeyAction。
    glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
        auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
        if (self == nullptr || !self->on_key_) {
            return;
        }
        InputEvent e{};
        e.type = InputEvent::Type::KEY;
        e.key = key;
        e.action = to_key_action(action);
        e.mouse_pos = self->last_mouse_pos_;
        self->on_key_(e);
    });

    // 鼠标移动：更新缓存位置；仅在有回调时投递 MOUSE_MOVE。
    glfwSetCursorPosCallback(window_, [](GLFWwindow* w, double xpos, double ypos) {
        auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
        if (self == nullptr) {
            return;
        }
        self->last_mouse_pos_ = Vector2{static_cast<float>(xpos), static_cast<float>(ypos)};
        if (self->on_mouse_move_) {
            InputEvent e{};
            e.type = InputEvent::Type::MOUSE_MOVE;
            e.mouse_pos = self->last_mouse_pos_;
            self->on_mouse_move_(e);
        }
    });

    // 鼠标按键：GLFW_MOUSE_BUTTON_* → MouseButton；action → KeyAction。
    glfwSetMouseButtonCallback(window_, [](GLFWwindow* w, int button, int action, int /*mods*/) {
        auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
        if (self == nullptr || !self->on_mouse_button_) {
            return;
        }
        InputEvent e{};
        e.type = InputEvent::Type::MOUSE_BUTTON;
        e.button = to_mouse_button(button);
        e.action = to_key_action(action);
        e.mouse_pos = self->last_mouse_pos_;
        self->on_mouse_button_(e);
    });

    // 滚轮：y offset → scroll_delta（缩放等用途）；x offset 暂时丢弃。
    glfwSetScrollCallback(window_, [](GLFWwindow* w, double /*xoff*/, double yoff) {
        auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
        if (self == nullptr || !self->on_scroll_) {
            return;
        }
        InputEvent e{};
        e.type = InputEvent::Type::MOUSE_SCROLL;
        e.scroll_delta = static_cast<float>(yoff);
        e.mouse_pos = self->last_mouse_pos_;
        self->on_scroll_(e);
    });

    // 窗口 framebuffer 尺寸变化（HiDPI 下与窗口逻辑尺寸不同）。
    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int width, int height) {
        auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
        if (self == nullptr || !self->on_resize_) {
            return;
        }
        self->on_resize_(width, height);
    });
}

}  // namespace eda
