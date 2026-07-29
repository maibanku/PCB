#pragma once

// eda/validation/drc_live.h
//
// P11 实时 DRC 联动模块（11-02 实时校验 / Board 变更 → 自动 DRC → 信号通知）。
//
// 定位：在 ValidationEngine（一次性聚合）之上，封装"dirty 标记 + 自动 DRC + 信号广播"
// 三件套，作为编辑器 → 校验中心的实时联动入口：
//   - 编辑器在修改 Board 后调用 mark_dirty()
//   - 主循环 / UI 空闲时调用 run_if_dirty() —— 若脏则重跑 4 条 DRC 规则、更新缓存、
//     emit results_changed(error_count)；否则直接返回缓存
//   - run_now() 强制重跑（无视 dirty / auto_run）
//   - auto_run=false 时，run_if_dirty 即便脏也不重跑（手动模式，仅 run_now 生效）
//
// 设计要点：
//   - 持有 ValidationEngine + 4 条 DRC 规则（Clearance / MinWidth / ShortCircuit / UnroutedNet）
//     规则在构造 / set_options 时按 DrcOptions 重建。
//   - cached_results_ + dirty_ 提供增量语义：未脏时 O(1) 返回缓存，避免重复计算。
//   - results_changed 是 Signal<int>（携带 error_count），供 11-04 结果面板 / 状态栏订阅。
//   - 本类非 Object（值语义聚合器，与 ValidationEngine 一致）；Signal 作为成员长期持有，
//     连接的 lambda 不绑定 Object，析构无需特殊处理。
//
// 简化（spec 明示）：
//   - 内部 debounce 留后续——当前 run_if_dirty 即调即跑，无延迟合并。
//
// 命名规范：namespace eda::validation、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <cstddef>
#include <vector>

#include "core/object/signal.h"
#include "eda/validation/drc.h"
#include "eda/validation/validation.h"

namespace eda::pcb {
class Board;
}

namespace eda::validation {

// ============================================================================
// DrcLiveController —— 实时 DRC 联动控制器（Board 变更感知 + 缓存 + 信号广播）。
// ============================================================================
class DrcLiveController {
public:
    DrcLiveController();
    ~DrcLiveController();

    DrcLiveController(const DrcLiveController&) = delete;
    DrcLiveController& operator=(const DrcLiveController&) = delete;

    // ---- 配置 ----
    // 绑定目标 Board（可空）；绑定后 mark_dirty。Board 变更由调用方负责再次 mark_dirty。
    void set_board(const pcb::Board* board);
    // 全局 DRC 参数；变更后按新参数重建 4 条规则 + mark_dirty。
    void set_options(DrcOptions options);
    const DrcOptions& options() const { return options_; }

    // ---- 自动 DRC 模式 ----
    // 默认 true：run_if_dirty 在脏时自动重跑。
    // 设为 false：run_if_dirty 即便脏也只返回缓存，需 run_now 显式刷新（手动模式）。
    void set_auto_run(bool enabled) { auto_run_ = enabled; }
    bool auto_run() const { return auto_run_; }

    // ---- dirty 标记 ----
    // 标记 Board 已修改：下一次 run_if_dirty（且 auto_run=true）将重算。
    // mark_dirty 自身不触发任何运行（无 debounce 的简化版）。
    void mark_dirty() { dirty_ = true; }

    // ---- 运行 ----
    // 若 dirty 且 auto_run=true：重跑 4 条 DRC 规则、更新缓存、emit results_changed(error_count)。
    // 否则（未脏 / auto_run=false）：直接返回缓存，不重算、不 emit。
    const std::vector<ValidationResult>& run_if_dirty();

    // 强制重跑（无视 dirty / auto_run）：始终重算、更新缓存、emit results_changed。
    const std::vector<ValidationResult>& run_now();

    // ---- 查询 ----
    // 最近一次缓存的结果（run_if_dirty / run_now 后更新）。
    const std::vector<ValidationResult>& results() const { return cached_results_; }
    // 缓存结果中 ERROR / WARNING 计数。
    std::size_t error_count() const;
    std::size_t warning_count() const;

    // ---- 信号 ----
    // 每次 do_run（实际重算）完成后触发，参数 = 当前 error_count（int）。
    // 未脏 / auto_run=false 的 run_if_dirty 不触发。
    Signal<int>& results_changed() { return results_changed_; }
    const Signal<int>& results_changed() const { return results_changed_; }

private:
    const pcb::Board* board_ = nullptr;
    DrcOptions options_;
    bool auto_run_ = true;
    bool dirty_ = true;  // 初始即脏：首次 run_if_dirty 触发首次计算
    std::vector<ValidationResult> cached_results_;
    ValidationEngine engine_;
    Signal<int> results_changed_;

    // 按 options_ 重建 4 条 DRC 规则（Clearance/MinWidth 用 options_，另两条无参数）。
    void rebuild_rules();
    // 实际重跑：engine.run_all → 缓存 → 清 dirty → emit results_changed(error_count)。
    void do_run();
};

}  // namespace eda::validation
