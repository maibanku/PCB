// eda/library/symbol.cpp
//
// P6 元件库 · 符号数据模型实现 + .sym.toml 序列化。
//
// 序列化：手写最小 TOML 子集读写（不引 toml++），对齐 eda/project/project.cpp 风格。
//   写：字段顺序固定（schema 稳定）；pins/graphics 按 number/索引序输出。
//   读：行扫描状态机（ROOT / PIN / GRAPHICS / GATE）；缺失字段沿用默认值；未识别字段忽略。
// 文件 IO 由 ResourceLoader/Saver 链（P2）按扩展名分发；本文件只实现字符串往返。

#include "eda/library/symbol.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <utility>

#include "eda/project/project.h"  // generate_uuid 复用

namespace eda {

// ============================================================================
// 枚举字符串映射（小写连字符，对齐 docs 序列化风格）
// ============================================================================

std::string_view to_string_view(ElectricalType t) {
    switch (t) {
        case ElectricalType::PASSIVE:          return "passive";
        case ElectricalType::INPUT:            return "input";
        case ElectricalType::OUTPUT:           return "output";
        case ElectricalType::BIDIRECTIONAL:    return "bidirectional";
        case ElectricalType::POWER_INPUT:      return "power_input";
        case ElectricalType::POWER_OUTPUT:     return "power_output";
        case ElectricalType::OPEN_COLLECTOR:   return "open_collector";
        case ElectricalType::OPEN_EMITTER:     return "open_emitter";
        case ElectricalType::TRI_STATE:        return "tri_state";
        case ElectricalType::UNSPECIFIED:      return "unspecified";
    }
    return "passive";
}

ElectricalType parse_electrical_type(std::string_view s) {
    if (s == "input")            return ElectricalType::INPUT;
    if (s == "output")           return ElectricalType::OUTPUT;
    if (s == "bidirectional")    return ElectricalType::BIDIRECTIONAL;
    if (s == "power_input")      return ElectricalType::POWER_INPUT;
    if (s == "power_output")     return ElectricalType::POWER_OUTPUT;
    if (s == "open_collector")   return ElectricalType::OPEN_COLLECTOR;
    if (s == "open_emitter")     return ElectricalType::OPEN_EMITTER;
    if (s == "tri_state")        return ElectricalType::TRI_STATE;
    if (s == "unspecified")      return ElectricalType::UNSPECIFIED;
    return ElectricalType::PASSIVE;  // 含 "passive" 与未识别值（前向兼容）
}

std::string_view to_string_view(Orientation o) {
    switch (o) {
        case Orientation::RIGHT: return "right";
        case Orientation::LEFT:  return "left";
        case Orientation::UP:    return "up";
        case Orientation::DOWN:  return "down";
    }
    return "right";
}

Orientation parse_orientation(std::string_view s) {
    if (s == "left")  return Orientation::LEFT;
    if (s == "up")    return Orientation::UP;
    if (s == "down")  return Orientation::DOWN;
    return Orientation::RIGHT;
}

std::string_view to_string_view(GraphicsKind k) {
    switch (k) {
        case GraphicsKind::POLYLINE: return "polyline";
        case GraphicsKind::ARC:      return "arc";
        case GraphicsKind::POLYGON:  return "polygon";
        case GraphicsKind::TEXT:     return "text";
    }
    return "polyline";
}

GraphicsKind parse_graphics_kind(std::string_view s) {
    if (s == "arc")     return GraphicsKind::ARC;
    if (s == "polygon") return GraphicsKind::POLYGON;
    if (s == "text")    return GraphicsKind::TEXT;
    return GraphicsKind::POLYLINE;
}

// ============================================================================
// Pin
// ============================================================================

Pin::Pin(std::string number, std::string name, geometry::Point position,
         ElectricalType etype, Orientation orientation,
         geometry::Coord length_nm, int gate)
    : number_(std::move(number)),
      name_(std::move(name)),
      position_(position),
      electrical_type_(etype),
      orientation_(orientation),
      length_nm_(length_nm),
      gate_(gate) {}

// ============================================================================
// Gate
// ============================================================================

Gate::Gate(int index, std::string name) : index_(index), name_(std::move(name)) {}

// ============================================================================
// Symbol
// ============================================================================

Symbol::Symbol() : uuid_(generate_uuid()) {}

void Symbol::reset() {
    // 保留 uuid_（稳定主键）；清空其余业务成员。deserialize() 开头调用。
    name_.clear();
    designation_.clear();
    description_.clear();
    keywords_.clear();
    datasheet_url_.clear();
    default_mpn_.clear();
    pins_.clear();
    graphics_.clear();
    pin_map_.clear();
    gates_.clear();
}

Pin& Symbol::add_pin(Pin p) {
    // 注：去重由调用方负责（find_pin_by_number + remove_pin_by_number 显式控制），
    // 与 eda::Project::add_page 一致的"append 语义"——简单、可预测。
    pins_.push_back(std::move(p));
    return pins_.back();
}

bool Symbol::remove_pin_by_number(const std::string& number) {
    auto it = std::find_if(pins_.begin(), pins_.end(),
                           [&](const Pin& x) { return x.number() == number; });
    if (it == pins_.end()) return false;
    pins_.erase(it);
    pin_map_.erase(number);
    return true;
}

const Pin* Symbol::find_pin_by_number(const std::string& number) const {
    auto it = std::find_if(pins_.begin(), pins_.end(),
                           [&](const Pin& x) { return x.number() == number; });
    return it == pins_.end() ? nullptr : &(*it);
}

Pin* Symbol::find_pin_by_number(const std::string& number) {
    auto it = std::find_if(pins_.begin(), pins_.end(),
                           [&](const Pin& x) { return x.number() == number; });
    return it == pins_.end() ? nullptr : &(*it);
}

const Pin* Symbol::find_pin_by_name(const std::string& name) const {
    auto it = std::find_if(pins_.begin(), pins_.end(),
                           [&](const Pin& x) { return x.name() == name; });
    return it == pins_.end() ? nullptr : &(*it);
}

GraphicsElement& Symbol::add_graphics(GraphicsElement g) {
    graphics_.push_back(std::move(g));
    return graphics_.back();
}

void Symbol::map_pin(std::string pin_number, std::string mapped_id) {
    pin_map_[std::move(pin_number)] = std::move(mapped_id);
}

bool Symbol::unmap_pin(const std::string& pin_number) {
    return pin_map_.erase(pin_number) > 0;
}

const std::string* Symbol::mapped_id(const std::string& pin_number) const {
    auto it = pin_map_.find(pin_number);
    return it == pin_map_.end() ? nullptr : &it->second;
}

Gate& Symbol::add_gate(Gate g) {
    gates_.push_back(std::move(g));
    return gates_.back();
}

// ============================================================================
// 最小 TOML 子集 —— 序列化（确定性输出，字段顺序固定）
// ============================================================================
//
// 输出 schema：
//   schema_version = 1
//   uuid = "..."
//   name = "..."
//   designation = "U?"
//   description = "..."            # 空字符串仍输出（schema 完整性）
//   keywords = "..."
//   datasheet_url = "..."
//   default_mpn = "..."
//
//   [[pins]]                       # 按 number 字典序
//   number = "1"
//   name = "VCC"
//   position = [x, y]              # nm
//   electrical_type = "power_input"
//   orientation = "right"
//   length_nm = 7_620_000          # 0.5 mil * 1500 = 300 mil
//   gate = 0
//
//   [[graphics]]                   # 按数组序
//   kind = "polyline"
//   stroke_width_nm = 100000
//   polyline = [[x,y], [x,y], ...] # 仅 kind=polyline
//   arc = {center=[x,y], radius=.., ...} # 仅 kind=arc
//   polygon = {outer=[[x,y],...]}  # 仅 kind=polygon
//   filled = false                 # 仅 kind=polygon
//   text = "..."                   # 仅 kind=text
//   anchor = [x,y]
//   text_size_nm = 1500000
//
//   [pin_map]                      # 仅当非空
//   "1" = "A1"                     # 键需引号（含数字字符串）
//
//   [[gates]]                      # 仅当非空
//   index = 0
//   name = "A"

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

void append_toml_point(std::string& out, geometry::Point p) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "[%lld, %lld]",
                  static_cast<long long>(p.x), static_cast<long long>(p.y));
    out += buf;
}

