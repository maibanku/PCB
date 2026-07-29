// Menu —— 实现（P4 菜单系统）。
//
// 要点：
//   - 构造期 heap 分配 popup_（VBox），挂为 Menu 子节点；默认绑进程单例 registry。
//   - add_item：reparent 到 popup_（若尚未挂入），并连接 activated 信号到
//     invoke_command_。用 lambda 捕获 item 指针以在无参 Signal<> 中识别来源。
//   - invoke_command_：command_id 非空 + registry 可用时查 handler 执行；之后 close。
//     handler 空或 command 缺失视为安全跳过（与 CommandPalette 一致），仍 close。
//   - layout_popup_：popup_ 锚点全置 0，set_position/set_size 锁定到标题栏下方；
//     宽度取 max(标题栏宽, items 最小宽)，高度取 items 最小高；调 sort_children
//     让 VBox 重排 MenuItem 垂直列表。
//   - _draw：标题栏背景 + 标题文本；is_open_ 时再画 popup 底色（item 由各自 _draw
//     自绘）。颜色全部查 ThemeManager 表，禁硬编码。

#include "scene/gui/widgets/menu.h"

#include <algorithm>

#include "editor/command_palette/command_registry.h"
#include "scene/gui/container.h"  // VBox
#include "scene/gui/theme.h"

