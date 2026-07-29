// main.cpp —— EDA Editor Shell（P1 + P4 组装）。
//
// 在 P1 窗口/光栅/Canvas2D 之上组装 P4 编辑器外壳：
//   - GlfwWindow 1280×800 + RasterizerGL initialize
//   - ThemeManager（默认 DARK）作为统一颜色 token 源
//   - DockManager 五区域 border layout：
//       LEFT   = 工程树面板占位
//       CENTER = PCB 画布（保留 P1 Demo 的网格 / 焊盘 / 走线 / 过孔）
//       RIGHT  = Inspector（反射属性面板，空 target 占位）
//       BOTTOM = 状态栏占位
//       TOP    = 预留 0px（无顶栏）
//   - InputMap（默认 KiCad 预设）：物理键 → 命令 id 映射中心
//   - CommandPalette（占位）：Ctrl+Shift+P 唤起，Esc 关闭
//   - 主循环：poll → clear → sort → draw(chrome + PCB) → swap
//
// 用法：
//   eda_main          — 持续运行到关窗
//   eda_main <N>      — 跑 N 帧后退出（烟雾测试用）
//
// 渲染说明：DockManager 提供布局几何（五区域 + 面板矩形），主题提供颜色 token，
// 实际像素绘制经 P1 的 Canvas2D 立即模式 API 下发。P4 控件自带 _draw 经
// ControlCanvas 分发，但本壳层为简化集成直接用 Canvas2D + 主题查表绘制 chrome；
// PCB 内容保留 P1 算法（网格 / 焊盘 / 走线 / 过孔），裁剪到 CENTER 区域内。

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/input/input_event.h"
#include "core/input/input_state.h"
#include "core/math/color.h"
#include "core/math/rect2.h"
#include "core/math/vector2.h"
#include "drivers/opengl/rasterizer_gl.h"
#include "editor/command_palette/command_palette.h"
#include "editor/command_palette/command_registry.h"
#include "editor/dock/dock_manager.h"
#include "editor/dock/dock_panel.h"
#include "editor/inspector/inspector.h"
#include "editor/shortcuts/input_map.h"
#include "editor/shortcuts/shortcut_def.h"
#include "platform/glfw/glfw_window.h"
#include "scene/2d/canvas_2d.h"
#include "scene/gui/container.h"
#include "scene/gui/control.h"
#include "scene/gui/theme.h"
#include "servers/rendering_server.h"

using namespace eda;