void append_toml_point_array(std::string& out, const std::vector<geometry::Point>& pts) {
    out += '[';
    for (size_t i = 0; i < pts.size(); ++i) {
        if (i > 0) out += ", ";
        append_toml_point(out, pts[i]);
    }
    out += ']';
}

void serialize_pin_into(std::string& out, const Pin& p) {
    out += "[[pins]]\n";
    out += "number = "; append_toml_string(out, p.number()); out += '\n';
    out += "name = "; append_toml_string(out, p.name()); out += '\n';
    out += "position = "; append_toml_point(out, p.position()); out += '\n';
    out += "electrical_type = ";
    append_toml_string(out, std::string(to_string_view(p.electrical_type())));
    out += '\n';
    out += "orientation = ";
    append_toml_string(out, std::string(to_string_view(p.orientation())));
    out += '\n';
    out += "length_nm = "; out += std::to_string(p.length_nm()); out += '\n';
    out += "gate = "; out += std::to_string(p.gate()); out += '\n';
}

void serialize_graphics_into(std::string& out, const GraphicsElement& g) {
    out += "[[graphics]]\n";
    out += "kind = ";
    append_toml_string(out, std::string(to_string_view(g.kind)));
    out += '\n';
    out += "stroke_width_nm = "; out += std::to_string(g.stroke_width_nm); out += '\n';

    switch (g.kind) {
        case GraphicsKind::POLYLINE:
            out += "polyline = ";
            append_toml_point_array(out, g.polyline.points);
            out += '\n';
            break;
        case GraphicsKind::ARC:
            out += "arc_center = "; append_toml_point(out, g.arc.center); out += '\n';
            out += "arc_radius = "; out += std::to_string(g.arc.radius); out += '\n';
            {
                char buf[96];
                std::snprintf(buf, sizeof(buf),
                              "arc_start_angle = %.17g\narc_end_angle = %.17g\narc_ccw = %s\n",
                              g.arc.start_angle, g.arc.end_angle,
                              g.arc.ccw ? "true" : "false");
                out += buf;
            }
            break;
        case GraphicsKind::POLYGON:
            out += "polygon_outer = ";
            append_toml_point_array(out, g.polygon.outer);
            out += '\n';
            out += "filled = "; out += (g.filled ? "true" : "false"); out += '\n';
            break;
        case GraphicsKind::TEXT:
            out += "text = "; append_toml_string(out, g.text); out += '\n';
            out += "anchor = "; append_toml_point(out, g.text_anchor); out += '\n';
            out += "text_size_nm = "; out += std::to_string(g.text_size_nm); out += '\n';
            break;
    }
}

}  // namespace

