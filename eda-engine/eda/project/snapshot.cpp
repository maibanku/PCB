// eda/project/snapshot.cpp
//
// P5 Task 2 · 版本快照实现。
//
// 设计要点：
//   - 快照文件 = TOML 注释元信息块 + ProjectSaver 序列化的 project.toml 正文。
//     ProjectLoader 行扫描器对每行 strip_comment，整行 `#` 被抹空跳过，
//     故元信息块对加载透明——可直接把整个快照文件当 project.toml 加载。
//   - 元信息键固定顺序：schema_version / uuid / created_time / note / parent_uuid
//     （schema 顺序稳定，便于人工查看与未来 grep）。
//   - 文件名：<秒级时间戳 20 位零填充>-<安全化标签>.toml；同秒同标签冲突时
//     追加 -N（N=2,3,...）。排序以 created_time 为主键，文件系统 last_write_time
//     为次键（稳定区分同秒内的多次 capture）。
//   - 目录迭代用 std::filesystem（与 tests 中 temp_directory_path 一致）。
//
// 依赖：eda/project/project.h（generate_uuid / ProjectSaver / ProjectLoader）。

#include "eda/project/snapshot.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/io/path.h"

namespace eda {

namespace {

// ============================================================================
// 常量
// ============================================================================

constexpr std::string_view kSnapshotsSubdir = ".eda-snapshots";
constexpr std::string_view kMetaPrefix = "# eda-snapshot ";  // 注意尾随空格
constexpr std::string_view kMetaKeyUuid = "uuid";
constexpr std::string_view kMetaKeyCreated = "created_time";
constexpr std::string_view kMetaKeyNote = "note";
constexpr std::string_view kMetaKeyParent = "parent_uuid";

constexpr int kSnapshotSchemaVersion = 1;
constexpr size_t kMaxLabelFilenameLen = 48;  // 文件名中标签段的最大长度

// ============================================================================
// 时间 / 文件名工具
// ============================================================================

int64_t unix_now_seconds() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// 20 位零填充：覆盖到约公元 5138 年，字典序与数值序一致。
std::string pad_timestamp(int64_t t) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%020lld",
                  static_cast<long long>(t));
    return buf;
}

// 文件名安全化：仅保留 [A-Za-z0-9_-]，其余转 '_'；空串 -> "untitled"；
// 截断到 kMaxLabelFilenameLen 字符。
std::string sanitize_label(const std::string& label) {
    std::string out;
    out.reserve(label.size());
    for (char c : label) {
        if ((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            c == '_' || c == '-') {
            out += c;
        } else {
            out += '_';
        }
    }
    if (out.empty()) out = "untitled";
    if (out.size() > kMaxLabelFilenameLen) out.resize(kMaxLabelFilenameLen);
    return out;
}

// 路径拼接：直接复用 core/io/path 的 Path::join（任务依赖 core/io/path）。
// Path::join 会做规范化（正斜杠、折叠 . / ..），对工程目录 + 快照子目录 /
// 文件名的拼接语义正确，且与 ProjectLoader/ProjectSaver 的路径处理保持一致。
std::string join_path(const std::string& a, const std::string& b) {
    return Path::join(a, b);
}

// ============================================================================
// TOML 字面量序列化 / 解析（与 project.cpp 的子集保持一致）
// ============================================================================

std::string toml_quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned int>(
                                      static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    out += "\"";
    return out;
}

std::string trim_ws(const std::string& s) {
    size_t b = 0, e = s.size();
    const auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (b < e && is_ws(s[b])) ++b;
    while (e > b && is_ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

bool parse_toml_string_literal(std::string_view raw, std::string& out) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') return false;
    out.clear();
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        const char c = raw[i];
        if (c == '\\') {
            if (i + 1 >= raw.size() - 1) return false;
            const char e = raw[++i];
            switch (e) {
                case '"':  out += '"'; break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                default:   out += e; break;
            }
        } else {
            out += c;
        }
    }
    return true;
}

bool parse_toml_int_literal(std::string_view raw, int64_t& out) {
    if (raw.empty()) return false;
    try {
        size_t pos = 0;
        const long long v = std::stoll(std::string(raw), &pos);
        if (pos != raw.size()) return false;
        out = static_cast<int64_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// 元信息块的写入 / 读取
// ============================================================================

std::string compose_snapshot_text(const SnapshotInfo& info,
                                  const ProjectTree& tree) {
    std::string meta;
    meta.reserve(256);
    meta += "# eda-snapshot schema_version = ";
    meta += std::to_string(kSnapshotSchemaVersion);
    meta += '\n';
    meta += "# eda-snapshot uuid = ";
    meta += toml_quote(info.uuid);
    meta += '\n';
    meta += "# eda-snapshot created_time = ";
    meta += std::to_string(info.created_time);
    meta += '\n';
    meta += "# eda-snapshot note = ";
    meta += toml_quote(info.note);
    meta += '\n';
    meta += "# eda-snapshot parent_uuid = ";
    meta += toml_quote(info.parent_uuid);
    meta += '\n';
    meta += '\n';  // 空行分隔元信息块与正文

    const std::string body = ProjectSaver::serialize(&tree);
    meta.append(body);
    return meta;
}

// 读取 .toml 文件首部 `# eda-snapshot` 元信息。返回是否找到至少一条 meta 行。
// 仅扫描前 32 行（元信息块固定在文件首部，遇到首条非 meta 非空行即停止）。
bool read_snapshot_metadata(const std::string& path, SnapshotInfo& info) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    std::string line;
    bool found_any = false;
    for (int i = 0; i < 32 && std::getline(f, line); ++i) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (!line.starts_with(kMetaPrefix)) {
            if (found_any) break;       // 正文已开始
            if (trim_ws(line).empty()) continue;  // 元信息块前的空行容忍
            break;                      // 既非 meta 也非空 -> 非快照
        }

        const std::string kv = trim_ws(line.substr(kMetaPrefix.size()));
        const size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim_ws(kv.substr(0, eq));
        const std::string raw_val = trim_ws(kv.substr(eq + 1));
        found_any = true;

        if (key == kMetaKeyUuid) {
            std::string v;
            if (parse_toml_string_literal(raw_val, v)) info.uuid = v;
        } else if (key == kMetaKeyCreated) {
            int64_t v = 0;
            if (parse_toml_int_literal(raw_val, v)) info.created_time = v;
        } else if (key == kMetaKeyNote) {
            std::string v;
            if (parse_toml_string_literal(raw_val, v)) info.note = v;
        } else if (key == kMetaKeyParent) {
            std::string v;
            if (parse_toml_string_literal(raw_val, v)) info.parent_uuid = v;
        }
        // 未知键忽略（前向兼容）。
    }
    return found_any;
}

// ============================================================================
// 工程树计数（diff 占位实现用）
// ============================================================================

void count_boards_pages(const std::string& project_toml_text,
                        int& board_count, int& page_count) {
    board_count = 0;
    page_count = 0;
    if (project_toml_text.empty()) return;

    ProjectTree tmp;
    if (!ProjectLoader::parse_into(tmp, project_toml_text)) return;
    board_count = static_cast<int>(tmp.root().board_count());
    for (const Board& b : tmp.root().boards()) {
        page_count += static_cast<int>(b.page_count());
    }
}

}  // namespace

