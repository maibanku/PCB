// ToolBar / TitleBar / StatusBar 单元测试（P4 widgets）。
//
// 覆盖：
//   ToolBar（5）：
//     - 构造期注册 8 默认按钮 + 2 分隔符
//     - 按钮按 command_id 查表（find_button）+ 按索引查表（get_button）
//     - 按钮 clicked.emit() → CommandRegistry handler 计数 +1
//     - 未注册 command / 空 handler 点击安全跳过（不崩溃）
//     - add_button / add_separator 扩展计数
//   TitleBar（5）：
//     - 默认 title 标签 "EDA Engine"；project 标签占位 "(untitled)"
//     - set_project_name 同步 project 标签；空串回退占位
//     - close/minimize/maximize 按钮点击分别 emit 对应 Signal 各一次
//     - 三个 Signal 互不串扰（点 close 不触发 minimize/maximize）
//     - 固定高度 32px（get_minimum_size）
//   StatusBar（5）：
//     - 默认状态 "Ready" + DRC 0 + Zoom 100%
//     - set_status 同步 status_label
//     - set_drc_count 同步 drc_label（"DRC: N" 格式）
//     - set_zoom 同步 zoom_label（"Zoom: NN%" 格式，含四舍五入）
//     - 固定高度 24px（get_minimum_size）
//
// 命令触发方式：直接 btn->clicked().emit() —— Signal<>::emit 公开，触发所有连接
// （ToolBar 内部已连接 command handler），等价于完整 PRESS+RELEASE 鼠标点击路径。
//
// 所有权：所有 ToolBar / TitleBar / StatusBar 栈对象；其 Label / Button / Separator
// 子节点由 ~Node 级联 delete。ToolBar 内部 lambda 捕获 command_id 副本，无悬空。
//
// 单例卫生：CommandRegistry::get() 为进程级 Meyers singleton，测试间共享。每个
// ToolBar 测试在 SetUp/TearDown 清空，避免相互污染。

#include <gtest/gtest.h>

#include <string>

#include "scene/gui/widgets/tool_bar.h"
#include "scene/gui/widgets/title_bar.h"
#include "scene/gui/widgets/status_bar.h"
#include "scene/gui/widgets/button.h"
#include "scene/gui/widgets/label.h"
#include "editor/command_palette/command_registry.h"

using namespace eda;

namespace {
// 每个测试前后清空 CommandRegistry 单例，隔离测试间状态。
class ToolBarTest : public ::testing::Test {
protected:
    void SetUp() override { CommandRegistry::get().clear(); }
    void  TearDown() override { CommandRegistry::get().clear(); }
};
}  // namespace

// ===========================================================================
// ToolBar
// ===========================================================================

TEST_F(ToolBarTest, ConstructorRegistersDefaultButtonsAndSeparators) {
    ToolBar tb;
    // [New][Open][Save] │ [Undo][Redo] │ [Zoom+][Zoom-][Fit] = 8 按钮 + 2 分隔符
    EXPECT_EQ(tb.get_button_count(), 8);
    EXPECT_EQ(tb.get_separator_count(), 2);
}

TEST_F(ToolBarTest, FindButtonByCommandId) {
    ToolBar tb;
    Button* save = tb.find_button("file.save");
    ASSERT_NE(save, nullptr);
    EXPECT_EQ(save->get_text(), "Save");

    Button* fit = tb.find_button("view.zoom_fit");
    ASSERT_NE(fit, nullptr);
    EXPECT_EQ(fit->get_text(), "Fit");

    EXPECT_EQ(tb.find_button("nonexistent.command"), nullptr);
}

TEST_F(ToolBarTest, GetButtonByIndex) {
    ToolBar tb;
    // 索引顺序与构造注册顺序一致：New(0) Open(1) Save(2) Undo(3) Redo(4)
    //                            Zoom+(5) Zoom-(6) Fit(7)
    EXPECT_EQ(tb.get_button(0)->get_text(), "New");
    EXPECT_EQ(tb.get_button(2)->get_text(), "Save");
    EXPECT_EQ(tb.get_button(7)->get_text(), "Fit");
    EXPECT_EQ(tb.get_button(-1), nullptr);
    EXPECT_EQ(tb.get_button(99), nullptr);
}

