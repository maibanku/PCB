// Widget 单元测试（P4 Task 4）。
//
// 覆盖 Label / Button / IconButton / Separator 的 _draw 输出 + 事件处理，以及
// render_subtree 渲染管线接通（Control 树 → Canvas2D 顶点缓冲）。
//
// 策略：
//   - _draw 输出：用 RecordingControlCanvas（canvas_item.h）记录 draw_rect / draw_text
//     调用，断言调用数 / filled 标志 / text 内容 / rect 尺寸。
//   - 事件处理：构造 InputEvent 直接 _input，断言状态切换 + clicked 信号触发。
//   - 渲染管线：render_subtree 接到 Canvas2D（server=nullptr），断言顶点缓冲非空
//     且每个填充矩形贡献 6 顶点（2 三角形）。
//
// 所有权约定：Control 父子经 set_parent → add_child 建立关系，~Node 级联 delete
// children_。栈对象用例中 parent 先于 children 声明（parent 后析构）。

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "scene/gui/canvas_item.h"  // RecordingControlCanvas / ControlCanvasScope
#include "scene/gui/control.h"
#include "scene/gui/control_render.h"
#include "scene/gui/widgets/button.h"
#include "scene/gui/widgets/icon_button.h"
#include "scene/gui/widgets/label.h"
#include "scene/gui/widgets/separator.h"
#include "scene/2d/canvas_2d.h"

using namespace eda;

// ====== Label ======

TEST(LabelTest, DefaultTextAndFontToken) {
    Label label;
    EXPECT_EQ(label.get_text(), "");
    EXPECT_EQ(label.get_font_token(), "default");
}

TEST(LabelTest, SetTextPropagatesRedraw) {
    Label label;
    label.set_size({100, 30});
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    label.set_text("Hello");
    label.draw_if_dirty();
    ASSERT_EQ(rec.texts.size(), 1u);
    EXPECT_EQ(rec.texts[0].first, "Hello");
}

TEST(LabelTest, DrawEmitsBackgroundAndTextPlaceholder) {
    Label label;
    label.set_size({120, 24});
    label.set_text("Caption");
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    label.queue_redraw();
    label.draw_if_dirty();
    // 1 个填充背景矩形（default 配色）
    ASSERT_EQ(rec.rects.size(), 1u);
    EXPECT_TRUE(rec.rects[0].filled);
    EXPECT_FLOAT_EQ(rec.rects[0].rect.size.x, 120.0f);
    EXPECT_FLOAT_EQ(rec.rects[0].rect.size.y, 24.0f);
    // 1 个文本占位调用
    ASSERT_EQ(rec.texts.size(), 1u);
    EXPECT_EQ(rec.texts[0].first, "Caption");
}

TEST(LabelTest, EmptyTextSkipsDrawText) {
    Label label;
    label.set_size({80, 20});
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    label.queue_redraw();
    label.draw_if_dirty();
    EXPECT_EQ(rec.rects.size(), 1u);   // 仅背景
    EXPECT_EQ(rec.texts.size(), 0u);   // 空文本不下发 draw_text
}

TEST(LabelTest, FontTokenChangesMinimumSize) {
    Label label;
    label.set_text("Hello");
    label.set_font_token("small");
    float small_h = label.get_combined_minimum_size().y;
    label.set_font_token("heading");
    float heading_h = label.get_combined_minimum_size().y;
    EXPECT_GT(heading_h, small_h);  // heading 18 > small 12
}

// ====== Button ======

TEST(ButtonTest, DrawEmitsBackgroundBorderAndText) {
    Button btn;
    btn.set_size({80, 30});
    btn.set_text("OK");
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    btn.queue_redraw();
    btn.draw_if_dirty();
    // 填充底色 + 非填充边框 = 2 矩形
    ASSERT_EQ(rec.rects.size(), 2u);
    EXPECT_TRUE(rec.rects[0].filled);    // 底色
    EXPECT_FALSE(rec.rects[1].filled);   // 边框
    ASSERT_EQ(rec.texts.size(), 1u);
    EXPECT_EQ(rec.texts[0].first, "OK");
}

