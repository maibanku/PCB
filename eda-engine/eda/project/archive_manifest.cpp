// eda/project/archive_manifest.cpp
//
// P5 Task 5 · ArchiveManifest 实现 —— 手写最小 TOML 子集序列化。
//
// 序列化（确定性输出，与 project.cpp / snapshot.cpp 风格一致）：
//   - 顶层字段顺序固定：schema_version / eda_version / created_time /
//     project_uuid / signature。
//   - libraries 数组按 name 字典序输出；resources 数组按 path 字典序输出。
//   - 时间戳为 Unix 秒（UTC）。
//   - 字符串字面量带转义（与 project.cpp 的 append_toml_string 同语义）。
//
// 反序列化（行扫描状态机）：
//   - ROOT 态读顶层键值；遇到 [[libraries]] / [[resources]] 切换到对应态。
//   - 未识别字段忽略（前向兼容）；缺字段沿用默认值。
//   - 解析失败仍尽量填充（失败容忍），与 ProjectLoader 一致。

#include "eda/project/archive_manifest.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace eda {

// ============================================================================
// 构造
// ============================================================================

ArchiveManifest::ArchiveManifest() = default;

// ============================================================================
// 依赖库引用解析：lib://name@version / lib://name
// ============================================================================

void ArchiveManifest::add_library_from_ref(const std::string& ref) {
    LibraryVersion lib;
    lib.ref = ref;

    // 去掉协议前缀 lib://
    constexpr std::string_view kPrefix = "lib://";
    std::string body;
    if (ref.size() >= kPrefix.size() &&
        ref.compare(0, kPrefix.size(), kPrefix) == 0) {
        body = ref.substr(kPrefix.size());
    } else {
        body = ref;  // 容忍：非 lib:// 也接受，整段作 name
    }

    // name = 首个 '@' 之前；version = '@' 之后
    const size_t at = body.find('@');
    if (at == std::string::npos) {
        lib.name = body;
        lib.version.clear();
    } else {
        lib.name = body.substr(0, at);
        lib.version = body.substr(at + 1);
    }
    libraries_.push_back(std::move(lib));
}

void ArchiveManifest::add_resource(const std::string& path, int64_t size) {
    resources_.push_back(ArchiveResource{path, size});
}

// ============================================================================
// 最小 TOML 子集 —— 序列化
// ============================================================================

namespace {

void append_toml_string(std::string& out, const std::string& s) {
    out += '"';
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
    out += '"';
}

template <typename T>
std::vector<const T*> sort_by_field_ptr(const std::vector<T>& items,
                                        const std::string& (*key)(const T&)) {
    std::vector<const T*> ptrs;
    ptrs.reserve(items.size());
    for (const T& item : items) ptrs.push_back(&item);
    std::sort(ptrs.begin(), ptrs.end(),
              [&](const T* a, const T* b) { return key(*a) < key(*b); });
    return ptrs;
}

const std::string& library_key(const LibraryVersion& l) { return l.name; }
const std::string& resource_key(const ArchiveResource& r) { return r.path; }

}  // namespace

std::string ArchiveManifest::serialize() const {
    std::string out;
    out.reserve(256);

    // 顶层字段（顺序固定）
    out += "schema_version = ";
    out += std::to_string(schema_version_);
    out += '\n';
    out += "eda_version = ";       append_toml_string(out, eda_version_); out += '\n';
    out += "created_time = ";      out += std::to_string(created_time_); out += '\n';
    out += "project_uuid = ";      append_toml_string(out, project_uuid_); out += '\n';
    out += "signature = ";         append_toml_string(out, signature_); out += '\n';

    // libraries：按 name 字典序输出；空则省略整段
    if (!libraries_.empty()) {
        auto libs = sort_by_field_ptr(libraries_, library_key);
        for (const LibraryVersion* lib : libs) {
            out += "\n[[libraries]]\n";
            out += "name = ";    append_toml_string(out, lib->name);    out += '\n';
            out += "version = "; append_toml_string(out, lib->version); out += '\n';
            out += "ref = ";     append_toml_string(out, lib->ref);     out += '\n';
        }
    }

    // resources：按 path 字典序输出
    if (!resources_.empty()) {
        auto ress = sort_by_field_ptr(resources_, resource_key);
        for (const ArchiveResource* res : ress) {
            out += "\n[[resources]]\n";
            out += "path = "; append_toml_string(out, res->path); out += '\n';
            out += "size = "; out += std::to_string(res->size);   out += '\n';
        }
    }

    return out;
}

// ============================================================================
// 最小 TOML 子集 —— 解析（行扫描状态机）
// ============================================================================

namespace {

std::string strip_comment(const std::string& line) {
    bool in_string = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            in_string = !in_string;
        } else if (c == '\\' && in_string && i + 1 < line.size()) {
            ++i;  // 跳过被转义字符（避免把 \" 误判为字符串边界）
        } else if (c == '#' && !in_string) {
            return line.substr(0, i);
        }
    }
    return line;
}

