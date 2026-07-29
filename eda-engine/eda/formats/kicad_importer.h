#pragma once

// eda/formats/kicad_importer.h
//
// P16 格式兼容 · KiCad 工程导入高层入口（骨架）。
// docs/16-格式兼容/01-KiCad格式 + docs/16-格式兼容/09-保真度报告。
//
// 角色：
//   - KiCadPcbParser 负责"文本 → Board"（S-expression 解析与字段映射）；
//   - KiCadImporter   负责"用户/向导层 API"：组合解析器，产出 FidelityReport，
//                     统一文件/字符串入口，并在 UI 可读的摘要中显式说明
//                     哪些元素成功导入、哪些被跳过、哪些尚未支持。
//
// FidelityReport 是 16-09 保真度报告的最小载体：计数差异 + 诊断原因清单。
// 后续 P16 迭代可在此结构上扩展 round-trip 校验、几何偏差度量、字段级映射表等。
//
// 命名规范：namespace eda::formats、PascalCase 类型、snake_case 方法、
//           成员尾下划线 _、include 从仓库根。

#include <cstddef>
#include <string>
#include <vector>

#include "eda/formats/kicad_pcb_parser.h"
#include "eda/pcb/board.h"

namespace eda::formats {

// ============================================================================
// FidelityReport —— 保真度报告（成功/跳过/未支持的元素分类统计）。
// 由 KiCadImporter 在导入完成后填充。
// ============================================================================
struct FidelityReport {
    // 元数据
    std::string source_format = "kicad_pcb";   // 报告所对应的源格式

    // 计数：原始文件 vs 实际入树
    std::size_t nets_total = 0;
    std::size_t nets_imported = 0;
    std::size_t segments_total = 0;
    std::size_t segments_imported = 0;
    std::size_t vias_total = 0;
    std::size_t vias_imported = 0;
    std::size_t pads_total = 0;
    std::size_t pads_imported = 0;
    std::size_t footprints_total = 0;
    std::size_t footprints_imported = 0;
    std::size_t outline_segments_total = 0;
    std::size_t outline_segments_imported = 0;

    // 状态
    bool parsed_ok = false;                    // 解析与映射是否成功完成
    std::string error_message;                 // 失败原因（parsed_ok == false 时）

    // 诊断
    std::vector<std::string> warnings;         // 非致命跳过原因（单条记录级）
    std::vector<std::string> unsupported_kinds; // 识别但不导入的图元种类（如 "gr_arc x3"）

    // 单行可读摘要（用于向导 UI 展示）。
    std::string summary() const;
};

// ============================================================================
// KiCadImporter —— 高层导入入口。
// 用法：
//   KiCadImporter importer;
//   pcb::Board board;
//   FidelityReport report;
//   if (!importer.import_file("foo.kicad_pcb", board, report)) { /* report.error_message */ }
// ============================================================================
class KiCadImporter {
public:
    KiCadImporter();
    ~KiCadImporter();

    // 导入 .kicad_pcb 文件 → Board + FidelityReport；成功返回 true。
    bool import_file(const std::string& file_path, pcb::Board& out, FidelityReport& report);

    // 导入 .kicad_pcb 文本 → Board + FidelityReport；成功返回 true。
    bool import_string(const std::string& text, pcb::Board& out, FidelityReport& report);
};

}  // namespace eda::formats
