// editor/command_palette/command_palette.h
//
// P4 Task 6 · 命令面板 UI（Ctrl+Shift+P 唤起的模糊搜索弹层）。
//
// 设计：
//   - 继承 Control（P4 Task 1）：复用锚点布局 / 焦点 / 自绘入口。
//   - 持有输入查询串 query_ 与排序后的匹配列表 matches_（CommandMatch = Command* + score）。
//   - 模糊匹配算法 fuzzy_match(query, text) -> FuzzyMatch{matched, score}：
//       * 子序列匹配（query 每字符按序出现在 text 中；任一未命中 → matched=false）。
//       * 加分项：首字符命中（text[0]）、词边界（前导分隔符）、连续相邻匹配、
//         整串前缀、精确等值；外加密度奖励（命中字符占比高者更优）。
//       * set_query 后按 score 降序取 top max_results_ 条。
//   - 键盘导航：move_selection_up / down（Up / Down；边界 clamp 不回绕）、
//     invoke_selected（Enter，执行选中命令 handler；handler 空的占位命令安全跳过）、
//     close（Esc 关闭弹层）。
//   - open / close：管理可见性；open 重置 query 与选中并取焦点。
//   - 依赖注入：set_registry(CommandRegistry*)，默认使用 CommandRegistry::get() 单例。
//
// 与 Godot editor/editor_command_palette.h 对齐（精简：未实现多模式前缀
// ">" / "@" / ":" / "#" 与最近使用加权，留待后续迭代）。

#pragma once

#include "editor/command_palette/command_registry.h"
#include "scene/gui/control.h"

#include <cstddef>
#include <string>
#include <vector>

namespace eda {

// ===========================================================================
// FuzzyMatch —— 模糊匹配结果。
// ===========================================================================
struct FuzzyMatch {
    bool   matched = false;  // query 是否为 text 的子序列
    double score   = 0.0;    // 越大越优（仅 matched=true 时有意义）
};

// 模糊匹配：subsequence + 连续加分 + 首字母 / 词边界加分 + 前缀 / 精确加分。
// 大小写不敏感（ASCII）。query 空 → matched=true, score=0（视为匹配全部，用于
// 命令面板"空查询展示全部命令"语义）。实现见 command_palette.cpp。
FuzzyMatch fuzzy_match(const std::string& query, const std::string& text);

// ===========================================================================
// CommandMatch —— 命令面板搜索结果项（命令 + 得分）。
// ===========================================================================
struct CommandMatch {
    const Command* command = nullptr;
    double         score   = 0.0;
};

// ===========================================================================
// CommandPalette —— 命令面板控件。
// ===========================================================================
class CommandPalette : public Control {
public:
    CommandPalette();
    ~CommandPalette() override = default;

    CommandPalette(const CommandPalette&) = delete;
    CommandPalette& operator=(const CommandPalette&) = delete;

    // —— 依赖注入：命令来源（默认 CommandRegistry::get() 单例）——
    void              set_registry(CommandRegistry* registry);
    CommandRegistry*  get_registry() const { return registry_; }

    // —— 查询 / 结果 ——
    // 设置查询串并重算 matches_（按 score 降序取 top max_results_）；选中重置为首项。
    void                    set_query(const std::string& query);
    const std::string&      get_query() const { return query_; }

    // 当前排序后的匹配结果（只读视图；下线字段在下次 set_query / set_registry 后失效）。
    const std::vector<CommandMatch>& get_matches() const { return matches_; }
    std::size_t                      get_match_count() const { return matches_.size(); }
    int                              get_selected_index() const { return selected_index_; }
    const Command*                  get_selected_command() const;

    // —— 键盘导航 ——
    // Up：选中上移一项（已为首项则不动）。返回新选中索引。
    int  move_selection_up();
    // Down：选中下移一项（已为末项则不动）。返回新选中索引。
    int  move_selection_down();

    // Enter：执行当前选中命令的 handler。
    // 返回是否真的触发（无选中 / handler 空 → false，不视为错误）。
    bool invoke_selected();

    // —— 弹层可见性 ——
    void open();    // 显示 + 重置 query / 选中 + 取焦点
    void close();   // Esc：隐藏 + 释放焦点
    bool is_open() const { return is_open_; }

    // —— 配置 ——
    void         set_max_results(std::size_t n) { max_results_ = n; }
    std::size_t  get_max_results() const { return max_results_; }

    // —— 类型查询（基于类名字符串，无 RTTI）——
    static std::string get_class_static() { return "CommandPalette"; }
    std::string        get_class() const override { return "CommandPalette"; }
    bool               is_class(const std::string& name) const override {
        return name == "CommandPalette" || Control::is_class(name);
    }

protected:
    // 自绘：弹层背景 + 查询行 + 结果列表（前若干条，选中项高亮）。
    // 仅在 is_open_ 时绘制；行为经公开方法验证，测试不依赖绘制输出。
    void _draw() override;

private:
    // 重算 matches_：对 registry_->all() 逐条 fuzzy_match（主字段 name，name 未命中
    // 时回落 id），过滤 matched，按 score 降序（同分按 name 字典序）稳定排序，
    // 截断到 max_results_，并把 selected_index_ clamp 到合法区间。
    void recompute_matches_();

    CommandRegistry*           registry_       = nullptr;
    std::string                query_;
    std::vector<CommandMatch>  matches_;
    int                        selected_index_ = 0;
    std::size_t                max_results_    = 16;
    bool                       is_open_        = false;
};

}  // namespace eda
