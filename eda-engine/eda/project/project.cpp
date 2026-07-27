// eda/project/project.cpp
//
// P5 Task 1 · 工程树数据模型 + project.toml 序列化实现。
//
// 序列化：手写最小 TOML 子集读写（不引 toml++）。
//   写：确定性输出——字段顺序固定；boards / pages / dependencies 按 uuid 字典序输出。
//   读：行扫描状态机（ROOT / BOARD / PAGE 三态）；缺失字段沿用默认值；未识别字段忽略。
//
// 文件 IO：直接 std::fstream（不抽 core/io/file，避免新增文件超出 Task 边界）。

#include "eda/project/project.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <utility>
#include <vector>

namespace eda {

// ============================================================================
// UUID v4 生成 / 校验
// ============================================================================

namespace {

// 进程级随机引擎（random_device 播种；UUID 用途足够，不密码学安全）。
std::mt19937_64& uuid_rng() {
    static std::mt19937_64 rng{std::random_device{}()};
    return rng;
}

}  // namespace

std::string generate_uuid() {
    // v4：version(4) 与 variant(10xx) 位固定，其余 122 位随机。
    std::uniform_int_distribution<unsigned long long> dist;
    unsigned long long a = dist(uuid_rng());
    unsigned long long b = dist(uuid_rng());
    // a 的位 12~15 设为 0x4（version）；b 的位 60~63 设为 0b1000（variant）
    a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    char buf[40];
    std::snprintf(buf, sizeof(buf),
                  "%08llx-%04llx-%04llx-%04llx-%012llx",
                  static_cast<unsigned long long>(a >> 32),
                  static_cast<unsigned long long>((a >> 16) & 0xFFFF),
                  static_cast<unsigned long long>(a & 0xFFFF),
                  static_cast<unsigned long long>(b >> 48),
                  static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFULL));
    return std::string(buf);
}

bool is_valid_uuid(const std::string& s) {
    // 形如 8-4-4-4-12 十六进制；连字符位置固定。
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else {
            char c = s[i];
            if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) {
                return false;
            }
        }
    }
    return true;
}

// ============================================================================
// PageType / BoardType 字符串映射
// ============================================================================

std::string_view to_string_view(PageType t) {
    switch (t) {
        case PageType::SCHEMATIC: return "schematic";
        case PageType::PCB:       return "pcb";
    }
    return "schematic";
}

PageType parse_page_type(std::string_view s) {
    if (s == "pcb") return PageType::PCB;
    return PageType::SCHEMATIC;  // 含 "schematic" 与未识别值
}

std::string_view to_string_view(BoardType t) {
    switch (t) {
        case BoardType::MAIN:     return "main";
        case BoardType::DAUGHTER: return "daughter";
    }
    return "main";
}

BoardType parse_board_type(std::string_view s) {
    if (s == "daughter") return BoardType::DAUGHTER;
    return BoardType::MAIN;  // 含 "main" 与未识别值
}

// ============================================================================
// Page
// ============================================================================

Page::Page() : uuid_(generate_uuid()) {}

// ============================================================================
// Board
// ============================================================================

Board::Board() : uuid_(generate_uuid()) {}

Page& Board::add_page(std::string name, PageType type, std::string path) {
    Page p;
    p.set_name(std::move(name));
    p.set_type(type);
    p.set_path(std::move(path));
    pages_.push_back(std::move(p));
    return pages_.back();
}

bool Board::remove_page_by_uuid(const std::string& uuid) {
    auto it = std::find_if(pages_.begin(), pages_.end(),
                           [&](const Page& p) { return p.uuid() == uuid; });
    if (it == pages_.end()) return false;
    pages_.erase(it);
    return true;
}

const Page* Board::find_page_by_uuid(const std::string& uuid) const {
    auto it = std::find_if(pages_.begin(), pages_.end(),
                           [&](const Page& p) { return p.uuid() == uuid; });
    return it == pages_.end() ? nullptr : &(*it);
}

Page* Board::find_page_by_uuid(const std::string& uuid) {
    auto it = std::find_if(pages_.begin(), pages_.end(),
                           [&](const Page& p) { return p.uuid() == uuid; });
    return it == pages_.end() ? nullptr : &(*it);
}

// ============================================================================
// Project
// ============================================================================

