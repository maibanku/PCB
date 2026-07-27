// DockPanel / DockManager 单元测试（P4 Task 4）。
//
// 覆盖（23 个用例）：
//   DockPanel（9）：
//     - 标题 / 图标存取
//     - set_content 挂入子节点；nullptr 剥离旧内容；替换内容剥离旧的
//     - 显示 / 隐藏状态切换
//     - 关闭 / 浮动请求标志（set / clear）
//     - 最小尺寸包含标题栏高度
//   DockManager 注册表 + 停靠状态机（8）：
//     - register_panel + get_panel / get_panel_id 查表
//     - 未注册 dock 自动分配 id
//     - dock 到 LEFT / TOP / RIGHT / BOTTOM / CENTER 五区域 + 状态查询
//     - dock 后 panel reparent 到 area Container（manager 孙节点）
//     - undock 清空状态 + 剥离父节点
//     - float_panel 标记 FLOATING + 剥离父节点
//     - 从 FLOATING 重新 dock 到区域
//     - get_panels_in(area) / get_floating_panels()
//   DockManager 区域尺寸与显示（2）：
//     - set/get_area_size
//     - set_panel_visible 隐藏面板（manager 同步 panel visible）
//   border layout（1）：
//     - sort_children 后五区域 + 内部面板按 border layout 落位
//   layout_changed 信号（1）：
//     - dock 触发信号；幂等 dock 不触发
//   布局持久化（2）：
//     - serialize_layout → deserialize_layout 往返一致
//     - save_layout(path) / load_layout(path) 文件往返
//
// 所有权约定（栈对象）：
//   - DockManager 先声明（后析构）：manager 拥有 5 个 heap-allocated area Container，
//     析构级联 delete。DockPanel 后声明（先析构）：先从所在 area 的 children_ 自剥离，
//     manager 析构时 area 的 children 已不含这些 panel，无双重释放。
//   - DockPanel.set_content 挂入的 Control 同样后声明（先析构自剥离）。

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "editor/dock/dock_manager.h"
#include "editor/dock/dock_panel.h"
#include "scene/gui/control.h"

using namespace eda;

// ====== DockPanel ======

TEST(DockPanelTest, TitleGetSet) {
    DockPanel panel;
    EXPECT_EQ(panel.get_title(), "");
    panel.set_title("layers");
    EXPECT_EQ(panel.get_title(), "layers");
    panel.set_title("properties");
    EXPECT_EQ(panel.get_title(), "properties");
}

TEST(DockPanelTest, IconGetSet) {
    DockPanel panel;
    EXPECT_EQ(panel.get_icon(), "");
    panel.set_icon("layers");
    EXPECT_EQ(panel.get_icon(), "layers");
}

TEST(DockPanelTest, SetContentReparentsChild) {
    DockPanel panel;     // 先声明：后析构
    Control   c;         // 后声明：先析构自剥离
    panel.set_content(&c);
    EXPECT_EQ(c.get_parent(), &panel);
    EXPECT_EQ(panel.get_content(), &c);
}

TEST(DockPanelTest, SetContentNullDetachesOld) {
    DockPanel panel;
    Control   c;
    panel.set_content(&c);
    ASSERT_EQ(c.get_parent(), &panel);
    panel.set_content(nullptr);
    EXPECT_EQ(c.get_parent(), nullptr);
    EXPECT_EQ(panel.get_content(), nullptr);
}

TEST(DockPanelTest, SetContentReplacesOld) {
    DockPanel panel;
    Control   c1, c2;
    panel.set_content(&c1);
    ASSERT_EQ(c1.get_parent(), &panel);
    panel.set_content(&c2);
    EXPECT_EQ(c2.get_parent(), &panel);
    EXPECT_NE(c1.get_parent(), &panel);  // c1 已剥离
    EXPECT_EQ(panel.get_content(), &c2);
}

TEST(DockPanelTest, VisibleToggle) {
    DockPanel panel;
    EXPECT_TRUE(panel.is_visible());
    panel.set_visible(false);
    EXPECT_FALSE(panel.is_visible());
    panel.set_visible(true);
    EXPECT_TRUE(panel.is_visible());
}

TEST(DockPanelTest, CloseRequestFlag) {
    DockPanel panel;
    EXPECT_FALSE(panel.is_close_requested());
    panel.request_close();
    EXPECT_TRUE(panel.is_close_requested());
    panel.clear_close_request();
    EXPECT_FALSE(panel.is_close_requested());
}

TEST(DockPanelTest, FloatRequestFlag) {
    DockPanel panel;
    EXPECT_FALSE(panel.is_float_requested());
    panel.request_float();
    EXPECT_TRUE(panel.is_float_requested());
    panel.clear_float_request();
    EXPECT_FALSE(panel.is_float_requested());
}

