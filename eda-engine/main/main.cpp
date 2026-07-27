#include <cstdio>
#include <cstdlib>

#include "core/input/input_state.h"
#include "core/math/color.h"
#include "core/math/rect2.h"
#include "core/math/vector2.h"
#include "drivers/opengl/rasterizer_gl.h"
#include "platform/glfw/glfw_window.h"
#include "scene/2d/canvas_2d.h"
#include "servers/rendering_server.h"

using namespace eda;

// Phase 0 Demo：EDA 风格矢量图形（网格 / 焊盘 / 走线 / 过孔）+ 滚轮缩放。
// 用法：
//   eda_main          — 持续运行到关窗
//   eda_main <N>      — 跑 N 帧后退出（烟雾测试用）
int main(int argc, char** argv) {
    int max_frames = -1;
    if (argc > 1) max_frames = std::atoi(argv[1]);

    GlfwWindow window(1280, 800, "EDA Engine — Phase 0 Demo", RenderingDriver::OPENGL);
    RasterizerGL gl;
    gl.initialize(window.get_native_handle());

    Canvas2D canvas(&gl);
    InputState input;
    float zoom = 1.0f;

    window.set_on_scroll([&](const InputEvent& e) {
        zoom *= (e.scroll_delta > 0) ? 1.1f : 0.9f;
        if (zoom < 0.1f) zoom = 0.1f;
        input.push(e);
    });
    window.set_on_key([&](const InputEvent& e) { input.push(e); });
    window.set_on_mouse_button([&](const InputEvent& e) { input.push(e); });
    window.set_on_mouse_move([&](const InputEvent& e) { input.push(e); });

    int frame = 0;
    while (!window.should_close()) {
        window.poll_events();
        input.clear();

        Vector2i sz = window.size();
        int w = sz.x > 0 ? sz.x : 1280;
        int h = sz.y > 0 ? sz.y : 800;
        gl.set_viewport(0, 0, w, h);
        gl.clear(Color{0.10f, 0.10f, 0.12f, 1.0f});

        canvas.clear();

        // 网格（深灰）
        canvas.set_color(Color{0.25f, 0.25f, 0.28f, 1.0f});
        const int grid = 20;
        for (int x = 0; x <= w; x += grid) {
            canvas.draw_line({static_cast<float>(x), 0.0f}, {static_cast<float>(x), static_cast<float>(h)});
        }
        for (int y = 0; y <= h; y += grid) {
            canvas.draw_line({0.0f, static_cast<float>(y)}, {static_cast<float>(w), static_cast<float>(y)});
        }

        // 焊盘（黄色矩形 ×2）
        canvas.set_color(Color{1.0f, 0.85f, 0.0f, 1.0f});
        canvas.draw_rect_filled(Rect2{{200.0f, 200.0f}, {40.0f, 40.0f}});
        canvas.draw_rect_filled(Rect2{{400.0f, 200.0f}, {40.0f, 40.0f}});

        // 走线（蓝色，加粗）
        canvas.set_color(Color{0.2f, 0.5f, 1.0f, 1.0f});
        canvas.set_line_thickness(4.0f);
        canvas.draw_line({240.0f, 220.0f}, {400.0f, 220.0f});
        canvas.set_line_thickness(1.0f);

        // 过孔（红色圆）
        canvas.set_color(Color{1.0f, 0.2f, 0.2f, 1.0f});
        canvas.draw_circle_filled({320.0f, 220.0f}, 8.0f);

        canvas.flush();
        gl.end_frame();
        window.swap_buffers();

        if (max_frames > 0 && ++frame >= max_frames) break;
    }

    gl.shutdown();
    std::printf("EDA main exited cleanly after %d frame(s); zoom=%.2f\n", frame, zoom);
    return 0;
}
