// eda/schematic/component.cpp
//
// P7 原理图 · Component 器件实例实现 + .cmp.toml 序列化。
//
// 序列化风格与 eda/library/symbol.cpp / device.cpp 一致：手写最小 TOML 子集，
// 确定性输出（字段顺序固定；attributes 按键字典序）。

#include "eda/schematic/component.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <utility>

#include "eda/project/project.h"  // generate_uuid 复用

namespace eda::sch {

// ============================================================================
// Rotation 枚举映射
// ============================================================================

Rotation rotation_from_degrees(int degrees) noexcept {
    switch (degrees) {
        case 0:   return Rotation::DEG_0;
        case 90:  return Rotation::DEG_90;
        case 180: return Rotation::DEG_180;
        case 270: return Rotation::DEG_270;
        default:  return Rotation::DEG_0;  // 非法值回落（前向兼容）
    }
}

int to_degrees(Rotation r) noexcept {
    return static_cast<int>(r);
}

std::string_view to_string_view(Rotation r) {
    switch (r) {
        case Rotation::DEG_0:   return "0";
        case Rotation::DEG_90:  return "90";
        case Rotation::DEG_180: return "180";
        case Rotation::DEG_270: return "270";
    }
    return "0";
}

Rotation parse_rotation(std::string_view s) {
    if (s == "90")  return Rotation::DEG_90;
    if (s == "180") return Rotation::DEG_180;
    if (s == "270") return Rotation::DEG_270;
    return Rotation::DEG_0;  // 含 "0" 与未识别值（前向兼容）
}

// ============================================================================
// Component
// ============================================================================

Component::Component() : uuid_(generate_uuid()) {}

// 显式逐成员移动：基类 Resource（含 ref_count_/path_）默认构造，保持目标自身计数与路径
// 不被动过；仅把业务字段从源搬过来。源对象业务字段进入有效但未指定的 moved-from 状态。
Component::Component(Component&& o) noexcept
    : uuid_(std::move(o.uuid_)),
      refdes_(std::move(o.refdes_)),
      symbol_(std::move(o.symbol_)),
      device_(std::move(o.device_)),
      symbol_ref_(std::move(o.symbol_ref_)),
      device_ref_(std::move(o.device_ref_)),
      position_(o.position_),
      rotation_(o.rotation_),
      attributes_(std::move(o.attributes_)) {}

Component& Component::operator=(Component&& o) noexcept {
    if (this != &o) {
        uuid_ = std::move(o.uuid_);
        refdes_ = std::move(o.refdes_);
        symbol_ = std::move(o.symbol_);
        device_ = std::move(o.device_);
        symbol_ref_ = std::move(o.symbol_ref_);
        device_ref_ = std::move(o.device_ref_);
        position_ = o.position_;
        rotation_ = o.rotation_;
        attributes_ = std::move(o.attributes_);
        // 基类 Resource（ref_count_/path_/metadata_ 等）保持目标对象自身状态，
        // 不从源搬入——重定位一个 RefCounted 值不得窃取他人的引用计数/资源路径。
    }
    return *this;
}

void Component::reset() {
    // 保留 uuid_（稳定主键）；清空其余业务成员。deserialize() 开头调用。
    refdes_.clear();
    symbol_.reset();
    device_.reset();
    symbol_ref_.clear();
    device_ref_.clear();
    position_ = geometry::Point{};
    rotation_ = Rotation::DEG_0;
    attributes_.clear();
}

void Component::set_symbol(Ref<Symbol> sym) {
    symbol_ = sym;
    if (sym.valid() && !sym->get_path().empty()) {
        symbol_ref_ = sym->get_path();
    }
}

void Component::set_device(Ref<Device> dev) {
    device_ = dev;
    if (dev.valid() && !dev->get_path().empty()) {
        device_ref_ = dev->get_path();
    }
}

bool Component::set_rotation_degrees(int degrees) {
    if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) {
        return false;
    }
    rotation_ = rotation_from_degrees(degrees);
    return true;
}

void Component::set_attribute(std::string name, std::string value) {
    attributes_[std::move(name)] = std::move(value);
}

bool Component::remove_attribute(const std::string& name) {
    return attributes_.erase(name) > 0;
}

const std::string* Component::find_attribute(const std::string& name) const {
    auto it = attributes_.find(name);
    return it == attributes_.end() ? nullptr : &it->second;
}

geometry::Point Component::transform_local_to_world(geometry::Point local,
                                                    geometry::Point origin,
                                                    Rotation rotation) {
    geometry::Point r{};
    switch (rotation) {
        case Rotation::DEG_0:
            r = local;
            break;
        case Rotation::DEG_90:  // CCW 90: (x, y) -> (-y, x)
            r.x = -local.y;
            r.y = local.x;
            break;
        case Rotation::DEG_180:
            r.x = -local.x;
            r.y = -local.y;
            break;
        case Rotation::DEG_270:  // CCW 270 (= CW 90): (x, y) -> (y, -x)
            r.x = local.y;
            r.y = -local.x;
            break;
    }
    return geometry::Point{origin.x + r.x, origin.y + r.y};
}

