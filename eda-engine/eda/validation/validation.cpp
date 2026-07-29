// eda/validation/validation.cpp
//
// P11 统一校验框架 · 核心实现（11-01 统一校验中心）。
//
// 本翻译单元聚合 ValidationRule/Engine 的非平凡方法：
//   * Severity ↔ 字符串
//   * ValidationEngine::add_rule / run_all / group_by_severity / clear_rules
//
// 设计要点：
//   - run_all 同步遍历规则，捕获每条规则的 vector<ValidationResult> 追加到聚合。
//   - completed 信号在聚合完成后触发（即使结果为空也触发，便于面板切换"运行中→完成"状态）。
//   - group_by_severity 预填三级空 vector，保证调用方按 Severity 索引恒定。
//
// 解耦：本文件不 include pcb/sch 头；规则对 board 的访问经 Object abstract API。
//
// 命名规范：namespace eda::validation、snake_case、成员尾下划线、include 从仓库根。

#include "eda/validation/validation.h"

#include <utility>

namespace eda::validation {

// ============================================================================
// Severity ↔ 字符串
// ============================================================================

std::string severity_string(Severity s) {
    switch (s) {
        case Severity::ERROR:   return "error";
        case Severity::WARNING: return "warning";
        case Severity::INFO:    return "info";
    }
    return "info";  // 防御
}

Severity parse_severity(const std::string& s) {
    if (s == "error" || s == "ERROR")   return Severity::ERROR;
    if (s == "warning" || s == "WARNING") return Severity::WARNING;
    return Severity::INFO;  // 含 "info"/未知
}

// ============================================================================
// ValidationEngine
// ============================================================================

void ValidationEngine::add_rule(std::unique_ptr<ValidationRule> rule) {
    if (!rule) return;  // 防御：忽略空注册
    rules_.push_back(std::move(rule));
}

std::vector<ValidationResult> ValidationEngine::run_all(const ValidationContext& ctx) {
    std::vector<ValidationResult> aggregated;
    // 预估容量：规则数 × 平均违例（粗估，避免频繁 reallocate）。
    aggregated.reserve(rules_.size() * 4);

    for (const auto& rule : rules_) {
        if (!rule) continue;
        auto violations = rule->check(ctx);
        for (auto& v : violations) {
            aggregated.push_back(std::move(v));
        }
    }

    // 完成通知：携带结果总数。空上下文 / 无规则时也触发（结果数 0）。
    completed_.emit(aggregated.size());
    return aggregated;
}

std::unordered_map<Severity, std::vector<ValidationResult>>
ValidationEngine::group_by_severity(const std::vector<ValidationResult>& results) const {
    std::unordered_map<Severity, std::vector<ValidationResult>> grouped;
    // 预填三级空 vector，保证调用方按 Severity 索引恒定（无需 default-construct 旁路）。
    grouped.reserve(3);
    grouped[Severity::ERROR];
    grouped[Severity::WARNING];
    grouped[Severity::INFO];

    for (const auto& r : results) {
        grouped[r.severity].push_back(r);
    }
    return grouped;
}

void ValidationEngine::clear_rules() {
    rules_.clear();
}

}  // namespace eda::validation