TEST(DockPanelTest, MinimumSizeIncludesTitleBar) {
    DockPanel panel;
    panel.set_title_bar_height(28.0f);
    // 无内容：最小尺寸 = (0, title_bar_height)
    Vector2 m = panel.get_minimum_size();
    EXPECT_FLOAT_EQ(m.x, 0.0f);
    EXPECT_FLOAT_EQ(m.y, 28.0f);
}

// ====== DockManager 注册表 ======

TEST(DockManagerRegistryTest, RegisterPanelAndGetById) {
    DockManager manager;
    DockPanel   pa, pb;
    manager.register_panel("layers", &pa);
    manager.register_panel("props", &pb);
    EXPECT_EQ(manager.get_panel("layers"), &pa);
    EXPECT_EQ(manager.get_panel("props"), &pb);
    EXPECT_EQ(manager.get_panel_id(&pa), "layers");
    EXPECT_EQ(manager.get_panel_id(&pb), "props");
    EXPECT_EQ(manager.get_panel("nonexistent"), nullptr);
    EXPECT_EQ(manager.get_registered_count(), 2u);
}

TEST(DockManagerRegistryTest, DockWithoutRegisterAutoIds) {
    DockManager manager;
    DockPanel   pa, pb;
    manager.dock(DockArea::LEFT, &pa);
    manager.dock(DockArea::LEFT, &pb);
    // 自动分配 "panel_<n>" id
    const std::string id_a = manager.get_panel_id(&pa);
    const std::string id_b = manager.get_panel_id(&pb);
    EXPECT_FALSE(id_a.empty());
    EXPECT_FALSE(id_b.empty());
    EXPECT_NE(id_a, id_b);
    EXPECT_EQ(manager.get_panel(id_a), &pa);
    EXPECT_EQ(manager.get_panel(id_b), &pb);
}

// ====== DockManager 停靠状态机 ======

TEST(DockManagerDockTest, DockToEachArea) {
    DockManager manager;
    DockPanel   p_left, p_top, p_right, p_bottom, p_center;
    manager.register_panel("l", &p_left);
    manager.register_panel("t", &p_top);
    manager.register_panel("r", &p_right);
    manager.register_panel("b", &p_bottom);
    manager.register_panel("c", &p_center);

    manager.dock(DockArea::LEFT,   &p_left);
    manager.dock(DockArea::TOP,    &p_top);
    manager.dock(DockArea::RIGHT,  &p_right);
    manager.dock(DockArea::BOTTOM, &p_bottom);
    manager.dock(DockArea::CENTER, &p_center);

    EXPECT_EQ(manager.get_area_of(&p_left),   DockArea::LEFT);
    EXPECT_EQ(manager.get_area_of(&p_top),    DockArea::TOP);
    EXPECT_EQ(manager.get_area_of(&p_right),  DockArea::RIGHT);
    EXPECT_EQ(manager.get_area_of(&p_bottom), DockArea::BOTTOM);
    EXPECT_EQ(manager.get_area_of(&p_center), DockArea::CENTER);

    EXPECT_EQ(manager.get_state(&p_left), DockState::DOCKED);
    EXPECT_EQ(manager.get_state(&p_center), DockState::DOCKED);
}

TEST(DockManagerDockTest, DockedPanelReparentedToAreaContainer) {
    DockManager manager;
    DockPanel   panel;
    manager.dock(DockArea::LEFT, &panel);
    ASSERT_NE(panel.get_parent(), nullptr);
    // 父应为 LEFT 区域 Container（manager 的孙节点）
    EXPECT_EQ(panel.get_parent()->get_parent(), &manager);
}

TEST(DockManagerDockTest, UndockClearsState) {
    DockManager manager;
    DockPanel   panel;
    manager.dock(DockArea::LEFT, &panel);
    ASSERT_EQ(manager.get_state(&panel), DockState::DOCKED);
    ASSERT_NE(panel.get_parent(), nullptr);

    manager.undock(&panel);
    EXPECT_EQ(manager.get_state(&panel), DockState::UNDOCKED);
    EXPECT_EQ(panel.get_parent(), nullptr);
    // 仍由注册表跟踪
    EXPECT_EQ(manager.get_panel_id(&panel), "panel_0");
}

TEST(DockManagerDockTest, FloatPanelSetsFloating) {
    DockManager manager;
    DockPanel   panel;
    manager.dock(DockArea::RIGHT, &panel);
    ASSERT_EQ(manager.get_state(&panel), DockState::DOCKED);

    manager.float_panel(&panel);
    EXPECT_EQ(manager.get_state(&panel), DockState::FLOATING);
    EXPECT_TRUE(manager.is_floating(&panel));
    EXPECT_EQ(panel.get_parent(), nullptr);  // 浮动 = 从区域剥离
}

