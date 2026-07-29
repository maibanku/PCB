#pragma once

// eda/formats/kicad_pcb_parser.h
//
// P16 格式兼容 · KiCad .kicad_pcb S-expression 解析器（骨架）。
// docs/16-格式兼容/01-KiCad格式/README.md。
//
// 输入：KiCad v6/v7/v8 `.kicad_pcb` 文本（S-expression）。
// 输出：填充 eda::pcb::Board（outline / tracks / pads / vias / nets）。
//
// MVP 范围（手动 S-expression 递归下降解析）：
//   (kicad_pcb
//     (layers ...)                         // 解析但仅用作 layer_id 字典（不强校验）
//     (net N "name")                       // 网络声明 → Board.add_net(name)
//     (segment (start X Y) (end X Y)
//              (width W) (layer "F.Cu")
//              (net N))                    // → Track（点对 + width + layer_id + net_uuid）
//     (via (at X Y) (size D) (drill D)
//          (layers "F.Cu" "B.Cu") (net N)) // → Via（at + drill + pad + 层对 + net_uuid）
//     (footprint "lib:foot" (layer "F.Cu")
//        (at X Y [R])
//        (property "Reference" "R1" ...)
//        (pad "1" smd rect (at X Y) (size W H)
//             (layers "F.Cu" "F.Mask") (net N "name")))  // → Pad + FootprintInstance
//     (gr_line (start X Y) (end X Y)
//              (layer "Edge.Cuts"))        // → 板框 outline（链式拼接为 Polygon）
//   )
//
// 坐标：KiCad 内部 mm → 内部 nm（mm_to_nm，见 eda/geometry/units.h）。
//
// 解析失败（括号不匹配、非 kicad_pcb 顶层、EOF 截断）→ 返回 false；
// 单条记录缺字段（如 segment 无 width）→ 记 warning 并跳过该条，整体仍返回 true。
//
// 命名规范：namespace eda::formats、PascalCase 类型、snake_case 方法、
//           成员尾下划线 _、include 从仓库根。

#include <cstddef>
#include <string>
#include <vector>

#include "eda/pcb/board.h"

namespace eda::formats {

// ============================================================================
// KiCadPcbParseStats —— 解析过程的统计与诊断信息。
// KiCadPcbParser 在解析过程中填充；KiCadImporter 据此生成 FidelityReport。
// ============================================================================
struct KiCadPcbParseStats {
    // 计数（原始文件中 vs 成功填入 Board 中）
    std::size_t nets_total = 0;
    std::size_t nets_imported = 0;          // 实际 add_net 的数量（跳过空名 net 0）
    std::size_t segments_total = 0;
    std::size_t segments_imported = 0;
    std::size_t vias_total = 0;
    std::size_t vias_imported = 0;
    std::size_t pads_total = 0;             // 所有 footprint 内 pad 总数
    std::size_t pads_imported = 0;
    std::size_t footprints_total = 0;
    std::size_t footprints_imported = 0;
    std::size_t outline_segments_total = 0; // Edge.Cuts 上的 gr_line
    std::size_t outline_segments_imported = 0;

    // 诊断
    std::vector<std::string> warnings;          // 非致命跳过原因（per-record）
    std::vector<std::string> unsupported_kinds; // 识别到但不导入的图元（gr_arc/gr_text 等）
    std::string error_message;                  // 致命错误（括号不匹配等）
};

// ============================================================================
// KiCadPcbParser —— 手写 S-expression 递归下降解析 + Board 填充。
// ============================================================================
class KiCadPcbParser {
public:
    KiCadPcbParser();
    ~KiCadPcbParser();

    // 从文件路径读取并解析；成功返回 true。
    // 失败时填 stats->error_message 并返回 false（Board 可能已被部分填充）。
    bool parse(const std::string& file_path, pcb::Board& out,
               KiCadPcbParseStats* stats = nullptr);

    // 直接解析 .kicad_pcb 文本；成功返回 true。
    bool parse_string(const std::string& text, pcb::Board& out,
                      KiCadPcbParseStats* stats = nullptr);
};

}  // namespace eda::formats