std::string Symbol::serialize() const {
    std::string out;
    out.reserve(512);

    out += "schema_version = 1\n";
    out += "uuid = "; append_toml_string(out, uuid_); out += '\n';
    out += "name = "; append_toml_string(out, name_); out += '\n';
    out += "designation = "; append_toml_string(out, designation_); out += '\n';
    out += "description = "; append_toml_string(out, description_); out += '\n';
    out += "keywords = "; append_toml_string(out, keywords_); out += '\n';
    out += "datasheet_url = "; append_toml_string(out, datasheet_url_); out += '\n';
    out += "default_mpn = "; append_toml_string(out, default_mpn_); out += '\n';

    // pins：按 number 字典序（确定性）
    std::vector<const Pin*> sorted_pins;
    sorted_pins.reserve(pins_.size());
    for (const Pin& p : pins_) sorted_pins.push_back(&p);
    std::sort(sorted_pins.begin(), sorted_pins.end(),
              [](const Pin* a, const Pin* b) { return a->number() < b->number(); });
    for (const Pin* p : sorted_pins) {
        out += '\n';
        serialize_pin_into(out, *p);
    }

    // graphics：保持插入序（绘制 z-order 语义）
    for (const GraphicsElement& g : graphics_) {
        out += '\n';
        serialize_graphics_into(out, g);
    }

    // pin_map：非空时输出，键字典序
    if (!pin_map_.empty()) {
        std::vector<std::pair<std::string, std::string>> entries(
            pin_map_.begin(), pin_map_.end());
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        out += "\n[pin_map]\n";
        for (const auto& [k, v] : entries) {
            append_toml_string(out, k);
            out += " = ";
            append_toml_string(out, v);
            out += '\n';
        }
    }

    // gates：按 index 升序
    if (!gates_.empty()) {
        std::vector<const Gate*> sorted_gates;
        sorted_gates.reserve(gates_.size());
        for (const Gate& g : gates_) sorted_gates.push_back(&g);
        std::sort(sorted_gates.begin(), sorted_gates.end(),
                  [](const Gate* a, const Gate* b) { return a->index() < b->index(); });
        for (const Gate* g : sorted_gates) {
            out += "\n[[gates]]\n";
            out += "index = "; out += std::to_string(g->index()); out += '\n';
            out += "name = "; append_toml_string(out, g->name()); out += '\n';
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
            ++i;
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
                case '"': out += '"'; break;
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
    // 允许下划线分隔（TOML 数字语法）
    std::string clean;
    clean.reserve(s.size());
    for (char c : s) if (c != '_') clean += c;
    if (clean.empty()) return false;
    try {
        size_t pos = 0;
        long long v = std::stoll(clean, &pos);
        if (pos != clean.size()) return false;
        out = static_cast<int64_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_toml_bool(std::string_view raw, bool& out) {
    if (raw == "true") { out = true; return true; }
    if (raw == "false") { out = false; return true; }
    return false;
}

bool parse_toml_double(std::string_view raw, double& out) {
    std::string s(raw);
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        double v = std::stod(s, &pos);
        if (pos != s.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

// 解析 [x, y] 整数对（nm）。
bool parse_toml_point(std::string_view raw, geometry::Point& out) {
    if (raw.size() < 5 || raw.front() != '[' || raw.back() != ']') return false;
    std::string_view body = raw.substr(1, raw.size() - 2);
    auto comma = body.find(',');
    if (comma == std::string_view::npos) return false;
    int64_t x, y;
    if (!parse_toml_int(trim_ws(std::string(body.substr(0, comma))), x)) return false;
    if (!parse_toml_int(trim_ws(std::string(body.substr(comma + 1))), y)) return false;
    out = geometry::Point{x, y};
    return true;
}

// 解析 [[x,y],[x,y],...]：点列表。
bool parse_toml_point_list(std::string_view raw, std::vector<geometry::Point>& out) {
    if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') return false;
    std::string_view body = raw.substr(1, raw.size() - 2);
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == ',' ||
               body[i] == '\n' || body[i] == '\r')) {
            ++i;
        }
        if (i >= body.size()) break;
        if (body[i] != '[') return false;
        size_t end = body.find(']', i);
        if (end == std::string_view::npos) return false;
        geometry::Point p;
        if (!parse_toml_point(body.substr(i, end - i + 1), p)) return false;
        out.push_back(p);
        i = end + 1;
    }
    return true;
}

}  // namespace

bool Symbol::deserialize(const std::string& text) {
    // 重置当前 Symbol 到空白（保留 uuid_；若文本含 uuid 则下方覆盖）
    reset();

    enum class Ctx { ROOT, PIN, GRAPHICS, GATE, PIN_MAP };
    Ctx ctx = Ctx::ROOT;
    Pin cur_pin;
    GraphicsElement cur_gfx;
    Gate cur_gate;
    std::string pin_map_key;  // pin_map 段当前 key 缓存（用于双行 key=value）

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = strip_comment(line);
        std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;

        // 数组表头 [[...]]
        if (trimmed.size() >= 2 && trimmed[0] == '[' && trimmed[1] == '[') {
            // flush 上一段
            if (ctx == Ctx::PIN) pins_.push_back(std::move(cur_pin));
            else if (ctx == Ctx::GRAPHICS) graphics_.push_back(std::move(cur_gfx));
            else if (ctx == Ctx::GATE) gates_.push_back(std::move(cur_gate));

            size_t end = trimmed.find("]]");
            if (end == std::string::npos) { ctx = Ctx::ROOT; continue; }
            std::string head = trim_ws(trimmed.substr(2, end - 2));

            if (head == "pins") {
                ctx = Ctx::PIN;
                cur_pin = Pin();
            } else if (head == "graphics") {
                ctx = Ctx::GRAPHICS;
                cur_gfx = GraphicsElement();
            } else if (head == "gates") {
                ctx = Ctx::GATE;
                cur_gate = Gate();
            } else {
                ctx = Ctx::ROOT;
            }
            continue;
        }

        // 单表头 [...]（含 [pin_map]）
        if (!trimmed.empty() && trimmed[0] == '[') {
            if (ctx == Ctx::PIN) pins_.push_back(std::move(cur_pin));
            else if (ctx == Ctx::GRAPHICS) graphics_.push_back(std::move(cur_gfx));
            else if (ctx == Ctx::GATE) gates_.push_back(std::move(cur_gate));

            size_t end = trimmed.find(']');
            if (end == std::string::npos) { ctx = Ctx::ROOT; continue; }
            std::string head = trim_ws(trimmed.substr(1, end - 1));
            ctx = (head == "pin_map") ? Ctx::PIN_MAP : Ctx::ROOT;
            continue;
        }

        // key = value
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_ws(trimmed.substr(0, eq));
        std::string raw_val = trim_ws(trimmed.substr(eq + 1));
        if (key.empty() || raw_val.empty()) continue;

        switch (ctx) {
            case Ctx::ROOT: {
                if (key == "uuid") {
                    std::string v;
                    if (parse_toml_string(raw_val, v)) uuid_ = std::move(v);
                } else if (key == "name") {
                    std::string v;
                    if (parse_toml_string(raw_val, v)) name_ = std::move(v);
                } else if (key == "designation") {
                    std::string v;
                    if (parse_toml_string(raw_val, v)) designation_ = std::move(v);
                } else if (key == "description") {
                    std::string v;
                    if (parse_toml_string(raw_val, v)) description_ = std::move(v);
                } else if (key == "keywords") {
                    std::string v;
                    if (parse_toml_string(raw_val, v)) keywords_ = std::move(v);
                } else if (key == "datasheet_url") {
                    std::string v;
                    if (parse_toml_string(raw_val, v)) datasheet_url_ = std::move(v);
                } else if (key == "default_mpn") {
                    std::string v;
                    if (parse_toml_string(raw_val, v)) default_mpn_ = std::move(v);
                }
                break;
            }
            case Ctx::PIN: {
                if (key == "number") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_pin.set_number(std::move(v));
                } else if (key == "name") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_pin.set_name(std::move(v));
                } else if (key == "position") {
                    geometry::Point p; if (parse_toml_point(raw_val, p)) cur_pin.set_position(p);
                } else if (key == "electrical_type") {
                    std::string v; if (parse_toml_string(raw_val, v))
                        cur_pin.set_electrical_type(parse_electrical_type(v));
                } else if (key == "orientation") {
                    std::string v; if (parse_toml_string(raw_val, v))
                        cur_pin.set_orientation(parse_orientation(v));
                } else if (key == "length_nm") {
                    int64_t v; if (parse_toml_int(raw_val, v)) cur_pin.set_length_nm(v);
                } else if (key == "gate") {
                    int64_t v; if (parse_toml_int(raw_val, v)) cur_pin.set_gate(static_cast<int>(v));
                }
                break;
            }
            case Ctx::GRAPHICS: {
                if (key == "kind") {
                    std::string v; if (parse_toml_string(raw_val, v))
                        cur_gfx.kind = parse_graphics_kind(v);
                } else if (key == "stroke_width_nm") {
                    int64_t v; if (parse_toml_int(raw_val, v)) cur_gfx.stroke_width_nm = v;
                } else if (key == "polyline") {
                    parse_toml_point_list(raw_val, cur_gfx.polyline.points);
                } else if (key == "arc_center") {
                    geometry::Point p; if (parse_toml_point(raw_val, p)) cur_gfx.arc.center = p;
                } else if (key == "arc_radius") {
                    int64_t v; if (parse_toml_int(raw_val, v)) cur_gfx.arc.radius = v;
                } else if (key == "arc_start_angle") {
                    double v; if (parse_toml_double(raw_val, v)) cur_gfx.arc.start_angle = v;
                } else if (key == "arc_end_angle") {
                    double v; if (parse_toml_double(raw_val, v)) cur_gfx.arc.end_angle = v;
                } else if (key == "arc_ccw") {
                    bool v; if (parse_toml_bool(raw_val, v)) cur_gfx.arc.ccw = v;
                } else if (key == "polygon_outer") {
                    parse_toml_point_list(raw_val, cur_gfx.polygon.outer);
                } else if (key == "filled") {
                    bool v; if (parse_toml_bool(raw_val, v)) cur_gfx.filled = v;
                } else if (key == "text") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_gfx.text = std::move(v);
                } else if (key == "anchor") {
                    geometry::Point p; if (parse_toml_point(raw_val, p)) cur_gfx.text_anchor = p;
                } else if (key == "text_size_nm") {
                    int64_t v; if (parse_toml_int(raw_val, v)) cur_gfx.text_size_nm = v;
                }
                break;
            }
            case Ctx::GATE: {
                if (key == "index") {
                    int64_t v; if (parse_toml_int(raw_val, v)) cur_gate.set_index(static_cast<int>(v));
                } else if (key == "name") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_gate.set_name(std::move(v));
                }
                break;
            }
            case Ctx::PIN_MAP: {
                // key 必为字符串字面量；value 也是字符串
                std::string k, v;
                if (parse_toml_string(key, k) && parse_toml_string(raw_val, v)) {
                    pin_map_[std::move(k)] = std::move(v);
                }
                break;
            }
        }
    }

    // flush 末段
    if (ctx == Ctx::PIN) pins_.push_back(std::move(cur_pin));
    else if (ctx == Ctx::GRAPHICS) graphics_.push_back(std::move(cur_gfx));
    else if (ctx == Ctx::GATE) gates_.push_back(std::move(cur_gate));

    return true;
}

Ref<Symbol> Symbol::parse(const std::string& text) {
    Ref<Symbol> s(new Symbol());
    if (!s->deserialize(text)) return Ref<Symbol>();
    return s;
}

}  // namespace eda
