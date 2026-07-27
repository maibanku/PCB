// Control 单元测试（P4 Task 1）。
//
// 覆盖（16 个用例）：
//   锚点 / 几何（6）：
//     - 默认 anchors {0,0,1,1} offsets {0,0,0,0} → 铺满父
//     - CENTER 预设：保持尺寸居中
//     - set_size 保持左上位置
//     - set_position 保持尺寸
//     - BOTTOM_RIGHT 预设：父尺寸变化时仍锚右下角
//     - FULL_RECT 预设：铺满父
//   最小尺寸（2）：
//     - 默认零
//     - get_combined_minimum_size 缓存至 update_minimum_size 失效
//   焦点（5）：
//     - set_focus 切换状态
//     - NONE 模式拒绝聚焦
//     - find_next_focus 遍历兄弟并回绕
//     - 非可聚焦兄弟跳过
//     - 聚焦控件自绘时框架追加焦点高亮边框
//   自绘（2）：
//     - queue_redraw → draw_if_dirty 分派 _draw
//     - 已 clean 的控件不再重绘
//   无障碍（1）：
//     - accessibility_label 存取
//
// 所有权约定：Control 父子经 set_parent → add_child 建立关系，~Node 级联 delete
// children_。栈对象用例中 parent 先于 children 声明（parent 后析构），子节点
// 析构时自删于父的 children_，parent 析构时 children_ 已空，安全。

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "scene/gui/control.h"
#include "scene/gui/canvas_item.h"  // RecordingControlCanvas / ControlCanvasScope

using namespace eda;

// ====== 锚点 / 几何 ======

TEST(ControlAnchorTest, DefaultAnchorsFillParent) {
    Control parent;
    parent.set_size({100, 100});              // 顶层 parent：viewport 默认 (0,0)
    Control c;
    c.set_parent(&parent);                    // 默认 anchors {0,0,1,1} offsets {0,0,0,0}
    EXPECT_FLOAT_EQ(c.get_size().x, 100.0f);
    EXPECT_FLOAT_EQ(c.get_size().y, 100.0f);
    EXPECT_FLOAT_EQ(c.get_position().x, 0.0f);
    EXPECT_FLOAT_EQ(c.get_position().y, 0.0f);
}

TEST(ControlAnchorTest, CenterPresetKeepsSizeAtCenter) {
    Control parent; parent.set_size({100, 100});
    Control c; c.set_parent(&parent);
    c.set_size({20, 20});
    c.set_anchors_preset(LayoutPreset::CENTER);  // anchors 0.5 + offsets ±10
    EXPECT_NEAR(c.get_position().x, 40.0f, 1e-4f);  // (100-20)/2
    EXPECT_NEAR(c.get_position().y, 40.0f, 1e-4f);
    EXPECT_FLOAT_EQ(c.get_size().x, 20.0f);
    EXPECT_FLOAT_EQ(c.get_size().y, 20.0f);
}

TEST(ControlAnchorTest, SetSizeKeepsPosition) {
    Control parent; parent.set_size({100, 100});
    Control c; c.set_parent(&parent);
    c.set_position({10, 10});
    c.set_size({40, 40});
    EXPECT_FLOAT_EQ(c.get_position().x, 10.0f);
    EXPECT_FLOAT_EQ(c.get_position().y, 10.0f);
    EXPECT_FLOAT_EQ(c.get_size().x, 40.0f);
    c.set_size({60, 20});
    EXPECT_FLOAT_EQ(c.get_position().x, 10.0f);  // 位置不变
    EXPECT_FLOAT_EQ(c.get_size().x, 60.0f);
}

TEST(ControlAnchorTest, SetPositionKeepsSize) {
    Control parent; parent.set_size({100, 100});
    Control c; c.set_parent(&parent);
    c.set_position({10, 10});
    c.set_size({30, 30});
    c.set_position({50, 50});                  // 移动后尺寸不变
    EXPECT_FLOAT_EQ(c.get_position().x, 50.0f);
    EXPECT_FLOAT_EQ(c.get_size().x, 30.0f);
}

TEST(ControlAnchorTest, RightAnchorStaysPinnedOnParentResize) {
    Control parent; parent.set_size({100, 100});
    Control c; c.set_parent(&parent);
    c.set_size({20, 20});
    c.set_anchors_preset(LayoutPreset::BOTTOM_RIGHT);  // 锚到右下角
    EXPECT_NEAR(c.get_position().x, 80.0f, 1e-4f);     // 100-20
    parent.set_size({200, 200});                        // 父尺寸变化
    EXPECT_NEAR(c.get_position().x, 180.0f, 1e-4f);    // 仍锚在右下：200-20
    EXPECT_FLOAT_EQ(c.get_size().x, 20.0f);
}

TEST(ControlAnchorTest, FullRectPresetFillsParent) {
    Control parent; parent.set_size({80, 60});
    Control c; c.set_parent(&parent);
    c.set_anchors_preset(LayoutPreset::FULL_RECT);
    EXPECT_FLOAT_EQ(c.get_size().x, 80.0f);
    EXPECT_FLOAT_EQ(c.get_size().y, 60.0f);
}

