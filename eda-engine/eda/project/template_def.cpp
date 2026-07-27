// eda/project/template_def.cpp
//
// P5 Task 3 · 工程模板定义实现。
//
// 设计要点：
//   - 行扫描状态机（与 ProjectLoader 风格一致）：四态
//       ROOT          —— 顶层字段（id / name / category / description / board_type）
//       DEFAULT_RULES —— [default_rules] 表
//       STACKUP       —— [stackup] 表
//       PARAM         —— [[params]] 数组表（每段表头新建一个 TemplateParam）
//   - 字符串 / 整 / 浮点 / 布尔 / 字符串数组字面量解析（与 project.cpp / snapshot.cpp
//     保持同一子集，不引 toml++）。
//   - 未识别字段忽略（前向兼容）；缺字段沿用默认值。
//   - 解析过程中临时持有 default_rules / stackup 的可变副本（struct 整体赋值给 def）。

#include "eda/project/template_def.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace eda {

// ============================================================================
// TemplateParamType 字符串映射
// ============================================================================

std::string_view to_string_view(TemplateParamType t) {
    switch (t) {
        case TemplateParamType::STRING: return "string";
        case TemplateParamType::INT:    return "int";
        case TemplateParamType::FLOAT:  return "float";
        case TemplateParamType::BOOL:   return "bool";
        case TemplateParamType::ENUM:   return "enum";
    }
    return "string";
}

TemplateParamType parse_template_param_type(std::string_view s) {
    if (s == "int")    return TemplateParamType::INT;
    if (s == "float")  return TemplateParamType::FLOAT;
    if (s == "bool")   return TemplateParamType::BOOL;
    if (s == "enum")   return TemplateParamType::ENUM;
    return TemplateParamType::STRING;  // 含 "string" 与未识别值
}

// ============================================================================
// TemplateDef
// ============================================================================

TemplateParam& TemplateDef::add_param() {
    params_.emplace_back();
    return params_.back();
}

const TemplateParam* TemplateDef::find_param(const std::string& name) const {
    auto it = std::find_if(params_.begin(), params_.end(),
                           [&](const TemplateParam& p) { return p.name == name; });
    return it == params_.end() ? nullptr : &(*it);
}

// ============================================================================
// 最小 TOML 子集 —— 工具函数
// ============================================================================
//
// 与 project.cpp / snapshot.cpp 的实现保持一致：行注释剥离、空白裁剪、字符串 / 整 /
// 浮点 / 布尔 / 字符串数组字面量解析。重复实现而非复用，是为了 Task 边界内自包含
// （core/io/toml_reader 尚未存在；T1 用手写解析，这里沿用同一模式）。

