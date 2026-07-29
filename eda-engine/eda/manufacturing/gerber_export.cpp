// eda/manufacturing/gerber_export.cpp
//
// P13 制造输出 · Gerber 端到端导出器实现。
//
// 流水线：Board → (GerberWriter / ExcellonWriter / PickPlaceWriter) → 文件落盘。
// 每个 write_* 调用返回 std::string 文本；本文件负责创建目录、拼接文件名、写盘、
// 维护 files_written_ 列表，并通过 Signal 通知观察者。

#include "eda/manufacturing/gerber_export.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "eda/manufacturing/excellon_writer.h"
#include "eda/manufacturing/gerber_writer.h"
#include "eda/manufacturing/pick_place_writer.h"
#include "eda/pcb/board.h"
#include "eda/pcb/layer.h"

namespace eda::mfg {

namespace {

// 把 content 以文本模式写入 dir/filename；返回 true 成功。
// 失败时通过 err 写入错误描述（不抛异常）。
bool write_text_file(const std::filesystem::path& path,
                     const std::string& content,
                     std::string& err) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        err = "cannot open for write: " + path.string();
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) {
        err = "write failed: " + path.string();
        return false;
    }
    out.close();
    if (!out) {
        err = "close failed: " + path.string();
        return false;
    }
    return true;
}

}  // namespace

// ============================================================================
// GerberExporter
// ============================================================================

GerberExporter::GerberExporter() = default;
GerberExporter::~GerberExporter() = default;

// ----------------------------------------------------------------------------
// sanitize_layer_id
// ----------------------------------------------------------------------------

std::string GerberExporter::sanitize_layer_id(const std::string& layer_id) {
    std::string out;
    out.reserve(layer_id.size());
    for (char c : layer_id) {
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-') {
            out.push_back(c);
        } else {
            // 点号、空格、点等 → 下划线（"F.Cu" → "F_Cu"）
            out.push_back('_');
        }
    }
    if (out.empty()) out = "layer";
    return out;
}

// ----------------------------------------------------------------------------
// export_all
// ----------------------------------------------------------------------------

ExportResult GerberExporter::export_all(const pcb::Board& board,
                                        const std::string& output_dir) {
    ExportResult result;
    files_written_.clear();

    // ---- 1. 创建输出目录（幂等：已存在不报错）----
    std::filesystem::path dir(output_dir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        result.success = false;
        result.error = "create_directories failed: " + ec.message();
        if (export_completed) export_completed(result);
        return result;
    }

    // ---- 2. 项目名（用于文件名前缀）----
    std::string project = board.name();
    if (project.empty()) project = "board";
    project = sanitize_layer_id(project);
    if (project.empty()) project = "board";

    // 子写入器（默认配置：Gerber 3:4 mm，Excellon 3:3 mm，CSV 4 位小数）
    const GerberWriter gerber;
    const ExcellonWriter excellon;
    const PickPlaceWriter pick_place;

    // 辅助：写一个文件，成功则记录 + 触发 file_written 信号
    auto emit_file = [&](const std::string& filename,
                         const std::string& content) -> bool {
        const auto path = dir / filename;
        std::string err;
        if (!write_text_file(path, content, err)) {
            result.success = false;
            result.error = err;
            return false;
        }
        files_written_.push_back(filename);
        file_written.emit(filename);
        return true;
    };

    // ---- 3. 铜层 Gerber（每个 COPPER 层一份）----
    // 同时记录顶层/底层 layer_id，供阻焊/丝印复用。
    std::string top_copper;     // 第一个铜层（copper_index==0 或栈顶铜层）
    std::string bottom_copper;  // 最后一个铜层
    for (std::size_t i = 0; i < board.layer_count(); ++i) {
        const auto& layer = board.layer(i);
        if (!layer.is_copper()) continue;
        const std::string& lid = layer.layer_id();
        if (top_copper.empty()) top_copper = lid;
        bottom_copper = lid;

        const std::string fn =
            project + "_" + sanitize_layer_id(lid) + ".gbr";
        if (!emit_file(fn, gerber.write_layer(board, lid))) {
            if (export_completed) export_completed(result);
            return result;
        }
    }

    // ---- 4. 阻焊（骨架：复用对应铜层的 Track/Pad）----
    // F.Mask ← F.Cu 内容；B.Mask ← B.Cu 内容。
    if (!top_copper.empty()) {
        const std::string fn =
            project + "_F_Mask.gbr";
        if (!emit_file(fn, gerber.write_layer(board, top_copper))) {
            if (export_completed) export_completed(result);
            return result;
        }
    }
    if (!bottom_copper.empty() && bottom_copper != top_copper) {
        const std::string fn =
            project + "_B_Mask.gbr";
        if (!emit_file(fn, gerber.write_layer(board, bottom_copper))) {
            if (export_completed) export_completed(result);
            return result;
        }
    }

    // ---- 5. 丝印（骨架：复用顶层铜层 Track/Pad）----
    if (!top_copper.empty()) {
        const std::string fn =
            project + "_F_SilkS.gbr";
        if (!emit_file(fn, gerber.write_layer(board, top_copper))) {
            if (export_completed) export_completed(result);
            return result;
        }
    }

    // ---- 6. 板框 outline ----
    {
        const std::string fn = project + "_outline.gbr";
        if (!emit_file(fn, gerber.write_outline(board))) {
            if (export_completed) export_completed(result);
            return result;
        }
    }

    // ---- 7. Excellon 钻孔（PTH；NPTH 骨架为空，不单独落盘）----
    {
        const std::string fn = project + "_drl.drl";
        if (!emit_file(fn, excellon.write_pth(board))) {
            if (export_completed) export_completed(result);
            return result;
        }
    }

    // ---- 8. Pick & Place CSV ----
    {
        const std::string fn = project + "_pickplace.csv";
        if (!emit_file(fn, pick_place.write(board))) {
            if (export_completed) export_completed(result);
            return result;
        }
    }

    // ---- 完成 ----
    result.files_written = files_written_;
    result.total_files = static_cast<int>(files_written_.size());
    result.success = true;
    if (export_completed) export_completed(result);
    return result;
}

// ----------------------------------------------------------------------------
// verify_roundtrip
// ----------------------------------------------------------------------------

bool GerberExporter::verify_roundtrip(const std::string& output_dir) const {
    if (files_written_.empty()) return false;
    const std::filesystem::path dir(output_dir);
    std::error_code ec;
    for (const auto& name : files_written_) {
        const auto path = dir / name;
        // 存在 + 普通文件 + 大小 > 0
        if (!std::filesystem::exists(path, ec)) return false;
        const std::uintmax_t sz = std::filesystem::file_size(path, ec);
        if (ec) return false;
        if (sz == 0) return false;
    }
    return true;
}

}  // namespace eda::mfg
