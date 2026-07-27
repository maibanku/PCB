// DockPanel —— 可停靠面板基类（P4 Task 4）。
//
// 在 Control（P4 Task 1：锚点 / 几何 / 最小尺寸 / 焦点 / 自绘）之上叠加"编辑器
// 面板外壳"：标题栏（标题文本 + 关闭/浮动按钮）+ 内容区（host 一个 Control）。
//
// 职责边界（对齐 Godot editor/dock/dock_panel.h）：
//   - 自身只绘制标题栏与外壳边框、维护标题 / 图标 / 内容指针；
//   - 不参与停靠命中检测 / 拖拽预览 / 区域布局（归 DockManager）；
//   - 不持有浮动窗口实体（float 简化为状态标记，由 DockManager 消费）。
//
// 内容区托管：
//   - set_content(c) 把 c 挂为本面板子节点（c->set_parent(this)），并在面板
//     resize 时把 c 锁定到标题栏下方的内矩形（anchors 全置 0 + set_position/
//     set_size，对齐 Container::fit_child_in_rect 的绝对像素锁定语义）。
//   - 旧内容若仍是本面板子节点，先 detach（set_parent(nullptr)），不 delete
//     （调用方拥有 c 的生命周期；典型为栈对象或由 DockManager 注册表管理）。
//
// 关闭 / 浮动按钮：
//   - 框架只维护"请求标志"（close_requested_ / float_requested_），由 DockManager
//     在主循环或 layout_changed 信号槽里消费（实际 undock / float_panel）。
//     这样面板自身不反向依赖 DockManager，保持单向依赖。
//
// 显示 / 隐藏（F2 切换显隐）：
//   - visible_ 为面板级状态标志（CanvasItem 暂无 visible 成员）。DockManager
//     布局时跳过 visible_=false 的面板（不计入 area 排列）。

#pragma once

#include "scene/gui/control.h"

#include <string>

namespace eda {

class DockPanel : public Control {
public:
    DockPanel();
    ~DockPanel() override = default;

    DockPanel(const DockPanel&) = delete;
    DockPanel& operator=(const DockPanel&) = delete;

    // —— 标题 ——
    void              set_title(const std::string& title);
    const std::string& get_title() const { return title_; }

    // —— 图标 token（主题查表键名，如 "layers" / "properties"）——
    void              set_icon(const std::string& icon);
    const std::string& get_icon() const { return icon_; }

    // —— 内容区托管 ——
    // 挂入 c 作为面板子节点（占用标题栏下方整个内矩形）。
    // 传入 nullptr 仅剥离旧内容。c 的生命周期归调用方。
    void     set_content(Control* c);
    Control* get_content() const { return content_; }

    // —— 显示 / 隐藏（F2 切换；DockManager 布局跳过隐藏面板）——
    void set_visible(bool v);
    bool is_visible() const { return visible_; }

    // —— 关闭 / 浮动按钮请求（框架置位，DockManager 消费）——
    void request_close();
    bool is_close_requested() const { return close_requested_; }
    void clear_close_request() { close_requested_ = false; }

    void request_float();
    bool is_float_requested() const { return float_requested_; }
    void clear_float_request() { float_requested_ = false; }

    // —— 标题栏几何 ——
    void  set_title_bar_height(float h);
    float get_title_bar_height() const { return title_bar_height_; }

    // 最小尺寸：标题栏高度 + 内容最小尺寸（若有）。
    Vector2 get_minimum_size() const override;

    // —— 类型查询（基于类名字符串，无 RTTI）——
    static std::string get_class_static() { return "DockPanel"; }
    std::string get_class() const override { return "DockPanel"; }
    bool is_class(const std::string& name) const override {
        return name == "DockPanel" || Control::is_class(name);
    }

protected:
    // 自身尺寸变化 → 重排内容到新内矩形。
    void _notification(int what) override;
    // 自绘：标题栏背景 + 标题文本 + 边框（颜色经 ThemeManager 查表，禁硬编码 RGB）。
    void _draw() override;

private:
    std::string title_;
    std::string icon_;
    Control*    content_          = nullptr;
    bool        visible_          = true;
    bool        close_requested_  = false;
    bool        float_requested_  = false;
    float       title_bar_height_ = 24.0f;

    // 把 content_ 锁定到标题栏下方的内矩形（绝对像素，不随父尺寸拉伸）。
    void layout_content_();
};

}  // namespace eda