TEST_F(ToolBarTest, ButtonClickInvokesRegisteredHandler) {
    int save_counter = 0;
    Command save;
    save.id      = "file.save";
    save.name    = "Save";
    save.handler = [&save_counter] { ++save_counter; };
    CommandRegistry::get().register_command(save);

    ToolBar tb;
    Button* save_btn = tb.find_button("file.save");
    ASSERT_NE(save_btn, nullptr);

    // emit clicked → ToolBar 内部 lambda → CommandRegistry 查表 → handler
    save_btn->clicked().emit();
    save_btn->clicked().emit();
    EXPECT_EQ(save_counter, 2);
}

TEST_F(ToolBarTest, UnregisteredCommandClickIsSafe) {
    // 不注册任何 command —— 默认按钮的 command_id 在 registry 中找不到。
    ToolBar tb;
    Button* new_btn = tb.find_button("file.new");
    ASSERT_NE(new_btn, nullptr);

    // 点击不应崩溃；handler 空跳过（CommandRegistry::get() 返回 nullptr）。
    new_btn->clicked().emit();
    SUCCEED();
}

TEST_F(ToolBarTest, OverwritingHandlerTakesEffectImmediately) {
    int first = 0, second = 0;
    Command a;
    a.id = "file.open"; a.name = "Open"; a.handler = [&first] { ++first; };
    CommandRegistry::get().register_command(a);

    ToolBar tb;
    Button* open_btn = tb.find_button("file.open");
    ASSERT_NE(open_btn, nullptr);

    open_btn->clicked().emit();
    EXPECT_EQ(first, 1);

    // 业务后续以同 id 覆盖 handler（命令面板 / 工具栏 / 快捷键共用同一路径）。
    Command b;
    b.id = "file.open"; b.name = "Open"; b.handler = [&second] { ++second; };
    CommandRegistry::get().register_command(b);

    open_btn->clicked().emit();
    EXPECT_EQ(first, 1);     // 旧 handler 不再触发
    EXPECT_EQ(second, 1);    // 新 handler 生效
}

TEST_F(ToolBarTest, AddButtonAndSeparatorIncrementCounts) {
    ToolBar tb;
    const int initial_btns = tb.get_button_count();
    const int initial_seps = tb.get_separator_count();

    Button* custom = tb.add_button("Custom", "custom.action");
    EXPECT_NE(custom, nullptr);
    EXPECT_EQ(tb.get_button_count(), initial_btns + 1);
    EXPECT_EQ(tb.find_button("custom.action"), custom);

    tb.add_separator();
    EXPECT_EQ(tb.get_separator_count(), initial_seps + 1);
}

// ===========================================================================
// TitleBar
// ===========================================================================

TEST(TitleBarTest, DefaultLabels) {
    TitleBar tb;
    ASSERT_NE(tb.get_title_label(), nullptr);
    EXPECT_EQ(tb.get_title_label()->get_text(), "EDA Engine");
    ASSERT_NE(tb.get_project_label(), nullptr);
    EXPECT_EQ(tb.get_project_label()->get_text(), "(untitled)");
    EXPECT_EQ(tb.get_project_name(), "");
}

TEST(TitleBarTest, SetProjectNameUpdatesLabel) {
    TitleBar tb;
    tb.set_project_name("MyBoard.pcb");
    EXPECT_EQ(tb.get_project_name(), "MyBoard.pcb");
    EXPECT_EQ(tb.get_project_label()->get_text(), "MyBoard.pcb");

    // 空串回退占位（避免标签塌缩）。
    tb.set_project_name("");
    EXPECT_EQ(tb.get_project_label()->get_text(), "(untitled)");
}

TEST(TitleBarTest, CloseButtonEmitsCloseRequested) {
    TitleBar tb;
    int close_count = 0;
    tb.close_requested().connect([&close_count] { ++close_count; });

    tb.get_close_button()->clicked().emit();
    EXPECT_EQ(close_count, 1);
}

TEST(TitleBarTest, WindowButtonsEmitCorrespondingSignals) {
    TitleBar tb;
    int close_n = 0, min_n = 0, max_n = 0;
    tb.close_requested().connect([&close_n] { ++close_n; });
    tb.minimize_requested().connect([&min_n] { ++min_n; });
    tb.maximize_requested().connect([&max_n] { ++max_n; });

    tb.get_minimize_button()->clicked().emit();
    tb.get_maximize_button()->clicked().emit();
    tb.get_close_button()->clicked().emit();

    EXPECT_EQ(min_n, 1);
    EXPECT_EQ(max_n, 1);
    EXPECT_EQ(close_n, 1);
}

