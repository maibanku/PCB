#pragma once

// eda/eco/eco_applier.h
//
// P14 ECO 工程变更订单 · 选择性应用（14-04 选择性应用 / 14-01 双向同步）。
//
// 把 EcoDiff::compute_diff 产出的差异列表应用到目标 Board：
//   * ADDED   → Board::add_net(name)（正向同步：原理图 → PCB）
//   * REMOVED → 骨架阶段记录为 skipped（Board 当前未暴露 remove_net；后续 Task 补齐）
//   * MODIFIED → 骨架阶段记录为 skipped（分类调整需 NetClass 资源，后续 Task 补齐）
//
// 命令化（与 P3 命令栈集成）：
//   * 若 EcoApplier 持有 UndoStack*，apply 会以 begin_macro / end_macro 包裹，
//     把每条可执行 EcoItem 包成 AddNetCommand 推入栈——整批 ECO 作为单个撤销单元。
//   * 未持有 UndoStack 时，apply 直接修改 Board（适用于无历史记录的批处理脚本）。
//
// 设计权衡：
//   * Board 当前仅有 add_net，无 remove_net；故 REMOVED 在骨架阶段不实际落地。
//     apply 如实把 REMOVED / MODIFIED 计入 skipped_count 并写入 errors，UI 可提示
//     用户"这些变更需手动处理"，避免静默丢失（与 docs/14-04 选择性应用语义一致）。
//   * AddNetCommand::undo 在 Board 暴露 remove_net 之前为受限实现（详见 .cpp 注释）。
//
// 命名规范：namespace eda::eco、PascalCase 类型、snake_case 方法/成员、include 从仓库根。

#include <cstddef>
#include <string>
#include <vector>

#include "eda/command/undo_stack.h"
#include "eda/eco/eco_diff.h"
#include "eda/pcb/board.h"

namespace eda::eco {

// ============================================================================
// ApplyResult —— apply 的返回汇总（14-04 选择性应用）。
// ============================================================================
struct ApplyResult {
    std::size_t applied_count = 0;   // 实际落地到 Board 的变更数
    std::size_t skipped_count = 0;   // 因约束/未支持而跳过的变更数
    std::vector<std::string> errors; // 每条跳过项的说明（含 entity_uuid）

    // 便捷：是否全部成功（无 errors）。
    bool ok() const noexcept { return errors.empty(); }
};

// ============================================================================
// EcoApplier —— 把 EcoItem 列表应用到 Board（14-04）。
// ============================================================================
//
// 用法：
//   eda::command::UndoStack stack;
//   eda::eco::EcoApplier applier(&stack);
//   auto diff = eda::eco::EcoDiff::compute_diff(sch, board);
//   auto result = applier.apply(diff, board);
//   // result.applied_count / skipped_count / errors
//   // stack.undo();  // 回滚整批 ECO（受 AddNetCommand::undo 能力限制）
class EcoApplier {
public:
    EcoApplier() = default;
    explicit EcoApplier(eda::command::UndoStack* stack) : undo_stack_(stack) {}

    // 设置/获取 UndoStack（可空）。apply 时若非空则命令化。
    void set_undo_stack(eda::command::UndoStack* stack) { undo_stack_ = stack; }
    eda::command::UndoStack* undo_stack() const noexcept { return undo_stack_; }

    // 把 changes 应用到 target。
    //   * entity_type 仅识别 "Net"；其他类型计入 skipped。
    //   * ADDED   → 新建 Net（命令化或直接调用）。
    //   * REMOVED / MODIFIED → 当前骨架跳过，记入 errors。
    ApplyResult apply(const std::vector<EcoItem>& changes, eda::pcb::Board& target);

private:
    eda::command::UndoStack* undo_stack_ = nullptr;
};

}  // namespace eda::eco