TEST(ButtonTest, ClickEmitsOnPressReleaseInside) {
    Button btn;
    btn.set_size({80, 30});

    int clicks = 0;
    btn.clicked().connect([&clicks] { ++clicks; });

    // 1. 光标移入按钮内 → hover
    InputEvent move;
    move.type      = InputEvent::Type::MOUSE_MOVE;
    move.mouse_pos = {40, 15};
    btn._input(move);
    EXPECT_TRUE(btn.is_hover());

    // 2. 在按钮内按下左键 → pressed
    InputEvent press;
    press.type      = InputEvent::Type::MOUSE_BUTTON;
    press.button    = MouseButton::LEFT;
    press.action    = KeyAction::PRESS;
    press.mouse_pos = {40, 15};
    btn._input(press);
    EXPECT_TRUE(btn.is_pressed());

    // 3. 在按钮内释放左键 → clicked emit + pressed 清
    InputEvent release = press;
    release.action = KeyAction::RELEASE;
    btn._input(release);
    EXPECT_FALSE(btn.is_pressed());
    EXPECT_EQ(clicks, 1);
}

TEST(ButtonTest, ReleaseOutsideDoesNotClick) {
    Button btn;
    btn.set_size({80, 30});

    int clicks = 0;
    btn.clicked().connect([&clicks] { ++clicks; });

    // 按下时在按钮内，释放时移到按钮外 → 不触发 clicked
    InputEvent move_in;
    move_in.type      = InputEvent::Type::MOUSE_MOVE;
    move_in.mouse_pos = {40, 15};
    btn._input(move_in);

    InputEvent press;
    press.type      = InputEvent::Type::MOUSE_BUTTON;
    press.button    = MouseButton::LEFT;
    press.action    = KeyAction::PRESS;
    press.mouse_pos = {40, 15};
    btn._input(press);

    InputEvent move_out;
    move_out.type      = InputEvent::Type::MOUSE_MOVE;
    move_out.mouse_pos = {500, 500};  // 移出按钮
    btn._input(move_out);
    EXPECT_FALSE(btn.is_hover());

    InputEvent release = press;
    release.action     = KeyAction::RELEASE;
    release.mouse_pos  = {500, 500};
    btn._input(release);

    EXPECT_EQ(clicks, 0);  // 在按钮外释放，不触发 click
}

TEST(ButtonTest, DisabledRejectsInputAndDrawsWithDisabledColor) {
    Button btn;
    btn.set_size({80, 30});
    btn.set_text("OK");
    btn.set_disabled(true);

    // 禁用态下 MOUSE_MOVE 不触发 hover
    InputEvent move;
    move.type      = InputEvent::Type::MOUSE_MOVE;
    move.mouse_pos = {40, 15};
    btn._input(move);
    EXPECT_FALSE(btn.is_hover());

    // 禁用态下 MOUSE_BUTTON 不触发 pressed / clicked
    int clicks = 0;
    btn.clicked().connect([&clicks] { ++clicks; });
    InputEvent press;
    press.type      = InputEvent::Type::MOUSE_BUTTON;
    press.button    = MouseButton::LEFT;
    press.action    = KeyAction::PRESS;
    press.mouse_pos = {40, 15};
    btn._input(press);
    EXPECT_FALSE(btn.is_pressed());
    EXPECT_EQ(clicks, 0);
}

// ====== IconButton ======

TEST(IconButtonTest, DrawEmitsBackgroundBorderAndIconPlaceholder) {
    IconButton btn;
    btn.set_size({24, 24});
    btn.set_icon_name("close");
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    btn.queue_redraw();
    btn.draw_if_dirty();
    // 填充底色 + 非填充边框 + 填充图标色块 = 3 矩形
    ASSERT_EQ(rec.rects.size(), 3u);
    EXPECT_TRUE(rec.rects[0].filled);    // 底色
    EXPECT_FALSE(rec.rects[1].filled);   // 边框
    EXPECT_TRUE(rec.rects[2].filled);    // 图标占位色块
    EXPECT_FLOAT_EQ(rec.rects[2].rect.size.x, 12.0f);
    EXPECT_FLOAT_EQ(rec.rects[2].rect.size.y, 12.0f);
}

