#include "drivers/opengl/rasterizer_gl.h"

// glad 必须在 GLFW 之前：glad 提供 OpenGL 现代函数指针表，
// GLFW 的 <GLFW/glfw3.h> 默认会探测到 glad 已加载后才不会自带 GL 头。
#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace eda {

// 屏幕像素坐标 → 用 uniform u_res 转 NDC（y 翻转以匹配 Canvas2D 的
// 「原点左上、y 向下」屏幕空间输出）。
static const char* kVertexShaderSource = R"(
#version 330 core
in vec2 a_pos;
in vec4 a_color;
uniform vec2 u_res;
out vec4 v_color;
void main() {
    vec2 ndc = (a_pos / u_res) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    v_color = a_color;
}
)";

static const char* kFragmentShaderSource = R"(
#version 330 core
in vec4 v_color;
out vec4 frag;
void main() {
    frag = v_color;
}
)";

unsigned int RasterizerGL::compile_program_() {
    auto compile_shader = [](unsigned int type, const char* source) {
        unsigned int shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        return shader;
    };

    unsigned int vs = compile_shader(GL_VERTEX_SHADER, kVertexShaderSource);
    unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShaderSource);

    unsigned int program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

RenderingDriver RasterizerGL::backend() const {
    return RenderingDriver::OPENGL;
}

void RasterizerGL::initialize(void* native_window_handle) {
    // platform 层已创建窗口与 GL context；这里绑定到当前线程，再用 glad
    // 通过 GLFW 的 proc address loader 加载 GL 函数指针表。
    glfwMakeContextCurrent(static_cast<GLFWwindow*>(native_window_handle));
    gladLoadGL(glfwGetProcAddress);

    shader_ = compile_program_();
    loc_resolution_ = glGetUniformLocation(shader_, "u_res");

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);

    initialized_ = true;
}

void RasterizerGL::shutdown() {
    if (!initialized_) return;
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shader_);
    initialized_ = false;
}

void RasterizerGL::begin_frame() {
    // 当前实现：帧内 GL 状态由 clear()/draw_triangles() 内联管理。
}

void RasterizerGL::end_frame() {
    // 提交命令流；glfwSwapBuffers 由 platform/Window 层在 end_frame 之后调用。
    glFlush();
}

void RasterizerGL::clear(Color color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void RasterizerGL::set_viewport(int x, int y, int w, int h) {
    viewport_w_ = w;
    viewport_h_ = h;
    glViewport(x, y, w, h);
}

void RasterizerGL::draw_triangles(const std::vector<CanvasVertex>& vertices) {
    if (vertices.empty()) return;

    glUseProgram(shader_);
    glUniform2f(loc_resolution_,
                static_cast<float>(viewport_w_),
                static_cast<float>(viewport_h_));

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(CanvasVertex)),
                 vertices.data(),
                 GL_DYNAMIC_DRAW);

    // 属性 0：pos（2 float，偏移 0）
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex),
                          reinterpret_cast<void*>(0));
    // 属性 1：color（4 float，偏移 8 = 2 * sizeof(float)）
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex),
                          reinterpret_cast<void*>(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
}

}  // namespace eda
