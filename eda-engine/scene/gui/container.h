// Container —— 布局容器基类与具体派生（P4 Task 2）。
//
// 在 Control（锚点 / 几何 / 最小尺寸 / 焦点）之上叠加"接管子控件几何"能力：
//   - fit_child_in_rect：把子控件锁定到容器内绝对像素矩形（anchors 全置 0 +
//     set_position/set_size），不再随容器尺寸拉伸；派生容器算好每个子的 Rect 后
//     统一由此下发。set_position/set_size 触发 NOTIFICATION_RESIZED，使子若为
//     Container 也能借此重排自身（嵌套布局链路）。
//   - 重排调度（dirty flag 机制）：
//       queue_sort()    标脏（幂等：多次调用合并为一次 flush）
//       sort_children() dirty 则清脏 + 调 _layout_children()
//   - 重排触发（dirty 何时被置位）：
//       * 自身 size 变化        —— _notification(NOTIFICATION_RESIZED) 钩子
//       * add/remove child      —— shadow Node::add_child/remove_child（重用 Node 树）
//       * 子最小尺寸变化        —— Control::update_minimum_size 向父冒泡置
//                                  min_size_dirty_，下次 get_combined_minimum_size
//                                  / _layout_children 重读子最小尺寸时自然拿到新值
//   - 最小尺寸向上传递：get_minimum_size() 虚，派生容器按布局算法聚合子最小尺寸；
//     Control::get_combined_minimum_size() 带缓存，子 min 变化经 update_minimum_size
//     冒泡失效缓存（容器布局消费）。
//
// 具体派生（同文件，最小可用子集）：
//   - BoxContainer（HBox / VBox 共用一维排列算法；axis=0 水平 / 1 垂直；
//                  spacing + BEGIN/CENTER/END 整体对齐 + 每子 size_flag）
//   - Grid（columns 列行列布局；row_spacing / col_spacing）
//   - MarginContainer（四向内边距；子铺满去边距后的内矩形）
//
// 对齐 Godot scene/gui/{container,box_container,grid_container,margin_container}.h
// （split / scroll / tab / center / aspect_ratio / flow 等留待后续 Task）。

#pragma once

#include "scene/gui/control.h"

#include <unordered_map>
#include <vector>

namespace eda {

// ====== 枚举：尺寸提示 / 一维对齐 ======

// 子控件在容器中的尺寸提示（对齐 Godot SizeFlag 简化子集）。
//   - FILL：占满容器主轴分配区域（默认）
//   - EXPAND：在 FILL 基础上瓜分剩余空间
//   - SHRINK_CENTER：仅取最小尺寸，交叉轴居中
//   - SHRINK_END：仅取最小尺寸，交叉轴尾对齐
enum class SizeFlag {
    FILL,
    EXPAND,
    SHRINK_CENTER,
    SHRINK_END,
};

// 一维容器（HBox/VBox）整体对齐：子组在主轴上的打包位置。
enum class BoxAlignment {
    BEGIN,   // 打包到起点（默认）
    CENTER,  // 居中
    END,     // 打包到末尾
};

// ====== Container 基类 ======

class Container : public Control {
public:
    Container() = default;
    ~Container() override = default;

    Container(const Container&) = delete;
    Container& operator=(const Container&) = delete;

    // —— 子节点管理（shadow Node::add_child/remove_child）——
    // 重用 Node 树：内部调 Node::add_child/remove_child 维护父子 ownership，
    // 额外 queue_sort 以触发重排。注意 add_child 非虚，经 Node*/CanvasItem*
    // 静态类型调用（如 CanvasItem::set_parent 内部）不会命中 shadow 版本；
    // 测试与业务应经 Container* 调用以同步布局。
    void add_child(Node* child);
    void remove_child(Node* child);

    // —— 重排调度（dirty flag 一帧去重）——
    void queue_sort();     // 标脏（幂等）
    void sort_children();  // dirty 则清脏 + _layout_children
    bool is_layout_dirty() const { return layout_dirty_; }

    // 把 child 锁定到容器内绝对像素矩形。
    // 实现：四向 anchor 全置 0 → 几何不随容器尺寸拉伸；set_position/set_size
    // 应用 rect 并触发 NOTIFICATION_RESIZED（嵌套容器可借此重排）。
    void fit_child_in_rect(Control* child, Rect2 rect);

    // —— 每子布局提示（Control 暂无 size_flag 成员，由 Container 侧记）——
    void     set_h_size_flag(Control* child, SizeFlag flag);
    void     set_v_size_flag(Control* child, SizeFlag flag);
    SizeFlag get_h_size_flag(const Control* child) const;
    SizeFlag get_v_size_flag(const Control* child) const;

    // —— 类型查询（基于类名字符串，无 RTTI）——
    static std::string get_class_static() { return "Container"; }
    std::string get_class() const override { return "Container"; }
    bool is_class(const std::string& name) const override {
        return name == "Container" || Control::is_class(name);
    }

protected:
    // 派生容器覆盖：按本容器布局算法排列所有 Control 子节点。
    // 默认：所有 Control 子节点铺满容器（FULL_RECT 语义）。
    virtual void _layout_children();

