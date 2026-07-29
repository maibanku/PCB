// eda/eco/eco_applier.cpp
//
// P14 ECO 工程变更订单 · 选择性应用实现（14-04）。
//
// apply 流程：
//   1. 过滤 entity_type == "Net" 的条目；其他类型计入 skipped。
//   2. 按 ChangeType 分派：
//        * ADDED   → AddNetCommand 推入栈（或直接 board.add_net）
//        * REMOVED → skipped（Board 无 remove_net，骨架阶段跳过）
//        * MODIFIED→ skipped（分类调整未实现，骨架阶段跳过）
//   3. 命令化模式：begin_macro("Apply ECO") 包裹整批，end_macro 合并为单步撤销。
//      若调用方已在 macro 中（macro_depth > 0），不再嵌套开 macro，直接逐条 push。
//
// AddNetCommand：
//   * execute() : board_->add_net(net_name_)，捕获返回 Net 的 UUID（用于审计）。
//   * undo()    : 骨架限制——Board 暂无 remove_net，undo 仅清空本地 added_uuid_，
//                 Board 上的 Net 不会被删除。TODO(P14 后续 Task)：接入 remove_net。
//   * redo()    : 重新 add_net（会分配新 UUID，但网络名一致；ECO 语义以名为键）。
//
// 注意：AddNetCommand 派生自 eda::command::Command（P3），用匿名命名空间藏在
// 本翻译单元内——applier 对外只暴露 EcoApplier / ApplyResult。

#include "eda/eco/eco_applier.h"

#include <memory>
#include <string>
#include <utility>

#include "eda/command/command.h"
#include "eda/pcb/net.h"

namespace eda::eco {

namespace {

// ============================================================================
// AddNetCommand —— "在 Board 上新增 Net" 命令（P3 Command 体系）。
// ============================================================================
class AddNetCommand : public eda::command::Command {
public:
    AddNetCommand(eda::pcb::Board* board, std::string net_name)
        : Command("Add Net " + net_name),
          board_(board),
          net_name_(std::move(net_name)) {}

    void execute() override {
        if (!board_) return;
        // add_net 每次分配新 UUID；ECO 以"网络名"为业务主键，UUID 仅作审计追踪。
        eda::pcb::Net& n = board_->add_net(net_name_);
        added_uuid_ = n.uuid();
    }

    void undo() override {
        // 骨架限制：Board 当前未暴露 remove_net（见 eda/pcb/board.h）。
        // 为保持命令对象的可重放语义，这里只清空本地追踪状态；Board 上的 Net
        // 暂不会被移除。待后续 Task 在 Board 上新增 remove_net(uuid) 后补全。
        added_uuid_.clear();
    }

    void redo() override {
        // 重做：再次添加（Board::add_net 必然成功并分配新 UUID）。
        execute();
    }

    const std::string& net_name() const noexcept { return net_name_; }
    const std::string& added_uuid() const noexcept { return added_uuid_; }

    static std::string get_class_static() { return "AddNetCommand"; }
    std::string get_class() const override { return "AddNetCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "AddNetCommand" || Command::is_class(n);
    }

private:
    eda::pcb::Board* board_;
    std::string net_name_;
    std::string added_uuid_;
};

}  // namespace

// ----------------------------------------------------------------------------
// EcoApplier::apply
// ----------------------------------------------------------------------------

ApplyResult EcoApplier::apply(const std::vector<EcoItem>& changes,
                              eda::pcb::Board& target) {
    ApplyResult result;

    // 命令化分支：在自己的 macro 中包裹整批 ECO（若调用方已在 macro 中则跳过嵌套）。
    const bool use_stack = (undo_stack_ != nullptr);
    const bool open_macro = use_stack && !undo_stack_->is_in_macro();
    if (open_macro) {
        undo_stack_->begin_macro("Apply ECO (" +
                                 std::to_string(changes.size()) +
                                 " changes)");
    }

    for (const EcoItem& item : changes) {
        // 仅处理网络级变更（骨架）
        if (item.entity_type != "Net") {
            ++result.skipped_count;
            result.errors.push_back(
                "Unsupported entity_type '" + item.entity_type +
                "' for " + item.entity_uuid);
            continue;
        }

        switch (item.type) {
            case ChangeType::ADDED: {
                // 正向同步：原理图新增 → PCB 新建 Net
                if (use_stack) {
                    auto cmd = std::make_unique<AddNetCommand>(&target, item.entity_uuid);
                    undo_stack_->push(std::move(cmd));  // push 内部调 execute()
                } else {
                    target.add_net(item.entity_uuid);
                }
                ++result.applied_count;
                break;
            }
            case ChangeType::REMOVED: {
                // 骨架限制：Board 无 remove_net。提示用户手动处理。
                ++result.skipped_count;
                result.errors.push_back(
                    "REMOVED net '" + item.entity_uuid +
                    "' skipped: Board::remove_net not available in skeleton");
                break;
            }
            case ChangeType::MODIFIED: {
                // 骨架限制：分类调整未实现。提示用户手动复核。
                ++result.skipped_count;
                result.errors.push_back(
                    "MODIFIED net '" + item.entity_uuid +
                    "' skipped: classification update not implemented");
                break;
            }
        }
    }

    if (open_macro) {
        undo_stack_->end_macro();
    }

    return result;
}

}  // namespace eda::eco
