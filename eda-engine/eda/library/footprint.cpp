// eda/library/footprint.cpp
//
// P6 元件库 · 封装数据模型实现 + .fp.toml 序列化。
//
// 序列化风格与 symbol.cpp 一致：手写最小 TOML 子集，确定性输出（字段顺序固定，
// pads 按 number 字典序，silkscreen 保留 z-order）。

#include "eda/library/footprint.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <utility>

#include "eda/project/project.h"  // generate_uuid 复用

namespace eda {

// ============================================================================
// 枚举字符串映射
// ============================================================================

std::string_view to_string_view(PadType t) {
    switch (t) {
        case PadType::SMD:          return "smd";
        case PadType::THROUGH_HOLE: return "through_hole";
        case PadType::NON_PLATED:   return "non_plated";
    }
    return "smd";
}

PadType parse_pad_type(std::string_view s) {
    if (s == "through_hole") return PadType::THROUGH_HOLE;
    if (s == "non_plated")   return PadType::NON_PLATED;
    return PadType::SMD;
}

std::string_view to_string_view(PadShape s) {
    switch (s) {
        case PadShape::CIRCLE:        return "circle";
        case PadShape::SQUARE:        return "square";
        case PadShape::RECTANGLE:     return "rectangle";
        case PadShape::OVAL:          return "oval";
        case PadShape::ROUNDED_RECT:  return "rounded_rect";
        case PadShape::POLYGON:       return "polygon";
    }
    return "rectangle";
}

PadShape parse_pad_shape(std::string_view s) {
    if (s == "circle")       return PadShape::CIRCLE;
    if (s == "square")       return PadShape::SQUARE;
    if (s == "oval")         return PadShape::OVAL;
    if (s == "rounded_rect") return PadShape::ROUNDED_RECT;
    if (s == "polygon")      return PadShape::POLYGON;
    return PadShape::RECTANGLE;
}

std::string_view to_string_view(PadLayer l) {
    switch (l) {
        case PadLayer::TOP:    return "top";
        case PadLayer::BOTTOM: return "bottom";
        case PadLayer::ALL:    return "all";
    }
    return "top";
}

PadLayer parse_pad_layer(std::string_view s) {
    if (s == "bottom") return PadLayer::BOTTOM;
    if (s == "all")    return PadLayer::ALL;
    return PadLayer::TOP;
}

// ============================================================================
// Pad
// ============================================================================

Pad::Pad(std::string number, geometry::Point position, PadType type, PadShape shape,
         geometry::Coord width_nm, geometry::Coord height_nm, PadLayer layer)
    : number_(std::move(number)),
      position_(position),
      type_(type),
      shape_(shape),
      width_nm_(width_nm),
      height_nm_(height_nm),
      layer_(layer) {}

// ============================================================================
// Footprint
// ============================================================================

Footprint::Footprint() : uuid_(generate_uuid()) {}

void Footprint::reset() {
    // 保留 uuid_（稳定主键）；清空其余业务成员。deserialize() 开头调用。
    name_.clear();
    description_.clear();
    keywords_.clear();
    pads_.clear();
    silkscreen_.clear();
    courtyard_ = geometry::Polygon{};
    ipc7351_ = Ipc7351Fields{};
    model_3d_ = Model3DRef{};
}

Pad& Footprint::add_pad(Pad p) {
    pads_.push_back(std::move(p));
    return pads_.back();
}

bool Footprint::remove_pad_by_number(const std::string& number) {
    auto it = std::find_if(pads_.begin(), pads_.end(),
                           [&](const Pad& x) { return x.number() == number; });
    if (it == pads_.end()) return false;
    pads_.erase(it);
    return true;
}

const Pad* Footprint::find_pad_by_number(const std::string& number) const {
    auto it = std::find_if(pads_.begin(), pads_.end(),
                           [&](const Pad& x) { return x.number() == number; });
    return it == pads_.end() ? nullptr : &(*it);
}

Pad* Footprint::find_pad_by_number(const std::string& number) {
    auto it = std::find_if(pads_.begin(), pads_.end(),
                           [&](const Pad& x) { return x.number() == number; });
    return it == pads_.end() ? nullptr : &(*it);
}

GraphicsElement& Footprint::add_silkscreen(GraphicsElement g) {
    silkscreen_.push_back(std::move(g));
    return silkscreen_.back();
}

// ============================================================================
// 最小 TOML 子集 —— 序列化辅助（与 symbol.cpp 重复但保持模块独立）
// ============================================================================
//
// 输出 schema：
//   schema_version = 1
//   uuid = "..."
//   name = "..."
//   description = "..."
//   keywords = "..."
//
//   [[pads]]
//   number = "1"
//   position = [x, y]
//   type = "smd"
//   shape = "rectangle"
//   size = [w, h]
//   layer = "top"
//   drill_diameter_nm = 0
//   paste_coverage = 1.0
//
//   [[silkscreen]]                 # 同 symbol.graphics
//   ...
//
//   [courtyard]                    # 仅当非空
//   outer = [[x,y], ...]
//
//   [ipc7351]
//   density_level = "M"
//   footprint_name = "..."
//   ...
//
//   [model_3d]                     # 仅当有 path
//   path = "res://3d/..."
//   offset_nm = [x, y, z]
//   rotation_deg = [rx, ry, rz]
//   scale = 1.0
//   body_height_nm = 0

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

void serialize_graphics_into(std::string& out, const GraphicsElement& g) {
    // 与 symbol.cpp 同名 helper 同实现；显式重复以保持 footprint.cpp 单独可编译。
    out += "[[silkscreen]]\n";
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

void serialize_pad_into(std::string& out, const Pad& p) {
    out += "[[pads]]\n";
    out += "number = "; append_toml_string(out, p.number()); out += '\n';
    out += "position = "; append_toml_point(out, p.position()); out += '\n';
    out += "type = "; append_toml_string(out, std::string(to_string_view(p.type()))); out += '\n';
    out += "shape = "; append_toml_string(out, std::string(to_string_view(p.shape()))); out += '\n';
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "size = [%lld, %lld]\n",
                      static_cast<long long>(p.width_nm()),
                      static_cast<long long>(p.height_nm()));
        out += buf;
    }
    out += "layer = "; append_toml_string(out, std::string(to_string_view(p.layer()))); out += '\n';
    out += "drill_diameter_nm = "; out += std::to_string(p.drill_diameter_nm()); out += '\n';
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "paste_coverage = %.17g\n", p.paste_coverage());
        out += buf;
    }
}

}  // namespace