namespace eda {

namespace {

// popup_ z_index：语义上的"浮在所有控件之上"（当前 DFS 渲染不依 z_index 排序，
// 但保留语义供后续 z-sort 渲染器消费；对齐 Godot PopupMenu 默认 z_index）。
constexpr int    kPopupZIndex      = 100;
// popup_ 与 items 的留白：上下 / 左右内边距。
constexpr float kPopupPaddingY     = 4.0f;
constexpr float kPopupMinWidth     = 120.0f;

}  // namespace

Menu::Menu() {
    // 默认绑进程单例（与 CommandPalette 一致）；测试可经 set_registry 注入本地。
    registry_ = &CommandRegistry::get();

    // heap 分配 popup_（VBox），挂为 Menu 子节点；~Node 级联 delete。
    popup_ = new VBox();
    popup_->set_z_index(kPopupZIndex);
    add_child(popup_);
}

// —— 标题 ——

void Menu::set_title(const std::string& title) {
    if (title_ == title) return;
    title_ = title;
    update_minimum_size();
    queue_redraw();
}

void Menu::set_title_bar_height(float h) {
    if (title_bar_height_ == h) return;
    title_bar_height_ = h;
    update_minimum_size();
    if (is_open_) layout_popup_();
    queue_redraw();
}

// —— Popup 可见性 ——

void Menu::open() {
    if (is_open_) return;
    is_open_ = true;
    layout_popup_();
    queue_redraw();
}

void Menu::close() {
    if (!is_open_) return;
    is_open_ = false;
    queue_redraw();
}

void Menu::close_popup() {
    // 幂等强制关闭（MenuBar 单开策略调用）。
    is_open_ = false;
    queue_redraw();
}

// —— 菜单项管理 ——

void Menu::add_item(MenuItem* item) {
    if (item == nullptr) return;
    // 仅在尚未挂入时 reparent 到 popup_（避免重复 add_child 触发环检测拒绝）。
    if (item->get_parent() != popup_) {
        // 若此前挂在别处，先剥离（CanvasItem::set_parent 自动处理）。
        if (item->get_parent() != nullptr) {
            item->get_parent()->remove_child(item);
        }
        popup_->add_child(item);
    }
    // 连接 activated：lambda 捕获 item 指针，槽内查 registry_ + close。
    // 连接幂等：仅在该 item 尚无任何连接时建立（首次 add_item）；避免重复 add_item
    // 产生多个等效连接导致 invoke_command_ 多次触发。
    if (item->activated().connection_count() == 0) {
        MenuItem* item_ptr = item;
        item->activated().connect([this, item_ptr]() { invoke_command_(item_ptr); });
    }
    if (is_open_) layout_popup_();
    queue_redraw();
}

MenuItem* Menu::add_separator() {
    auto* sep = new MenuItem();
    sep->set_separator(true);
    popup_->add_child(sep);  // heap，归 popup_ 所有（~Node 级联 delete）
    if (is_open_) layout_popup_();
    queue_redraw();
    return sep;
}

std::vector<MenuItem*> Menu::get_items() const {
    std::vector<MenuItem*> out;
    if (popup_ == nullptr) return out;
    for (Node* n : popup_->get_children()) {
        if (auto* mi = dynamic_cast<MenuItem*>(n)) {
            out.push_back(mi);
        }
    }
    return out;
}

int Menu::get_item_count() const {
    return static_cast<int>(get_items().size());
}

// —— 命令注册中心 ——

void Menu::set_registry(CommandRegistry* r) {
    registry_ = r;
    queue_redraw();
}

// —— 最小尺寸 ——

Vector2 Menu::get_minimum_size() const {
    // 宽：标题文本估值 + 左右内边距；高：标题栏高度。
    constexpr float kCharWidth = 14.0f * 0.5f;
    float title_w = kCharWidth * static_cast<float>(title_.size());
    return Vector2{title_w + 16.0f, title_bar_height_};
}

// —— 通知 ——

void Menu::_notification(int what) {
    Control::_notification(what);
    if (what == NOTIFICATION_RESIZED) {
        if (is_open_) layout_popup_();
    }
}

// —— 自绘 ——

void Menu::_draw() {
    auto* cv = current_canvas();
    if (cv == nullptr) return;

    const auto& theme = ThemeManager::get().get_current();
    Vector2     s = get_size();
    if (s.x <= 0.0f || s.y <= 0.0f) return;

    // —— 标题栏背景（hover / open 用 highlight；默认 secondary_background）——
    Color title_bg = is_open_ ? theme.get_color("highlight")
                              : theme.get_color("secondary_background");
    Color title_fg = is_open_ ? theme.get_color("highlight_foreground")
                              : theme.get_color("foreground");
    Rect2 title_bar{{0.0f, 0.0f}, {s.x, title_bar_height_}};
    cv->draw_rect(title_bar, title_bg, /*filled=*/true);

    // 标题文本（左对齐 8px 内边距）。
    if (!title_.empty()) {
        cv->draw_text(title_, {8.0f, 4.0f}, title_fg);
    }

    // —— Popup 底色（仅 is_open_ 时；item 由各自 _draw 自绘）——
    if (is_open_ && popup_ != nullptr) {
        Vector2 pp = popup_->get_position();
        Vector2 ps = popup_->get_size();
        if (ps.x > 0.0f && ps.y > 0.0f) {
            Rect2 popup_rect{pp, ps};
            cv->draw_rect(popup_rect, theme.get_color("secondary_background"),
                          /*filled=*/true);
            // 1px 边框勾勒 popup 轮廓。
            cv->draw_rect(popup_rect, theme.get_color("border"),
                          /*filled=*/false);
        }
    }
}

// —— 内部：布局 popup_ ——

void Menu::layout_popup_() {
    if (popup_ == nullptr) return;

    // 计算 items 的最小尺寸（VBox 聚合）。
    Vector2 items_min = popup_->get_combined_minimum_size();
    Vector2 menu_size = get_size();

    // 宽：max(标题栏宽, items 最小宽)；保底 kPopupMinWidth。
    float popup_w = std::max({menu_size.x, items_min.x, kPopupMinWidth});
    // 高：items 最小高 + 上下内边距。
    float popup_h = items_min.y + kPopupPaddingY * 2.0f;

    Rect2 popup_rect{{0.0f, title_bar_height_}, {popup_w, popup_h}};

    // 锚点全置 0：popup 几何脱离 Menu 尺寸（绝对像素，不被二次拉伸）。
    popup_->set_anchor(Side::LEFT, 0.0f);
    popup_->set_anchor(Side::TOP, 0.0f);
    popup_->set_anchor(Side::RIGHT, 0.0f);
    popup_->set_anchor(Side::BOTTOM, 0.0f);
    popup_->set_position(popup_rect.pos);
    popup_->set_size(popup_rect.size);

    // 内边距：把 popup 的子（items）整体下移 padding；VBox 当前无 margin 成员，
    // 通过 sort_children 触发重排即可——items 在 popup_ 内按 VBox 算法从 (0,0)
    // 起排，配合 popup_ 的绝对位置已足够（内边距作为后续 Task 的 polish）。
    popup_->queue_sort();
}

// —— 内部：activated 槽（查 registry + close）——

void Menu::invoke_command_(MenuItem* item) {
    if (item != nullptr && registry_ != nullptr && !item->get_command_id().empty()) {
        if (const Command* cmd = registry_->get(item->get_command_id())) {
            if (cmd->handler) {
                cmd->handler();
            }
            // handler 空或 command 缺失视为安全跳过（与 CommandPalette 一致），
            // 仍执行 close 以保持"点一下就关"的交互预期。
        }
    }
    close();
}

}  // namespace eda