Project::Project() : uuid_(generate_uuid()) {}

void Project::touch() {
    modified_time_ = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

Board& Project::add_board(std::string name, BoardType type) {
    Board b;
    b.set_name(std::move(name));
    b.set_type(type);
    boards_.push_back(std::move(b));
    return boards_.back();
}

bool Project::remove_board_by_uuid(const std::string& uuid) {
    auto it = std::find_if(boards_.begin(), boards_.end(),
                           [&](const Board& b) { return b.uuid() == uuid; });
    if (it == boards_.end()) return false;
    boards_.erase(it);
    return true;
}

const Board* Project::find_board_by_uuid(const std::string& uuid) const {
    auto it = std::find_if(boards_.begin(), boards_.end(),
                           [&](const Board& b) { return b.uuid() == uuid; });
    return it == boards_.end() ? nullptr : &(*it);
}

Board* Project::find_board_by_uuid(const std::string& uuid) {
    auto it = std::find_if(boards_.begin(), boards_.end(),
                           [&](const Board& b) { return b.uuid() == uuid; });
    return it == boards_.end() ? nullptr : &(*it);
}

void Project::add_dependency(std::string lib_ref) {
    dependencies_.push_back(std::move(lib_ref));
}

bool Project::remove_dependency(const std::string& lib_ref) {
    auto it = std::find(dependencies_.begin(), dependencies_.end(), lib_ref);
    if (it == dependencies_.end()) return false;
    dependencies_.erase(it);
    return true;
}

// ============================================================================
// ProjectTree
// ============================================================================

ProjectTree::ProjectTree() : root_() {}

const Board* ProjectTree::find_board_by_uuid(const std::string& uuid) const {
    return root_.find_board_by_uuid(uuid);
}

const Page* ProjectTree::find_page_by_uuid(const std::string& uuid) const {
    for (const Board& b : root_.boards()) {
        if (const Page* p = b.find_page_by_uuid(uuid)) return p;
    }
    return nullptr;
}

bool ProjectTree::load(const std::string& project_toml_path) {
    return ProjectLoader::load_into(*this, project_toml_path);
}

bool ProjectTree::save(const std::string& project_toml_path) const {
    return ProjectSaver::save(project_toml_path, this);
}

std::string ProjectTree::serialize_to_string() const {
    return ProjectSaver::serialize(this);
}

bool ProjectTree::parse_from_string(const std::string& text) {
    return ProjectLoader::parse_into(*this, text);
}

// ============================================================================
// 最小 TOML 子集 —— 序列化（确定性输出）
// ============================================================================
//
// 输出 schema：
//   schema_version = 1
//   name = "..."
//   uuid = "..."
//   modified_time = 1754496000
//
//   dependencies = ["lib://standard@1.2.0", ...]    # 按字典序；空则省略
//
//   [[boards]]                                      # 按 uuid 字典序
//   name = "..."
//   uuid = "..."
//   type = "main"
//
//     [[boards.pages]]                              # 按 uuid 字典序
//     name = "..."
//     uuid = "..."
//     type = "schematic"
//     path = "res://sheets/sheet1.sch.toml"
//     modified_time = 1754496000

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
                                  static_cast<unsigned int>(static_cast<unsigned char>(c)));
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
std::vector<const T*> sort_by_uuid_ptr(const std::vector<T>& items) {
    std::vector<const T*> ptrs;
    ptrs.reserve(items.size());
    for (const T& item : items) ptrs.push_back(&item);
    std::sort(ptrs.begin(), ptrs.end(),
              [](const T* a, const T* b) { return a->uuid() < b->uuid(); });
    return ptrs;
}

void serialize_pages_into(std::string& out, const Board& b) {
    auto pages = sort_by_uuid_ptr(b.pages());
    for (const Page* pg : pages) {
        out += "\n  [[boards.pages]]\n";
        out += "  name = "; append_toml_string(out, pg->name()); out += '\n';
        out += "  uuid = "; append_toml_string(out, pg->uuid()); out += '\n';
        out += "  type = "; append_toml_string(out, std::string(to_string_view(pg->type()))); out += '\n';
        out += "  path = "; append_toml_string(out, pg->path()); out += '\n';
        out += "  modified_time = "; out += std::to_string(pg->modified_time()); out += '\n';
    }
}

