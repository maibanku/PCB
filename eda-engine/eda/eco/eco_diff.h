#pragma once

// eda/eco/eco_diff.h
//
// P14 ECO 工程变更订单 · 差异检测（14-02 差异检测 / 14-01 原理图PCB双向同步）。
//
// 比较原理图网表（eda::sch::Schematic::generate_netlist）与 PCB 板上网络
// （eda::pcb::Board::nets）的差异，输出结构化 EcoItem 列表，作为 ECO 应用的输入。
//
// 对齐 docs/14-ECO工程变更/02-差异检测：检测 Added / Removed / Modified 三类差异。
// 重命名（Renamed）与器件/封装/引脚级 diff 留待后续 Task；本期只做"网络级"diff，
// 即 P14 骨架的双向同步咽喉——网络表对照。
//
// 匹配键：网络名（string）。"网络别名"与模糊匹配在 P14 后续 Task 实现；当前按
// 严格字符串相等。
//
// diff 维度（骨架）：
//   * ADDED    : 原理图存在、PCB 缺失的网络 → 正向同步时需在 PCB 新建 Net。
//   * REMOVED  : PCB 存在、原理图缺失的网络 → 正向同步时需从 PCB 删除 Net。
//   * MODIFIED : 双方都有同名网络，但分类（电源/普通）不一致 → 提示用户复核。
//
// 确定性：EcoItem 列表按 (entity_type, entity_uuid/name) 字典序稳定排列，
// 同一 Schematic + Board 跨次运行结果一致。
//
// 命名规范：namespace eda::eco、PascalCase 类型、snake_case 方法/成员、include 从仓库根。

#include <string>
#include <vector>

#include "eda/pcb/board.h"
#include "eda/schematic/schematic.h"

namespace eda::eco {

// ============================================================================
// ChangeType —— 差异类型枚举（14-02 差异检测）。
// ============================================================================
enum class ChangeType {
    ADDED,      // 原理图有、PCB 无
    REMOVED,    // PCB 有、原理图无
    MODIFIED    // 双方都有但内容/分类不同
};

// 枚举 ↔ 字符串（description / 日志 / 序列化用）。
const char* change_type_to_string(ChangeType type);
ChangeType change_type_from_string(const std::string& s);

// ============================================================================
// EcoItem —— 单条差异（14-03 ECO 列表的最小单元）。
// ============================================================================
//
// entity_type  : "Net" / "Component" / "Footprint" / "Pin"（本期骨架仅产出 "Net"）。
// entity_uuid  : 受影响实体的稳定标识。对网络 diff 使用网络名（PCB 与原理图两侧
//                均以名为匹配键），便于上层 ECO 列表直接按名定位。
// description  : 人类可读描述（"Add net VCC" / "Remove net OLD_NET" / ...）。
struct EcoItem {
    ChangeType type = ChangeType::ADDED;
    std::string entity_type;     // "Net"（骨架）
    std::string entity_uuid;     // 网络名（骨架阶段以名为 UUID）
    std::string description;

    bool operator==(const EcoItem&) const = default;
};

// ============================================================================
// EcoDiff —— 静态差异计算入口。
// ============================================================================
//
// compute_diff 比较 Schematic.generate_netlist() 与 Board.nets()：
//   1. 取原理图 Netlist 与 Board 的 Net 列表；
//   2. 按名建立索引（O(n+m)）；
//   3. 原理图独有 → ADDED；PCB 独有 → REMOVED；共有但分类不同 → MODIFIED；
//   4. 输出按 (type, entity_uuid) 稳定排序的 EcoItem 列表。
//
// MODIFIED 判定（骨架）：原理图 Net::is_power 与 PCB Net 的"电源语义"不一致。
//   PCB 侧启发式：net_class() == "power"（大小写不敏感）视为电源网络。
//   未来扩展：pin 成员集合对比 / 网络类属性 / 别名。
class EcoDiff {
public:
    // 比较原理图网表与 PCB 网络，返回差异列表（确定性排序）。
    static std::vector<EcoItem> compute_diff(const eda::sch::Schematic& schematic,
                                             const eda::pcb::Board& board);
};

}  // namespace eda::eco
