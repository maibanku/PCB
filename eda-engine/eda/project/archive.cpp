// eda/project/archive.cpp
//
// P5 Task 5 · Archive 实现 —— 手写简单 tar-like .edapkg 容器（不引 zip 库）。
//
// 容器格式（小端无符号整数；详见 archive.h 头注释）：
//   [magic 8B "EDAPKG1\n"][entry_count u32]
//   [条目]*N = [name_len u32][name][data_len u64][data]
//
//   pack 写入顺序：先所有工程文件（按相对路径字典序），最后写 manifest.toml。
//   读侧不依赖该顺序（线性扫描，靠 name 识别 manifest）。
//
// 自包含 / 可离线移交：
//   - 入库路径一律相对工程根、正斜杠；无盘符 / 绝对路径。
//   - Project::dependencies() 的 "lib://name@version" 写入 manifest.libraries[]，
//     实现版本冻结（解包方据此判断是否需要替换 / 升级）。
//   - 全部工程文件作为条目嵌入；解包后无需外部依赖即可打开。
//
// 依赖：eda/project/project.h（ProjectTree / ProjectLoader）、
//       eda/project/archive_manifest.h（ArchiveManifest）、std::filesystem。

#include "eda/project/archive.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "eda/project/project.h"

namespace eda {

// ============================================================================
// ArchiveMode 字符串映射
// ============================================================================

std::string_view to_string_view(ArchiveMode m) {
    switch (m) {
        case ArchiveMode::FULL:       return "full";
        case ArchiveMode::PRODUCTION: return "production";
        case ArchiveMode::SNAPSHOT:   return "snapshot";
    }
    return "full";
}

ArchiveMode parse_archive_mode(std::string_view s) {
    if (s == "production") return ArchiveMode::PRODUCTION;
    if (s == "snapshot")   return ArchiveMode::SNAPSHOT;
    return ArchiveMode::FULL;  // 含 "full" 与未识别值
}

namespace {

// ============================================================================
// 小端二进制读写
// ============================================================================

void write_u32(std::ostream& os, uint32_t v) {
    unsigned char buf[4] = {
        static_cast<unsigned char>(v & 0xFFu),
        static_cast<unsigned char>((v >> 8) & 0xFFu),
        static_cast<unsigned char>((v >> 16) & 0xFFu),
        static_cast<unsigned char>((v >> 24) & 0xFFu)};
    os.write(reinterpret_cast<const char*>(buf), sizeof(buf));
}

void write_u64(std::ostream& os, uint64_t v) {
    unsigned char buf[8];
    for (int i = 0; i < 8; ++i) {
        buf[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFFu);
    }
    os.write(reinterpret_cast<const char*>(buf), sizeof(buf));
}

bool read_u32(std::istream& is, uint32_t& v) {
    unsigned char buf[4];
    is.read(reinterpret_cast<char*>(buf), sizeof(buf));
    if (!is) return false;
    v = uint32_t(buf[0]) | (uint32_t(buf[1]) << 8) |
        (uint32_t(buf[2]) << 16) | (uint32_t(buf[3]) << 24);
    return true;
}

bool read_u64(std::istream& is, uint64_t& v) {
    unsigned char buf[8];
    is.read(reinterpret_cast<char*>(buf), sizeof(buf));
    if (!is) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= uint64_t(buf[i]) << (8 * i);
    }
    return true;
}

bool read_magic(std::istream& is) {
    char buf[Archive::kMagicLen];
    is.read(buf, static_cast<std::streamsize>(Archive::kMagicLen));
    if (!is) return false;
    return std::memcmp(buf, Archive::kMagic, Archive::kMagicLen) == 0;
}

// 读取固定长度字节到 string（len=0 时返回空串，不访问空指针）。
bool read_bytes(std::istream& is, std::string& out, size_t len) {
    out.clear();
    if (len == 0) return static_cast<bool>(is);
    out.resize(len);
    is.read(&out[0], static_cast<std::streamsize>(len));
    return static_cast<bool>(is);
}

// ============================================================================
// 路径 / 时间工具
// ============================================================================

std::string to_forward_slashes(const std::string& s) {
    std::string out = s;
    for (auto& c : out) {
        if (c == '\\') c = '/';
    }
    return out;
}

// 路径中任一段以 '.' 开头即视为隐藏（.eda-snapshots / .git / .DS_Store 等）。
bool has_hidden_segment(const std::string& rel) {
    size_t start = 0;
    while (start <= rel.size()) {
        size_t end = rel.find('/', start);
        if (end == std::string::npos) end = rel.size();
        if (end > start && rel[start] == '.') return true;
        if (end >= rel.size()) break;
        start = end + 1;
    }
    return false;
}

// 模式筛选：project.toml 永远包含；其余按模式策略。
bool mode_includes(ArchiveMode mode, const std::string& rel) {
    if (rel == "project.toml") return true;

    switch (mode) {
        case ArchiveMode::FULL:
            return true;  // include_hidden 由调用方另行过滤
        case ArchiveMode::SNAPSHOT:
            return false;  // 仅 project.toml
        case ArchiveMode::PRODUCTION: {
            // 仅生产输出目录（output/ gerber/ bom/ manufacturing/）下文件。
            constexpr std::string_view kProdDirs[] = {
                "output/", "gerber/", "bom/", "manufacturing/"};
            for (std::string_view d : kProdDirs) {
                if (rel.size() > d.size() &&
                    rel.compare(0, d.size(), d) == 0) {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

// 入库条目名安全化校验：非空、无反斜杠、非绝对路径、无 ".." 段（防穿越）。
bool is_safe_entry_name(const std::string& name) {
    if (name.empty()) return false;
    if (name.find('\\') != std::string::npos) return false;
    if (name.front() == '/') return false;
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find('/', start);
        if (end == std::string::npos) end = name.size();
        if (end > start) {
            std::string_view seg(name.data() + start, end - start);
            if (seg == "..") return false;
        }
        if (end >= name.size()) break;
        start = end + 1;
    }
    return true;
}

int64_t unix_now_seconds() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// ============================================================================
// 文件采集
// ============================================================================

struct CollectedFile {
    std::string rel_path;  // 正斜杠相对工程根
    std::string abs_path;  // 磁盘绝对路径（读取时用）
    uintmax_t size = 0;
};

// 路径前缀比较（避免跨平台 native string 编码差异），用于跳过输出文件自身。
bool same_file(const std::string& a, const std::string& b) {
    std::error_code ec;
    return std::filesystem::equivalent(a, b, ec) && !ec;
}

}  // namespace

// ============================================================================
// Archive::pack
// ============================================================================

bool Archive::pack(const std::string& project_dir,
                   const std::string& output_path,
                   const ArchiveOptions& options) {
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path proj_dir = fs::weakly_canonical(project_dir, ec);
    if (ec) proj_dir = fs::path(project_dir);

    if (!fs::exists(proj_dir, ec)) return false;
    const fs::path proj_toml = proj_dir / "project.toml";
    if (!fs::exists(proj_toml, ec)) return false;

    // 读 project.toml 提取工程 UUID + 依赖
    ProjectTree tree;
    if (!tree.load(proj_toml.string())) return false;

    // 采集工程目录下全部常规文件（按模式 / 隐藏规则过滤）
    std::vector<CollectedFile> files;
    try {
        for (const fs::directory_entry& entry :
             fs::recursive_directory_iterator(
                 proj_dir, fs::directory_options::skip_permission_denied)) {
            std::error_code rec_ec;
            if (!entry.is_regular_file(rec_ec)) continue;

            const fs::path& p = entry.path();
            std::string rel_str = to_forward_slashes(
                fs::relative(p, proj_dir, rec_ec).string());
            if (rec_ec || rel_str.empty()) continue;

            // 跳过输出文件自身（若它落在工程目录内）
            if (same_file(p.string(), output_path)) continue;

            // 隐藏过滤（仅 FULL 模式受 options.include_hidden 控制；
            // SNAPSHOT/PRODUCTION 已由 mode_includes 严格收窄，此处不复过滤）
            if (options.mode == ArchiveMode::FULL &&
                !options.include_hidden && has_hidden_segment(rel_str)) {
                continue;
            }

            if (!mode_includes(options.mode, rel_str)) continue;

            CollectedFile f;
            f.rel_path = std::move(rel_str);
            f.abs_path = fs::weakly_canonical(p, rec_ec).string();
            if (rec_ec) f.abs_path = p.string();
            f.size = entry.file_size(rec_ec);
            if (rec_ec) continue;

            files.push_back(std::move(f));
        }
    } catch (const std::exception&) {
        // 中途异常：用已采集的部分继续（容错）
    }

    // 按相对路径字典序，确保确定性输出
    std::sort(files.begin(), files.end(),
              [](const CollectedFile& a, const CollectedFile& b) {
                  return a.rel_path < b.rel_path;
              });

    // 构建 manifest
    ArchiveManifest m;
    m.set_eda_version(options.eda_version.empty()
                          ? ArchiveOptions::kDefaultEdaVersion
                          : options.eda_version);
    m.set_created_time(unix_now_seconds());
    m.set_project_uuid(tree.root().uuid());
    m.set_signature(std::string());  // 占位：空串
    for (const std::string& dep : tree.root().dependencies()) {
        m.add_library_from_ref(dep);
    }
    for (const CollectedFile& f : files) {
        m.add_resource("res://" + f.rel_path,
                       static_cast<int64_t>(f.size));
    }

    // 写容器
    std::ofstream os(output_path, std::ios::binary | std::ios::trunc);
    if (!os) return false;

    os.write(kMagic, static_cast<std::streamsize>(kMagicLen));
    const uint32_t entry_count =
        static_cast<uint32_t>(files.size() + 1);  // +1 for manifest.toml
    write_u32(os, entry_count);

    constexpr size_t kCopyBuf = 65536;
    std::vector<char> copy_buf(kCopyBuf);

    for (const CollectedFile& f : files) {
        // name
        write_u32(os, static_cast<uint32_t>(f.rel_path.size()));
        os.write(f.rel_path.data(),
                 static_cast<std::streamsize>(f.rel_path.size()));
        // size
        write_u64(os, static_cast<uint64_t>(f.size));

        // data：流式拷贝（不一次性载入大文件）
        std::ifstream src(f.abs_path, std::ios::binary);
        if (!src) return false;
        uint64_t remaining = f.size;
        while (remaining > 0) {
            const size_t want = static_cast<size_t>(
                remaining < kCopyBuf ? remaining : kCopyBuf);
            src.read(copy_buf.data(), static_cast<std::streamsize>(want));
            const std::streamsize got = src.gcount();
            if (got <= 0) return false;
            os.write(copy_buf.data(), got);
            if (!os) return false;
            remaining -= static_cast<uint64_t>(got);
        }
    }

    // manifest.toml 条目（约定最后写入；读侧线性扫描，不依赖顺序）
    const std::string manifest_text = m.serialize();
    const std::string manifest_name = "manifest.toml";
    write_u32(os, static_cast<uint32_t>(manifest_name.size()));
    os.write(manifest_name.data(),
             static_cast<std::streamsize>(manifest_name.size()));
    write_u64(os, static_cast<uint64_t>(manifest_text.size()));
    os.write(manifest_text.data(),
             static_cast<std::streamsize>(manifest_text.size()));

    return static_cast<bool>(os);
}

// ============================================================================
// Archive::unpack
// ============================================================================

bool Archive::unpack(const std::string& archive_path,
                     const std::string& output_dir) {
    namespace fs = std::filesystem;

    std::ifstream is(archive_path, std::ios::binary);
    if (!is) return false;
    if (!read_magic(is)) return false;

    uint32_t entry_count = 0;
    if (!read_u32(is, entry_count)) return false;

    std::error_code mk_ec;
    fs::create_directories(output_dir, mk_ec);
    // create_directories 已存在时不报错；仅在权限 / 路径冲突失败时 ec 非空
    if (mk_ec) return false;

    constexpr size_t kCopyBuf = 65536;
    std::vector<char> copy_buf(kCopyBuf);

    for (uint32_t i = 0; i < entry_count; ++i) {
        uint32_t name_len = 0;
        if (!read_u32(is, name_len)) return false;
        std::string name;
        if (!read_bytes(is, name, name_len)) return false;
        if (!is_safe_entry_name(name)) return false;  // 防路径穿越

        uint64_t data_len = 0;
        if (!read_u64(is, data_len)) return false;

        // 输出路径：output_dir + name（fs::path 接受正斜杠）
        const fs::path out_path = fs::path(output_dir) / name;
        if (out_path.has_parent_path()) {
            std::error_code parent_ec;
            fs::create_directories(out_path.parent_path(), parent_ec);
            if (parent_ec) return false;
        }

        std::ofstream os(out_path, std::ios::binary | std::ios::trunc);
        if (!os) return false;

        uint64_t remaining = data_len;
        while (remaining > 0) {
            const size_t want = static_cast<size_t>(
                remaining < kCopyBuf ? remaining : kCopyBuf);
            is.read(copy_buf.data(), static_cast<std::streamsize>(want));
            const std::streamsize got = is.gcount();
            if (got <= 0) return false;
            os.write(copy_buf.data(), got);
            if (!os) return false;
            remaining -= static_cast<uint64_t>(got);
        }
    }

    return true;
}

// ============================================================================
// Archive::verify
// ============================================================================

namespace {

// 条目扫描结果：name + size（不含 data；verify 不需要内容，仅做尺寸交叉校验）
struct EntryInfo {
    std::string name;
    uint64_t size = 0;
};

}  // namespace

bool Archive::verify(const std::string& archive_path) {
    std::ifstream is(archive_path, std::ios::binary);
    if (!is) return false;
    if (!read_magic(is)) return false;

    uint32_t entry_count = 0;
    if (!read_u32(is, entry_count)) return false;

    std::vector<EntryInfo> entries;
    entries.reserve(entry_count);
    std::string manifest_text;
    bool manifest_found = false;

    for (uint32_t i = 0; i < entry_count; ++i) {
        uint32_t name_len = 0;
        if (!read_u32(is, name_len)) return false;
        std::string name;
        if (!read_bytes(is, name, name_len)) return false;
        uint64_t data_len = 0;
        if (!read_u64(is, data_len)) return false;

        if (name == "manifest.toml") {
            if (!read_bytes(is, manifest_text,
                            static_cast<size_t>(data_len))) {
                return false;
            }
            manifest_found = true;
        } else {
            // 跳过 data
            is.seekg(static_cast<std::streamoff>(data_len), std::ios::cur);
            if (!is) return false;
            entries.push_back(EntryInfo{name, data_len});
        }
    }

    if (!manifest_found) return false;

    ArchiveManifest m;
    if (!m.deserialize(manifest_text)) return false;

    // 正向：manifest.resources 每条都应在 entries 中存在且 size 匹配
    for (const ArchiveResource& res : m.resources()) {
        // res.path 形如 "res://project.toml" -> 条目名 "project.toml"
        std::string entry_name;
        if (res.path.size() >= 6 &&
            res.path.compare(0, 6, "res://") == 0) {
            entry_name = res.path.substr(6);
        } else {
            entry_name = res.path;  // 容忍非 res:// 路径，直接按字面匹配
        }
        const auto it = std::find_if(
            entries.begin(), entries.end(),
            [&](const EntryInfo& e) { return e.name == entry_name; });
        if (it == entries.end()) return false;
        if (it->size != static_cast<uint64_t>(res.size)) return false;
    }

    // 反向：entries 中每个条目都应在 manifest.resources 中登记
    for (const EntryInfo& e : entries) {
        const std::string res_path = "res://" + e.name;
        const auto it = std::find_if(
            m.resources().begin(), m.resources().end(),
            [&](const ArchiveResource& r) { return r.path == res_path; });
        if (it == m.resources().end()) return false;
    }

    return true;
}

// ============================================================================
// Archive::read_manifest
// ============================================================================

bool Archive::read_manifest(const std::string& archive_path,
                            ArchiveManifest& out) {
    std::ifstream is(archive_path, std::ios::binary);
    if (!is) return false;
    if (!read_magic(is)) return false;

    uint32_t entry_count = 0;
    if (!read_u32(is, entry_count)) return false;

    for (uint32_t i = 0; i < entry_count; ++i) {
        uint32_t name_len = 0;
        if (!read_u32(is, name_len)) return false;
        std::string name;
        if (!read_bytes(is, name, name_len)) return false;
        uint64_t data_len = 0;
        if (!read_u64(is, data_len)) return false;

        if (name == "manifest.toml") {
            std::string text;
            if (!read_bytes(is, text, static_cast<size_t>(data_len))) {
                return false;
            }
            return out.deserialize(text);
        }
        // 跳过非 manifest 条目
        is.seekg(static_cast<std::streamoff>(data_len), std::ios::cur);
        if (!is) return false;
    }
    return false;  // 未找到 manifest.toml
}

}  // namespace eda