void serialize_project_into(std::string& out, const Project& p) {
    // 顶层字段顺序固定
    out += "schema_version = ";
    out += std::to_string(p.schema_version());
    out += '\n';
    out += "name = "; append_toml_string(out, p.name()); out += '\n';
    out += "uuid = "; append_toml_string(out, p.uuid()); out += '\n';
    out += "modified_time = "; out += std::to_string(p.modified_time()); out += '\n';

    // dependencies：拷贝并字典序输出；空则省略整段
    if (!p.dependencies().empty()) {
        std::vector<std::string> deps = p.dependencies();
        std::sort(deps.begin(), deps.end());
        out += "\ndependencies = [";
        for (size_t i = 0; i < deps.size(); ++i) {
            if (i > 0) out += ", ";
            append_toml_string(out, deps[i]);
        }
        out += "]\n";
    }

    // boards：按 uuid 字典序输出
    auto boards = sort_by_uuid_ptr(p.boards());
    for (const Board* b : boards) {
        out += "\n[[boards]]\n";
        out += "name = "; append_toml_string(out, b->name()); out += '\n';
        out += "uuid = "; append_toml_string(out, b->uuid()); out += '\n';
        out += "type = "; append_toml_string(out, std::string(to_string_view(b->type()))); out += '\n';
        serialize_pages_into(out, *b);
    }
}

}  // namespace

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

// 解析 TOML 基本字符串字面量 "..."（带转义）。失败返回 false。
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

// 解析 TOML 字符串数组：["a", "b", "c"]。允许空数组 []。
// 容忍空白与换行外（已按行处理）；元素仅支持字符串字面量。
bool parse_toml_string_array(std::string_view raw, std::vector<std::string>& out) {
    if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') return false;
    std::string_view body = raw.substr(1, raw.size() - 2);
    size_t i = 0;
    while (i < body.size()) {
        // 跳过分隔符与空白
        while (i < body.size()) {
            char c = body[i];
            if (c == ' ' || c == '\t' || c == ',' || c == '\n' || c == '\r') {
                ++i;
            } else {
                break;
            }
        }
        if (i >= body.size()) break;
        if (body[i] != '"') return false;
        // 寻找配对的结束引号（尊重转义）
        size_t start = i;
        ++i;
        while (i < body.size()) {
            if (body[i] == '\\') { i += 2; continue; }
            if (body[i] == '"') break;
            ++i;
        }
        if (i >= body.size()) return false;
        ++i;  // 跳过结束引号
        std::string elem;
        if (!parse_toml_string(body.substr(start, i - start), elem)) return false;
        out.push_back(std::move(elem));
    }
    return true;
}

// 路径段切分（按 '.'，忽略空白）。例："boards.pages" -> ["boards","pages"]
std::vector<std::string> split_header_path(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '.') {
            std::string t = trim_ws(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur += c;
        }
    }
    std::string t = trim_ws(cur);
    if (!t.empty()) out.push_back(t);
    return out;
}

}  // namespace

// ============================================================================
// ProjectLoader / ProjectSaver 实现
// ============================================================================

std::string ProjectSaver::serialize(const Project& root) {
    std::string out;
    out.reserve(256);
    serialize_project_into(out, root);
    return out;
}

std::string ProjectSaver::serialize(const ProjectTree* tree) {
    if (tree == nullptr) return std::string();
    return serialize(tree->root());
}