namespace {

std::string strip_comment(const std::string& line) {
    bool in_string = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            in_string = !in_string;
        } else if (c == '\\' && in_string && i + 1 < line.size()) {
            ++i;  // 跳过被转义字符（避免 \" 误判字符串边界）
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

bool parse_toml_int(std::string_view raw, long long& out) {
    if (raw.empty()) return false;
    try {
        size_t pos = 0;
        long long v = std::stoll(std::string(raw), &pos);
        if (pos != raw.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_toml_double(std::string_view raw, double& out) {
    if (raw.empty()) return false;
    try {
        size_t pos = 0;
        double v = std::stod(std::string(raw), &pos);
        if (pos != raw.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

// 解析 TOML 字符串数组：["a", "b", "c"]。允许空数组 []。
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

}  // namespace

// ============================================================================
// TemplateLoader 实现
// ============================================================================

bool TemplateLoader::parse_into(TemplateDef& def, const std::string& text) {
    def = TemplateDef();  // 重置：文件是真相，丢弃既有状态

    enum class Ctx { ROOT, DEFAULT_RULES, STACKUP, PARAM };
    Ctx ctx = Ctx::ROOT;
    TemplateParam* cur_param = nullptr;

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = strip_comment(line);
        std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;

        // 数组表头 [[params]]
        if (trimmed.size() >= 2 && trimmed[0] == '[' && trimmed[1] == '[') {
            size_t end = trimmed.find("]]");
            if (end == std::string::npos) continue;
            std::string path = trim_ws(trimmed.substr(2, end - 2));
            if (path == "params") {
                ctx = Ctx::PARAM;
                cur_param = &def.add_param();
            } else {
                ctx = Ctx::ROOT;  // 未识别数组表头，退出嵌套
                cur_param = nullptr;
            }
            continue;
        }

        // 单表头 [section]
        if (!trimmed.empty() && trimmed[0] == '[') {
            size_t end = trimmed.find(']');
            if (end == std::string::npos) continue;
            std::string sec = trim_ws(trimmed.substr(1, end - 1));
            cur_param = nullptr;
            if (sec == "default_rules") {
                ctx = Ctx::DEFAULT_RULES;
            } else if (sec == "stackup") {
                ctx = Ctx::STACKUP;
            } else {
                ctx = Ctx::ROOT;
            }
            continue;
        }

        // key = value
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_ws(trimmed.substr(0, eq));
        std::string raw_val = trim_ws(trimmed.substr(eq + 1));
        if (key.empty() || raw_val.empty()) continue;

        if (ctx == Ctx::ROOT) {
            if (key == "id") {
                std::string v;
                if (parse_toml_string(raw_val, v)) def.set_id(std::move(v));
            } else if (key == "name") {
                std::string v;
                if (parse_toml_string(raw_val, v)) def.set_name(std::move(v));
            } else if (key == "category") {
                std::string v;
                if (parse_toml_string(raw_val, v)) def.set_category(std::move(v));
            } else if (key == "description") {
                std::string v;
                if (parse_toml_string(raw_val, v)) def.set_description(std::move(v));
            } else if (key == "board_type") {
                std::string v;
                if (parse_toml_string(raw_val, v)) def.set_board_type(parse_board_type(v));
            }
            // schema_version 等未知键忽略（前向兼容）
        } else if (ctx == Ctx::DEFAULT_RULES) {
            TemplateRules r = def.default_rules();
            if (key == "clearance_mm") {
                double v;
                if (parse_toml_double(raw_val, v)) r.clearance_mm = v;
            } else if (key == "track_width_mm") {
                double v;
                if (parse_toml_double(raw_val, v)) r.track_width_mm = v;
            } else if (key == "via_drill_mm") {
                double v;
                if (parse_toml_double(raw_val, v)) r.via_drill_mm = v;
            }
            def.set_default_rules(r);
        } else if (ctx == Ctx::STACKUP) {
            StackupPreset s = def.stackup_preset();
            if (key == "layer_count") {
                long long v = 0;
                if (parse_toml_int(raw_val, v) && v > 0) {
                    s.layer_count = static_cast<int>(v);
                }
            }
            def.set_stackup_preset(s);
        } else if (ctx == Ctx::PARAM && cur_param != nullptr) {
            if (key == "name") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_param->name = std::move(v);
            } else if (key == "type") {
                std::string v;
                if (parse_toml_string(raw_val, v)) {
                    cur_param->type = parse_template_param_type(v);
                }
            } else if (key == "default") {
                std::string v;
                if (parse_toml_string(raw_val, v)) cur_param->default_value = std::move(v);
            } else if (key == "min") {
                std::string v;
                if (parse_toml_string(raw_val, v)) {
                    cur_param->min_value = std::move(v);
                    cur_param->has_min = true;
                }
            } else if (key == "max") {
                std::string v;
                if (parse_toml_string(raw_val, v)) {
                    cur_param->max_value = std::move(v);
                    cur_param->has_max = true;
                }
            } else if (key == "options") {
                std::vector<std::string> opts;
                if (parse_toml_string_array(raw_val, opts)) {
                    cur_param->options = std::move(opts);
                }
            }
        }
    }
    return true;
}

bool TemplateLoader::load_into(TemplateDef& def, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream oss;
    oss << f.rdbuf();
    return parse_into(def, oss.str());
}

std::optional<TemplateDef> TemplateLoader::parse(const std::string& text) {
    TemplateDef def;
    if (!parse_into(def, text)) return std::nullopt;
    return def;
}

std::optional<TemplateDef> TemplateLoader::load(const std::string& path) {
    TemplateDef def;
    if (!load_into(def, path)) return std::nullopt;
    return def;
}

}  // namespace eda
