// eda/formats/kicad_importer.cpp
//
// P16 格式兼容 · KiCadImporter 高层导入实现（骨架）。
//
// 职责：
//   1. 调 KiCadPcbParser 完成 S-expression 解析与 Board 填充；
//   2. 把 KiCadPcbParseStats 映射为 FidelityReport（用户层视图）；
//   3. 暴露 import_file / import_string 单一入口，方便迁移向导调用。
//
// 命名规范：namespace eda::formats、PascalCase 类型、snake_case 方法、
//           成员尾下划线 _、include 从仓库根。

#include "eda/formats/kicad_importer.h"

#include <string>
#include <utility>

#include "eda/formats/kicad_pcb_parser.h"
#include "eda/pcb/board.h"

namespace eda::formats {

namespace {

// 把 parser 的内部统计拷贝进 FidelityReport。
void copy_stats_to_report(const KiCadPcbParseStats& s, FidelityReport& r) {
    r.nets_total = s.nets_total;
    r.nets_imported = s.nets_imported;
    r.segments_total = s.segments_total;
    r.segments_imported = s.segments_imported;
    r.vias_total = s.vias_total;
    r.vias_imported = s.vias_imported;
    r.pads_total = s.pads_total;
    r.pads_imported = s.pads_imported;
    r.footprints_total = s.footprints_total;
    r.footprints_imported = s.footprints_imported;
    r.outline_segments_total = s.outline_segments_total;
    r.outline_segments_imported = s.outline_segments_imported;
    r.warnings = s.warnings;
    r.unsupported_kinds = s.unsupported_kinds;
}

}  // namespace

KiCadImporter::KiCadImporter() = default;
KiCadImporter::~KiCadImporter() = default;

bool KiCadImporter::import_file(const std::string& file_path, pcb::Board& out,
                                FidelityReport& report) {
    report = FidelityReport{};
    KiCadPcbParser parser;
    KiCadPcbParseStats stats;
    const bool ok = parser.parse(file_path, out, &stats);
    copy_stats_to_report(stats, report);
    report.parsed_ok = ok;
    if (!ok) report.error_message = stats.error_message;
    return ok;
}

bool KiCadImporter::import_string(const std::string& text, pcb::Board& out,
                                  FidelityReport& report) {
    report = FidelityReport{};
    KiCadPcbParser parser;
    KiCadPcbParseStats stats;
    const bool ok = parser.parse_string(text, out, &stats);
    copy_stats_to_report(stats, report);
    report.parsed_ok = ok;
    if (!ok) report.error_message = stats.error_message;
    return ok;
}

// 单行人类可读摘要，给向导 UI / 命令行直接展示。
std::string FidelityReport::summary() const {
    std::string s = source_format + ": ";
    if (!parsed_ok) {
        s += "FAILED (" + error_message + ")";
        return s;
    }
    s += "OK | nets " + std::to_string(nets_imported) + "/" + std::to_string(nets_total);
    s += " | segs " + std::to_string(segments_imported) + "/" + std::to_string(segments_total);
    s += " | vias " + std::to_string(vias_imported) + "/" + std::to_string(vias_total);
    s += " | pads " + std::to_string(pads_imported) + "/" + std::to_string(pads_total);
    s += " | fps " + std::to_string(footprints_imported) + "/" + std::to_string(footprints_total);
    s += " | outline " + std::to_string(outline_segments_imported) + "/" +
         std::to_string(outline_segments_total);
    if (!unsupported_kinds.empty()) {
        s += " | unsupported:";
        for (const auto& u : unsupported_kinds) {
            s += " " + u;
        }
    }
    if (!warnings.empty()) {
        s += " | warnings=" + std::to_string(warnings.size());
    }
    return s;
}

}  // namespace eda::formats
