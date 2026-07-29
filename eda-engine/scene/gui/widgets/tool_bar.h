// ToolBar —— 水平按钮组（P4 widgets · 编辑器顶栏）。
//
// 继承 HBox：每个按钮 / 分隔符 add_child 为本容器子节点，由 BoxContainer 一维排列
// 算法自动布局（spacing / alignment / size_flag 全复用 HBox 语义）。
//
// 命令绑定（核心）：
//   - add_button(text, command_id) 创建 Button，把 clicked 信号连接到一个 lambda：
//       clicked → CommandRegistry::get().get(command_id)->handler()
//     即按钮点击 = 命令执行。命令面板（Ctrl+Shift+P）/ 快捷键 / 工具栏按钮共用同一
//     Command handler，单一真相源。命令缺失或 handler 空（占位）时安全跳过。
//   - 查表发生在点击时刻而非注册时刻：业务后续覆盖 handler（同 id 重新 register_command）
//     无需重新绑按钮，立即生效。
//
// 构造期注册常用工具（对齐任务规范）：
//   [New][Open][Save] │ [Undo][Redo] │ [Zoom+][Zoom-][Fit]
//   即 file.{new,open,save} / edit.{undo,redo} / view.{zoom_in,zoom_out,zoom_fit}
//   + 两组分隔符。内置 id 常量见 editor/command_palette/command_registry.h namespace builtin。
//
// _draw：背景铺满 secondary_background token（与任务配色规范一致）。
//
// 所有权：add_button / add_separator 在堆上 new 子控件，经 Container::add_child 挂树；
// 父 ToolBar 析构（~Node 级联 delete children）时一并释放。buttons_ / separators_ /
// by_command_ 仅持有非拥有指针，成员先于 base Node 析构，无悬空访问。

#pragma once

#include "scene/gui/container.h"  // HBox
#include "scene/gui/widgets/separator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eda {

class Button;

class ToolBar : public HBox {
public:
    ToolBar();

    ToolBar(const ToolBar&) = delete;
    ToolBar& operator=(const ToolBar&) = delete;

    // —— 添加按钮：点击时调 CommandRegistry::get().get(command_id)->handler() ——
    // 返回新建按钮指针（ToolBar 经 Node 树拥有；调用方不得 delete）。
    Button* add_button(const std::string& text, const std::string& command_id);

    // —— 添加分隔符（垂直 1px 线，配合 spacing 形成视觉分组）——
    Separator* add_separator();

    // —— 按索引访问按钮（含构造期 8 个默认按钮）；越界返回 nullptr ——
    Button* get_button(int index) const;

    // 按 command_id 查按钮；未绑定返回 nullptr。
    Button* find_button(const std::string& command_id) const;

    int get_button_count() const { return static_cast<int>(buttons_.size()); }
    int get_separator_count() const { return static_cast<int>(separators_.size()); }

    // —— 类型查询（基于类名字符串，无 RTTI）——
    static std::string get_class_static() { return "ToolBar"; }
    std::string get_class() const override { return "ToolBar"; }
    bool is_class(const std::string& name) const override {
        return name == "ToolBar" || HBox::is_class(name);
    }

protected:
    void _draw() override;

private:
    std::vector<Button*>    buttons_;     // 非拥有；按 add 顺序
    std::vector<Separator*> separators_;  // 非拥有
    std::unordered_map<std::string, Button*> by_command_;  // 非拥有；command_id → button
};

}  // namespace eda