TEST(DockManagerDockTest, RedockFromFloatingToArea) {
    DockManager manager;
    DockPanel   panel;
    manager.float_panel(&panel);
    ASSERT_EQ(manager.get_state(&panel), DockState::FLOATING);

    manager.dock(DockArea::CENTER, &panel);
    EXPECT_EQ(manager.get_state(&panel), DockState::DOCKED);
    EXPECT_EQ(manager.get_area_of(&panel), DockArea::CENTER);
    EXPECT_NE(panel.get_parent(), nullptr);
}

TEST(DockManagerDockTest, GetPanelsInAreaAndFloating) {
    DockManager manager;
    DockPanel   a, b, c, d;
    manager.register_panel("a", &a);
    manager.register_panel("b", &b);
    manager.register_panel("c", &c);
    manager.register_panel("d", &d);

    manager.dock(DockArea::LEFT, &a);
    manager.dock(DockArea::LEFT, &b);
    manager.dock(DockArea::RIGHT, &c);
    manager.float_panel(&d);

    auto left = manager.get_panels_in(DockArea::LEFT);
    ASSERT_EQ(left.size(), 2u);
    EXPECT_EQ(left[0], &a);
    EXPECT_EQ(left[1], &b);

    auto right = manager.get_panels_in(DockArea::RIGHT);
    ASSERT_EQ(right.size(), 1u);
    EXPECT_EQ(right[0], &c);

    auto floating = manager.get_floating_panels();
    ASSERT_EQ(floating.size(), 1u);
    EXPECT_EQ(floating[0], &d);
}

// ====== DockManager 区域尺寸与显示 ======

TEST(DockManagerAreaTest, SetGetAreaSize) {
    DockManager manager;
    // 默认值（LEFT/RIGHT=200, TOP/BOTTOM=80）
    EXPECT_FLOAT_EQ(manager.get_area_size(DockArea::LEFT), 200.0f);
    EXPECT_FLOAT_EQ(manager.get_area_size(DockArea::TOP), 80.0f);

    manager.set_area_size(DockArea::LEFT, 320.0f);
    EXPECT_FLOAT_EQ(manager.get_area_size(DockArea::LEFT), 320.0f);
    manager.set_area_size(DockArea::BOTTOM, 120.0f);
    EXPECT_FLOAT_EQ(manager.get_area_size(DockArea::BOTTOM), 120.0f);
}

TEST(DockManagerAreaTest, SetPanelVisibleHides) {
    DockManager manager;
    DockPanel   panel;
    manager.dock(DockArea::LEFT, &panel);
    ASSERT_TRUE(manager.is_panel_visible(&panel));

    manager.set_panel_visible(&panel, false);
    EXPECT_FALSE(manager.is_panel_visible(&panel));
    EXPECT_FALSE(panel.is_visible());

    // get_panels_in 含隐藏；get_visible_panels_in 不含
    auto all = manager.get_panels_in(DockArea::LEFT);
    EXPECT_EQ(all.size(), 1u);
    auto vis = manager.get_visible_panels_in(DockArea::LEFT);
    EXPECT_EQ(vis.size(), 0u);
}

// ====== border layout 几何 ======

namespace {
// 沿父链累加 get_position()，得到控件相对 DockManager 根的绝对位置。
// Control::get_position() 返回的是相对父原点的偏移；border layout 验证需要绝对坐标。
Vector2 absolute_position(const Control* c) {
    Vector2 acc{0.0f, 0.0f};
    while (c != nullptr) {
        acc = acc + c->get_position();
        c = dynamic_cast<const Control*>(c->get_parent());
    }
    return acc;
}
}  // namespace