// ============================================================================
// SnapshotManager 构造 / 路径
// ============================================================================

SnapshotManager::SnapshotManager(std::string project_dir)
    : project_dir_(std::move(project_dir)), limit_(kDefaultLimit) {}

SnapshotManager::SnapshotManager(std::string project_dir, size_t limit)
    : project_dir_(std::move(project_dir)), limit_(limit) {}

std::string SnapshotManager::snapshots_dir() const {
    return join_path(project_dir_, std::string(kSnapshotsSubdir));
}

// ============================================================================
// capture
// ============================================================================

std::optional<SnapshotInfo> SnapshotManager::capture(
    const ProjectTree& tree, const std::string& label) {
    namespace fs = std::filesystem;

    // 组装元信息
    SnapshotInfo info;
    info.uuid = generate_uuid();
    info.created_time = unix_now_seconds();
    info.note = label;

    // parent_uuid：链到当前最新快照（pre-write 快照）
    const std::vector<SnapshotInfo> existing = list();
    if (!existing.empty()) {
        info.parent_uuid = existing.front().uuid;
    }

    // 确保目录存在（已存在不报错）
    const std::string dir = snapshots_dir();
    std::error_code dir_ec;
    fs::create_directories(dir, dir_ec);
    // create_directories 在无权限 / 路径冲突时会失败；ec 非空则视作 IO 错误。
    if (dir_ec) return std::nullopt;

    // 构造文件名（同秒同标签冲突追加 -N）
    const std::string base =
        pad_timestamp(info.created_time) + "-" + sanitize_label(label);
    std::string filename = base + ".toml";
    std::string filepath = join_path(dir, filename);
    {
        std::error_code exist_ec;
        int suffix = 2;
        while (fs::exists(filepath, exist_ec)) {
            filename = base + "-" + std::to_string(suffix) + ".toml";
            filepath = join_path(dir, filename);
            ++suffix;
        }
    }
    info.filename = filename;

    // 写入文件
    const std::string text = compose_snapshot_text(info, tree);
    {
        std::ofstream f(filepath, std::ios::binary | std::ios::trunc);
        if (!f) return std::nullopt;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!f) return std::nullopt;
    }

    // 记录字节数
    std::error_code size_ec;
    const auto sz = fs::file_size(filepath, size_ec);
    info.size = size_ec ? 0 : static_cast<int64_t>(sz);

    // 上限清理（重新列：含刚写入的新快照）
    std::vector<SnapshotInfo> latest = list();
    prune(&latest);

    return info;
}

