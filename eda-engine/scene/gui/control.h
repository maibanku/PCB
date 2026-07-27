// Control —— GUI 控件基类（P4 Task 1）。
//
// 在 CanvasItem（自绘节点）之上叠加：
//   - 锚点布局：anchors[4]（0..1 相对父尺寸）+ offsets[4]（像素偏移）。
//     LEFT/TOP 决定左上角，RIGHT/BOTTOM 决定右下角。set_anchors_preset 按预设
//     重算（FULL_RECT/CENTER/TOP_LEFT/...）。几何由锚点 + 父尺寸实时计算，
//     父尺寸变化时自动重定位（锚定语义）。
//   - 几何：get_position/get_size 覆盖 CanvasItem，由锚点 + 父尺寸实时计算；
//     set_position/set_size 调整 offsets 保持另一端不动（对齐 Godot 行为）。
//   - 最小尺寸：get_minimum_size 虚函数（派生控件覆盖），get_combined_minimum_size
//     带缓存 + update_minimum_size 失效并向父冒泡（容器布局消费）。
//   - 焦点：set_focus_mode(NONE/CLICK/ALL)、set_focus/has_focus、find_next_focus
//     DFS 遍历下一个可聚焦控件（Tab/Shift+Tab；无障碍前置）。
//   - 无障碍：accessibility_label（屏幕阅读器朗读文本）。
//   - 自绘：draw_if_dirty 覆盖以追加焦点可视高亮（WCAG 2.1 AA：不依赖 hover）。

#pragma once

#include "scene/gui/canvas_item.h"
#include "core/math/vector2.h"
#include "core/math/rect2.h"
#include "core/math/color.h"

#include <string>

namespace eda {

// 边（锚点 offsets/anchors 数组索引：L=0, T=1, R=2, B=3）。
enum class Side : int { LEFT = 0, TOP = 1, RIGHT = 2, BOTTOM = 3 };

// 焦点模式：NONE 不可聚焦 / CLICK 鼠标点击 + Tab / ALL 点击 + Tab + 全键。
enum class FocusMode { NONE, CLICK, ALL };

// 鼠标事件过滤：STOP 拦截 / PASS 透传 / IGNORE 完全忽略。
enum class MouseFilter { STOP, PASS, IGNORE };

// 生长方向（容器布局扩张方向）。
enum class GrowDirection { BEGIN, END, BOTH };

// 锚点预设（对齐 Godot LayoutPreset 常用子集；其余 Task 2 按同构补全）。
enum class LayoutPreset {
    TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT,
    CENTER,
    LEFT_WIDE, TOP_WIDE, RIGHT_WIDE, BOTTOM_WIDE,
    FULL_RECT
};

// 通知标识（与 Node/Object 既有常量错峰：P2 用 10..13，Object 用 0..1）。
inline constexpr int NOTIFICATION_DRAW          = 30;
inline constexpr int NOTIFICATION_FOCUS_ENTER   = 31;
inline constexpr int NOTIFICATION_FOCUS_EXIT    = 32;
inline constexpr int NOTIFICATION_RESIZED       = 33;
inline constexpr int NOTIFICATION_SORT_CHILDREN = 34;

class Control : public CanvasItem {
public:
    Control();

    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;

    // —— 锚点 / 偏移（0..1 相对父尺寸 + 像素偏移）——
    void  set_anchor(Side side, float value);
    float get_anchor(Side side) const { return anchors_[static_cast<int>(side)]; }
    void  set_offset(Side side, float value);
    float get_offset(Side side) const { return offsets_[static_cast<int>(side)]; }

    // 按当前尺寸重算 anchors + offsets 到指定预设（不改变实际位置/尺寸）。
    void  set_anchors_preset(LayoutPreset preset);

    // —— 几何（覆盖 CanvasItem：由锚点 + 父尺寸计算）——
    Vector2 get_position() const override;
    Vector2 get_size() const override;
    void    set_position(Vector2 p) override;  // 保持尺寸平移
    void    set_size(Vector2 s) override;      // 保持位置调整右下偏移
    void    set_rect(Rect2 r) override;        // 位置 + 尺寸一起设
    Rect2   get_rect() const override { return Rect2{get_position(), get_size()}; }

    // 顶层 Control（父非 Control）的视口尺寸，由 Window/SceneTree 注入。
    void    set_viewport_size(Vector2 s) { viewport_size_ = s; queue_redraw(); }
    Vector2 get_viewport_size() const { return viewport_size_; }

    // —— 最小尺寸（容器布局消费）——
    virtual Vector2 get_minimum_size() const { return Vector2{0, 0}; }
    Vector2 get_combined_minimum_size() const;  // 带缓存
    void    update_minimum_size();              // 标记失效 + 向父冒泡

    // —— 自绘入口 ——
    void draw_if_dirty() override;   // dirty 则 _draw() + 焦点高亮
    // _draw() 继承自 CanvasItem（默认空；派生控件覆盖以自绘）

    // —— 焦点（无障碍前置）——
    void      set_focus_mode(FocusMode m) { focus_mode_ = m; }
    FocusMode get_focus_mode() const { return focus_mode_; }
    void      set_focus(bool focus);
    bool      has_focus() const { return has_focus_; }
    // DFS 遍历下一个可聚焦控件（reverse=true 为上一个）。无则 nullptr。
    Control*  find_next_focus(bool reverse) const;

    // —— 无障碍 ——
    void              set_accessibility_label(const std::string& s) { a11y_label_ = s; }
    const std::string& get_accessibility_label() const { return a11y_label_; }

    // —— 类型查询 ——
    static std::string get_class_static() { return "Control"; }
    std::string get_class() const override { return "Control"; }
    bool is_class(const std::string& name) const override {
        return name == "Control" || CanvasItem::is_class(name);
    }

protected:
    // 内部通知占位（Task 1 仅占位；后续 Task 接信号分发 focus_enter/exit/resized）。
    virtual void _notification(int /*what*/) {}

private:
    float     anchors_[4] = {0.0f, 0.0f, 1.0f, 1.0f};  // L T R B，默认铺满父
    float     offsets_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    Vector2   viewport_size_{0, 0};
    FocusMode focus_mode_           = FocusMode::NONE;
    bool      has_focus_            = false;
    mutable bool     min_size_dirty_ = true;
    mutable Vector2  cached_min_size_{0, 0};
    std::string a11y_label_;

    // 父 Control 的 size 或退回 viewport_size_（顶层 Control 由 Window 注入）。
    Vector2 parent_size_() const;
};

}  // namespace eda