    // 自身 size 变化（NOTIFICATION_RESIZED）→ queue_sort。
    void _notification(int what) override;

    // 直接 Control 子节点（跳过非 Control 的 Node）。const：get_minimum_size 调用。
    std::vector<Control*> get_control_children() const;

private:
    bool layout_dirty_ = true;  // 首次 sort_children 必发，确保初次布局生效

    struct ChildHints {
        SizeFlag h = SizeFlag::FILL;
        SizeFlag v = SizeFlag::FILL;
    };
    std::unordered_map<const Control*, ChildHints> hints_;
};

// ====== BoxContainer（HBox / VBox 共用一维排列算法）======

class BoxContainer : public Container {
public:
    void set_spacing(float s) { spacing_ = s; queue_sort(); }
    float get_spacing() const { return spacing_; }

    void set_alignment(BoxAlignment a) { alignment_ = a; queue_sort(); }
    BoxAlignment get_alignment() const { return alignment_; }

    Vector2 get_minimum_size() const override;

protected:
    // axis: 0 = 水平（HBox），1 = 垂直（VBox）。
    explicit BoxContainer(int axis) : axis_(axis) {}
    void _layout_children() override;

    float        spacing_   = 0.0f;
    BoxAlignment alignment_ = BoxAlignment::BEGIN;
    int          axis_      = 0;

private:
    // 主轴 / 交叉轴分量提取（按 axis 取 x 或 y）。
    static float main_of(Vector2 v, int axis) { return axis == 0 ? v.x : v.y; }
    static float cross_of(Vector2 v, int axis) { return axis == 0 ? v.y : v.x; }
};

// 水平排列（主轴 = x）。
class HBox : public BoxContainer {
public:
    HBox() : BoxContainer(0) {}

    static std::string get_class_static() { return "HBox"; }
    std::string get_class() const override { return "HBox"; }
    bool is_class(const std::string& name) const override {
        return name == "HBox" || BoxContainer::is_class(name);
    }
};

// 垂直排列（主轴 = y）。
class VBox : public BoxContainer {
public:
    VBox() : BoxContainer(1) {}

    static std::string get_class_static() { return "VBox"; }
    std::string get_class() const override { return "VBox"; }
    bool is_class(const std::string& name) const override {
        return name == "VBox" || BoxContainer::is_class(name);
    }
};

// ====== Grid（行列布局）======

class Grid : public Container {
public:
    void set_columns(int c) { columns_ = c > 0 ? c : 1; queue_sort(); }
    int  get_columns() const { return columns_; }

    void  set_row_spacing(float s) { row_spacing_ = s; queue_sort(); }
    float get_row_spacing() const { return row_spacing_; }
    void  set_col_spacing(float s) { col_spacing_ = s; queue_sort(); }
    float get_col_spacing() const { return col_spacing_; }

    Vector2 get_minimum_size() const override;

    static std::string get_class_static() { return "Grid"; }
    std::string get_class() const override { return "Grid"; }
    bool is_class(const std::string& name) const override {
        return name == "Grid" || Container::is_class(name);
    }

protected:
    void _layout_children() override;

private:
    int   columns_     = 1;   // 列数；行数 = ceil(n / columns_)
    float row_spacing_ = 0.0f;
    float col_spacing_ = 0.0f;
};

// ====== MarginContainer（四向内边距）======

class MarginContainer : public Container {
public:
    void set_margin_left(float v) {
        margins_[static_cast<int>(Side::LEFT)] = v; queue_sort();
    }
    void set_margin_top(float v) {
        margins_[static_cast<int>(Side::TOP)] = v; queue_sort();
    }
    void set_margin_right(float v) {
        margins_[static_cast<int>(Side::RIGHT)] = v; queue_sort();
    }
    void set_margin_bottom(float v) {
        margins_[static_cast<int>(Side::BOTTOM)] = v; queue_sort();
    }
    void set_margins(float l, float t, float r, float b) {
        margins_[static_cast<int>(Side::LEFT)]   = l;
        margins_[static_cast<int>(Side::TOP)]    = t;
        margins_[static_cast<int>(Side::RIGHT)]  = r;
        margins_[static_cast<int>(Side::BOTTOM)] = b;
        queue_sort();
    }

    float get_margin_left() const { return margins_[static_cast<int>(Side::LEFT)]; }
    float get_margin_top() const { return margins_[static_cast<int>(Side::TOP)]; }
    float get_margin_right() const { return margins_[static_cast<int>(Side::RIGHT)]; }
    float get_margin_bottom() const { return margins_[static_cast<int>(Side::BOTTOM)]; }

    Vector2 get_minimum_size() const override;

    static std::string get_class_static() { return "MarginContainer"; }
    std::string get_class() const override { return "MarginContainer"; }
    bool is_class(const std::string& name) const override {
        return name == "MarginContainer" || Container::is_class(name);
    }

protected:
    void _layout_children() override;

private:
    // L T R B，索引对齐 Side 枚举（LEFT=0, TOP=1, RIGHT=2, BOTTOM=3）。
    float margins_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

}  // namespace eda