namespace {

// GLFW 修饰键原始键码（GLFW 3.3+ 稳定公开契约；不引 GLFW 头以保持 editor 模块零依赖）。
// InputMap::key_from_glfw 未覆盖修饰键，故在此显式跟踪其按下状态以维护当前 mods。
constexpr int GLFW_KEY_LEFT_SHIFT_    = 340;
constexpr int GLFW_KEY_RIGHT_SHIFT_   = 344;
constexpr int GLFW_KEY_LEFT_CONTROL_  = 341;
constexpr int GLFW_KEY_RIGHT_CONTROL_ = 345;
constexpr int GLFW_KEY_LEFT_ALT_      = 342;
constexpr int GLFW_KEY_RIGHT_ALT_     = 346;
constexpr int GLFW_KEY_LEFT_SUPER_    = 343;
constexpr int GLFW_KEY_RIGHT_SUPER_   = 347;

// 沿父链累加 get_position()，得到控件相对 DockManager 根的绝对像素位置。
// Dock 树全部由 Control 派生节点构成（VBox/HBox/Container/DockPanel/DockManager），
// static_cast<Node* const> -> const Control* 在本组装中安全。
Vector2 absolute_position(const Control* c) {
    Vector2 acc{0.0f, 0.0f};
    const Node* n = c;
    while (n != nullptr) {
        const auto* ci = static_cast<const Control*>(n);
        acc = acc + ci->get_position();
        n = n->get_parent();
    }
    return acc;
}

// 主题颜色查表；缺失键回退到 fallback（不污染业务调色板）。
Color theme_color_or(const char* key, Color fallback) {
    const Theme& t = ThemeManager::get().get_current();
    if (t.has_color(key)) return t.get_color(key);
    return fallback;
}

// 用 Canvas2D 绘制 1px 非填充矩形边框（Canvas2D 暂无 draw_rect_outline）。
void draw_rect_outline(Canvas2D& canvas, Rect2 r, Color color) {
    canvas.set_color(color);
    canvas.set_line_thickness(1.0f);
    const Vector2 p = r.pos;
    const Vector2 s = r.size;
    canvas.draw_line({p.x, p.y}, {p.x + s.x, p.y});              // top
    canvas.draw_line({p.x, p.y + s.y}, {p.x + s.x, p.y + s.y});  // bottom
    canvas.draw_line({p.x, p.y}, {p.x, p.y + s.y});              // left
    canvas.draw_line({p.x + s.x, p.y}, {p.x + s.x, p.y + s.y});  // right
}

// 绘制 DockPanel chrome：内容区填充 + 标题栏 + 边框。颜色全走主题 token。
void draw_panel_chrome(Canvas2D& canvas, const DockPanel& panel) {
    const Vector2 pos = absolute_position(&panel);
    const Vector2 size = panel.get_size();
    if (size.x <= 0.0f || size.y <= 0.0f) return;

    const Color content_bg = theme_color_or(
        "secondary_background", Color{0.18f, 0.20f, 0.24f, 1.0f});
    const Color titlebar_bg = theme_color_or(
        "secondary_background", Color{0.18f, 0.20f, 0.24f, 1.0f});
    const Color border_clr = theme_color_or(
        "border", Color{0.10f, 0.12f, 0.14f, 1.0f});

    const float title_h = panel.get_title_bar_height();
    const Rect2 content_rect{{pos.x, pos.y + title_h},
                             {size.x, std::max(0.0f, size.y - title_h)}};
    const Rect2 title_rect{{pos.x, pos.y}, {size.x, title_h}};

    // 内容区填充（占位；真实内容由 set_content 接入）。
    canvas.set_color(content_bg);
    canvas.draw_rect_filled(content_rect);
    // 标题栏（视觉区分：略亮叠色经 secondary_background 同色——保持主题一致性）。
    canvas.set_color(titlebar_bg);
    canvas.draw_rect_filled(title_rect);
    // 外框。
    draw_rect_outline(canvas, Rect2{pos, size}, border_clr);
    // 标题栏分隔线。
    const Color sep = theme_color_or("border", border_clr);
    canvas.set_color(sep);
    canvas.set_line_thickness(1.0f);
    canvas.draw_line({pos.x, pos.y + title_h}, {pos.x + size.x, pos.y + title_h});
}

// 绘制 PCB Demo（P1 保留）：网格 + 焊盘 + 走线 + 过孔，裁剪到 CENTER 区域。
// 坐标以 center_rect.pos 为原点偏移；网格按 20px 步进铺满 CENTER。
void draw_pcb_demo(Canvas2D& canvas, Rect2 center_rect, float zoom) {
    const Vector2 origin = center_rect.pos;
    const float w = center_rect.size.x;
    const float h = center_rect.size.y;
    if (w <= 0.0f || h <= 0.0f) return;

    // 背景填充（PCB 画布色，区别于 chrome 的 secondary_background）。
    const Color canvas_bg = theme_color_or(
        "background", Color{0.10f, 0.10f, 0.12f, 1.0f});
    canvas.set_color(canvas_bg);
    canvas.draw_rect_filled(center_rect);

    // 网格（深灰）。
    const Color grid_color = theme_color_or(
        "border", Color{0.25f, 0.25f, 0.28f, 1.0f});
    canvas.set_color(grid_color);
    canvas.set_line_thickness(1.0f);
    const int grid = 20;
    for (int x = 0; x <= static_cast<int>(w); x += grid) {
        const float fx = origin.x + static_cast<float>(x);
        canvas.draw_line({fx, origin.y}, {fx, origin.y + h});
    }
    for (int y = 0; y <= static_cast<int>(h); y += grid) {
        const float fy = origin.y + static_cast<float>(y);
        canvas.draw_line({origin.x, fy}, {origin.x + w, fy});
    }

    // CENTER 内的局部坐标（保留 P1 像素布局，缩放后放置在画布左上区域）。
    auto lx = [&](float x) { return origin.x + x * zoom; };
    auto ly = [&](float y) { return origin.y + y * zoom; };

    // 焊盘（黄色矩形 ×2）。
    const Color pad_color = theme_color_or(
        "warning", Color{1.0f, 0.85f, 0.0f, 1.0f});
    canvas.set_color(pad_color);
    const float pad_w = 40.0f * zoom;
    const float pad_h = 40.0f * zoom;
    canvas.draw_rect_filled(Rect2{{lx(40.0f), ly(40.0f)}, {pad_w, pad_h}});
    canvas.draw_rect_filled(Rect2{{lx(240.0f), ly(40.0f)}, {pad_w, pad_h}});

    // 走线（蓝色，加粗）。
    const Color trace_color = theme_color_or(
        "accent", Color{0.2f, 0.5f, 1.0f, 1.0f});
    canvas.set_color(trace_color);
    canvas.set_line_thickness(4.0f * zoom);
    canvas.draw_line({lx(80.0f), ly(60.0f)}, {lx(240.0f), ly(60.0f)});
    canvas.set_line_thickness(1.0f);

    // 过孔（红色圆）。
    const Color via_color = theme_color_or(
        "error", Color{1.0f, 0.2f, 0.2f, 1.0f});
    canvas.set_color(via_color);
    canvas.draw_circle_filled({lx(160.0f), ly(60.0f)}, 8.0f * zoom);

    // CENTER 边框（强调画布边界）。
    draw_rect_outline(canvas, center_rect,
                      theme_color_or("border", grid_color));
}

// 绘制命令面板占位弹层（顶部居中圆角矩形——简化为直角）。
void draw_palette_overlay(Canvas2D& canvas, const CommandPalette& palette,
                          Vector2 viewport_size) {
    if (!palette.is_open()) return;
    const Color overlay_bg = theme_color_or(
        "overlay", Color{0.0f, 0.0f, 0.0f, 0.50f});
    const Color panel_bg = theme_color_or(
        "secondary_background", Color{0.20f, 0.22f, 0.26f, 1.0f});
    const Color accent = theme_color_or(
        "accent", Color{0.31f, 0.76f, 0.97f, 1.0f});

    // 全屏暗化层。
    canvas.set_color(overlay_bg);
    canvas.draw_rect_filled(Rect2{{0.0f, 0.0f}, viewport_size});

    // 弹层矩形：顶部居中，宽 60%、高 320。
    const float pw = viewport_size.x * 0.6f;
    const float ph = 320.0f;
    const Rect2 body{{(viewport_size.x - pw) * 0.5f, 60.0f}, {pw, ph}};
    canvas.set_color(panel_bg);
    canvas.draw_rect_filled(body);
    // 顶部 accent 强调条（查询行占位）。
    canvas.set_color(accent);
    canvas.draw_rect_filled(Rect2{{body.pos.x, body.pos.y}, {body.size.x, 4.0f}});
    draw_rect_outline(canvas, body, accent);
}

}  // namespace

