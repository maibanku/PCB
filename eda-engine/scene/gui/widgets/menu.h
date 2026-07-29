// Menu —— 下拉菜单控件（P4 菜单系统）。
//
// 在 Control 之上叠加"菜单标题 + 浮动下拉 Popup"两层结构：
//   - title_：菜单栏上的标题文本（"File" / "Edit" / ...），由 MenuBar 水平排列。
//   - popup_：下拉弹层（VBox），垂直排列 MenuItem 列表。heap 分配、经 add_child
//     挂为 Menu 子节点，由 Menu 经 Node 树 ownership 持有（~Node 级联 delete）。
//     z_index 抬高（语义上的"浮在所有控件之上"；当前 DFS 渲染不依 z_index 排序，
//     但保留语义供后续 z-sort 渲染器消费）。
//   - is_open_：Popup 可见性。open()/close() 切换 + 重排 popup_ 到标题栏正下方。
//   - add_item(MenuItem*)：把 item 挂入 popup_，并连接其 activated 信号到
//     invoke_command_(item)——后者查 CommandRegistry::get(command_id).handler() 执行，
//     最后 close()。
//   - add_separator()：新建 separator MenuItem（heap，归 popup_ 所有）并返回。
//
// 边界：
//   - Menu 不持有 add_item 传入的 MenuItem 生命周期（与 DockPanel::set_content 一致：
//     调用方拥有）。栈用例须遵循"Menu 先于 MenuItem 声明"约定（子先析构自删于父的
//     children_，避免 ~Node 级联 delete 栈对象的双释放）。
//   - Alt+F / Esc 等键盘关闭由 MenuBar 统一调度（Menu 自身只暴露 open/close）。
//
// 对齐 Godot scene/gui/popup_menu.h + scene/gui/menu_button.h（精简：无 submenu /
// 多级嵌套 / checkable，留待后续 Task）。

#pragma once

#include "scene/gui/control.h"
#include "scene/gui/widgets/menu_item.h"

#include <string>

namespace eda {

class CommandRegistry;
class VBox;

class Menu : public Control {
public:
    Menu();
    ~Menu() override = default;

    Menu(const Menu&) = delete;
    Menu& operator=(const Menu&) = delete;

    // —— 标题（菜单栏上显示的文本，如 "File"）——
    void              set_title(const std::string& title);
    const std::string& get_title() const { return title_; }

    // —— 标题栏几何 ——
    void  set_title_bar_height(float h);
    float get_title_bar_height() const { return title_bar_height_; }

    // —— Popup 可见性 ——
    void open();     // 显示 popup + 重排到标题栏下方
    void close();    // 隐藏 popup
    bool is_open() const { return is_open_; }

    // —— MenuBar 单开策略：强制关闭（即使当前未开也幂等）——
    void close_popup();

    // —— 菜单项管理 ——
    // 把 item 挂入 popup_（不接管 ownership；调用方拥有 item 生命周期）+ 连接
    // activated 信号到 invoke_command_。重复添加同一 item 仅生效一次。
    void add_item(MenuItem* item);

    // 新建一个 separator MenuItem（heap，归 popup_ 所有），挂入 popup_ 并返回。
    MenuItem* add_separator();

    // —— Popup 直接访问（布局 / 测试用）——
    VBox* get_popup() const { return popup_; }

    // popup_ 当前 MenuItem 列表（跳过非 MenuItem 子节点；items 在其脱离 popup_
    // 后即不再返回，避免悬空指针）。
    std::vector<MenuItem*> get_items() const;
    int                    get_item_count() const;

    // —— 命令注册中心（默认进程单例；测试可用 set_registry 注入本地 registry）——
    void               set_registry(CommandRegistry* r);
    CommandRegistry*   get_registry() const { return registry_; }

    // —— 最小尺寸：仅标题栏（popup 浮动在外，不计入）——
    Vector2 get_minimum_size() const override;

    // —— 类型查询 ——
    static std::string get_class_static() { return "Menu"; }
    std::string        get_class() const override { return "Menu"; }
    bool               is_class(const std::string& name) const override {
        return name == "Menu" || Control::is_class(name);
    }

protected:
    void _draw() override;
    void _notification(int what) override;

private:
    std::string title_;
    VBox*       popup_             = nullptr;  // heap，经 add_child 由 Node 树持有
    bool        is_open_           = false;
    float       title_bar_height_  = 22.0f;
    CommandRegistry* registry_     = nullptr;

    // 把 popup_ 锁定到标题栏正下方的绝对像素矩形（不随 Menu 尺寸拉伸）。
    void layout_popup_();
    // activated 信号槽：查 registry_ 执行 handler → close。
    void invoke_command_(MenuItem* item);
};

}  // namespace eda
