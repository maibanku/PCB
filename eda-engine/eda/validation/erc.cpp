// eda/validation/erc.cpp
//
// P11 ERC 基础规则实现（11-01 统一校验中心 / 03-电气网络分析）。
//
// 数据访问契约：见 erc.h 顶部。规则经 board->get_meta(key) 读 Variant::ARRAY，
// 元素为 Variant::DICTIONARY。空 board 或缺键 → 返回空（安全跳过）。
//
// 实现要点：
//   - **生命周期**：get_meta 返回 Variant 按值（拷贝），as_array()/as_dictionary()
//     返回内部容器的 const 引用——必须把 Variant 存到局部变量，使其在遍历期间存活，
//     否则引用立即悬空（写代码时的高频坑）。
//   - Variant::as_array() 对非 ARRAY 类型返回空静态视图，故空 board / 缺键天然安全。
//   - as_string() / as_int() 对类型不匹配返回零值（""/0），不抛异常。
//   - 位号比较大小写敏感（与 EDA 工具惯例一致：R1 ≠ r1）。
//
// 命名规范：namespace eda::validation、snake_case、成员尾下划线、include 从仓库根。

#include "eda/validation/erc.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/object/object.h"
#include "core/object/variant.h"
#include "eda/geometry/types.h"

namespace eda::validation {

namespace {

// ---------------------------------------------------------------------------
// 辅助：从 pad/footprint/net 字典中读取字段，类型不匹配返回零值（不抛异常）。
// ---------------------------------------------------------------------------

std::string dict_string(const Variant& dict, const std::string& key) {
    const auto& d = dict.as_dictionary();
    auto it = d.find(key);
    return it == d.end() ? std::string{} : it->second.as_string();
}

int64_t dict_int(const Variant& dict, const std::string& key) {
    const auto& d = dict.as_dictionary();
    auto it = d.find(key);
    return it == d.end() ? 0 : it->second.as_int();
}

// 从字典读取可选坐标（缺 x 或 y 时返回 nullopt）。
std::optional<geometry::Point> dict_point(const Variant& dict) {
    const auto& d = dict.as_dictionary();
    const bool has_x = d.find("x") != d.end();
    const bool has_y = d.find("y") != d.end();
    if (!has_x || !has_y) return std::nullopt;
    return geometry::Point{dict_int(dict, "x"), dict_int(dict, "y")};
}

}  // namespace

// ============================================================================
// UnconnectedPinRule
// ============================================================================

std::vector<ValidationResult> UnconnectedPinRule::check(const ValidationContext& ctx) const {
    std::vector<ValidationResult> results;
    if (ctx.board == nullptr) return results;

    // 把 Variant 存到局部变量，as_array() 返回的视图在函数体内一直有效。
    const Variant pads_var = ctx.board->get_meta("erc.pads");
    const auto& pads = pads_var.as_array();
    results.reserve(pads.size());

    for (const auto& pad_var : pads) {
        const std::string net_uuid = dict_string(pad_var, "net_uuid");
        if (!net_uuid.empty()) continue;  // 已连

        const std::string pad_uuid = dict_string(pad_var, "uuid");
        const std::string refdes = dict_string(pad_var, "refdes");

        ValidationResult r;
        r.severity = Severity::WARNING;
        r.category = "ERC";
        r.fixable = true;  // 可自动修复：分配 / 合并到新网络
        r.entity_uuid = pad_uuid;
        r.location = dict_point(pad_var);
        // 文案：含 refdes 时优先用人可读位号，否则退化为 UUID。
        r.message = refdes.empty()
                        ? ("Unconnected pin " + pad_uuid + " (no net assigned)")
                        : ("Unconnected pin on " + refdes + " (pad " + pad_uuid + ")");
        results.push_back(std::move(r));
    }
    return results;
}

// ============================================================================
// DuplicateRefdesRule
// ============================================================================

std::vector<ValidationResult> DuplicateRefdesRule::check(const ValidationContext& ctx) const {
    std::vector<ValidationResult> results;
    if (ctx.board == nullptr) return results;

    const Variant fps_var = ctx.board->get_meta("erc.footprints");
    const auto& footprints = fps_var.as_array();

    // refdes → 首次出现的 entity_uuid（用于"重复"报错时引用原对象）。
    std::unordered_map<std::string, std::string> first_seen;

    for (const auto& fp_var : footprints) {
        const std::string refdes = dict_string(fp_var, "refdes");
        if (refdes.empty()) continue;  // 未放置，跳过

        const std::string fp_uuid = dict_string(fp_var, "uuid");
        auto it = first_seen.find(refdes);
        if (it == first_seen.end()) {
            first_seen.emplace(refdes, fp_uuid);
            continue;
        }

        // 重复：报当前对象。fixable=false（需用户决定保留哪个）。
        ValidationResult r;
        r.severity = Severity::ERROR;
        r.category = "ERC";
        r.fixable = false;
        r.entity_uuid = fp_uuid;
        r.message = "Duplicate reference designator '" + refdes +
                    "' (conflicts with " + it->second + ")";
        results.push_back(std::move(r));
    }
    return results;
}

// ============================================================================
// PowerGroundConflictRule（占位 —— 多驱动电源/地网络基础判定）
// ============================================================================

std::vector<ValidationResult> PowerGroundConflictRule::check(const ValidationContext& ctx) const {
    std::vector<ValidationResult> results;
    if (ctx.board == nullptr) return results;

    const Variant nets_var = ctx.board->get_meta("erc.nets");
    const auto& nets = nets_var.as_array();

    for (const auto& net_var : nets) {
        const std::string type = dict_string(net_var, "type");
        const bool is_power = (type == "power" || type == "ground");
        if (!is_power) continue;

        const int64_t drivers = dict_int(net_var, "driver_count");
        if (drivers <= 1) continue;  // 单驱动，无冲突

        const std::string name = dict_string(net_var, "name");
        ValidationResult r;
        r.severity = Severity::ERROR;
        r.category = "ERC";
        r.fixable = false;  // 需用户判断电气语义
        r.entity_uuid = name;  // 网络名作引用（PI 接入后改 net UUID）
        r.message = "Power/ground net '" + name + "' has " + std::to_string(drivers) +
                    " drivers (expected single driver)";
        results.push_back(std::move(r));
    }
    return results;
}

}  // namespace eda::validation
