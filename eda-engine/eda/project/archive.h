#pragma once

// eda/project/archive.h
//
// P5 Task 5 · 工程打包归档（.edapkg）—— 容器读写 / 校验门面（Archive）。
//
// 范围：
//   - pack(project_dir, output_path, options)：把整工程目录打包为单文件 .edapkg。
//   - unpack(archive_path, output_dir)：把 .edapkg 还原到目录。
//   - verify(archive_path)：校验容器 magic + manifest 完整 + 资源齐全。
//   - read_manifest(archive_path, out)：仅读 manifest.toml（不解包）。
//
// 归档格式（自研简单 tar-like 容器，不引 minizip / zlib）：
//   全部小端无符号整数；条目顺序写入、读侧线性扫描。
//
//   [magic: 8 字节]      "EDAPKG1\n"  （ASCII，含主版本号 '1'）
//   [entry_count: u32]   条目数（含 manifest.toml 自身）
//   [条目 0]
//   [条目 1]
//   ...
//   [条目 N-1]            约定：manifest.toml 作为最后一条写入（读侧不依赖顺序）
//
//   每个条目：
//     [name_len: u32]     相对工程根的正斜杠路径字节数（UTF-8，无 NUL）
//     [name: name_len]    路径字符串
//     [data_len: u64]     内容字节数
//     [data: data_len]    原始字节
//
// 设计要点：
//   - 自包含 / 可离线移交：工程目录拷贝到任何机器解包即可打开；无绝对路径 / 盘符
//     入库；res:// 引用在 manifest 中冻结版本（lib://name@version → libraries[]）。
//   - 三种归档模式（ArchiveMode）：
//       FULL         全量归档（默认）：project.toml + libs + 附件 + 资源 +
//                    板 / 图页文本；排除 .eda-snapshots/（历史快照不入归档，
//                    除非 options.include_hidden）。
//       PRODUCTION   生产归档：仅 project.toml + 生产输出目录（output/ gerber/
//                    bom/ manufacturing/ 下文件），用于交付制造文件。
//       SNAPSHOT     快照归档：仅 project.toml，最小集，便于审查 / 邮件分发。
//   - manifest.toml 内的 resources[] 列出全部条目（manifest.toml 自身除外），
//     path 形如 "res://project.toml"；verify 据此交叉校验文件齐全。
//   - 依赖库冻结：Project::dependencies() 的 "lib://name@version" 全部写入
//     manifest.libraries[]；如本地 libs/ 下有对应文件，作为普通条目一同入库。
//   - 签名占位：manifest.signature 首版固定空串，预留给 P17 数字签名。
//
// 依赖：eda/project/project.h（ProjectTree / ProjectLoader）、
//       eda/project/archive_manifest.h（ArchiveManifest）、std::filesystem。
// 不引 minizip / zlib；不改 CMakeLists.txt。

#include <cstdint>
#include <string>
#include <vector>

#include "eda/project/archive_manifest.h"

namespace eda {

// ============================================================================
// 归档模式
// ============================================================================

enum class ArchiveMode {
    FULL,        // 完整归档：project.toml + libs + 附件 + 资源 + 板 / 图页文本
    PRODUCTION,  // 生产归档：project.toml + 生产输出目录
    SNAPSHOT,    // 快照归档：仅 project.toml
};

// 归档可序列化用字符串名：FULL -> "full"，PRODUCTION -> "production"，SNAPSHOT -> "snapshot"。
std::string_view to_string_view(ArchiveMode m);
// 反序列化：未识别字符串默认 FULL（前向兼容）。
ArchiveMode parse_archive_mode(std::string_view s);

// ============================================================================
// 归档选项
// ============================================================================

struct ArchiveOptions {
    // 归档模式（默认 FULL）。
    ArchiveMode mode = ArchiveMode::FULL;

    // 引擎版本字符串（写入 manifest.eda_version）。空则用 kDefaultEdaVersion。
    std::string eda_version;

    // 是否包含隐藏目录 / 文件（.eda-snapshots/ / .git/ 等）。默认 false。
    // SNAPSHOT / PRODUCTION 模式下本字段被忽略（按各自规则筛选）。
    bool include_hidden = false;

    // 默认引擎版本号（manifest.eda_version 为空时回退到此值）。
    static constexpr const char* kDefaultEdaVersion = "0.1.0";
};

// ============================================================================
// Archive —— .edapkg 容器读写 / 校验门面（全静态）
// ============================================================================

class Archive {
public:
    // 容器 magic（8 字节，含主版本号 '1' 与换行）。
    static constexpr const char* kMagic = "EDAPKG1\n";
    static constexpr size_t kMagicLen = 8;

    // 当前容器格式主版本（magic 第 7 字节 '1'）。
    static constexpr uint32_t kFormatVersion = 1;

    // ------------------------------------------------------------------
    // 打包
    // ------------------------------------------------------------------
    // 把 project_dir 整工程打包为 output_path 的 .edapkg。
    //   - project_dir 必须存在且其下有 project.toml；否则返回 false。
    //   - output_path 的父目录必须可写；若文件已存在则覆盖。
    //   - 失败（IO 错 / 解析 project.toml 失败）返回 false。
    // 成功后 manifest.toml 内嵌于 .edapkg 末尾，记录全部资源 + 依赖库版本。
    static bool pack(const std::string& project_dir,
                     const std::string& output_path,
                     const ArchiveOptions& options);

    // ------------------------------------------------------------------
    // 解包
    // ------------------------------------------------------------------
    // 把 archive_path 的 .edapkg 还原到 output_dir。
    //   - output_dir 不存在则创建（含父目录）。
    //   - archive_path 不存在或 magic 不匹配返回 false。
    //   - 解包后 output_dir 内文件结构 = 打包时的工程目录（含 project.toml）。
    static bool unpack(const std::string& archive_path,
                       const std::string& output_dir);

    // ------------------------------------------------------------------
    // 校验
    // ------------------------------------------------------------------
    // 校验 archive_path 的完整性：
    //   1) magic 匹配；
    //   2) 容器内含 manifest.toml 条目；
    //   3) manifest 可解析；
    //   4) manifest.resources 中每个 res:// 路径都能在容器条目中找到，且 size 匹配；
    //   5) 反向：容器内除 manifest.toml 外的每个条目都在 manifest.resources 中登记。
    // 全部通过返回 true；任一环节失败返回 false。
    static bool verify(const std::string& archive_path);

    // ------------------------------------------------------------------
    // 仅读 manifest（不解包）
    // ------------------------------------------------------------------
    // 从 archive_path 读出 manifest.toml 并解析到 out。失败（文件不存在 /
    // magic 错 / 无 manifest 条目 / 解析失败）返回 false。
    static bool read_manifest(const std::string& archive_path,
                              ArchiveManifest& out);
};

}  // namespace eda