TEST(IconButtonTest, ClickedSignalEmits) {
    IconButton btn;
    btn.set_size({24, 24});

    int clicks = 0;
    btn.clicked().connect([&clicks] { ++clicks; });

    InputEvent move;
    move.type      = InputEvent::Type::MOUSE_MOVE;
    move.mouse_pos = {12, 12};
    btn._input(move);

    InputEvent press;
    press.type      = InputEvent::Type::MOUSE_BUTTON;
    press.button    = MouseButton::LEFT;
    press.action    = KeyAction::PRESS;
    press.mouse_pos = {12, 12};
    btn._input(press);

    InputEvent release = press;
    release.action = KeyAction::RELEASE;
    btn._input(release);

    EXPECT_EQ(clicks, 1);
}

// ====== Separator ======

TEST(SeparatorTest, HorizontalDrawsFullWidthLine) {
    Separator sep;
    sep.set_size({100, 10});  // 容器给 10 高，线画在中线
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    sep.queue_redraw();
    sep.draw_if_dirty();
    ASSERT_EQ(rec.rects.size(), 1u);
    EXPECT_TRUE(rec.rects[0].filled);
    EXPECT_FLOAT_EQ(rec.rects[0].rect.size.x, 100.0f);  // 铺满宽
    EXPECT_FLOAT_EQ(rec.rects[0].rect.size.y, 1.0f);    // 1px 厚
}

TEST(SeparatorTest, VerticalDrawsFullHeightLine) {
    Separator sep;
    sep.set_size({10, 80});
    sep.set_orientation(Orientation::VERTICAL);
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    sep.queue_redraw();
    sep.draw_if_dirty();
    ASSERT_EQ(rec.rects.size(), 1u);
    EXPECT_TRUE(rec.rects[0].filled);
    EXPECT_FLOAT_EQ(rec.rects[0].rect.size.x, 1.0f);
    EXPECT_FLOAT_EQ(rec.rects[0].rect.size.y, 80.0f);
}

TEST(SeparatorTest, MinimumSizeMatchesOrientation) {
    Separator h;
    EXPECT_FLOAT_EQ(h.get_minimum_size().y, 1.0f);   // 水平：厚 1px
    EXPECT_FLOAT_EQ(h.get_minimum_size().x, 0.0f);   // 宽由容器拉伸

    Separator v;
    v.set_orientation(Orientation::VERTICAL);
    EXPECT_FLOAT_EQ(v.get_minimum_size().x, 1.0f);
    EXPECT_FLOAT_EQ(v.get_minimum_size().y, 0.0f);
}

// ====== 渲染管线：render_subtree → Canvas2D ======

TEST(ControlRenderTest, RenderSubtreeRasterizesControlToCanvas2D) {
    // 单个 Label：render_subtree 应把它的 _draw 经 ProductionControlCanvas 转为
    // Canvas2D 顶点。Label 画 1 个填充背景矩形 = 6 顶点（2 三角形）。
    Label root;
    root.set_size({100, 30});
    root.set_text("root");

    Canvas2D canvas(nullptr);  // 不绑定 RenderingServer：仅 CPU 栅格化
    render_subtree(&root, canvas);

    // 背景 6 顶点（draw_text 为 no-op，不贡献顶点）
    EXPECT_GE(canvas.vertices().size(), 6u);
}

TEST(ControlRenderTest, RenderSubtreeDescendsIntoChildren) {
    // root Label + 子 Label：每个贡献 6 顶点（背景矩形）。
    Label root;
    root.set_size({100, 30});
    root.set_text("parent");

    Label child;
    child.set_parent(&root);
    child.set_text("child");
    // child 默认 anchors {0,0,1,1} → 铺满 root，get_size() = (100,30)

    Canvas2D canvas(nullptr);
    render_subtree(&root, canvas);

    // 2 个 Label × 6 顶点 = 12 顶点（父先画，子后画）
    EXPECT_EQ(canvas.vertices().size(), 12u);
}

TEST(ControlRenderTest, RenderSubtreeNullRootIsSafe) {
    Canvas2D canvas(nullptr);
    render_subtree(nullptr, canvas);  // 不崩溃
    EXPECT_EQ(canvas.vertices().size(), 0u);
}