TEST(DockManagerLayoutTest, BorderLayoutPlacesAreasAndPanels) {
    DockManager manager;
    DockPanel   p_left, p_center;
    manager.register_panel("l", &p_left);
    manager.register_panel("c", &p_center);

    manager.set_size({800, 600});
    manager.set_area_size(DockArea::LEFT,   200);
    manager.set_area_size(DockArea::RIGHT,  200);
    manager.set_area_size(DockArea::TOP,    80);
    manager.set_area_size(DockArea::BOTTOM, 80);

    manager.dock(DockArea::LEFT,   &p_left);
    manager.dock(DockArea::CENTER, &p_center);
    manager.sort_children();

    // LEFT 区域 = {0,0}-{200,600}；LEFT 内 VBox 把 p_left 放在 (0,0)，
    // 交叉轴 FILL → 宽 200；主轴取最小高度（标题栏 24）。
    // 相对 LEFT_area：position (0,0)，size (200, 24)。
    EXPECT_FLOAT_EQ(p_left.get_position().x, 0.0f);
    EXPECT_FLOAT_EQ(p_left.get_position().y, 0.0f);
    EXPECT_FLOAT_EQ(p_left.get_size().x, 200.0f);
    EXPECT_FLOAT_EQ(p_left.get_size().y, 24.0f);  // 标题栏高度
    // 绝对位置（LEFT_area 在 manager 内 (0,0)）：仍为 (0,0)。
    Vector2 abs_left = absolute_position(&p_left);
    EXPECT_FLOAT_EQ(abs_left.x, 0.0f);
    EXPECT_FLOAT_EQ(abs_left.y, 0.0f);

    // CENTER 区域 = {200,80}-{400,440}；CENTER Container 单子铺满。
    // 相对 CENTER_area：position (0,0)，size (400, 440)。
    EXPECT_FLOAT_EQ(p_center.get_position().x, 0.0f);
    EXPECT_FLOAT_EQ(p_center.get_position().y, 0.0f);
    EXPECT_FLOAT_EQ(p_center.get_size().x, 400.0f);
    EXPECT_FLOAT_EQ(p_center.get_size().y, 440.0f);
    // 绝对位置（CENTER_area 在 manager 内 (200,80)）。
    Vector2 abs_center = absolute_position(&p_center);
    EXPECT_FLOAT_EQ(abs_center.x, 200.0f);
    EXPECT_FLOAT_EQ(abs_center.y, 80.0f);
}

// ====== layout_changed 信号 ======

TEST(DockManagerSignalTest, LayoutChangedEmittedOnDock) {
    DockManager manager;
    DockPanel   panel;
    int         count = 0;
    manager.layout_changed().connect([&count]() { ++count; });

    EXPECT_EQ(count, 0);
    manager.dock(DockArea::LEFT, &panel);
    EXPECT_GE(count, 1);

    int before = count;
    manager.dock(DockArea::LEFT, &panel);  // 幂等：area 未变，不应触发
    EXPECT_EQ(count, before);
}

// ====== 布局持久化 ======

TEST(DockManagerLayoutIOTest, SerializeDeserializeRoundTrip) {
    DockManager m1;
    DockPanel   p1, p2, p3;
    m1.register_panel("layers",     &p1);
    m1.register_panel("properties", &p2);
    m1.register_panel("navigator",  &p3);

    m1.dock(DockArea::LEFT,  &p1);
    m1.dock(DockArea::RIGHT, &p2);
    m1.float_panel(&p3);
    m1.set_area_size(DockArea::LEFT, 250.0f);
    m1.set_panel_visible(&p2, false);

    const std::string text = m1.serialize_layout();
    ASSERT_FALSE(text.empty());
    // 内容自检（关键片段存在）
    EXPECT_NE(text.find("[area]"), std::string::npos);
    EXPECT_NE(text.find("[[panel]]"), std::string::npos);
    EXPECT_NE(text.find("layers"), std::string::npos);

    // 反序列化到新 manager（相同 id 注册，不同 panel 对象）
    DockManager m2;
    DockPanel   q1, q2, q3;
    m2.register_panel("layers",     &q1);
    m2.register_panel("properties", &q2);
    m2.register_panel("navigator",  &q3);
    EXPECT_TRUE(m2.deserialize_layout(text));

    EXPECT_EQ(m2.get_state(&q1), DockState::DOCKED);
    EXPECT_EQ(m2.get_area_of(&q1), DockArea::LEFT);
    EXPECT_EQ(m2.get_state(&q2), DockState::DOCKED);
    EXPECT_EQ(m2.get_area_of(&q2), DockArea::RIGHT);
    EXPECT_FALSE(m2.is_panel_visible(&q2));
    EXPECT_EQ(m2.get_state(&q3), DockState::FLOATING);
    EXPECT_TRUE(m2.is_floating(&q3));
    EXPECT_FLOAT_EQ(m2.get_area_size(DockArea::LEFT), 250.0f);
}

TEST(DockManagerLayoutIOTest, SaveLoadLayoutFile) {
    namespace fs = std::filesystem;
    const auto tmp = fs::temp_directory_path() / "eda_dock_test.layout.toml";
    const std::string path = tmp.string();

    DockManager m1;
    DockPanel   p1;
    m1.register_panel("layers", &p1);
    m1.dock(DockArea::LEFT, &p1);
    m1.set_area_size(DockArea::LEFT, 300.0f);

    const std::string content = m1.save_layout(path);
    ASSERT_FALSE(content.empty());
    EXPECT_TRUE(fs::exists(tmp));

    DockManager m2;
    DockPanel   q1;
    m2.register_panel("layers", &q1);
    ASSERT_TRUE(m2.load_layout(path));
    EXPECT_EQ(m2.get_area_of(&q1), DockArea::LEFT);
    EXPECT_FLOAT_EQ(m2.get_area_size(DockArea::LEFT), 300.0f);

    std::error_code ec;
    fs::remove(tmp, ec);
}