bool ProjectSaver::save(const std::string& project_toml_path, const ProjectTree* tree) {
    if (tree == nullptr) return false;
    const std::string text = serialize(tree);
    std::ofstream f(project_toml_path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}

bool ProjectLoader::parse_into(ProjectTree& tree, const std::string& text) {
    // 重置 root：文件是真相，丢弃内存中既有状态（避免叠加）
    tree.root() = Project();

    Project& p = tree.root();

    enum class Ctx { ROOT, BOARD, PAGE };
    Ctx ctx = Ctx::ROOT;
    Board* cur_board = nullptr;
    Page* cur_page = nullptr;

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = strip_comment(line);
        std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;

        // 数组表头 [[a.b]]
        if (trimmed.size() >= 2 && trimmed[0] == '[' && trimmed[1] == '[') {
            size_t end = trimmed.find("]]");
            if (end == std::string::npos) continue;  // 残缺，忽略
            std::string path_str = trimmed.substr(2, end - 2);
            std::vector<std::string> path = split_header_path(path_str);

            if (path.size() == 1 && path[0] == "boards") {
                ctx = Ctx::BOARD;
                cur_board = &p.add_board(std::string(), BoardType::MAIN);
                cur_page = nullptr;
            } else if (path.size() == 2 && path[0] == "boards" && path[1] == "pages") {
                if (cur_board == nullptr) {
                    // 没有 [[boards]] 直接出现 page：建一个匿名 board 容器
                    cur_board = &p.add_board(std::string(), BoardType::MAIN);
                }
                ctx = Ctx::PAGE;
                cur_page = &cur_board->add_page(std::string(), PageType::SCHEMATIC, std::string());
            } else {
                // 未识别的数组表头；退出嵌套上下文，避免错误填充
                ctx = Ctx::ROOT;
                cur_board = nullptr;
                cur_page = nullptr;
            }
            continue;
        }

        // 单表头 [a.b]（schema 未使用，识别后退出嵌套上下文）
        if (!trimmed.empty() && trimmed[0] == '[') {
            size_t end = trimmed.find(']');
            if (end == std::string::npos) continue;
            ctx = Ctx::ROOT;
            cur_board = nullptr;
            cur_page = nullptr;
            continue;
        }

        // key = value
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;  // 残行忽略
        std::string key = trim_ws(trimmed.substr(0, eq));
        std::string raw_val = trim_ws(trimmed.substr(eq + 1));
        if (key.empty() || raw_val.empty()) continue;

        if (ctx == Ctx::ROOT) {
            if (key == "name") {
                std::string v;
                if (parse_toml_string(raw_val, v)) p.set_name(std::move(v));
            } else if (key == "uuid") {
                std::string v;
                if (parse_toml_string(raw_val, v)) p.assign_uuid_for_test(v);
            } else if (key == "schema_version") {
                int64_t v;
                if (parse_toml_int(raw_val, v)) {
                    // 写回 schema_version：用 assign_uuid_for_test 同样的"绕过 const"模式
                    // 这里直接保留默认（首版固定 1）；后续支持迁移器时按版本号分发
                    (void)v;
                }
            } else if (key == "modified_time") {
                int64_t v;
                if (parse_toml_int(raw_val, v)) p.set_modified_time(v);
            } else if (key == "dependencies") {
                std::vector<std::string> deps;
                if (parse_toml_string_array(raw_val, deps)) {
                    for (auto& d : deps) p.add_dependency(std::move(d));
                }
            }
            // 未知键：忽略（前向兼容）
        } else if (ctx == Ctx::BOARD && cur_board) {
            if (key == "name") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_board->set_name(std::move(v));
            } else if (key == "uuid") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_board->assign_uuid_for_test(v);
            } else if (key == "type") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_board->set_type(parse_board_type(v));
            }
        } else if (ctx == Ctx::PAGE && cur_page) {
            if (key == "name") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_page->set_name(std::move(v));
            } else if (key == "uuid") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_page->assign_uuid_for_test(v);
            } else if (key == "type") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_page->set_type(parse_page_type(v));
            } else if (key == "path") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_page->set_path(std::move(v));
            } else if (key == "modified_time") {
                int64_t v;
                if (parse_toml_int(raw_val, v)) cur_page->set_modified_time(v);
            }
        }
    }
    return true;
}

bool ProjectLoader::load_into(ProjectTree& tree, const std::string& project_toml_path) {
    std::ifstream f(project_toml_path, std::ios::binary);
    if (!f) return false;
    std::ostringstream oss;
    oss << f.rdbuf();
    return parse_into(tree, oss.str());
}

Ref<ProjectTree> ProjectLoader::parse(const std::string& text) {
    Ref<ProjectTree> tree(new ProjectTree());
    if (!parse_into(*tree, text)) return Ref<ProjectTree>();
    return tree;
}

Ref<ProjectTree> ProjectLoader::load(const std::string& project_toml_path) {
    Ref<ProjectTree> tree(new ProjectTree());
    if (!load_into(*tree, project_toml_path)) return Ref<ProjectTree>();
    return tree;
}

}  // namespace eda