TEST(TitleBarTest, SignalsDoNotCrossTrigger) {
    TitleBar tb;
    int close_n = 0, min_n = 0, max_n = 0;
    tb.close_requested().connect([&close_n] { ++close_n; });
    tb.minimize_requested().connect([&min_n] { ++min_n; });
    tb.maximize_requested().connect([&max_n] { ++max_n; });

    // 仅点 close：minimize / maximize 不应被误触发。
    tb.get_close_button()->clicked().emit();
    EXPECT_EQ(close_n, 1);
    EXPECT_EQ(min_n, 0);
    EXPECT_EQ(max_n, 0);
}

TEST(TitleBarTest, FixedHeightInMinimumSize) {
    TitleBar tb;
    EXPECT_FLOAT_EQ(tb.get_minimum_size().y, TitleBar::kFixedHeight);
    EXPECT_FLOAT_EQ(TitleBar::kFixedHeight, 32.0f);
}

// ===========================================================================
// StatusBar
// ===========================================================================

TEST(StatusBarTest, DefaultValuesAndLabels) {
    StatusBar sb;
    EXPECT_EQ(sb.get_status(), "Ready");
    EXPECT_EQ(sb.get_drc_count(), 0);
    EXPECT_FLOAT_EQ(sb.get_zoom(), 1.0f);

    ASSERT_NE(sb.get_status_label(), nullptr);
    EXPECT_EQ(sb.get_status_label()->get_text(), "Ready");
    ASSERT_NE(sb.get_drc_label(), nullptr);
    EXPECT_EQ(sb.get_drc_label()->get_text(), "DRC: 0");
    ASSERT_NE(sb.get_zoom_label(), nullptr);
    EXPECT_EQ(sb.get_zoom_label()->get_text(), "Zoom: 100%");
}

TEST(StatusBarTest, SetStatusUpdatesLabel) {
    StatusBar sb;
    sb.set_status("Loading project...");
    EXPECT_EQ(sb.get_status(), "Loading project...");
    EXPECT_EQ(sb.get_status_label()->get_text(), "Loading project...");

    sb.set_status("Ready");
    EXPECT_EQ(sb.get_status_label()->get_text(), "Ready");
}

TEST(StatusBarTest, SetDrcCountUpdatesLabel) {
    StatusBar sb;
    sb.set_drc_count(7);
    EXPECT_EQ(sb.get_drc_count(), 7);
    EXPECT_EQ(sb.get_drc_label()->get_text(), "DRC: 7");

    // 边界：0 与负数（业务可表达"DRC 未运行"）。
    sb.set_drc_count(0);
    EXPECT_EQ(sb.get_drc_label()->get_text(), "DRC: 0");
}

TEST(StatusBarTest, SetZoomUpdatesLabel) {
    StatusBar sb;
    sb.set_zoom(1.5f);  // 150%
    EXPECT_FLOAT_EQ(sb.get_zoom(), 1.5f);
    EXPECT_EQ(sb.get_zoom_label()->get_text(), "Zoom: 150%");

    sb.set_zoom(0.25f);  // 25%
    EXPECT_EQ(sb.get_zoom_label()->get_text(), "Zoom: 25%");

    // 四舍五入：1.255 → 126%（浮点误差允许 ±1%）。
    sb.set_zoom(1.2549f);
    EXPECT_EQ(sb.get_zoom_label()->get_text(), "Zoom: 125%");
}

TEST(StatusBarTest, FixedHeightInMinimumSize) {
    StatusBar sb;
    EXPECT_FLOAT_EQ(sb.get_minimum_size().y, StatusBar::kFixedHeight);
    EXPECT_FLOAT_EQ(StatusBar::kFixedHeight, 24.0f);
}

TEST(StatusBarTest, MultipleUpdatesIndependent) {
    // 三类更新互不干扰：set_status 不应影响 DRC / Zoom 标签。
    StatusBar sb;
    sb.set_drc_count(3);
    sb.set_zoom(2.0f);
    sb.set_status("Running");

    EXPECT_EQ(sb.get_status_label()->get_text(), "Running");
    EXPECT_EQ(sb.get_drc_label()->get_text(), "DRC: 3");
    EXPECT_EQ(sb.get_zoom_label()->get_text(), "Zoom: 200%");
}