std::string Footprint::serialize() const {
    std::string out;
    out.reserve(512);

    out += "schema_version = 1\n";
    out += "uuid = "; append_toml_string(out, uuid_); out += '\n';
    out += "name = "; append_toml_string(out, name_); out += '\n';
    out += "description = "; append_toml_string(out, description_); out += '\n';
    out += "keywords = "; append_toml_string(out, keywords_); out += '\n';

    // pads：按 number 字典序
    std::vector<const Pad*> sorted_pads;
    sorted_pads.reserve(pads_.size());
    for (const Pad& p : pads_) sorted_pads.push_back(&p);
    std::sort(sorted_pads.begin(), sorted_pads.end(),
              [](const Pad* a, const Pad* b) { return a->number() < b->number(); });
    for (const Pad* p : sorted_pads) {
        out += '\n';
        serialize_pad_into(out, *p);
    }

    // silkscreen：保留 z-order
    for (const GraphicsElement& g : silkscreen_) {
        out += '\n';
        serialize_graphics_into(out, g);
    }

    // courtyard：仅当 outer 非空
    if (has_courtyard()) {
        out += "\n[courtyard]\n";
        out += "outer = ";
        append_toml_point_array(out, courtyard_.outer);
        out += '\n';
        // holes：每个内环单独输出（holes_H_<i> 形式保留 order）
        for (size_t i = 0; i < courtyard_.holes.size(); ++i) {
            out += "hole_";
            out += std::to_string(i);
            out += " = ";
            append_toml_point_array(out, courtyard_.holes[i]);
            out += '\n';
        }
    }

    // ipc7351：始终输出（即使 density_level 为默认 "M"，便于读取方拿到稳定 schema）
    out += "\n[ipc7351]\n";
    out += "density_level = "; append_toml_string(out, ipc7351_.density_level()); out += '\n';
    out += "footprint_name = "; append_toml_string(out, ipc7351_.footprint_name()); out += '\n';
    out += "lead_span_nm = "; out += std::to_string(ipc7351_.lead_span_nm()); out += '\n';
    out += "body_width_nm = "; out += std::to_string(ipc7351_.body_width_nm()); out += '\n';
    out += "body_length_nm = "; out += std::to_string(ipc7351_.body_length_nm()); out += '\n';
    out += "courtyard_excess_nm = "; out += std::to_string(ipc7351_.courtyard_excess_nm()); out += '\n';

    // model_3d：仅当有 path
    if (has_model_3d()) {
        out += "\n[model_3d]\n";
        out += "path = "; append_toml_string(out, model_3d_.path()); out += '\n';
        {
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "offset_nm = [%lld, %lld, %lld]\n",
                          static_cast<long long>(model_3d_.offset_x_nm()),
                          static_cast<long long>(model_3d_.offset_y_nm()),
                          static_cast<long long>(model_3d_.offset_z_nm()));
            out += buf;
            std::snprintf(buf, sizeof(buf),
                          "rotation_deg = [%.17g, %.17g, %.17g]\n",
                          model_3d_.rotation_x_deg(),
                          model_3d_.rotation_y_deg(),
                          model_3d_.rotation_z_deg());
            out += buf;
            std::snprintf(buf, sizeof(buf),
                          "scale = %.17g\nbody_height_nm = %lld\n",
                          model_3d_.scale(),
                          static_cast<long long>(model_3d_.body_height_nm()));
            out += buf;
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

bool parse_toml_bool(std::string_view raw, bool& out) {
    if (raw == "true") { out = true; return true; }
    if (raw == "false") { out = false; return true; }
    return false;
}

// 解析整数/双精度列表 [a, b, ...]，按目标类型分支
bool parse_int_pair(std::string_view raw, int64_t& a, int64_t& b) {
    if (raw.size() < 5 || raw.front() != '[' || raw.back() != ']') return false;
    std::string_view body = raw.substr(1, raw.size() - 2);
    auto comma = body.find(',');
    if (comma == std::string_view::npos) return false;
    if (!parse_toml_int(trim_ws(std::string(body.substr(0, comma))), a)) return false;
    if (!parse_toml_int(trim_ws(std::string(body.substr(comma + 1))), b)) return false;
    return true;
}

bool parse_int_triple(std::string_view raw, int64_t& a, int64_t& b, int64_t& c) {
    if (raw.size() < 7 || raw.front() != '[' || raw.back() != ']') return false;
    std::string_view body = raw.substr(1, raw.size() - 2);
    auto c1 = body.find(',');
    if (c1 == std::string_view::npos) return false;
    auto c2 = body.find(',', c1 + 1);
    if (c2 == std::string_view::npos) return false;
    if (!parse_toml_int(trim_ws(std::string(body.substr(0, c1))), a)) return false;
    if (!parse_toml_int(trim_ws(std::string(body.substr(c1 + 1, c2 - c1 - 1))), b)) return false;
    if (!parse_toml_int(trim_ws(std::string(body.substr(c2 + 1))), c)) return false;
    return true;
}

bool parse_double_triple(std::string_view raw, double& a, double& b, double& c) {
    if (raw.size() < 7 || raw.front() != '[' || raw.back() != ']') return false;
    std::string_view body = raw.substr(1, raw.size() - 2);
    auto c1 = body.find(',');
    if (c1 == std::string_view::npos) return false;
    auto c2 = body.find(',', c1 + 1);
    if (c2 == std::string_view::npos) return false;
    if (!parse_toml_double(trim_ws(std::string(body.substr(0, c1))), a)) return false;
    if (!parse_toml_double(trim_ws(std::string(body.substr(c1 + 1, c2 - c1 - 1))), b)) return false;
    if (!parse_toml_double(trim_ws(std::string(body.substr(c2 + 1))), c)) return false;
    return true;
}

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
        int64_t x, y;
        if (!parse_int_pair(body.substr(i, end - i + 1), x, y)) return false;
        out.push_back(geometry::Point{x, y});
        i = end + 1;
    }
    return true;
}

}  // namespace