std::vector<ConnectionPoint> Component::connection_points() const {
    std::vector<ConnectionPoint> out;
    if (!symbol_.valid()) return out;
    const auto& pins = symbol_->pins();
    out.reserve(pins.size());
    for (const auto& pin : pins) {
        ConnectionPoint cp;
        cp.pin_number = pin.number();
        cp.position = transform_local_to_world(pin.position(), position_, rotation_);
        out.push_back(std::move(cp));
    }
    return out;
}

std::optional<geometry::Point> Component::pin_position(const std::string& pin_number) const {
    if (!symbol_.valid()) return std::nullopt;
    for (const auto& pin : symbol_->pins()) {
        if (pin.number() == pin_number) {
            return transform_local_to_world(pin.position(), position_, rotation_);
        }
    }
    return std::nullopt;
}

// ============================================================================
// 最小 TOML 子集 —— 序列化（确定性输出，字段顺序固定）
// ============================================================================
//
// 输出 schema：
//   schema_version = 1
//   uuid = "..."
//   refdes = "R1"
//   symbol_ref = "res://lib/r.sym.toml"
//   device_ref = "res://lib/r.dev.toml"
//   position = [x, y]              # nm
//   rotation = 0                   # 0/90/180/270
//
//   [attributes]                   # 仅当非空
//   "resistance" = "10kΩ"
//   "value" = "LM358"

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

}  // namespace

std::string Component::serialize() const {
    std::string out;
    out.reserve(256);

    out += "schema_version = 1\n";
    out += "uuid = "; append_toml_string(out, uuid_); out += '\n';
    out += "refdes = "; append_toml_string(out, refdes_); out += '\n';
    out += "symbol_ref = "; append_toml_string(out, symbol_ref_); out += '\n';
    out += "device_ref = "; append_toml_string(out, device_ref_); out += '\n';
    out += "position = "; append_toml_point(out, position_); out += '\n';
    out += "rotation = "; append_toml_string(out, std::string(to_string_view(rotation_)));
    out += '\n';

    if (!attributes_.empty()) {
        std::vector<std::pair<std::string, std::string>> entries(
            attributes_.begin(), attributes_.end());
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        out += "\n[attributes]\n";
        for (const auto& [k, v] : entries) {
            append_toml_string(out, k);
            out += " = ";
            append_toml_string(out, v);
            out += '\n';
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

}  // namespace

bool Component::deserialize(const std::string& text) {
      // 重置（保留 uuid_；若文本含 uuid 则下方覆盖）
    reset();

    enum class Ctx { ROOT, ATTRIBUTES };
    Ctx ctx = Ctx::ROOT;

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = strip_comment(line);
        std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;

        // 表头 [...]
        if (trimmed[0] == '[') {
            size_t end = trimmed.find(']');
            if (end == std::string::npos) { ctx = Ctx::ROOT; continue; }
            std::string head = trim_ws(trimmed.substr(1, end - 1));
            ctx = (head == "attributes") ? Ctx::ATTRIBUTES : Ctx::ROOT;
            continue;
        }

        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_ws(trimmed.substr(0, eq));
        std::string raw_val = trim_ws(trimmed.substr(eq + 1));
        if (key.empty() || raw_val.empty()) continue;

        if (ctx == Ctx::ATTRIBUTES) {
            std::string k, v;
            if (parse_toml_string(key, k) && parse_toml_string(raw_val, v)) {
                attributes_[std::move(k)] = std::move(v);
            }
            continue;
        }

        // ROOT
        if (key == "uuid") {
            std::string v;
            if (parse_toml_string(raw_val, v)) uuid_ = std::move(v);
        } else if (key == "refdes") {
            std::string v;
            if (parse_toml_string(raw_val, v)) refdes_ = std::move(v);
        } else if (key == "symbol_ref") {
            std::string v;
            if (parse_toml_string(raw_val, v)) symbol_ref_ = std::move(v);
        } else if (key == "device_ref") {
            std::string v;
            if (parse_toml_string(raw_val, v)) device_ref_ = std::move(v);
        } else if (key == "position") {
            geometry::Point p;
            if (parse_toml_point(raw_val, p)) position_ = p;
        } else if (key == "rotation") {
            std::string v;
            if (parse_toml_string(raw_val, v)) rotation_ = parse_rotation(v);
        }
    }

    return true;
}

Ref<Component> Component::parse(const std::string& text) {
    Ref<Component> c(new Component());
    if (!c->deserialize(text)) return Ref<Component>();
    return c;
}

}  // namespace eda::sch
