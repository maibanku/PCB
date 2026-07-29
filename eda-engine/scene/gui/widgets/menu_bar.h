// MenuBar —— 顶部菜单栏（P4 菜单系统）。
//
// 在 HBox（P4 Task 2 水平排列容器）之上叠加"多 Menu 编排 + 单 Popup 打开"策略：
//   - add_menu(Menu*)：把 Menu 挂为本 HBox 子节点（水平排列；Menu 由 Node 树
//     ownership 持有）。
//   - open_menu(Menu*) / click_menu_title(Menu*)：单开策略——先 close_all_popups，
//     再 open 给定 menu。保证同一时间最多一个 Popup 可见（其他自动 close）。
//   - close_all_popups() / handle_escape()：关闭全部 Popup（Esc 兜底）。
//   - handle_alt_mnemonic(int glfw_key)：Alt+<letter> 快捷打开标题首字母匹配的
//     Menu（Alt+F → "File"）；命中则 close_all + open 并返回 true。
//   - set_registry：递归下发到所有 Menu（依赖注入，便于测试覆盖）。
//
// 默认菜单结构：构造期注册 EDA 标准 7 个菜单（File / Edit / View / Place / Design /
// Tools / Help），其中 File/Edit/View 的 MenuItem 绑定 CommandRegistry 11 个内置
// 命令 id；Place/Design/Tools/Help 暂为占位（无内置命令，空 Popup）。
//   File  : New / Open / Save / Save As / --- / Close
//   Edit  : Undo / Redo
//   View  : Zoom In / Zoom Out / Zoom Fit
//
// 所有权：默认构造建 heap Menu + heap MenuItem，全部经 Node 树级联 delete。
// 测试栈用例若自行 add_menu / add_item 须遵循"MenuBar 先声明、Menu 次之、MenuItem
// 最后"约定（子先析构自删于父的 children_，避免 ~Node 级联 delete 栈对象双释放）。
//
// 对齐 Godot scene/gui/menu_bar.h（精简：无真正键盘焦点链 / help 搜索，留待后续）。

#pragma once

#include "scene/gui/container.h"  // HBox
#include "scene/gui/widgets/menu.h"

#include <vector>

namespace eda {

class CommandRegistry;

class MenuBar : public HBox {
public:
    MenuBar();
    ~MenuBar() override = default;

    MenuBar(const MenuBar&) = delete;
    MenuBar& operator=(const MenuBar&) = delete;

    // —— Menu 管理 ——
    // 把 menu 挂为本 HBox 子节点（水平排列）。menu 经 Node 树 ownership 持有。
    void add_menu(Menu* menu);

    int   get_menu_count() const;
    Menu* get_menu(int index) const;

    // —— 单 Popup 打开策略 ——
    // 关闭所有 Popup，再打开 m（nullptr 时仅 close_all）。返回是否成功 open。
    bool open_menu(Menu* m);

    // 点击 m 的标题（语义同 open_menu：close_all + open m）。
    bool click_menu_title(Menu* m);

    // 关闭所有 Popup。
    void close_all_popups();

    // —— 键盘调度 ——
    // Esc：关闭所有 Popup。
    void handle_escape();
    // Alt+<letter>：找到标题首字母（大小写不敏感）匹配的 Menu，close_all + open。
    // key 为 GLFW key code（大写字母 ASCII 区段）。命中返回 true。
    bool handle_alt_mnemonic(int key);

    // —— 依赖注入：递归下发 registry 到所有 Menu ——
    void set_registry(CommandRegistry* r);

    // —— 类型查询 ——
    static std::string get_class_static() { return "MenuBar"; }
    std::string        get_class() const override { return "MenuBar"; }
    bool               is_class(const std::string& name) const override {
        return name == "MenuBar" || HBox::is_class(name);
    }

private:
    // 构造期注册 EDA 标准 7 菜单结构（File/Edit/View/Place/Design/Tools/Help）。
    void register_default_menus_();

    // 便捷：创建一个 Menu 并挂入（heap，归本 HBox 所有）。
    Menu* new_menu_(const std::string& title);
    // 便捷：创建一个 MenuItem 并挂入指定 menu（heap，归 menu->popup_ 所有）。
    void  new_item_(Menu* menu, const std::string& text,
                    const std::string& command_id, const std::string& shortcut);
};

}  // namespace eda