std::string trim_ws(const std::string& s) {
    size_t b = 0, e = s.size();
    auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (b < e && is_ws(s[b])) ++b;
    while (e > b && is_ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

bool parse_toml_string(std::string_view raw, std::string& out) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') return false;
    out.clear();
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\') {
            if (i + 1 >= raw.size() - 1) return false;
            char e = raw[++i];
            switch (e) {
                case '"':  out += '"'; break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                default:   out += e; break;
            }
        } else {
            out += c;
        }
    }
    return true;
}

bool parse_toml_int(std::string_view raw, int64_t& out) {
    std::string s(raw);
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        long long v = std::stoll(s, &pos);
        if (pos != s.size()) return false;
        out = static_cast<int64_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

bool ArchiveManifest::deserialize(const std::string& text) {
    // 重置：文件是真相，丢弃既有状态
    *this = ArchiveManifest();

    enum class Ctx { ROOT, LIBRARY, RESOURCE };
    Ctx ctx = Ctx::ROOT;
    LibraryVersion cur_lib;
    ArchiveResource cur_res;

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = strip_comment(line);
        std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;

        // 数组表头 [[...]]
        if (trimmed.size() >= 2 && trimmed[0] == '[' && trimmed[1] == '[') {
            // 进入新表前，先把上一条 LIBRARY / RESOURCE 提交
            if (ctx == Ctx::LIBRARY) libraries_.push_back(std::move(cur_lib));
            else if (ctx == Ctx::RESOURCE) resources_.push_back(std::move(cur_res));
            cur_lib = LibraryVersion{};
            cur_res = ArchiveResource{};

            size_t end = trimmed.find("]]");
            if (end == std::string::npos) { ctx = Ctx::ROOT; continue; }
            std::string header = trim_ws(trimmed.substr(2, end - 2));
            if (header == "libraries") {
                ctx = Ctx::LIBRARY;
            } else if (header == "resources") {
                ctx = Ctx::RESOURCE;
            } else {
                ctx = Ctx::ROOT;  // 未识别表头
            }
            continue;
        }

        // 单表头 [...]（schema 未使用，识别后回到 ROOT）
        if (trimmed[0] == '[') {
            if (ctx == Ctx::LIBRARY) libraries_.push_back(std::move(cur_lib));
            else if (ctx == Ctx::RESOURCE) resources_.push_back(std::move(cur_res));
            cur_lib = LibraryVersion{};
            cur_res = ArchiveResource{};
            ctx = Ctx::ROOT;
            continue;
        }

        // key = value
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_ws(trimmed.substr(0, eq));
        std::string raw_val = trim_ws(trimmed.substr(eq + 1));
        if (key.empty() || raw_val.empty()) continue;

        if (ctx == Ctx::ROOT) {
            if (key == "schema_version") {
                int64_t v = 0;
                if (parse_toml_int(raw_val, v)) schema_version_ = static_cast<int>(v);
            } else if (key == "eda_version") {
                std::string v;
                if (parse_toml_string(raw_val, v)) eda_version_ = std::move(v);
            } else if (key == "created_time") {
                int64_t v = 0;
                if (parse_toml_int(raw_val, v)) created_time_ = v;
            } else if (key == "project_uuid") {
                std::string v;
                if (parse_toml_string(raw_val, v)) project_uuid_ = std::move(v);
            } else if (key == "signature") {
                std::string v;
                if (parse_toml_string(raw_val, v)) signature_ = std::move(v);
            }
            // 未知键忽略
        } else if (ctx == Ctx::LIBRARY) {
            if (key == "name") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_lib.name = std::move(v);
            } else if (key == "version") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_lib.version = std::move(v);
            } else if (key == "ref") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_lib.ref = std::move(v);
            }
        } else if (ctx == Ctx::RESOURCE) {
            if (key == "path") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_res.path = std::move(v);
            } else if (key == "size") {
                int64_t v = 0;
                if (parse_toml_int(raw_val, v)) cur_res.size = v;
            }
        }
    }

    // 文件末尾仍处于 LIBRARY / RESOURCE：提交最后一条
    if (ctx == Ctx::LIBRARY) libraries_.push_back(std::move(cur_lib));
    else if (ctx == Ctx::RESOURCE) resources_.push_back(std::move(cur_res));

    return true;
}

// ============================================================================
// ArchiveManifestLoader 门面
// ============================================================================

bool ArchiveManifestLoader::parse_into(ArchiveManifest& m, const std::string& text) {
    return m.deserialize(text);
}

bool ArchiveManifestLoader::load_into(ArchiveManifest& m, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream oss;
    oss << f.rdbuf();
    return m.deserialize(oss.str());
}

bool ArchiveManifestLoader::save(const ArchiveManifest& m, const std::string& path) {
    const std::string text = m.serialize();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}

}  // namespace eda
