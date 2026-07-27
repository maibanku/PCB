#pragma once

// eda/command/undo_stack.h
//
// P3 命令栈 · Task 8：UndoStack —— 事务化撤销/重做栈。
//
// 对应 plan「几何与命令栈实现计划.md」Task 8 / 子功能 README「08-命令栈与选择」。
//
// 核心能力：
//   * push(unique_ptr<Command>)：执行并入栈，自动截断 redo 历史。
//   * undo() / redo() / can_undo() / can_redo()：单步往返。
//   * begin_macro(name) / end_macro()：事务化合并——宏内多次 push 在 end_macro
//     时合并为单个撤销单元（典型：一次"批量移动"包含多个图元的位移）。
//     宏可嵌套，内层宏作为子命令并入外层。
//   * set_merge_enabled(bool)：开启后，push 会尝试与栈顶 merge_with 合并，
//     用于连续同类微编辑（逐帧拖动）合并为单步。
//   * 内存上限：默认 100 步，超限淘汰最旧；set_memory_limit(0) 表示无限。
//   * clean_index / is_clean / set_clean：保存点脏检测（"文件已保存"标记）。
//   * changed 信号：任何 push/undo/redo/clear 触发，UI 据此刷新 Ctrl+Z/Y 菜单态。
//
// 内存上限淘汰策略（关键不变量）：
//   * 仅当 commands_.size() > memory_limit_ 时触发，每次循环淘汰 commands_[0]。
//   * 若被淘汰命令"已应用"（index_ > 0）：它已永久落地到 live 对象，无法撤销回
//     其 execute() 之前；index_ 与 clean_index_ 同步前移（>0 则减 1，==0 则 clean
//     丢失——初始态已不可达）。
//   * 若被淘汰命令"未应用"（index_ == 0）：它是 redo 历史，淘汰后无法重做；
//     clean_index_==0（初始态）保留，clean_index_>0 因依赖被淘汰命令而丢失。
//   * 截断 redo 历史（push 新分支）时，clean_index_ 若落在被截断区间则丢失。
//
// 不可拷贝、不可移动：内部持有 Signal<>，Signal 必须在稳定地址上长期存在
// （Object 的 connected_signals_ 按 SignalBase* 地址追踪）。

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "core/object/signal.h"
#include "eda/command/command.h"

namespace eda::command {

class UndoStack {
public:
    // 默认内存上限（步数）。超限淘汰最旧。0 表示无限。
    static constexpr std::size_t kDefaultMemoryLimit = 100;
    // clean_index_ 的"已丢失"哨兵：保存点无法再通过 undo/redo 到达。
    static constexpr std::size_t kNoCleanIndex = static_cast<std::size_t>(-1);

    UndoStack() = default;
    ~UndoStack() = default;

    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;
    UndoStack(UndoStack&&) = delete;
    UndoStack& operator=(UndoStack&&) = delete;

    // -------- push / undo / redo --------

    // 执行 cmd 并入栈，取得所有权。空指针忽略。
    // 若在 macro 内：execute 后收入最内层 macro，不进主栈。
    // 若 merge 开启且栈顶合并成功：execute 后吸收，不进主栈。
    // 否则：截断 redo 历史 → 入栈 → 强制内存上限。
    void push(std::unique_ptr<Command> cmd);

    // 撤销一步。返回 false 表示无可撤销或处于 macro 中。
    bool undo();

    // 重做一步。返回 false 表示无可重做或处于 macro 中。
    bool redo();

    bool can_undo() const noexcept { return index_ > 0; }
    bool can_redo() const noexcept { return index_ < commands_.size(); }

    // 不在 macro 中时清空全部命令与状态；clean 重置为 0（初始即干净）。
    void clear() noexcept;

    // -------- 查询 --------

    std::size_t size() const noexcept { return commands_.size(); }
    std::size_t index() const noexcept { return index_; }
    const Command* at(std::size_t i) const noexcept;

    // -------- 内存上限 --------

    std::size_t get_memory_limit() const noexcept { return memory_limit_; }
    // 0 = 无限。设置后立即按需淘汰。
    void set_memory_limit(std::size_t limit) noexcept;

    // -------- 保存点（clean state）--------

    // 当前 index 等于 clean_index_ 时为"已保存"态。
    // clean 丢失（kNoCleanIndex）时永远不为 clean。
    bool is_clean() const noexcept { return index_ == clean_index_; }
    std::size_t get_clean_index() const noexcept { return clean_index_; }
    // 把当前 index 标记为 clean（典型：文件刚保存）。
    void set_clean() noexcept { clean_index_ = index_; }
    // 标记 clean 不可达（典型：保存点之后又被截断）。
    void reset_clean() noexcept { clean_index_ = kNoCleanIndex; }

    // -------- Macro（事务）--------

    void begin_macro(const std::string& name);
    void end_macro();
    bool is_in_macro() const noexcept { return macro_depth_ > 0; }
    int macro_depth() const noexcept { return macro_depth_; }

    // -------- Merge 开关 --------

    void set_merge_enabled(bool enabled) noexcept { merge_enabled_ = enabled; }
    bool is_merge_enabled() const noexcept { return merge_enabled_; }

    // -------- 变更信号 --------

    Signal<>& changed() noexcept { return changed_; }

private:
    std::vector<std::unique_ptr<Command>> commands_;
    std::size_t index_ = 0;         // 下一个待 redo 的命令下标；== size 表示全 redo
    std::size_t clean_index_ = 0;   // 保存点 index；kNoCleanIndex 表示已丢失
    std::size_t memory_limit_ = kDefaultMemoryLimit;
    bool merge_enabled_ = false;

    int macro_depth_ = 0;
    std::vector<std::unique_ptr<MacroCommand>> macro_stack_;  // 嵌套宏栈

    Signal<> changed_;

    // 截断 [pos, end) 的 redo 历史；clean 落在被截断区间则丢失。
    void truncate_redo_from(std::size_t pos);
    // 按 memory_limit_ 从最旧开始淘汰。
    void enforce_memory_limit();
    void emit_changed() { changed_.emit(); }
};

}  // namespace eda::command
