// eda/eco/eco_diff.cpp
//
// P14 ECO 工程变更订单 · 差异检测实现（14-02 差异检测）。
//
// 算法（网络级 diff，骨架）：
//   1. sch_netlist = schematic.generate_netlist() —— O(n·α(n))，已含 Union-Find 连通性。
//   2. 建 sch_by_name : map<name -> Net* 指针>，原理图侧重名索引。
//   3. 遍历 board.nets()，建 pcb_by_name : map<name -> Net*>。
//   4. 双指针合并：
//        * 仅 sch 有 → ADDED
//        * 仅 pcb 有 → REMOVED
//        * 双方都有 → 比较 is_power（sch）vs looks_power（pcb：net_class=="power"）
//                      不一致 → MODIFIED
//   5. 结果按 (type 权重, entity_uuid) 字典序排序，确保跨次运行确定性。
//
// 复杂度 O((N+M)·log(N+M))（map 插入 + 最终排序；N/M 为 sch/pcb 网络数）。
// 万级网络亚秒级（参考 docs/14-02：myers diff 思想；本骨架用直方图 diff 足够）。

#include "eda/eco/eco_diff.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

namespace eda::eco {

// ----------------------------------------------------------------------------
// 枚举字符串映射
// ----------------------------------------------------------------------------

const char* change_type_to_string(ChangeType type) {
    switch (type) {
        case ChangeType::ADDED:    return "ADDED";
        case ChangeType::REMOVED:  return "REMOVED";
        case ChangeType::MODIFIED: return "MODIFIED";
    }
    return "UNKNOWN";
}

ChangeType change_type_from_string(const std::string& s) {
    if (s == "ADDED")    return ChangeType::ADDED;
    if (s == "REMOVED")  return ChangeType::REMOVED;
    if (s == "MODIFIED") return ChangeType::MODIFIED;
    return ChangeType::ADDED;  // 前向兼容默认值
}

// ----------------------------------------------------------------------------
// 内部辅助：判定 PCB Net 是否"看起来像电源网络"。
// 启发式：net_class() 大小写不敏感等于 "power"。
// 未来由 P14 后续 Task 替换为基于 NetClass 资源的严格判定。
// ----------------------------------------------------------------------------
namespace {

bool pcb_net_looks_power(const eda::pcb::Net& n) {
    std::string c = n.net_class();
    // 大小写不敏感比较
    std::transform(c.begin(), c.end(), c.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return c == "power";
}

// 排序权重：按 ADDED < MODIFIED < REMOVED 分组，组内按 entity_uuid 字典序。
int type_weight(ChangeType t) {
    switch (t) {
        case ChangeType::ADDED:    return 0;
        case ChangeType::MODIFIED: return 1;
        case ChangeType::REMOVED:  return 2;
    }
    return 3;
}

bool eco_item_less(const EcoItem& a, const EcoItem& b) {
    if (a.type != b.type) {
        return type_weight(a.type) < type_weight(b.type);
    }
    return a.entity_uuid < b.entity_uuid;
}

}  // namespace

// ----------------------------------------------------------------------------
// EcoDiff::compute_diff
// ----------------------------------------------------------------------------

std::vector<EcoItem> EcoDiff::compute_diff(const eda::sch::Schematic& schematic,
                                           const eda::pcb::Board& board) {
    // 1. 原理图网表 → 名 → (Net*, is_power)
    const eda::sch::Netlist nl = schematic.generate_netlist();
    std::map<std::string, const eda::sch::Net*> sch_by_name;
    for (const auto& sn : nl.nets()) {
        // 同名网络在原理图侧重名理论上不应出现（generate_netlist 已归并）；
        // 出现则以首个为准，后续被忽略。
        sch_by_name.emplace(sn.name, &sn);
    }

    // 2. PCB 网络 → 名 → Net*
    std::map<std::string, const eda::pcb::Net*> pcb_by_name;
    for (const auto& pn : board.nets()) {
        if (!pn) continue;
        pcb_by_name.emplace(pn->name(), pn.get());
    }

    std::vector<EcoItem> items;

    // 3. 合并遍历两个有序 map（std::map 已按 key 排序）
    auto sit = sch_by_name.begin();
    auto pit = pcb_by_name.begin();
    const auto send = sch_by_name.end();
    const auto pend = pcb_by_name.end();

    while (sit != send || pit != pend) {
        const bool has_sch = (sit != send);
        const bool has_pcb = (pit != pend);

        if (has_sch && has_pcb && sit->first == pit->first) {
            // 同名：比较分类
            const auto& name = sit->first;
            const bool sch_power = sit->second->is_power;
            const bool pcb_power = pcb_net_looks_power(*pit->second);
            if (sch_power != pcb_power) {
                EcoItem item;
                item.type = ChangeType::MODIFIED;
                item.entity_type = "Net";
                item.entity_uuid = name;
                item.description =
                    "Net classification changed: schematic is_power=" +
                    std::string(sch_power ? "true" : "false") +
                    " vs pcb power=" + std::string(pcb_power ? "true" : "false");
                items.push_back(std::move(item));
            }
            ++sit;
            ++pit;
        } else if (has_pcb && (!has_sch || sit->first > pit->first)) {
            // PCB 独有 → REMOVED（正向同步时需从 PCB 删除）
            EcoItem item;
            item.type = ChangeType::REMOVED;
            item.entity_type = "Net";
            item.entity_uuid = pit->first;
            item.description = "Remove net " + pit->first + " (not in schematic)";
            items.push_back(std::move(item));
            ++pit;
        } else {
            // 原理图独有 → ADDED（正向同步时需在 PCB 新建）
            EcoItem item;
            item.type = ChangeType::ADDED;
            item.entity_type = "Net";
            item.entity_uuid = sit->first;
            item.description = "Add net " + sit->first + " (missing in board)";
            items.push_back(std::move(item));
            ++sit;
        }
    }

    // 4. 稳定排序（合并遍历已基本有序，但按 type 权重重排以保证语义分组）
    std::stable_sort(items.begin(), items.end(), eco_item_less);
    return items;
}

}  // namespace eda::eco
