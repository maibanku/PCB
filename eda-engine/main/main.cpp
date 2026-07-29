// main.cpp —— EDA Editor Shell（完整编辑器外壳：P1 + P4 组装）。
//
// 在 P1 窗口/光栅/Canvas2D 之上组装 P4 完整编辑器外壳。Control 树由 root 持有：
//   root（Control，视口尺寸）
//     └ top_layout（VBox，顶部固定）
//         ├ TitleBar（32px，应用标题 + 项目名 + 最小化/最大化/关闭）
//         ├ MenuBar（7 个标准菜单：File/Edit/View/Place/Design/Tools/Help）
//         └ ToolBar（[New][Open][Save] │ [Undo][Redo] │ [Zoom+][Zoom-][Fit]）
//     └ docks（DockManager，填充中段）
//         ├ LEFT   = Project Tree 面板
//         ├ RIGHT  = Inspector 面板（反射属性编辑器，空 target 占位）
//         └ CENTER = PCB 画布（保留 P1 Demo：网格 / 焊盘 / 走线 / 过孔）
//     └ status_bar（StatusBar，24px 底栏：状态文本 + DRC 计数 + Zoom 百分比）
//
// 渲染管线：
//   1) canvas.clear()
//   2) queue_subtree_redraw(root) —— 标脏全部 CanvasItem（每帧重绘 GUI chrome）
//   3) render_subtree(root, canvas) —— DFS 前序遍历 Control 树：
//        Control._draw → ProductionControlCanvas::{draw_rect, draw_text}
//        → Canvas2D::{draw_rect_filled, draw_line} → 顶点缓冲
//   4) draw_pcb_demo(canvas, center_rect, zoom) —— PCB Demo 直接走 Canvas2D
//   5) draw_palette_overlay(...) —— 命令面板占位弹层
//   6) canvas.flush() → RenderingServer → RasterizerGL → GPU
//
// 用法：
//   eda_main          — 持续运行到关窗
//   eda_main <N>      — 跑 N 帧后退出（烟雾测试用）

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
#include "platform/glfw/glfw_window.h"
#include "scene/2d/canvas_2d.h"
#include "scene/gui/canvas_item.h"             // CanvasItem（subtree queue_redraw）
#include "scene/gui/container.h"               // VBox / Container / SizeFlag
#include "scene/gui/control.h"                 // Control / Side
#include "scene/gui/control_render.h"          // render_subtree
#include "scene/gui/theme.h"
#include "scene/gui/widgets/menu_bar.h"
#include "scene/gui/widgets/status_bar.h"
#include "scene/gui/widgets/title_bar.h"
#include "scene/gui/widgets/tool_bar.h"
#include "scene/main/node.h"
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

// 标脏子树全部 CanvasItem：render_subtree 只对 dirty 节点调 _draw；每帧重标可保证
// GUI chrome（TitleBar/MenuBar/ToolBar/StatusBar/Dock 面板）每帧都重绘到 Canvas2D
// 顶点缓冲（canvas.clear() 每帧清空，需重新提交）。
void queue_subtree_redraw(Node* n) {
    if (n == nullptr) return;
    n->for_each_in_subtree([](Node* node) {
        if (auto* ci = dynamic_cast<CanvasItem*>(node)) {
            ci->queue_redraw();
        }
    });
}

// 锚点全置 0 + set_position + set_size：把控件锁定到绝对像素矩形（与
// Container::fit_child_in_rect 同语义），使其几何脱离父尺寸拉伸、由外部直接定位。
// 用于 root 的直接子节点（top_layout/docks/status_bar）：root 是普通 Control，
// 不做容器布局，需手动设几何。
void lock_rect(Control& c, Vector2 pos, Vector2 size) {
    c.set_anchor(Side::LEFT, 0.0f);
    c.set_anchor(Side::TOP, 0.0f);
    c.set_anchor(Side::RIGHT, 0.0f);
    c.set_anchor(Side::BOTTOM, 0.0f);
    c.set_position(pos);
    c.set_size(size);
}

