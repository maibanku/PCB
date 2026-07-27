#pragma once

// eda/project/snapshot.h
//
// P5 Task 2 · 版本快照（Version Snapshot）
//
// 范围：工程级"另存为"快照，与 Git（文件级版本控制）互补。
//   - capture：把当前 ProjectTree 序列化为 TOML，落盘到 <project>/.eda-snapshots/
//   - list：列出所有快照（按 created_time 倒序，最新在前）
//   - restore：把指定快照内容覆盖回 ProjectTree（回滚）
//   - diff：两份快照差异（占位：仅 boards / pages 数量；深 diff 后续 Task）
//   - prune：上限清理（默认 100 份，超限丢最旧）
//
// 存储格式：
//   路径：<project_dir>/.eda-snapshots/<created_time_padded>-<sanitized_label>.toml
//   正文：首部以 `# eda-snapshot <key> = <value>` 注释行携带元信息
//         （schema_version / uuid / created_time / note / parent_uuid），
//         随后是 ProjectSaver 序列化的 project.toml 正文。
//         ProjectLoader 的行扫描器对每行做 strip_comment，整行 `#` 注释会被
//         抹成空行后跳过，因此元信息块对加载器透明——快照文件既是可恢复的
//         工程树，又是携带元数据的独立单元。
//
// 时间戳：Unix 秒（UTC），与 project.toml 的 modified_time 一致。同秒内的多次
//   capture 由文件系统 last_write_time 做 tie-breaker，list / prune 排序稳定。
//
// 依赖：eda/project/project.h（ProjectTree + ProjectSaver/Loader）。
// 不改 CMakeLists.txt：eda/project/snapshot.cpp 由主循环加入 eda_project target。

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "eda/project/project.h"

namespace eda {

// ============================================================================
// SnapshotInfo —— 单次快照的元信息（list 返回；不含工程树正文，避免拷贝整棵树）
// ============================================================================

struct SnapshotInfo {
    // 快照自身的 UUID（区别于工程 / 板 / 页的 UUID）。作为稳定 snapshot_id，
    // 供 restore / diff 引用。
    std::string uuid;

    // 目录内基名（如 "00000000001754496000-first.toml"），用于内部寻址。
    std::string filename;

    // 捕获时间（Unix 秒，UTC）。list 按此字段倒序排列。
    int64_t created_time = 0;

    // 用户标签（原样保留，未做文件名安全化）。
    std::string note;

    // 上一份快照的 UUID（链式时间线；首份为空串）。
    std::string parent_uuid;

    // 快照文件字节数。
    int64_t size = 0;
};

// ============================================================================
// SnapshotDiff —— 两份快照的差异（占位实现）
// ============================================================================

struct SnapshotDiff {
    int board_count_a = 0;  // 快照 A 的板数
    int board_count_b = 0;  // 快照 B 的板数
    int page_count_a = 0;   // 快照 A 的图页总数（所有板求和）
    int page_count_b = 0;   // 快照 B 的图页总数

    // delta = b - a：正值表示 B 比 A 多。
    int board_delta() const { return board_count_b - board_count_a; }
    int page_delta() const { return page_count_b - page_count_a; }
};

// ============================================================================
// SnapshotManager —— 绑定一个工程目录，管理其下全部快照
// ============================================================================

class SnapshotManager {
public:
    // 默认上限：超过则丢最旧（prune 按 created_time 升序丢首份）。
    static constexpr size_t kDefaultLimit = 100;

    // 绑定工程目录：快照存于 <project_dir>/.eda-snapshots/。
    explicit SnapshotManager(std::string project_dir);

    // 同上，并指定上限。
    SnapshotManager(std::string project_dir, size_t limit);

    // 捕获当前工程树快照。label 为用户可读标签（文件名会做安全化，note 原样）。
    // 写入失败（目录不可写 / 磁盘满等）返回空 optional。
    // 写入成功后立即 prune 到上限（可能删掉刚写入之外的最旧条目）。
    std::optional<SnapshotInfo> capture(const ProjectTree& tree,
                                        const std::string& label);

    // 列出所有快照，按 created_time 倒序（同秒按文件系统写入时间倒序）。
    // 目录不存在或为空时返回空 vector。非快照 .toml（无元信息头）被跳过。
    std::vector<SnapshotInfo> list() const;

    // 回滚：把指定快照内容覆盖到 tree（ProjectLoader 会重置 root 再填充）。
    // snapshot_id 即 SnapshotInfo::uuid。找不到对应快照返回 false。
    // 注意：本方法不自动捕获"恢复前"快照；调用方需要时应先手动 capture。
    bool restore(ProjectTree& tree, const std::string& snapshot_id) const;

    // 占位 diff：boards / pages 数量差异。深 diff（字段级 / 树结构 diff）后续 Task。
    // snapshot_id 即 SnapshotInfo::uuid。找不到的快照对应计数字段为 0。
    SnapshotDiff diff(const std::string& snapshot_a_id,
                      const std::string& snapshot_b_id) const;

    // 上限读写。
    size_t limit() const { return limit_; }
    void set_limit(size_t n) { limit_ = n; }

    // 路径访问。
    const std::string& project_dir() const { return project_dir_; }
    // 拼接：<project_dir>/.eda-snapshots（正斜杠）。
    std::string snapshots_dir() const;

private:
    // 上限清理：infos 须已按 list() 规则排序（倒序，最新在前）。
    // 从末尾丢最旧直到 infos->size() <= limit_；同步删盘上文件。
    // 返回实际删除的文件数。
    size_t prune(std::vector<SnapshotInfo>* infos);

    std::string project_dir_;
    size_t limit_ = kDefaultLimit;
};

}  // namespace eda
