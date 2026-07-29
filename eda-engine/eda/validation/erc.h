#pragma once

// eda/validation/erc.h
//
// P11 ERC 基础规则（电气规则检查，11-01 统一校验中心 / 03-电气网络分析）。
//
// 三条骨架规则（各 : ValidationRule）：
//   1. UnconnectedPinRule     —— 引脚 / 焊盘未连任何网络（WARNING，可修 = 标注新网络）
//   2. DuplicateRefdesRule    —— 位号重复（ERROR，违反 EDA 基本唯一性约束）
//   3. PowerGroundConflictRule —— 电源 / 地冲突（ERROR，占位；待 03-电气网络分析
//                                  落地真实多驱动 / 短路 / PI 分析后替换实现）
//
// 数据访问契约（解耦关键 —— 不强依赖 pcb/sch 头）：
//   规则不直接 include pcb/board.h。它们通过 Object abstract API 读取"违例数据源"，
//   具体走 board->get_meta(...) 读预置的元数据键。集成阶段由 pcb::Board（或上层适配器）
//   把真实对象树导出为这些 meta 键；测试可直接在 Object 上 set_meta 构造用例。
//
//   元数据键（key）：
//     erc.pads         : ARRAY<DICTIONARY>  每元素 {uuid, refdes?, net_uuid, x?, y?}
//                                                  net_uuid 为空 → 未连
//     erc.footprints   : ARRAY<DICTIONARY>  每元素 {uuid, refdes}
//                                                  refdes 为空跳过（未放置）
//     erc.nets         : ARRAY<DICTIONARY>  每元素 {name, type, driver_count}
//                                                  type ∈ {"power","ground","signal"}
//
//   坐标约定：与 geometry::Point 一致，int64_t nm（P3 几何内核 D1）。
//
// 边界：
//   - 本骨架不实现去重（同一 pad 同时触发多规则——11-04 面板层归并）。
//   - 不实现 PI / 多驱动语义（PowerGroundConflictRule 仅占位：driver_count>1 的电源/
//     地网络报 ERROR，作为后续 03-电气网络分析的接入点）。
//
// 命名规范：namespace eda::validation、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <string>
#include <vector>

#include "eda/validation/validation.h"

namespace eda::validation {

// ============================================================================
// UnconnectedPinRule —— 引脚 / 焊盘未连任何网络。
// 严重度：WARNING（强烈建议修复；可自动修复——分配 / 合并到新网络）。
// 数据：board->get_meta("erc.pads")，net_uuid 为空的条目即为违例。
// ============================================================================
class UnconnectedPinRule : public ValidationRule {
public:
    std::string name() const override { return "erc.unconnected_pin"; }

    std::vector<ValidationResult> check(const ValidationContext& ctx) const override;

    static std::string get_class_static() { return "UnconnectedPinRule"; }
    std::string get_class() const override { return "UnconnectedPinRule"; }
    bool is_class(const std::string& n) const override {
        return n == "UnconnectedPinRule" || ValidationRule::is_class(n);
    }
};

// ============================================================================
// DuplicateRefdesRule —— 位号重复（违反 EDA 唯一性约束）。
// 严重度：ERROR（阻断级；不可自动修复——需用户决定保留哪个）。
// 数据：board->get_meta("erc.footprints")，非空 refdes 重复即报。
// ============================================================================
class DuplicateRefdesRule : public ValidationRule {
public:
    std::string name() const override { return "erc.duplicate_refdes"; }

    std::vector<ValidationResult> check(const ValidationContext& ctx) const override;

    static std::string get_class_static() { return "DuplicateRefdesRule"; }
    std::string get_class() const override { return "DuplicateRefdesRule"; }
    bool is_class(const std::string& n) const override {
        return n == "DuplicateRefdesRule" || ValidationRule::is_class(n);
    }
};

// ============================================================================
// PowerGroundConflictRule —— 电源 / 地冲突（占位）。
// 严重度：ERROR（多驱动电源/地网络）。
// 数据：board->get_meta("erc.nets")，type 为 power/ground 且 driver_count>1 即报。
//
// 注：本规则为 03-电气网络分析的接入点；当前仅做"多驱动"基础判定，未来扩展为
//     完整 PI / 短路 / 多输出冲突分析（见 docs/11-校验与DFX/03-电气网络分析/README.md）。
// ============================================================================
class PowerGroundConflictRule : public ValidationRule {
public:
    std::string name() const override { return "erc.power_ground_conflict"; }

    std::vector<ValidationResult> check(const ValidationContext& ctx) const override;

    static std::string get_class_static() { return "PowerGroundConflictRule"; }
    std::string get_class() const override { return "PowerGroundConflictRule"; }
    bool is_class(const std::string& n) const override {
        return n == "PowerGroundConflictRule" || ValidationRule::is_class(n);
    }
};

}  // namespace eda::validation
