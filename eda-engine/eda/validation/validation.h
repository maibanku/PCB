#pragma once

// eda/validation/validation.h
//
// P11 统一校验框架 · 核心抽象（11-01 统一校验中心 / D1）。
//
// 定位（见 docs/11-校验与DFX/README.md + 01-统一校验中心/README.md）：
//   统一校验中心把 ERC（原理图）、DRC（PCB）、一致性（原理图 ↔ PCB）、DFX（DFM/DFA/DFT）
//   汇聚为"单一入口 + 统一违例流"。本头提供该流的骨架抽象：
//     - Severity            统一严重度（ERROR/WARNING/INFO）
//     - ValidationResult    统一违例记录（分类 + 定位 + 实体引用 + 是否可修）
//     - ValidationContext   检查上下文（board/schematic 指针，可空）
//     - ValidationRule      规则抽象（IRule::check(context) 契约）
//     - ValidationEngine    聚合器（注册规则 + run_all + 分组 + completed 信号）
//
// 解耦边界（强约束）：
//   - 本模块**不强依赖 pcb/sch 头**——ValidationContext 以 `const Object*` 泛化持有
//     board/schematic（Object 是 P2 对象体系公共基类，所有领域对象均继承自它）。
//   - 规则实现（如 erc.h/cpp）通过 Object 的 abstract API（get_class / is_class /
//     get / get_meta）查询数据，避免把 eda_validation 拖入对 eda_pcb / eda_schematic
//     的链接依赖（与 eda/pcb/CMakeLists.txt 同样的"反向依赖隔离"原则）。
//   - 集成阶段（P11 后续）若需直查 pcb::Board，可由派生 Rule 在 .cpp 内 include
//     pcb 头并以 is_class + static_cast 下转，新增独立链接关系，不污染本骨架。
//
// 与 P2 对象体系集成：
//   - ValidationRule 继承 eda::Object；派生规则可被 cast_to / ClassDB 识别。
//   - notification(int) 接 ClassDB 生命周期（PREDELETE / POSTINITIALIZE）。
//   - ValidationEngine 持有规则 unique_ptr；自身非 Object（值语义聚合器）。
//
// 与 P3 信号系统集成：
//   - ValidationEngine::completed 是 Signal<std::size_t>，run_all 结束后以结果总数触发；
//     供 11-04 结果面板订阅、F4 CLI 进度回调、增量校验 dirty 标记使用。
//
// 命名规范：namespace eda::validation、PascalCase 类型、snake_case 方法/成员、
//           成员尾下划线 _、include 从仓库根（#include "eda/validation/validation.h"）。

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/object/object.h"
#include "core/object/signal.h"
#include "eda/geometry/types.h"

namespace eda::validation {

// ============================================================================
// Severity —— 统一严重度（11-04 面板按级分组着色）。
// ============================================================================
enum class Severity {
    ERROR = 0,    // 阻断级：必须修复才能投产（如短路、重复位号）
    WARNING = 1,  // 警示级：强烈建议修复（如悬空引脚、未连）
    INFO = 2,     // 提示级：仅供参考（如统计、最佳实践提示）
};

// Severity ↔ 字符串（序列化 / 日志 / CLI 输出友好）。
std::string severity_string(Severity s);
Severity parse_severity(const std::string& s);

// ============================================================================
// ValidationResult —— 单条违例记录（统一结果流的最小单元）。
//
// 字段契约（与 11-04 结果面板对齐）：
//   severity     分级（面板着色 + 过滤）
//   category     规则分类标签（"ERC"/"DRC"/"Consistency"/"DFM"/"DFA"/"DFT"）
//   message      人类可读描述（已本地化的最终文案）
//   location     板上坐标（nm，可选）——面板定位跳转 / DRC 高亮
//   entity_uuid  关联对象 UUID（可空）——选中 / ECO 联动
//   fixable      是否可自动修复——驱动"批修建议"按钮可用性
// ============================================================================
struct ValidationResult {
    Severity severity = Severity::WARNING;
    std::string category;                       // "ERC" / "DRC" / ...
    std::string message;                        // 人类可读
    std::optional<geometry::Point> location;    // 板上坐标（nm），可空
    std::string entity_uuid;                    // 关联对象 UUID，可空
    bool fixable = false;                       // 是否可自动修复

