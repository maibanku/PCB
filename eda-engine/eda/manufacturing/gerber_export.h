#pragma once

// eda/manufacturing/gerber_export.h
//
// P13 制造输出 · Gerber 端到端导出器（Board → 多层 Gerber + Excellon + Pick&Place）。
// 13-制造输出/04-导出流水线（集成）。
//
// GerberExporter 把 pcb::Board 一次性落盘为一整套工厂可用的制造文件：
//   - 每个铜层（F.Cu / B.Cu / Inner*.Cu）→ {project}_{layer}.gbr
//   - 顶层/底层阻焊（F.Mask / B.Mask）→ {project}_{layer}.gbr（骨架：复用对应铜层 Track/Pad）
//   - 顶层丝印（F.SilkS）→ {project}_F_SilkS.gbr（骨架：复用 F.Cu）
//   - 板框 outline → {project}_outline.gbr（GerberWriter::write_outline）
//   - Excellon 钻孔（PTH）→ {project}_drl.drl（ExcellonWriter::write_pth）
//   - Pick & Place → {project}_pickplace.csv（PickPlaceWriter::write）
//
// 文件名约定：{project}_{layer_token}.gbr，layer_token 由 sanitize_layer_id 把
//   "F.Cu" → "F_Cu"（点号 → 下划线），保证跨平台文件名安全。
// project 取自 Board.name()，同样经过 sanitize_layer_id 规整（空名回退 "board"）。
//
// 信号（观察者，解耦 UI/CLI）：
//   - file_written(string filename)  每写完一个文件触发（filename 仅文件名，不含目录）
//   - export_completed(ExportResult)  全部完成后触发（含成功标志与文件列表）
//
// verify_roundtrip(output_dir) —— 简单回归校验：检查上次 export_all 写出的每个文件
//   在 output_dir 下仍然存在且 size > 0（可读性简化为非空）。
//
// 依赖关系：
//   - eda_manufacturing（GerberWriter / ExcellonWriter / PickPlaceWriter）
//   - eda_pcb（Board::add_track / add_pad / add_via 等非内联方法，仅在测试组装 Board 时）
//   - eda_core（Signal）
//
// 命名规范：namespace eda::mfg、PascalCase 类型、snake_case 方法、成员尾下划线 _、
//           include 从仓库根。

#include <functional>
#include <string>
#include <vector>

#include "core/object/signal.h"

namespace eda::pcb {
class Board;
}  // namespace eda::pcb

namespace eda::mfg {

// ============================================================================
// ExportResult —— 一次 export_all 的结果汇总。
// ============================================================================
struct ExportResult {
    std::vector<std::string> files_written;  // 已写入文件名列表（仅文件名，不含目录）
    int total_files = 0;                      // = files_written.size()（冗余字段便于日志）
    bool success = false;                     // 全部写入成功为 true
    std::string error;                        // 失败原因（成功时为空）
};

// ============================================================================
// GerberExporter —— Board → 制造文件目录的端到端流水线。
//
// 非可拷贝（Signal 不可拷贝/移动，作为成员直接持有）；使用方法：默认构造后多次调用
// export_all 即可，files_written_ 每次刷新。
// ============================================================================
class GerberExporter {
public:
    GerberExporter();
    ~GerberExporter();

    GerberExporter(const GerberExporter&) = delete;
    GerberExporter& operator=(const GerberExporter&) = delete;

    // ---- 主接口 ----
    // 把 board 的全套制造文件写入 output_dir（目录不存在则创建）。
    // 返回 ExportResult：成功时 success=true 且 files_written 列出全部已写文件。
    ExportResult export_all(const pcb::Board& board, const std::string& output_dir);

    // 回归校验：检查上次 export_all 写出的每个文件在 output_dir 下存在且非空。
    // 未曾导出过（files_written_ 为空）时返回 false。
    bool verify_roundtrip(const std::string& output_dir) const;

    // ---- 信号 ----
    // 每写完一个文件触发（参数 = 文件名，不含目录）。
    // 多订阅者（eda::Signal 支持 connect 多个回调）；std::string 与 Variant 兼容。
    Signal<std::string> file_written;
    // 全部写完后触发一次（参数 = ExportResult，含 files_written 与 success）。
    // ExportResult 是 POD 结构，不可装箱为 eda::Variant（Signal 要求 to_variant/unpack_arg
    // 双向重载），故此处采用 std::function 单订阅者回调；多订阅者可在回调内自行分发。
    std::function<void(const ExportResult&)> export_completed;

    // ---- 工具 ----
    // layer_id / 项目名 → 文件名安全片段：非 [A-Za-z0-9_-] 字符替换为 '_'。
    //   "F.Cu"     → "F_Cu"
    //   "F.SilkS"  → "F_SilkS"
    //   "my board" → "my_board"
    static std::string sanitize_layer_id(const std::string& layer_id);

    // 访问上次导出的文件名列表（仅文件名）。
    const std::vector<std::string>& files_written() const { return files_written_; }

private:
    std::vector<std::string> files_written_;  // 上次 export_all 写入的文件名（仅名）
};

}  // namespace eda::mfg
