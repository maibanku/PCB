#pragma once

// eda/validation/drc.h
//
// P11 DRC 基础规则（设计规则检查，11-01 统一校验中心 / PCB 物理规则）。
//
// 四条骨架规则（各 : ValidationRule）：
//   1. ClearanceRule    —— 铜→铜间距 < min_clearance（Track-Track / Track-Pad / Pad-Pad）
//   2. MinWidthRule     —— Track.width < min_width
//   3. ShortCircuitRule —— 不同 Net 的图元几何重叠（Track 中心线相交 / Pad-Box 重叠 / Track 穿入 Pad）
//   4. UnroutedNetRule  —— Net 含引脚（Pad/Via 成员）但无任何 Track 成员
//
// 与 ERC（erc.h）不同：DRC 规则**直接下转 pcb::Board** 访问 Track/Pad/Via/Net 的真实
// 几何数据。这是 validation.h 顶部预留的集成路径：
//   "集成阶段若需直查 pcb::Board，可由派生 Rule 在 .cpp 内 include
//    pcb 头并以 is_class + static_cast 下转，新增独立链接关系，不污染本骨架。"
// 故本规则的链接关系在 eda/validation/CMakeLists.txt 显式声明（eda_pcb + eda_geometry）。
//
// 检测算法（与 P3 几何内核对接）：
//   - **ClearanceRule（邻域 + 精确距离）**：
//       1) 把每条 Track / 每个 Pad 的包围盒装入 RTree（geometry::RTree<int>）。
//       2) 对每个图元 i，以其 AABB 外扩 min_clearance 查询邻居 j（邻域预筛选）。
//       3) 对候选 (i, j)（j > i，同层，非同 Net）跑精确铜距：
//          * Track-Track : Σ段对的 segment_segment_distance − (w1+w2)/2
//                          （segment_segment_intersect 命中即 0）
//          * Track-Pad   : min 段到 Pad-Box 的 segment_to_box − w/2
//          * Pad-Pad     : box_to_box 距离
//       4) 铜距 < min_clearance → ERROR（location 取违例中点，entity 引用第二条图元 UUID）。
//   - **MinWidthRule**：直接逐条 Track 比较 path.width 与 min_width，<min_width 即 ERROR。
//   - **ShortCircuitRule（不同 Net 几何重叠）**：
//       * Track-Track 同层异 Net：中心线段 segment_segment_intersect 命中 → ERROR。
//       * Pad-Pad 同层异 Net：bounding_box 互相 intersects → ERROR。
//       * Track-Pad 同层异 Net：Track 任一段穿入 Pad-Box（segment_to_box == 0）→ ERROR。
//   - **UnroutedNetRule**：逐 Net 分类其成员 UUID（Board::find_track/find_pad/find_via），
//     若含 Pad/Via 成员但 0 个 Track 成员 → WARNING（可修复 = 添加布线）。
//
// 边界（骨架阶段，文档化简化）：
//   - Via 暂不参与 DRC（按 spec 仅 Track/Pad；后续可对称加入，Via 的 pad_diameter 视为 Pad）。
//   - 跨层不检查（层间间距由 08-07 盲埋孔 / 叠层专项规则负责）。
//   - Pad 形状精度：CIRCLE 用 width=直径，RECT/OVAL/OCTAGON 用 AABB（box 距离精确）。
//   - 去重归并延后到 11-04 面板层（与 ValidationEngine 边界一致）。
//
// 命名规范：namespace eda::validation、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <vector>

#include "eda/geometry/types.h"
#include "eda/validation/validation.h"

namespace eda::validation {

// ============================================================================
// DrcOptions —— DRC 规则参数（最小间距 + 最小线宽，单位 nm）。
//
// 默认值：min_clearance = 200 μm（200_000 nm），min_width = 150 μm（150_000 nm）。
// 取值参考 IPC-2222 "外层最小间距" 与常规 PCB 厂工艺能力（骨架默认，可被覆写）。
// ============================================================================
struct DrcOptions {
    geometry::Coord min_clearance_nm = 200'000;   // 铜→铜最小间距（nm）
    geometry::Coord min_width_nm     = 150'000;   // 走线最小宽度（nm）
};

// ============================================================================
// ClearanceRule —— 铜→铜间距 < min_clearance（Track-Track / Track-Pad / Pad-Pad）。
// 严重度：ERROR（间距不足可能导致焊锡桥接 / 漏电）。
// 算法：R-tree 邻域预筛选 + 精确段/盒距离（见上）。
// ============================================================================
class ClearanceRule : public ValidationRule {
public:
    ClearanceRule() = default;
    explicit ClearanceRule(DrcOptions options) : options_(options) {}

    std::string name() const override { return "drc.clearance"; }

    std::vector<ValidationResult> check(const ValidationContext& ctx) const override;

    const DrcOptions& options() const { return options_; }
    void set_options(DrcOptions o) { options_ = o; }

    static std::string get_class_static() { return "ClearanceRule"; }
    std::string get_class() const override { return "ClearanceRule"; }
    bool is_class(const std::string& n) const override {
        return n == "ClearanceRule" || ValidationRule::is_class(n);
    }

private:
    DrcOptions options_;
};

// ============================================================================
// MinWidthRule —— Track.width < min_width。
// 严重度：ERROR（细于工艺能力的走线易断 / 易蚀刻过头）。
// ============================================================================
class MinWidthRule : public ValidationRule {
public:
    MinWidthRule() = default;
    explicit MinWidthRule(DrcOptions options) : options_(options) {}

    std::string name() const override { return "drc.min_width"; }

    std::vector<ValidationResult> check(const ValidationContext& ctx) const override;

    const DrcOptions& options() const { return options_; }
    void set_options(DrcOptions o) { options_ = o; }

    static std::string get_class_static() { return "MinWidthRule"; }
    std::string get_class() const override { return "MinWidthRule"; }
    bool is_class(const std::string& n) const override {
        return n == "MinWidthRule" || ValidationRule::is_class(n);
    }

private:
    DrcOptions options_;
};

// ============================================================================
// ShortCircuitRule —— 不同 Net 的图元几何重叠（短路）。
// 严重度：ERROR（直接电气短路，最严重违例）。
// 判定：同层 + 异 Net + 图元几何重叠（Track 中心线相交 / Pad-Box 相交 / Track 穿入 Pad-Box）。
// ============================================================================
class ShortCircuitRule : public ValidationRule {
public:
    std::string name() const override { return "drc.short_circuit"; }

    std::vector<ValidationResult> check(const ValidationContext& ctx) const override;

    static std::string get_class_static() { return "ShortCircuitRule"; }
    std::string get_class() const override { return "ShortCircuitRule"; }
    bool is_class(const std::string& n) const override {
        return n == "ShortCircuitRule" || ValidationRule::is_class(n);
    }
};

// ============================================================================
// UnroutedNetRule —— Net 含引脚（Pad/Via 成员）但无任何 Track 成员。
// 严重度：WARNING（强烈建议补布线；可自动修复 = 走 auto-router / 手工连线）。
// ============================================================================
class UnroutedNetRule : public ValidationRule {
public:
    std::string name() const override { return "drc.unrouted_net"; }

    std::vector<ValidationResult> check(const ValidationContext& ctx) const override;

    static std::string get_class_static() { return "UnroutedNetRule"; }
    std::string get_class() const override { return "UnroutedNetRule"; }
    bool is_class(const std::string& n) const override {
        return n == "UnroutedNetRule" || ValidationRule::is_class(n);
    }
};

}  // namespace eda::validation