    // 便捷工厂（链式构造，规则实现里写起来更紧凑）。
    ValidationResult& with_severity(Severity s) { severity = s; return *this; }
    ValidationResult& with_category(std::string c) { category = std::move(c); return *this; }
    ValidationResult& with_message(std::string m) { message = std::move(m); return *this; }
    ValidationResult& with_location(geometry::Point p) { location = p; return *this; }
    ValidationResult& with_entity(std::string u) { entity_uuid = std::move(u); return *this; }
    ValidationResult& with_fixable(bool f = true) { fixable = f; return *this; }
};

// ============================================================================
// ValidationContext —— 检查上下文（board/schematic 泛化指针，均可空）。
//
// 设计原则：
//   - 不强依赖 pcb/sch 头：以 `const Object*` 持有，规则通过 Object abstract API 查询。
//   - 空安全：任何一侧为 nullptr 时，规则应跳过该侧检查（不抛异常、不崩）。
//   - 值语义：可拷贝传递；调用方保证所指 Object 在 check() 期间存活。
//
// 集成约定（11-01 文档）：
//   - 全板校验：board+schematic 均非空 → 跑 ERC+DRC+一致性。
//   - 仅原理图：schematic 非空、board 空 → 跑 ERC。
//   - 仅 PCB：board 非空、schematic 空 → 跑 DRC+DFM/DFA/DFT。
// ============================================================================
struct ValidationContext {
    const Object* board = nullptr;        // 指向 eda::pcb::Board（或测试桩）
    const Object* schematic = nullptr;    // 指向 eda::sch::Schematic（未来；当前未落地）

    // 便捷构造（避免大括号初始化时字段顺序歧义）。
    ValidationContext() = default;
    ValidationContext(const Object* b, const Object* s = nullptr)
        : board(b), schematic(s) {}

    // 空上下文（无任何领域对象）——规则必须安全跳过。
    static ValidationContext empty() { return ValidationContext{}; }
};

// ============================================================================
// ValidationRule —— 规则抽象基类（IRule::check(context) 契约，11-01 文档）。
//
// 派生规则覆写：
//   - name()        规则唯一名（用于日志 / 配置 / 调试）
//   - check(ctx)    执行检查，返回违例列表（空 = 通过）
//
// 默认实现：
//   - get_class / is_class（ClassDB 识别）
//   - notification（生命周期挂点，默认空）
// ============================================================================
class ValidationRule : public Object {
public:
    ValidationRule() = default;
    ~ValidationRule() override = default;

    ValidationRule(const ValidationRule&) = delete;
    ValidationRule& operator=(const ValidationRule&) = delete;

    // ---- 规则契约 ----
    virtual std::string name() const = 0;
    virtual std::vector<ValidationResult> check(const ValidationContext& ctx) const = 0;

    // ---- ClassDB 元信息 ----
    static std::string get_class_static() { return "ValidationRule"; }
    std::string get_class() const override { return "ValidationRule"; }
    bool is_class(const std::string& n) const override {
        return n == "ValidationRule" || Object::is_class(n);
    }
};

// ============================================================================
// ValidationEngine —— 规则聚合器（11-01 统一执行入口）。
//
// 职责：
//   - 注册规则（add_rule，按注册顺序保留）
//   - 一次性运行所有规则（run_all），聚合违例列表
//   - 按 Severity 分组（group_by_severity），供面板分级展示
//   - completed 信号（run_all 结束触发，携带结果总数）——面板 / CLI / 增量校验订阅
//
// 非目标（11-01 显式推迟）：
//   - 去重 / 归并：同问题被多规则触发时归并——延后到 11-04 面板层做。
//   - 增量校验：仅对受影响对象重算——延后到 11-01 增量 + dirty 标记落地。
//   - 取消 / 进度：本期 run_all 同步阻塞；进度/取消待双模式（实时/全板）落地。
// ============================================================================
class ValidationEngine {
public:
    ValidationEngine() = default;
    ~ValidationEngine() = default;

    ValidationEngine(const ValidationEngine&) = delete;
    ValidationEngine& operator=(const ValidationEngine&) = delete;

    // ---- 规则注册 ----
    // 取得规则 unique_ptr 所有权。空指针忽略（防御）。
    void add_rule(std::unique_ptr<ValidationRule> rule);

    // 便捷模板：构造并注册（add_rule(std::make_unique<R>(...)) 的语法糖）。
    template <typename RuleT, typename... Args>
    RuleT* emplace_rule(Args&&... args) {
        auto owned = std::make_unique<RuleT>(std::forward<Args>(args)...);
        RuleT* raw = owned.get();
        add_rule(std::move(owned));
        return raw;
    }

    // ---- 一次性执行 ----
    // 同步运行所有已注册规则，按注册顺序聚合违例。
    // 完成后 emit completed(results.size())。
    // ctx 中 board/schematic 为空时，规则各自安全跳过。
    std::vector<ValidationResult> run_all(const ValidationContext& ctx);

    // ---- 结果分组 ----
    // 按 Severity 分组为 {Severity → [results]}，便于面板分级展示。
    // 三级（ERROR/WARNING/INFO）均出现在 map 中（即便为空 vector）。
    std::unordered_map<Severity, std::vector<ValidationResult>> group_by_severity(
        const std::vector<ValidationResult>& results) const;

    // ---- 查询 / 清空 ----
    std::size_t rule_count() const { return rules_.size(); }
    void clear_rules();

    // ---- 完成信号 ----
    // run_all 结束后触发，参数 = 本次结果总数（0 表示无违例/空上下文）。
    Signal<std::size_t>& completed() { return completed_; }
    const Signal<std::size_t>& completed() const { return completed_; }

private:
    std::vector<std::unique_ptr<ValidationRule>> rules_;
    Signal<std::size_t> completed_;
};

}  // namespace eda::validation