// 绘制 PCB Demo（P1 保留）：网格 + 焊盘 + 走线 + 过孔，绝对坐标位于 center_rect 内。
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
    bool  exit_requested = false;

    auto clamp_zoom = [&] {
        if (zoom < 0.1f) zoom = 0.1f;
        if (zoom > 10.0f) zoom = 10.0f;
    };

    // 2) 命令注册表 + 命令面板。
    //    register_builtins 注册 11 个占位命令（handler 空）；此处覆盖 zoom 三连的
    //    handler，使 ToolBar 按钮 / Menu 项 / InputMap 快捷键三路均路由到同一回调。
    CommandRegistry& registry = CommandRegistry::get();
    registry.register_builtins();
    registry.register_command(
        {builtin::VIEW_ZOOM_IN, "Zoom In", "Increase zoom level",
         "View", "Ctrl++", [&] { zoom *= 1.1f; clamp_zoom(); }});
    registry.register_command(
        {builtin::VIEW_ZOOM_OUT, "Zoom Out", "Decrease zoom level",
         "View", "Ctrl+-", [&] { zoom *= 0.9f; clamp_zoom(); }});
    registry.register_command(
        {builtin::VIEW_ZOOM_FIT, "Zoom to Fit", "Reset zoom to 100%",
         "View", "", [&] { zoom = 1.0f; }});

    CommandPalette palette;

    // 3) InputMap（默认 KiCad 预设）。
    InputMap input_map(InputMap::Preset::KICAD);

    // 4) 构建 Control 树。
    //    栈声明顺序 = 析构逆序：父先声明（后析构）。子先析构时 ~Node 会把自己从
    //    父的 children_ 抹除，故父析构时不会级联 delete 到栈对象——避免双释放。
    //    顺序链：root → top_layout → title_bar → menu_bar → tool_bar → docks
    //            → status_bar → project_panel → inspector_panel → inspector
    Control      root;
    VBox         top_layout;
    TitleBar     title_bar;
    MenuBar      menu_bar;
    ToolBar      tool_bar;
    DockManager  docks;
    StatusBar    status_bar;
    DockPanel    project_panel;
    DockPanel    inspector_panel;
    Inspector    inspector;

    // —— 顶层 chrome 配置 ——
    title_bar.set_project_name("demo.pcb");
    title_bar.close_requested().connect([&] { exit_requested = true; });
    menu_bar.set_registry(&registry);  // Menu 默认已绑进程单例；显式注入便于替换。

    // —— Dock 面板配置 ——
    project_panel.set_title("Project Tree");
    inspector_panel.set_title("Inspector");
    inspector_panel.set_content(&inspector);  // Inspector 作为 RIGHT 面板内容

    // —— 挂树：root → {top_layout, docks, status_bar} ——
    //    root 是普通 Control（非 Container），用 Node::add_child 挂接；几何由
    //    apply_layout 每帧手动锁定（lock_rect）。
    root.add_child(&top_layout);
    top_layout.add_child(&title_bar);
    top_layout.add_child(&menu_bar);
    top_layout.add_child(&tool_bar);
    root.add_child(&docks);
    root.add_child(&status_bar);

    // —— Dock 注册 + 停靠 ——
    docks.register_panel("project",   &project_panel);
    docks.register_panel("inspector", &inspector_panel);
    docks.dock(DockArea::LEFT,  &project_panel);
    docks.dock(DockArea::RIGHT, &inspector_panel);

    // 让面板在各自区域内铺满主轴（LEFT/RIGHT=VBox → 垂直 EXPAND）。
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
    expand_in_area(&project_panel,   /*vertical_area=*/true);
    expand_in_area(&inspector_panel, /*vertical_area=*/true);

    // —— 布局函数：每帧根据窗口尺寸锁定三段几何 + 触发容器 sort_children ——
    auto apply_layout = [&](int ww, int hh) {
        root.set_viewport_size({static_cast<float>(ww), static_cast<float>(hh)});

        // 顶部 VBox：TitleBar(32) + MenuBar(~22) + ToolBar(~26) = top_h
        const float title_h   = TitleBar::kFixedHeight;
        const float menu_h    = menu_bar.get_combined_minimum_size().y;
        const float toolbar_h = tool_bar.get_combined_minimum_size().y;
        const float status_h  = StatusBar::kFixedHeight;
        const float top_h     = title_h + menu_h + toolbar_h;
        const float dock_h    = std::max(0.0f, static_cast<float>(hh) - top_h - status_h);

        // top_layout：锚顶部，宽 = ww，高 = top_h。sort_children 让 VBox 把三个
        // 子控件按最小尺寸垂直堆叠（title_h / menu_h / toolbar_h 各取最小高）。
        lock_rect(top_layout, {0.0f, 0.0f},
                  {static_cast<float>(ww), top_h});
        top_layout.sort_children();

        // docks：顶部之下、状态栏之上，铺满中段。DockManager.sort_children 触发
        // border layout：计算五区域 Rect + fit_child_in_rect 到 area Container，
        // 级联 area.sort_children() 排布内部面板。
        lock_rect(docks, {0.0f, top_h},
                  {static_cast<float>(ww), dock_h});
        docks.set_area_size(DockArea::LEFT,   220.0f);
        docks.set_area_size(DockArea::TOP,     0.0f);  // 顶栏由 top_layout 承载
        docks.set_area_size(DockArea::RIGHT,  260.0f);
        docks.set_area_size(DockArea::BOTTOM,  0.0f);  // 底栏由 status_bar 承载
        docks.sort_children();

        // status_bar：锚底部，宽 = ww，高 = 24。
        lock_rect(status_bar,
                  {0.0f, static_cast<float>(hh) - status_h},
                  {static_cast<float>(ww), status_h});

        // 状态栏反映当前 zoom（DRC 计数暂保持默认 0）。
        status_bar.set_zoom(zoom);
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
        clamp_zoom();
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
        // 经 InputMap 派发 → 命令 id → registry handler。
        const auto cmd = input_map.lookup(e);
        if (cmd) {
            if (*cmd == "command_palette.open") {
                if (palette.is_open()) palette.close(); else palette.open();
                return;
            }
            if (const Command* c = registry.get(*cmd)) {
                if (c->handler) c->handler();
            }
        }
    });
    window.set_on_mouse_button([&](const InputEvent& e) { input.push(e); });
    window.set_on_mouse_move([&](const InputEvent& e) { input.push(e); });

    // 6) 主循环：poll → clear → layout → render_subtree(GUI) → draw PCB → swap。
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

        // —— GUI Control 树（TitleBar / MenuBar / ToolBar / Dock 面板 / StatusBar）
        //    经 render_subtree 栅格化到 Canvas2D 顶点缓冲。每帧标脏保证 chrome
        //    在 canvas.clear() 后重新提交。渲染入口：control_render.h。 ——
        queue_subtree_redraw(&root);
        render_subtree(&root, canvas);

        // —— CENTER：PCB Demo（P1 保留，直接走 Canvas2D）——
        //    center_rect 由 DockManager border layout 几何推导：
        //    内列横向 = [left_w, ww - right_w]，纵向 = [top_h, hh - status_h]。
        const float left_w   = docks.get_area_size(DockArea::LEFT);
        const float right_w  = docks.get_area_size(DockArea::RIGHT);
        const float title_h   = TitleBar::kFixedHeight;
        const float menu_h    = menu_bar.get_combined_minimum_size().y;
        const float toolbar_h = tool_bar.get_combined_minimum_size().y;
        const float status_h  = StatusBar::kFixedHeight;
        const float top_h     = title_h + menu_h + toolbar_h;
        const float inner_x   = left_w;
        const float inner_w   = std::max(0.0f, static_cast<float>(ww) - left_w - right_w);
        const float inner_y   = top_h;
        const float inner_h   = std::max(0.0f, static_cast<float>(hh) - top_h - status_h);
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