// ============================================================================
// list
// ============================================================================

namespace {

// 用于 list 内部排序的临时条目：携带文件系统写入时间做 tie-breaker。
struct ListedEntry {
    SnapshotInfo info;
    std::filesystem::file_time_type write_time;
};

}  // namespace

std::vector<SnapshotInfo> SnapshotManager::list() const {
    namespace fs = std::filesystem;

    std::vector<ListedEntry> entries;

    std::error_code exist_ec;
    if (!fs::exists(snapshots_dir(), exist_ec)) {
        return {};
    }

    try {
        for (const fs::directory_entry& entry :
             fs::directory_iterator(snapshots_dir())) {
            std::error_code rec;
            if (!entry.is_regular_file(rec)) continue;

            const fs::path& p = entry.path();
            if (p.extension() != ".toml") continue;

            SnapshotInfo info;
            info.filename = p.filename().string();
            if (!read_snapshot_metadata(p.string(), info)) continue;  // 非快照

            std::error_code sz_ec;
            info.size = static_cast<int64_t>(fs::file_size(p, sz_ec));

            std::error_code wt_ec;
            const auto wt = fs::last_write_time(p, wt_ec);

            entries.push_back({std::move(info), wt});
        }
    } catch (const std::exception&) {
        // 中途异常：返回已收集的部分（排序仍执行）。
    }

    // 排序（倒序，最新在前）：
    //   1) created_time 高到低（主键）
    //   2) 文件系统 last_write_time 高到低（同秒内区分多次 capture）
    //   3) filename 字典序高到低（文件系统时间分辨率极粗时的最终保底，确保稳定）
    std::sort(entries.begin(), entries.end(),
              [](const ListedEntry& a, const ListedEntry& b) {
                  if (a.info.created_time != b.info.created_time) {
                      return a.info.created_time > b.info.created_time;
                  }
                  if (a.write_time != b.write_time) {
                      return a.write_time > b.write_time;
                  }
                  return a.info.filename > b.info.filename;
              });

    std::vector<SnapshotInfo> out;
    out.reserve(entries.size());
    for (auto& e : entries) {
        out.push_back(std::move(e.info));
    }
    return out;
}

// ============================================================================
// restore
// ============================================================================

bool SnapshotManager::restore(ProjectTree& tree,
                              const std::string& snapshot_id) const {
    if (snapshot_id.empty()) return false;

    const std::vector<SnapshotInfo> all = list();
    const auto it = std::find_if(all.begin(), all.end(),
                                [&](const SnapshotInfo& s) {
                                    return s.uuid == snapshot_id;
                                });
    if (it == all.end()) return false;

    const std::string path = join_path(snapshots_dir(), it->filename);
    return ProjectLoader::load_into(tree, path);
}

// ============================================================================
// diff（占位）
// ============================================================================

SnapshotDiff SnapshotManager::diff(const std::string& snapshot_a_id,
                                   const std::string& snapshot_b_id) const {
    SnapshotDiff result;
    const std::vector<SnapshotInfo> all = list();

    auto load_text = [&](const std::string& id) -> std::string {
        if (id.empty()) return std::string();
        const auto it = std::find_if(all.begin(), all.end(),
                                    [&](const SnapshotInfo& s) {
                                        return s.uuid == id;
                                    });
        if (it == all.end()) return std::string();
        const std::string path = join_path(snapshots_dir(), it->filename);
        std::ifstream f(path, std::ios::binary);
        if (!f) return std::string();
        std::ostringstream oss;
        oss << f.rdbuf();
        return oss.str();
    };

    int ba = 0, pa = 0, bb = 0, pb = 0;
    count_boards_pages(load_text(snapshot_a_id), ba, pa);
    count_boards_pages(load_text(snapshot_b_id), bb, pb);
    result.board_count_a = ba;
    result.page_count_a = pa;
    result.board_count_b = bb;
    result.page_count_b = pb;
    return result;
}

// ============================================================================
// prune
// ============================================================================

size_t SnapshotManager::prune(std::vector<SnapshotInfo>* infos) {
    if (infos == nullptr) return 0;
    if (infos->size() <= limit_) return 0;
    if (limit_ == 0) {
        // 上限为 0：全部删除。
        size_t removed = 0;
        namespace fs = std::filesystem;
        for (const SnapshotInfo& s : *infos) {
            std::error_code ec;
            if (fs::remove(join_path(snapshots_dir(), s.filename), ec)) ++removed;
        }
        infos->clear();
        return removed;
    }

    const size_t to_remove = infos->size() - limit_;
    size_t removed = 0;
    namespace fs = std::filesystem;
    for (size_t i = infos->size() - to_remove; i < infos->size(); ++i) {
        std::error_code ec;
        if (fs::remove(join_path(snapshots_dir(), (*infos)[i].filename), ec)) {
            ++removed;
        }
    }
    infos->resize(infos->size() - to_remove);
    return removed;
}

}  // namespace eda