bool Footprint::deserialize(const std::string& text) {
    // 重置当前 Footprint 到空白（保留 uuid_；若文本含 uuid 则下方覆盖）
    reset();

    enum class Ctx { ROOT, PAD, SILKSCREEN };
    Ctx ctx = Ctx::ROOT;
    // 当前单表头段名（courtyard / ipc7351 / model_3d）；用于 ROOT 分支派发字段。
    std::string current_section;
    Pad cur_pad;
    GraphicsElement cur_gfx;

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = strip_comment(line);
        std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;

        // 数组表头 [[...]]
        if (trimmed.size() >= 2 && trimmed[0] == '[' && trimmed[1] == '[') {
            if (ctx == Ctx::PAD) pads_.push_back(std::move(cur_pad));
            else if (ctx == Ctx::SILKSCREEN) silkscreen_.push_back(std::move(cur_gfx));

            size_t end = trimmed.find("]]");
            if (end == std::string::npos) { ctx = Ctx::ROOT; current_section.clear(); continue; }
            std::string head = trim_ws(trimmed.substr(2, end - 2));
            current_section.clear();
            if (head == "pads") {
                ctx = Ctx::PAD;
                cur_pad = Pad();
            } else if (head == "silkscreen") {
                ctx = Ctx::SILKSCREEN;
                cur_gfx = GraphicsElement();
            } else {
                ctx = Ctx::ROOT;
            }
            continue;
        }

        // 单表头 [...]
        if (!trimmed.empty() && trimmed[0] == '[') {
            if (ctx == Ctx::PAD) pads_.push_back(std::move(cur_pad));
            else if (ctx == Ctx::SILKSCREEN) silkscreen_.push_back(std::move(cur_gfx));
            size_t end = trimmed.find(']');
            if (end == std::string::npos) { ctx = Ctx::ROOT; current_section.clear(); continue; }
            current_section = trim_ws(trimmed.substr(1, end - 1));
            ctx = Ctx::ROOT;
            cur_pad = Pad();
            cur_gfx = GraphicsElement();
            continue;
        }

        // key = value
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_ws(trimmed.substr(0, eq));
        std::string raw_val = trim_ws(trimmed.substr(eq + 1));
        if (key.empty() || raw_val.empty()) continue;

        // 先按"当前单表头段"派发
        if (ctx == Ctx::ROOT && !current_section.empty()) {
            if (current_section == "courtyard") {
                if (key == "outer") {
                    parse_toml_point_list(raw_val, courtyard_.outer);
                } else if (key.rfind("hole_", 0) == 0) {
                    std::vector<geometry::Point> hole;
                    if (parse_toml_point_list(raw_val, hole)) courtyard_.holes.push_back(std::move(hole));
                }
                continue;
            } else if (current_section == "ipc7351") {
                if (key == "density_level") {
                    std::string v; if (parse_toml_string(raw_val, v)) ipc7351_.set_density_level(std::move(v));
                } else if (key == "footprint_name") {
                    std::string v; if (parse_toml_string(raw_val, v)) ipc7351_.set_footprint_name(std::move(v));
                } else if (key == "lead_span_nm") {
                    int64_t v; if (parse_toml_int(raw_val, v)) ipc7351_.set_lead_span_nm(v);
                } else if (key == "body_width_nm") {
                    int64_t v; if (parse_toml_int(raw_val, v)) ipc7351_.set_body_width_nm(v);
                } else if (key == "body_length_nm") {
                    int64_t v; if (parse_toml_int(raw_val, v)) ipc7351_.set_body_length_nm(v);
                } else if (key == "courtyard_excess_nm") {
                    int64_t v; if (parse_toml_int(raw_val, v)) ipc7351_.set_courtyard_excess_nm(v);
                }
                continue;
            } else if (current_section == "model_3d") {
                Model3DRef m = model_3d_;  // 增量填入（避免被默认值覆盖未出现字段）
                if (key == "path") {
                    std::string v; if (parse_toml_string(raw_val, v)) m.set_path(std::move(v));
                } else if (key == "offset_nm") {
                    int64_t x, y, z;
                    if (parse_int_triple(raw_val, x, y, z)) m.set_offset_nm(x, y, z);
                } else if (key == "rotation_deg") {
                    double x, y, z;
                    if (parse_double_triple(raw_val, x, y, z)) m.set_rotation_deg(x, y, z);
                } else if (key == "scale") {
                    double v; if (parse_toml_double(raw_val, v)) m.set_scale(v);
                } else if (key == "body_height_nm") {
                    int64_t v; if (parse_toml_int(raw_val, v)) m.set_body_height_nm(v);
                }
                model_3d_ = std::move(m);
                continue;
            }
        }

        switch (ctx) {
            case Ctx::ROOT: {
                if (key == "uuid") {
                    std::string v; if (parse_toml_string(raw_val, v)) uuid_ = std::move(v);
                } else if (key == "name") {
                    std::string v; if (parse_toml_string(raw_val, v)) name_ = std::move(v);
                } else if (key == "description") {
                    std::string v; if (parse_toml_string(raw_val, v)) description_ = std::move(v);
                } else if (key == "keywords") {
                    std::string v; if (parse_toml_string(raw_val, v)) keywords_ = std::move(v);
                }
                break;
            }
            case Ctx::PAD: {
                if (key == "number") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_pad.set_number(std::move(v));
                } else if (key == "position") {
                    int64_t x, y;
                    if (parse_int_pair(raw_val, x, y)) cur_pad.set_position(geometry::Point{x, y});
                } else if (key == "type") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_pad.set_type(parse_pad_type(v));
                } else if (key == "shape") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_pad.set_shape(parse_pad_shape(v));
                } else if (key == "size") {
                    int64_t w, h;
                    if (parse_int_pair(raw_val, w, h)) {
                        cur_pad.set_width_nm(w);
                        cur_pad.set_height_nm(h);
                    }
                } else if (key == "layer") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_pad.set_layer(parse_pad_layer(v));
                } else if (key == "drill_diameter_nm") {
                    int64_t v; if (parse_toml_int(raw_val, v)) cur_pad.set_drill_diameter_nm(v);
                } else if (key == "paste_coverage") {
                    double v; if (parse_toml_double(raw_val, v)) cur_pad.set_paste_coverage(v);
                }
                break;
            }
            case Ctx::SILKSCREEN: {
                if (key == "kind") {
                    std::string v; if (parse_toml_string(raw_val, v))
                        cur_gfx.kind = parse_graphics_kind(v);
                } else if (key == "stroke_width_nm") {
                    int64_t v; if (parse_toml_int(raw_val, v)) cur_gfx.stroke_width_nm = v;
                } else if (key == "polyline") {
                    parse_toml_point_list(raw_val, cur_gfx.polyline.points);
                } else if (key == "arc_center") {
                    int64_t x, y;
                    if (parse_int_pair(raw_val, x, y)) cur_gfx.arc.center = geometry::Point{x, y};
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
                    int64_t x, y;
                    if (parse_int_pair(raw_val, x, y)) cur_gfx.text_anchor = geometry::Point{x, y};
                } else if (key == "text_size_nm") {
                    int64_t v; if (parse_toml_int(raw_val, v)) cur_gfx.text_size_nm = v;
                }
                break;
            }
        }
    }

    if (ctx == Ctx::PAD) pads_.push_back(std::move(cur_pad));
    else if (ctx == Ctx::SILKSCREEN) silkscreen_.push_back(std::move(cur_gfx));

    return true;
}

Ref<Footprint> Footprint::parse(const std::string& text) {
    Ref<Footprint> f(new Footprint());
    if (!f->deserialize(text)) return Ref<Footprint>();
    return f;
}

}  // namespace eda