// ====== 最小尺寸（容器布局消费）======

namespace {
// 测试用控件：显式给定最小尺寸，验证缓存与失效。
class FixedSizeControl : public Control {
public:
    void set_minimum_size(Vector2 s) {
        min_ = s;
        update_minimum_size();  // 标脏 + 向父冒泡
    }
    Vector2 get_minimum_size() const override { return min_; }

private:
    Vector2 min_{0, 0};
};
}  // namespace

TEST(ControlMinSizeTest, DefaultIsZero) {
    Control c;
    EXPECT_FLOAT_EQ(c.get_minimum_size().x, 0.0f);
    EXPECT_FLOAT_EQ(c.get_combined_minimum_size().x, 0.0f);
}

TEST(ControlMinSizeTest, CombinedCachesUntilDirty) {
    FixedSizeControl c;
    c.set_minimum_size({30, 20});
    EXPECT_FLOAT_EQ(c.get_combined_minimum_size().x, 30.0f);
    EXPECT_FLOAT_EQ(c.get_combined_minimum_size().y, 20.0f);
    c.set_minimum_size({50, 10});
    EXPECT_FLOAT_EQ(c.get_combined_minimum_size().x, 50.0f);  // 失效后重算
}

// ====== 焦点 ======

TEST(ControlFocusTest, SetFocusTogglesState) {
    Control c;
    c.set_focus_mode(FocusMode::CLICK);
    EXPECT_FALSE(c.has_focus());
    c.set_focus(true);
    EXPECT_TRUE(c.has_focus());
    c.set_focus(false);
    EXPECT_FALSE(c.has_focus());
}

TEST(ControlFocusTest, NoneModeRefusesFocus) {
    Control c;  // 默认 NONE
    c.set_focus(true);
    EXPECT_FALSE(c.has_focus());
}

TEST(ControlFocusTest, FindNextFocusWalksSiblings) {
    Control parent;
    Control a, b, d;
    for (auto* ch : {&a, &b, &d}) {
        ch->set_parent(&parent);
        ch->set_focus_mode(FocusMode::CLICK);
    }
    a.set_focus(true);
    Control* next = a.find_next_focus(/*reverse=*/false);
    EXPECT_EQ(next, &b);
    Control* wrap = d.find_next_focus(/*reverse=*/false);
    EXPECT_EQ(wrap, &a);  // 末尾回绕到首
}

TEST(ControlFocusTest, NonFocusableSiblingSkipped) {
    Control parent;
    Control a, b, d;
    a.set_parent(&parent); a.set_focus_mode(FocusMode::CLICK);
    b.set_parent(&parent);  // NONE，跳过
    d.set_parent(&parent); d.set_focus_mode(FocusMode::ALL);
    Control* next = a.find_next_focus(false);
    EXPECT_EQ(next, &d);
}

// ====== 自绘（伪渲染器验证）======

namespace {
// 测试用控件：_draw 画一个 10×10 红色填充矩形。
class StubControl : public Control {
protected:
    void _draw() override {
        current_canvas()->draw_rect(Rect2{{0, 0}, {10, 10}},
                                    Color{1, 0, 0, 1}, /*filled=*/true);
    }
};
}  // namespace

TEST(ControlDrawTest, DrawDispatchedOnRedraw) {
    StubControl c;
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    c.queue_redraw();
    EXPECT_TRUE(c.is_draw_dirty());
    c.draw_if_dirty();
    EXPECT_FALSE(c.is_draw_dirty());
    ASSERT_EQ(rec.rects.size(), 1u);
    EXPECT_FLOAT_EQ(rec.rects[0].rect.size.x, 10.0f);
    EXPECT_TRUE(rec.rects[0].filled);
}

TEST(ControlDrawTest, CleanControlNotRedrawn) {
    StubControl c;
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    c.draw_if_dirty();          // 初始 dirty=true，首帧画一次
    rec.rects.clear();
    c.draw_if_dirty();          // 已 clean，不再画
    EXPECT_EQ(rec.rects.size(), 0u);
}

TEST(ControlFocusTest, FocusedControlDrawsHighlight) {
    StubControl c;
    c.set_focus_mode(FocusMode::CLICK);
    c.set_focus(true);
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    c.queue_redraw();
    c.draw_if_dirty();
    // StubControl 自绘 1 个填充矩形 + 框架绘制 1 个非填充焦点高亮边框
    ASSERT_EQ(rec.rects.size(), 2u);
    EXPECT_TRUE(rec.rects[0].filled);    // 控件自绘
    EXPECT_FALSE(rec.rects[1].filled);   // 框架焦点高亮（无障碍：焦点可视指示）
}

// ====== 无障碍 ======

TEST(ControlA11yTest, AccessibilityLabelStored) {
    Control c;
    c.set_accessibility_label("delete footprint");
    EXPECT_EQ(c.get_accessibility_label(), "delete footprint");
}
