// Container 单元测试（P4 Task 2）。
//
// 覆盖（10 个用例）：
//   HBox（3）：
//     - 三个子控件水平排列 + spacing（BEGIN 打包，交叉轴 FILL）
//     - CENTER 对齐：子组居中
//     - EXPAND：余量瓜分
//   VBox（1）：
//     - 三个子控件垂直排列 + spacing
//   Grid（1）：
//     - 2×2 行列对齐 + row_spacing/col_spacing
//   MarginContainer（1）：
//     - 四向内边距，子铺满内矩形
//   重排触发（2）：
//     - 容器 resize 后子控件重新排列（_notification RESIZED → queue_sort）
//     - add_child 触发 dirty；remove_child 同步剔除提示
//   最小尺寸向上传递（2）：
//     - HBox 聚合子最小尺寸（和 / 最大）
//     - 子 min 变化经 update_minimum_size 冒泡失效容器缓存
//
// 所有权约定：栈对象用例中容器先于子控件声明（容器后析构），子析构时自删于父的
// children_，容器析构时 children_ 已空，无双重释放（与 test_control.cpp 一致）。

#include <gtest/gtest.h>

#include <vector>

#include "scene/gui/container.h"

using namespace eda;

namespace {
// 测试用控件：显式给定最小尺寸，验证容器布局与缓存失效。
// 复用 Control::update_minimum_size 失效缓存 + 向父冒泡。
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

// ====== HBox ======

TEST(HBoxTest, HorizontalLayoutWithSpacing) {
    HBox box;  // 父先声明：子先析构自删于父，避免双重释放
    box.set_size({100, 40});
    box.set_spacing(5);

    FixedSizeControl c1, c2, c3;
    c1.set_minimum_size({30, 20});
    c2.set_minimum_size({20, 20});
    c3.set_minimum_size({10, 20});
    box.add_child(&c1);
    box.add_child(&c2);
    box.add_child(&c3);
    box.sort_children();

    // 主轴 x：BEGIN 打包，cursor=0 → 0 / 35 / 60
    EXPECT_FLOAT_EQ(c1.get_position().x, 0.0f);
    EXPECT_FLOAT_EQ(c2.get_position().x, 35.0f);  // 30 + spacing 5
    EXPECT_FLOAT_EQ(c3.get_position().x, 60.0f);  // 35 + 20 + 5
    // 主轴尺寸 = 各自最小（无 EXPAND）
    EXPECT_FLOAT_EQ(c1.get_size().x, 30.0f);
    EXPECT_FLOAT_EQ(c2.get_size().x, 20.0f);
    EXPECT_FLOAT_EQ(c3.get_size().x, 10.0f);
    // 交叉轴 y：默认 FILL，铺满容器高度 40
    EXPECT_FLOAT_EQ(c1.get_position().y, 0.0f);
    EXPECT_FLOAT_EQ(c1.get_size().y, 40.0f);
    EXPECT_FLOAT_EQ(c2.get_size().y, 40.0f);
    EXPECT_FLOAT_EQ(c3.get_size().y, 40.0f);
}

TEST(HBoxTest, CenterAlignmentPacksChildrenAtCenter) {
    HBox box;
    box.set_size({100, 40});
    box.set_spacing(5);
    box.set_alignment(BoxAlignment::CENTER);

    FixedSizeControl c1, c2, c3;
    c1.set_minimum_size({30, 20});
    c2.set_minimum_size({20, 20});
    c3.set_minimum_size({10, 20});
    box.add_child(&c1);
    box.add_child(&c2);
    box.add_child(&c3);
    box.sort_children();

    // 子组占用 = 30+20+10 + 5*2 = 70；余量 30，居中偏移 15
    EXPECT_FLOAT_EQ(c1.get_position().x, 15.0f);  // 0 + 15
    EXPECT_FLOAT_EQ(c2.get_position().x, 50.0f);  // 15 + 30 + 5
    EXPECT_FLOAT_EQ(c3.get_position().x, 75.0f);  // 50 + 20 + 5
}

TEST(HBoxTest, ExpandFlagDistributesExtra) {
    HBox box;
    box.set_size({100, 40});
    box.set_spacing(0);

    FixedSizeControl c1, c2;
    c1.set_minimum_size({20, 20});
    c2.set_minimum_size({30, 20});
    box.add_child(&c1);
    box.add_child(&c2);
    box.set_h_size_flag(&c1, SizeFlag::EXPAND);  // c1 瓜分余量
    box.set_h_size_flag(&c2, SizeFlag::FILL);
    box.sort_children();

    // 余量 = 100 - 20 - 30 = 50；c1 EXPAND 独得 50 → 20 + 50 = 70
    EXPECT_FLOAT_EQ(c1.get_size().x, 70.0f);
    EXPECT_FLOAT_EQ(c2.get_size().x, 30.0f);
    EXPECT_FLOAT_EQ(c1.get_position().x, 0.0f);
    EXPECT_FLOAT_EQ(c2.get_position().x, 70.0f);
}

// ====== VBox ======

TEST(VBoxTest, VerticalLayoutWithSpacing) {
    VBox box;
    box.set_size({40, 100});
    box.set_spacing(5);

    FixedSizeControl c1, c2, c3;
    c1.set_minimum_size({20, 30});
    c2.set_minimum_size({20, 20});
    c3.set_minimum_size({20, 10});
    box.add_child(&c1);
    box.add_child(&c2);
    box.add_child(&c3);
    box.sort_children();

    // 主轴 y：BEGIN 打包 0 / 35 / 60
    EXPECT_FLOAT_EQ(c1.get_position().y, 0.0f);
    EXPECT_FLOAT_EQ(c2.get_position().y, 35.0f);
    EXPECT_FLOAT_EQ(c3.get_position().y, 60.0f);
    EXPECT_FLOAT_EQ(c1.get_size().y, 30.0f);
    EXPECT_FLOAT_EQ(c2.get_size().y, 20.0f);
    EXPECT_FLOAT_EQ(c3.get_size().y, 10.0f);
    // 交叉轴 x：FILL 铺满 40
    EXPECT_FLOAT_EQ(c1.get_size().x, 40.0f);
    EXPECT_FLOAT_EQ(c1.get_position().x, 0.0f);
}

// ====== Grid ======

TEST(GridTest, RowColAlignmentWithSpacing) {
    Grid grid;
    grid.set_columns(2);
    grid.set_col_spacing(2);
    grid.set_row_spacing(3);
    grid.set_size({80, 80});

    FixedSizeControl c1, c2, c3, c4;
    c1.set_minimum_size({30, 20});  // (row 0, col 0)
    c2.set_minimum_size({40, 15});  // (row 0, col 1)
    c3.set_minimum_size({20, 25});  // (row 1, col 0)
    c4.set_minimum_size({15, 10});  // (row 1, col 1)
    grid.add_child(&c1);
    grid.add_child(&c2);
    grid.add_child(&c3);
    grid.add_child(&c4);
    grid.sort_children();

    // 列宽：col0 = max(30, 20) = 30；col1 = max(40, 15) = 40
    // 行高：row0 = max(20, 15) = 20；row1 = max(25, 10) = 25
    // col_x: [0, 30+2=32]；row_y: [0, 20+3=23]
    EXPECT_FLOAT_EQ(c1.get_position().x, 0.0f);
    EXPECT_FLOAT_EQ(c1.get_position().y, 0.0f);
    EXPECT_FLOAT_EQ(c2.get_position().x, 32.0f);
    EXPECT_FLOAT_EQ(c2.get_position().y, 0.0f);
    EXPECT_FLOAT_EQ(c3.get_position().x, 0.0f);
    EXPECT_FLOAT_EQ(c3.get_position().y, 23.0f);
    EXPECT_FLOAT_EQ(c4.get_position().x, 32.0f);
    EXPECT_FLOAT_EQ(c4.get_position().y, 23.0f);
    // 子尺寸 = 自身最小（单元格内左上对齐）
    EXPECT_FLOAT_EQ(c1.get_size().x, 30.0f);
    EXPECT_FLOAT_EQ(c2.get_size().x, 40.0f);
    EXPECT_FLOAT_EQ(c3.get_size().y, 25.0f);
    EXPECT_FLOAT_EQ(c4.get_size().y, 10.0f);
}

// ====== MarginContainer ======

TEST(MarginContainerTest, InnerRectFromFourMargins) {
    MarginContainer margin;
    margin.set_margins(/*l=*/5, /*t=*/10, /*r=*/15, /*b=*/20);
    margin.set_size({100, 100});

    FixedSizeControl child;
    child.set_minimum_size({30, 30});
    margin.add_child(&child);
    margin.sort_children();

    // 内矩形：pos=(5,10), size=(100-5-15, 100-10-20)=(80, 70)
    EXPECT_FLOAT_EQ(child.get_position().x, 5.0f);
    EXPECT_FLOAT_EQ(child.get_position().y, 10.0f);
    EXPECT_FLOAT_EQ(child.get_size().x, 80.0f);
    EXPECT_FLOAT_EQ(child.get_size().y, 70.0f);
}

// ====== 重排触发 ======

TEST(ContainerRelayoutTest, ResizeTriggersChildrenRelayout) {
    HBox box;
    box.set_spacing(5);

    FixedSizeControl c1, c2, c3;
    c1.set_minimum_size({30, 20});
    c2.set_minimum_size({20, 20});
    c3.set_minimum_size({10, 20});
    box.add_child(&c1);
    box.add_child(&c2);
    box.add_child(&c3);

    box.set_size({100, 40});
    box.sort_children();
    // 初始：交叉轴铺满 40
    EXPECT_FLOAT_EQ(c1.get_size().y, 40.0f);
    EXPECT_FLOAT_EQ(c3.get_position().x, 60.0f);

    // resize → Control::set_size 发 NOTIFICATION_RESIZED
    //        → Container::_notification 捕获 → queue_sort 标脏
    box.set_size({200, 80});
    EXPECT_TRUE(box.is_layout_dirty());  // dirty 已置位
    box.sort_children();                 // flush
    EXPECT_FALSE(box.is_layout_dirty());

    // 重排后：交叉轴跟随新高度 80；主轴 BEGIN 打包位置不变（无 EXPAND）
    EXPECT_FLOAT_EQ(c1.get_size().y, 80.0f);
    EXPECT_FLOAT_EQ(c2.get_size().y, 80.0f);
    EXPECT_FLOAT_EQ(c3.get_size().y, 80.0f);
    EXPECT_FLOAT_EQ(c1.get_position().x, 0.0f);
    EXPECT_FLOAT_EQ(c2.get_position().x, 35.0f);
    EXPECT_FLOAT_EQ(c3.get_position().x, 60.0f);
}

TEST(ContainerRelayoutTest, AddChildMarksDirtyAndRemoveCleansHint) {
    HBox box;
    box.set_size({100, 40});
    EXPECT_TRUE(box.is_layout_dirty());  // 构造默认脏
    box.sort_children();
    EXPECT_FALSE(box.is_layout_dirty());

    FixedSizeControl c1;
    c1.set_minimum_size({30, 20});
    box.add_child(&c1);
    EXPECT_TRUE(box.is_layout_dirty());  // add_child 触发标脏
    box.sort_children();
    EXPECT_FALSE(box.is_layout_dirty());

    // size_flag 提示存于容器侧表；remove_child 应剔除
    box.set_h_size_flag(&c1, SizeFlag::EXPAND);
    EXPECT_EQ(box.get_h_size_flag(&c1), SizeFlag::EXPAND);
    box.remove_child(&c1);
    // 移除后查询返回默认 FILL（提示已剔除，无悬空指针）
    EXPECT_EQ(box.get_h_size_flag(&c1), SizeFlag::FILL);
    EXPECT_TRUE(box.is_layout_dirty());  // remove_child 也标脏
}

// ====== 最小尺寸向上传递 ======

TEST(ContainerMinSizeTest, AggregatesChildrenMinimumSize) {
    HBox box;
    box.set_spacing(5);

    FixedSizeControl c1, c2;
    c1.set_minimum_size({30, 20});
    c2.set_minimum_size({40, 15});
    box.add_child(&c1);
    box.add_child(&c2);

    // HBox min：主轴 = 30 + 40 + 5 = 75；交叉轴 = max(20, 15) = 20
    Vector2 m = box.get_combined_minimum_size();
    EXPECT_FLOAT_EQ(m.x, 75.0f);
    EXPECT_FLOAT_EQ(m.y, 20.0f);
}

TEST(ContainerMinSizeTest, ChildMinChangeBubblesUpAndInvalidatesCache) {
    HBox box;
    box.set_spacing(5);

    FixedSizeControl c1, c2;
    c1.set_minimum_size({30, 20});
    c2.set_minimum_size({40, 15});
    box.add_child(&c1);
    box.add_child(&c2);

    // 初次查询填充缓存
    ASSERT_FLOAT_EQ(box.get_combined_minimum_size().x, 75.0f);

    // 子 min 变化 → FixedSizeControl::set_minimum_size 调 update_minimum_size
    //             → 冒泡至 box（Control::update_minimum_size 非 virtual，
    //                经 Control* 调用，置 box.min_size_dirty_ = true）
    c1.set_minimum_size({50, 25});

    // 重算：30→50, 20→25 → 主轴 50+40+5=95；交叉轴 max(25,15)=25
    Vector2 m = box.get_combined_minimum_size();
    EXPECT_FLOAT_EQ(m.x, 95.0f);
    EXPECT_FLOAT_EQ(m.y, 25.0f);
}
