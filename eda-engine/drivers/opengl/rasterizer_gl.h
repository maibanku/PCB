#pragma once

#include "servers/rendering_server.h"

namespace eda {

// RasterizerGL —— RenderingServer 的 OpenGL 3.3 core 实现。
//
// 顶点布局：pos (Vector2, 2 float) + color (Color, 4 float)，stride = 24 字节，
// 与 servers/CanvasVertex 一致：
//   - location 0: vec2 a_pos   偏移 0
//   - location 1: vec4 a_color 偏移 8 (= 2 * sizeof(float))
//
// 职责边界：
//   - 本类只管 GL 状态与帧内绘制；窗口/上下文创建、glfwPollEvents 与
//     glfwSwapBuffers 由 platform/Window 层负责。
//   - initialize(void* native_window_handle) 中 native_window_handle 为
//     GLFWwindow*：绑定到当前线程后用 glad 加载 GL 函数。
class RasterizerGL : public RenderingServer {
public:
    // —— 生命周期 ——
    RenderingDriver backend() const override;
    void initialize(void* native_window_handle) override;
    void shutdown() override;

    // —— 帧边界 ——
    void begin_frame() override;
    void end_frame() override;

    // —— 帧内绘制 ——
    void clear(Color color) override;
    void set_viewport(int x, int y, int w, int h) override;
    void draw_triangles(const std::vector<CanvasVertex>& vertices) override;

private:
    // 编译内嵌 vertex/fragment shader 并链成 program（GL 3.3 core）。
    unsigned int compile_program_();

    unsigned int shader_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int loc_resolution_ = -1;
    int viewport_w_ = 1;
    int viewport_h_ = 1;
    bool initialized_ = false;
};

}  // namespace eda
