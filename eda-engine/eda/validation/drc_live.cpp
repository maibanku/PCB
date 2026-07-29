// eda/validation/drc_live.cpp
//
// P11 实时 DRC 联动实现（11-02 实时校验）。
//
// 数据访问：board_ 作为 const pcb::Board* 持有，run 时构造 ValidationContext 并交给
// ValidationEngine::run_all。规则对 Board 的下转在 drc.cpp 内完成（is_class + static_cast）。
//
// 信号：do_run 完成后 emit results_changed(error_count)。
//   - run_if_dirty：仅在 dirty_ && auto_run_ 时调 do_run，否则直接返回 cached_results_。
//   - run_now：无视 dirty_ / auto_run_，始终 do_run。
//
// 命名规范：namespace eda::validation、snake_case、成员尾下划线、include 从仓库根。

#include "eda/validation/drc_live.h"

#include <cstddef>
#include <vector>

#include "eda/pcb/board.h"

namespace eda::validation {

// ============================================================================
// 构造 / 析构
// ============================================================================

DrcLiveController::DrcLiveController() {
    rebuild_rules();  // 用默认 options_ 注册 4 条 DRC 规则
}

DrcLiveController::~DrcLiveController() = default;

// ============================================================================
// 配置
// ============================================================================

void DrcLiveController::set_board(const pcb::Board* board) {
    board_ = board;
    mark_dirty();  // Board 切换 → 视为脏
}

void DrcLiveController::set_options(DrcOptions options) {
    options_ = options;
    rebuild_rules();  // 规则参数变化 → 清空重建
    mark_dirty();
}

// ============================================================================
// 规则重建
// ============================================================================

void DrcLiveController::rebuild_rules() {
    engine_.clear_rules();
    engine_.emplace_rule<ClearanceRule>(options_);
    engine_.emplace_rule<MinWidthRule>(options_);
    engine_.emplace_rule<ShortCircuitRule>();
    engine_.emplace_rule<UnroutedNetRule>();
}

// ============================================================================
// 运行
// ============================================================================

const std::vector<ValidationResult>& DrcLiveController::run_if_dirty() {
    if (dirty_ && auto_run_) {
        do_run();
    }
    return cached_results_;
}

const std::vector<ValidationResult>& DrcLiveController::run_now() {
    do_run();
    return cached_results_;
}

void DrcLiveController::do_run() {
    // 构造上下文：board_ 为空时规则各自安全跳过（validation.h 的空上下文契约）。
    ValidationContext ctx;
    if (board_ != nullptr) {
        ctx.board = board_;  // 隐式 derived→base 指针转换（pcb::Board → Object）
    }
    cached_results_ = engine_.run_all(ctx);
    dirty_ = false;
    results_changed_.emit(static_cast<int>(error_count()));
}

// ============================================================================
// 查询
// ============================================================================

std::size_t DrcLiveController::error_count() const {
    std::size_t n = 0;
    for (const auto& r : cached_results_) {
        if (r.severity == Severity::ERROR) ++n;
    }
    return n;
}

std::size_t DrcLiveController::warning_count() const {
    std::size_t n = 0;
    for (const auto& r : cached_results_) {
        if (r.severity == Severity::WARNING) ++n;
    }
    return n;
}

}  // namespace eda::validation
