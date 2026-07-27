// DockManager —— 五区域可停靠布局管理器（P4 Task 4）。
//
// 在 Container（P4 Task 2：queue_sort / sort_children / fit_child_in_rect /
// _layout_children）之上叠加类 Godot/VS 的"边框布局"：
//   - 五区域 DockArea：LEFT / TOP / RIGHT / BOTTOM / CENTER
//   - 每个区域由一个 Container 承载（LEFT/RIGHT 用 VBox；TOP/BOTTOM 用 HBox；
//     CENTER 用 Container 单子铺满）。DockManager 自身继承 Container，把五个
//     区域 Container 当作子节点排列，区域 Container 再排布其内部 docked 面板。
//   - 几何：border layout——LEFT/RIGHT 占左右整列（宽 = area_sizes_），
//     TOP/BOTTOM 占左右列之间的顶/底行（高 = area_sizes_），CENTER 填剩余中心。
//
// 面板注册表：
//   - register_panel(id, DockPanel*) 把面板登记到 id->panel 表（用于 layout 持久化）。
//   - dock(area, panel) 把面板挂入对应区域 Container（reparent 自动从旧区域剥离）。
//     未注册的面板会被自动以 "panel_<n>" 注册，便于即插即用。
//   - undock(panel) 从任何区域剥离，置 UNDOCKED 状态（隐藏，仍由注册表跟踪）。
//   - float_panel(panel) 置 FLOATING 状态（拖出主窗口；本 Task 简化为状态标记，
//     不创建真实浮动窗口实体——仅从区域布局中剔除并标记，下游可据此渲染浮层）。
//
// 布局持久化（.layout.toml）：
//   - save_layout(path) 写文件并返回内容；serialize_layout() 仅返回字符串（无 IO）。
//   - load_layout(path) / deserialize_layout(text) 恢复区域尺寸 + 每面板的
//     area / visible / floating / size。load 需要面板已注册（按 id 查表）。
//
// 信号：
//   - layout_changed：dock / undock / float_panel / set_area_size /
//     set_panel_visible 触发，UI 层 connect 后重排或持久化。

#pragma once

#include "editor/dock/dock_panel.h"
#include "scene/gui/container.h"
#include "core/object/signal.h"

#include <string>
#include <vector>

namespace eda {

// 停靠区域（索引用于 area_sizes_[5] / areas_[5]）。
enum class DockArea {
    LEFT   = 0,
    TOP    = 1,
    RIGHT  = 2,
    BOTTOM = 3,
    CENTER = 4,
};

// 面板在管理器中的放置状态。
enum class DockState {
    UNDOCKED,  // 未放置（默认 / undock 后）
    DOCKED,    // 在某区域内（area_ 有效）
    FLOATING,  // 浮动（拖出主窗口，简化为状态标记）
};

class DockManager : public Container {
public:
    DockManager();
    ~DockManager() override = default;

    DockManager(const DockManager&) = delete;
    DockManager& operator=(const DockManager&) = delete;

    // —— 注册表 ——
    // 以 id 登记面板（不放置）。重复 id / 已注册面板会被拒绝（幂等）。
    void        register_panel(const std::string& id, DockPanel* panel);
    DockPanel*  get_panel(const std::string& id) const;
    std::string get_panel_id(const DockPanel* panel) const;
    size_t      get_registered_count() const { return records_.size(); }

    // —— 停靠 / 卸下 / 浮动 ——
    // 挂入指定区域（未注册则自动以 "panel_<n>" 注册）。
    void dock(DockArea area, DockPanel* panel);
    // 从任何区域剥离（仍由注册表跟踪；visible 保持）。
    void undock(DockPanel* panel);
    // 标记为浮动（从区域布局中剔除；不创建真实窗口实体）。
    void float_panel(DockPanel* panel);

    // —— 状态查询 ——
    DockState get_state(const DockPanel* panel) const;
    DockArea  get_area_of(const DockPanel* panel) const;       // 仅 DOCKED 时有意义
    bool      is_floating(const DockPanel* panel) const;
    bool      is_panel_visible(const DockPanel* panel) const;
    void      set_panel_visible(DockPanel* panel, bool visible);

    std::vector<DockPanel*> get_panels_in(DockArea area) const;       // 含隐藏
    std::vector<DockPanel*> get_visible_panels_in(DockArea area) const;
    std::vector<DockPanel*> get_floating_panels() const;

    // —— 区域尺寸（LEFT/RIGHT = 宽；TOP/BOTTOM = 高；CENTER 忽略）——
    void  set_area_size(DockArea area, float size);
    float get_area_size(DockArea area) const;

    // —— 布局持久化 ——
    // 写 .layout.toml 到 path，同时返回文件内容。
    std::string save_layout(const std::string& path) const;
    // 从 path 读 .layout.toml 并恢复（区域尺寸 + 每面板 area/visible/floating/size）。
    // 返回是否成功打开并完整解析。未注册的 panel id 会被跳过（不报错）。
    bool load_layout(const std::string& path);

    // 字符串版（无文件 IO；测试友好）。
    std::string serialize_layout() const;
    bool        deserialize_layout(const std::string& text);

    // —— 信号 ——
    // dock / undock / float_panel / set_area_size / set_panel_visible 均触发。
    Signal<>& layout_changed() { return layout_changed_; }

    // —— 类型查询 ——
    static std::string get_class_static() { return "DockManager"; }
    std::string get_class() const override { return "DockManager"; }
    bool is_class(const std::string& name) const override {
        return name == "DockManager" || Container::is_class(name);
    }

protected:
    // border layout：计算五区域 Rect 并 fit_child_in_rect 到对应 area Container，
    // 再级联 area->sort_children() 排布内部面板。
    void _layout_children() override;

private:
    struct PanelRecord {
        std::string id;
        DockPanel*  panel   = nullptr;
        DockArea    area    = DockArea::CENTER;  // 仅 DOCKED 时有效
        DockState   state   = DockState::UNDOCKED;
        bool        visible = true;
        float       size    = 0.0f;  // 面板在区域内的偏好尺寸（用户拖拽分隔条）
    };

    std::vector<PanelRecord>                           records_;
    Container*                                         areas_[5]   = {};
    float                                              area_sizes_[5] = {200.0f, 80.0f, 200.0f, 80.0f, 0.0f};
    Signal<>                                           layout_changed_;

    // 注册表查询（非常/常版本）。
    PanelRecord*       find_record_(DockPanel* panel);
    const PanelRecord* find_record_(const DockPanel* panel) const;
    PanelRecord*       find_record_by_id_(const std::string& id);
    std::string        next_auto_id_() const;

    // 在构造期创建五个区域 Container 并挂为本面板子节点（manager 拥有 areas）。
    void build_areas_();
    // border layout：返回某区域的像素 Rect。
    Rect2 compute_area_rect_(DockArea a) const;
    // 把面板 reparent 到指定区域 Container。
    void  reparent_to_area_(DockArea area, DockPanel* panel);
};

}  // namespace eda