int main(int argc, char** argv) {
    int max_frames = -1;
    if (argc > 1) max_frames = std::atoi(argv[1]);

    // 0) 主题（默认 DARK；先于控件构造，确保后续 _draw 查表命中）。
    ThemeManager::get().set_theme(ThemePreset::DARK);

    // 1) 窗口 + GL 光栅化器。
    GlfwWindow window(1280, 800, "EDA Engine — Editor Shell", RenderingDriver::OPENGL);
    RasterizerGL gl;
    gl.initialize(window.get_native_handle());

    Canvas2D canvas(&gl);
    InputState input;
    float zoom = 1.0f;
    bool exit_requested = false;

    // 2) 命令注册表（内置占位：open/save/undo/redo/zoom...）+ 命令面板。
    CommandRegistry& registry = CommandRegistry::get();
    registry.register_builtins();
    CommandPalette palette;

    // 3) InputMap（默认 KiCad 预设）。
    InputMap input_map(InputMap::Preset::KICAD);

    // 4) DockManager + 面板。
    //    声明顺序 = 析构逆序：manager 最先析构（级联 delete 五个 area Container），
    //    面板随后析构（已从 area 自剥离），Inspector 最后构造最先析构
    //    （从 inspector_panel 自剥离后再让 panel 析构）。栈对象不触发双重释放。
    DockManager manager;
    DockPanel   project_panel;
    DockPanel   inspector_panel;
    DockPanel   status_panel;
    Inspector   inspector;

    project_panel.set_title("Project Tree");
    inspector_panel.set_title("Inspector");
    status_panel.set_title("Status");
    inspector_panel.set_content(&inspector);

    // 注册 + 停靠。
    manager.register_panel("project",   &project_panel);
    manager.register_panel("inspector", &inspector_panel);
    manager.register_panel("status",    &status_panel);
    manager.dock(DockArea::LEFT,   &project_panel);
    manager.dock(DockArea::RIGHT,  &inspector_panel);
    manager.dock(DockArea::BOTTOM, &status_panel);

    // 让面板在各自区域内铺满主轴（VBox→垂直；HBox→水平）。
    // area 是 panel 的父节点（dock 后 reparent 到 LEFT/RIGHT/BOTTOM 的 VBox/HBox）。
    auto expand_in_area = [](DockPanel* panel, bool vertical_area) {
        Node* parent = panel->get_parent();
        if (parent == nullptr) return;
        auto* area_c = static_cast<Container*>(parent);
        if (vertical_area) {
            area_c->set_v_size_flag(panel, SizeFlag::EXPAND);
        } else {
            area_c->set_h_size_flag(panel, SizeFlag::EXPAND);
        }
    };
    expand_in_area(&project_panel,   /*vertical_area=*/true);   // LEFT=VBox
    expand_in_area(&inspector_panel, /*vertical_area=*/true);   // RIGHT=VBox
    expand_in_area(&status_panel,    /*vertical_area=*/false);  // BOTTOM=HBox

    // 初始布局：用窗口尺寸灌入 manager.size + 五区域 border layout 尺寸。
    auto apply_layout = [&](int ww, int hh) {
        manager.set_size(Vector2{static_cast<float>(ww), static_cast<float>(hh)});
        manager.set_area_size(DockArea::LEFT,   220.0f);
        manager.set_area_size(DockArea::TOP,     0.0f);
        manager.set_area_size(DockArea::RIGHT,  260.0f);
        manager.set_area_size(DockArea::BOTTOM, 60.0f);
        manager.sort_children();
    };

    Vector2i initial_size = window.size();
    apply_layout(initial_size.x > 0 ? initial_size.x : 1280,
                 initial_size.y > 0 ? initial_size.y : 800);

    // 5) 输入回调：维护当前 mods（InputMap.lookup 需此）+ 派发命令。
    auto refresh_mods = [&]() {
        auto down = [&](int k) { return input.is_key_down(k); };
        ModifierFlags m = ModifierFlags::NONE;
        if (down(GLFW_KEY_LEFT_CONTROL_) || down(GLFW_KEY_RIGHT_CONTROL_))
            m = m | ModifierFlags::CTRL;
        if (down(GLFW_KEY_LEFT_SHIFT_) || down(GLFW_KEY_RIGHT_SHIFT_))
            m = m | ModifierFlags::SHIFT;
        if (down(GLFW_KEY_LEFT_ALT_) || down(GLFW_KEY_RIGHT_ALT_))
            m = m | ModifierFlags::ALT;
        if (down(GLFW_KEY_LEFT_SUPER_) || down(GLFW_KEY_RIGHT_SUPER_))
            m = m | ModifierFlags::SUPER;
        input_map.set_current_mods(m);
    };

    const int glfw_esc = InputMap::glfw_from_key(Key::ESCAPE);

    window.set_on_scroll([&](const InputEvent& e) {
        zoom *= (e.scroll_delta > 0) ? 1.1f : 0.9f;
        if (zoom < 0.1f) zoom = 0.1f;
        if (zoom > 10.0f) zoom = 10.0f;
        input.push(e);
    });
    window.set_on_key([&](const InputEvent& e) {
        input.push(e);
        refresh_mods();
        if (e.type != InputEvent::Type::KEY) return;
        if (e.action != KeyAction::PRESS && e.action != KeyAction::REPEAT) return;

        // Esc：面板开则关闭，否则请求退出。
        if (e.key == glfw_esc) {
            if (palette.is_open()) {
                palette.close();
            } else {
                exit_requested = true;
            }
            return;
        }
        // 经 InputMap 派发 → 命令 id。
        const auto cmd = input_map.lookup(e);
        if (cmd) {
            if (*cmd == "command_palette.open") {
                if (palette.is_open()) palette.close(); else palette.open();
            }
            // 其余命令（file.open / edit.undo / view.zoom_in ...）handler 暂为占位。
        }
    });
    window.set_on_mouse_button([&](const InputEvent& e) { input.push(e); });
    window.set_on_mouse_move([&](const InputEvent& e) { input.push(e); });

    // 6) 主循环。
    int frame = 0;
    while (!window.should_close() && !exit_requested) {
        window.poll_events();
        input.clear();

        Vector2i sz = window.size();
        int ww = sz.x > 0 ? sz.x : 1280;
        int hh = sz.y > 0 ? sz.y : 800;
        gl.set_viewport(0, 0, ww, hh);

        const Color clear_bg = theme_color_or(
            "background", Color{0.10f, 0.10f, 0.12f, 1.0f});
        gl.clear(clear_bg);

        // 每帧重排（窗口尺寸可能变化；幂等）。
        apply_layout(ww, hh);

        canvas.clear();

        // —— chrome：LEFT / RIGHT / BOTTOM 面板 ——
        draw_panel_chrome(canvas, project_panel);
        draw_panel_chrome(canvas, inspector_panel);
        draw_panel_chrome(canvas, status_panel);

        // —— CENTER：PCB Demo（P1 保留）——
        const float left_w   = manager.get_area_size(DockArea::LEFT);
        const float right_w  = manager.get_area_size(DockArea::RIGHT);
        const float top_h    = manager.get_area_size(DockArea::TOP);
        const float bottom_h = manager.get_area_size(DockArea::BOTTOM);
        const float inner_x = left_w;
        const float inner_w = std::max(0.0f, static_cast<float>(ww) - left_w - right_w);
        const float inner_y = top_h;
        const float inner_h = std::max(0.0f, static_cast<float>(hh) - top_h - bottom_h);
        const Rect2 center_rect{{inner_x, inner_y}, {inner_w, inner_h}};
        draw_pcb_demo(canvas, center_rect, zoom);

        // —— 命令面板占位弹层（open 时覆盖）——
        draw_palette_overlay(canvas, palette,
                             Vector2{static_cast<float>(ww), static_cast<float>(hh)});

        canvas.flush();
        gl.end_frame();
        window.swap_buffers();

        if (max_frames > 0 && ++frame >= max_frames) break;
    }

    // 卸下 Inspector 内容，避免 panel 析构级联 delete 栈对象。
    inspector_panel.set_content(nullptr);

    gl.shutdown();
    std::printf("EDA editor shell exited cleanly after %d frame(s); zoom=%.2f\n",
                frame, zoom);
    return 0;
}
