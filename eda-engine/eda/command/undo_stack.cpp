// eda/command/undo_stack.cpp
//
// P3 命令栈 · Task 8：UndoStack 实现。
//
// 关键算法说明见 undo_stack.h 顶部注释。这里重点说明 macro 与淘汰两个流程：
//
// 1) Macro 流程：
//    - begin_macro 在 macro_stack_ 压入新 MacroCommand，macro_depth_++。
//    - push 在 macro 内：cmd->execute() 后 add_child 到 macro_stack_.back()，
//      不进主栈。live 状态已推进。
//    - end_macro 弹出最内层 macro：
//        * 若仍在更外层 macro 内：把该 macro 作为子命令加入外层 macro。
//        * 若是最外层 macro（macro_depth_ 归 0）：压入主栈。子命令已 execute，
//          故主栈压入时不再次 execute，后续 undo/redo 经 MacroCommand 转发。
//        * 空 macro（child_count==0）不入主栈，避免污染撤销历史。
//
// 2) 内存淘汰（enforce_memory_limit）：
//    每次淘汰 commands_[0]，按其"是否已应用"分两路调整 index_ 与 clean_index_。
//    详细推导见 undo_stack.h 顶部"内存上限淘汰策略"。

#include "eda/command/undo_stack.h"

#include <utility>

namespace eda::command {

// ===========================================================================
// push / undo / redo
// ===========================================================================

void UndoStack::push(std::unique_ptr<Command> cmd) {
    if (!cmd) return;

    // 1. 在 macro 内：执行并收入最内层 macro，直接返回。
    if (macro_depth_ > 0) {
        cmd->execute();
        macro_stack_.back()->add_child(std::move(cmd));
        emit_changed();
        return;
    }

    // 2. 始终先执行（应用动作到 live 对象）。
    cmd->execute();

    // 3. 若开启 merge 且当前位于栈顶（无 redo 历史），尝试与栈顶合并。
    //    合并成功则吸收 cmd，不单独入栈。注意必须在未截断 redo 前尝试，
    //    因为截断会改变 commands_.back()。
    if (merge_enabled_ && !commands_.empty() && index_ == commands_.size()) {
        if (commands_.back()->merge_with(cmd.get())) {
            // cmd 已被吸收；unique_ptr 析构释放。emit 以通知 UI 刷新。
            emit_changed();
            return;
        }
    }

    // 4. 截断 redo 历史（push 新分支后，旧 redo 分支作废）。
    truncate_redo_from(index_);

    // 5. 入栈并推进 index。
    commands_.push_back(std::move(cmd));
    index_ = commands_.size();

    // 6. 强制内存上限。
    enforce_memory_limit();

    emit_changed();
}

bool UndoStack::undo() {
    if (!can_undo()) return false;
    if (macro_depth_ > 0) return false;  // macro 中禁止半途 undo
    --index_;
    commands_[index_]->undo();
    emit_changed();
    return true;
}

bool UndoStack::redo() {
    if (!can_redo()) return false;
    if (macro_depth_ > 0) return false;  // macro 中禁止半途 redo
    commands_[index_]->redo();
    ++index_;
    emit_changed();
    return true;
}

void UndoStack::clear() noexcept {
    commands_.clear();
    macro_stack_.clear();
    macro_depth_ = 0;
    index_ = 0;
    clean_index_ = 0;  // 空栈即初始态，视为 clean
    emit_changed();
}

const Command* UndoStack::at(std::size_t i) const noexcept {
    if (i >= commands_.size()) return nullptr;
    return commands_[i].get();
}

// ===========================================================================
// 内存上限
// ===========================================================================

void UndoStack::set_memory_limit(std::size_t limit) noexcept {
    memory_limit_ = limit;
    enforce_memory_limit();
    emit_changed();
}

// ===========================================================================
// Macro
// ===========================================================================

void UndoStack::begin_macro(const std::string& name) {
    macro_stack_.push_back(std::make_unique<MacroCommand>(name));
    ++macro_depth_;
}

void UndoStack::end_macro() {
    if (macro_depth_ == 0) return;  // 未配对的 end_macro：静默忽略

    auto finished = std::move(macro_stack_.back());
    macro_stack_.pop_back();
    --macro_depth_;

    if (macro_depth_ > 0) {
        // 仍在更外层 macro 内：把本 macro 作为子命令加入外层。
        // 即使 finished 为空也保留（用户显式开启的子事务）。
        macro_stack_.back()->add_child(std::move(finished));
        return;
    }

    // 最外层 macro 关闭：压入主栈。
    // 空 macro 不入栈（避免空操作占用一个 undo 单元）。
    if (finished->child_count() == 0) {
        return;
    }

    // 与普通 push 一致：截断 redo → 入栈（不重新 execute，子命令已执行过）→ 限流。
    truncate_redo_from(index_);
    commands_.push_back(std::move(finished));
    index_ = commands_.size();
    enforce_memory_limit();
    emit_changed();
}

// ===========================================================================
// 内部：截断 / 淘汰
// ===========================================================================

void UndoStack::truncate_redo_from(std::size_t pos) {
    if (pos >= commands_.size()) return;
    commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(pos),
                    commands_.end());
    // clean_index_ 若落在 (pos, end] 区间则丢失；pos 处的 index 值仍可达（保留）。
    if (clean_index_ != kNoCleanIndex && clean_index_ > pos) {
        clean_index_ = kNoCleanIndex;
    }
}

void UndoStack::enforce_memory_limit() {
    if (memory_limit_ == 0) return;  // 无限
    while (commands_.size() > memory_limit_) {
        // 淘汰最旧：commands_[0]。
        const bool dropped_applied = (index_ > 0);
        commands_.erase(commands_.begin());

        if (dropped_applied) {
            // 被淘汰命令已应用到 live 对象，永久落地，无法 undo 回其之前。
            --index_;
            if (clean_index_ == kNoCleanIndex) {
                // 已丢失，保持。
            } else if (clean_index_ > 0) {
                --clean_index_;  // 保存点随栈整体前移
            } else {
                // clean 原在初始态（0），但被淘汰命令已应用、不可撤销，
                // 初始态不再可达 → clean 丢失。
                clean_index_ = kNoCleanIndex;
            }
        } else {
            // 被淘汰命令未应用（属于 redo 历史），淘汰后不可重做。
            // clean_index_==0（初始态）保留；clean_index_>0 依赖该命令的 redo
            // 才能到达 → 丢失。
            if (clean_index_ != kNoCleanIndex && clean_index_ > 0) {
                clean_index_ = kNoCleanIndex;
            }
        }
    }
}

}  // namespace eda::command
