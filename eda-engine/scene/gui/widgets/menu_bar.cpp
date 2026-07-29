// MenuBar —— 实现（P4 菜单系统）。
//
// 要点：
//   - 默认构造调用 register_default_menus_ 建 7 菜单 + 内置命令绑定。
//   - open_menu / click_menu_title：先 close_all_popups 再 open(m)，保证单开。
//   - handle_alt_mnemonic：按标题首字母（大小写不敏感）匹配；GLFW key code 中
//     字母键与 ASCII 大写一致（GLFW_KEY_A = 65），故统一转大写比较。
//   - set_registry：递归下发到所有子 Menu（菜单项在 activated 槽里读 registry_）。
//   - 栈用例须遵循"MenuBar 先声明、Menu 次之、MenuItem 最后"约定（与 HBox /
//     Container 一致，避免 ~Node 级联 delete 栈对象双释放）。

#include "scene/gui/widgets/menu_bar.h"

#include <algorithm>
#include <cctype>

#include "editor/command_palette/command_registry.h"

namespace eda {

MenuBar::MenuBar() {
    set_spacing(2.0f);
    register_default_menus_();
}

// —— 默认菜单结构 ——

void MenuBar::register_default_menus_() {
    // File：5 个内置命令 + separator（Save As 与 Close 间）。
    Menu* file_menu = new_menu_("File");
    new_item_(file_menu, "New Project",  builtin::FILE_NEW,     "Ctrl+N");
    new_item_(file_menu, "Open File...", builtin::FILE_OPEN,    "Ctrl+O");
    new_item_(file_menu, "Save File",    builtin::FILE_SAVE,    "Ctrl+S");
    new_item_(file_menu, "Save As...",   builtin::FILE_SAVE_AS, "Ctrl+Shift+S");
    file_menu->add_separator();
    new_item_(file_menu, "Close File",   builtin::FILE_CLOSE,   "Ctrl+W");

    // Edit：Undo / Redo。
    Menu* edit_menu = new_menu_("Edit");
    new_item_(edit_menu, "Undo", builtin::EDIT_UNDO, "Ctrl+Z");
    new_item_(edit_menu, "Redo", builtin::EDIT_REDO, "Ctrl+Y");

    // View：Zoom In / Zoom Out / Zoom Fit。
    Menu* view_menu = new_menu_("View");
    new_item_(view_menu, "Zoom In",    builtin::VIEW_ZOOM_IN,  "Ctrl++");
    new_item_(view_menu, "Zoom Out",   builtin::VIEW_ZOOM_OUT, "Ctrl+-");
    new_item_(view_menu, "Zoom to Fit", builtin::VIEW_ZOOM_FIT, "");

    // Place / Design / Tools / Help：占位（无内置命令；空 Popup）。
    new_menu_("Place");
    new_menu_("Design");
    new_menu_("Tools");
    new_menu_("Help");
}

Menu* MenuBar::new_menu_(const std::string& title) {
    auto* m = new Menu();
    m->set_title(title);
    add_menu(m);
    return m;
}

void MenuBar::new_item_(Menu* menu, const std::string& text,
                        const std::string& command_id,
                        const std::string& shortcut) {
    auto* item = new MenuItem();
    item->set_text(text);
    item->set_command_id(command_id);
    if (!shortcut.empty()) {
        item->set_shortcut_text(shortcut);
    }
    menu->add_item(item);
}

// —— Menu 管理 ——

void MenuBar::add_menu(Menu* menu) {
    if (menu == nullptr) return;
    // 若已挂在别处先剥离（CanvasItem::set_parent 内部处理）。
    if (menu->get_parent() != this) {
        if (menu->get_parent() != nullptr) {
            menu->get_parent()->remove_child(menu);
        }
        add_child(menu);  // HBox::add_child → Container::add_child → Node::add_child
    }
    queue_sort();
}

int MenuBar::get_menu_count() const {
    int n = 0;
    for (Node* c : get_children()) {
        if (dynamic_cast<Menu*>(c) != nullptr) ++n;
    }
    return n;
}

Menu* MenuBar::get_menu(int index) const {
    int i = 0;
    for (Node* c : get_children()) {
        if (auto* m = dynamic_cast<Menu*>(c)) {
            if (i == index) return m;
            ++i;
        }
    }
    return nullptr;
}

// —— 单 Popup 打开策略 ——

bool MenuBar::open_menu(Menu* m) {
    close_all_popups();
    if (m == nullptr) return false;
    m->open();
    return true;
}

bool MenuBar::click_menu_title(Menu* m) {
    return open_menu(m);
}

void MenuBar::close_all_popups() {
    for (Node* c : get_children()) {
        if (auto* m = dynamic_cast<Menu*>(c)) {
            m->close_popup();
        }
    }
}

// —— 键盘调度 ——

void MenuBar::handle_escape() {
    close_all_popups();
}

bool MenuBar::handle_alt_mnemonic(int key) {
    // GLFW 字母键 code 与 ASCII 大写一致（A=65..Z=90）。统一转大写比较。
    if (key < 'A' || key > 'Z') {
        // 允许传入小写 ASCII（a=97..z=122）也兼容。
        if (key >= 'a' && key <= 'z') {
            key = std::toupper(static_cast<unsigned char>(key));
        } else {
            return false;
        }
    }
    char wanted = static_cast<char>(key);
    for (Node* c : get_children()) {
        auto* m = dynamic_cast<Menu*>(c);
        if (m == nullptr) continue;
        const std::string& t = m->get_title();
        if (!t.empty()) {
            char first = static_cast<char>(
                std::toupper(static_cast<unsigned char>(t.front())));
            if (first == wanted) {
                open_menu(m);
                return true;
            }
        }
    }
    return false;
}

// —— 依赖注入 ——

void MenuBar::set_registry(CommandRegistry* r) {
    for (Node* c : get_children()) {
        if (auto* m = dynamic_cast<Menu*>(c)) {
            m->set_registry(r);
        }
    }
}

}  // namespace eda
