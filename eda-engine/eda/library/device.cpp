// eda/library/device.cpp
//
// P6 元件库 · 器件一体化绑定实现 + .dev.toml 序列化。
//
// 序列化风格与 symbol.cpp / footprint.cpp 一致：手写最小 TOML 子集，确定性输出。
// Property.value 通过 Variant::serialize()/deserialize() 编码到 JSON 字符串后以 TOML
// 字符串形式承载——首版不引入 toml 嵌套表，保持解析器简单。

#include "eda/library/device.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <utility>

#include "eda/project/project.h"  // generate_uuid 复用

namespace eda {

// ============================================================================
// Property
// ============================================================================

Property::Property(std::string name, Variant value, std::string type_name,
                   std::string unit, bool visible, bool editable)
    : name_(std::move(name)),
      value_(std::move(value)),
      type_name_(std::move(type_name)),
      unit_(std::move(unit)),
      visible_(visible),
      editable_(editable) {}

// ============================================================================
// Device
// ============================================================================

Device::Device() : uuid_(generate_uuid()) {}

void Device::map_pin(std::string symbol_pin_number, std::string footprint_pad_number) {
    pin_map_[std::move(symbol_pin_number)] = std::move(footprint_pad_number);
}

bool Device::unmap_pin(const std::string& symbol_pin_number) {
    return pin_map_.erase(symbol_pin_number) > 0;
}

const std::string* Device::mapped_pad(const std::string& symbol_pin_number) const {
    auto it = pin_map_.find(symbol_pin_number);
    return it == pin_map_.end() ? nullptr : &it->second;
}

Property& Device::add_property(Property p) {
    properties_.push_back(std::move(p));
    return properties_.back();
}

const Property* Device::find_property(const std::string& name) const {
    auto it = std::find_if(properties_.begin(), properties_.end(),
                           [&](const Property& p) { return p.name() == name; });
    return it == properties_.end() ? nullptr : &(*it);
}

Property* Device::find_property(const std::string& name) {
    auto it = std::find_if(properties_.begin(), properties_.end(),
                           [&](const Property& p) { return p.name() == name; });
    return it == properties_.end() ? nullptr : &(*it);
}

bool Device::remove_property(const std::string& name) {
    auto it = std::find_if(properties_.begin(), properties_.end(),
                           [&](const Property& p) { return p.name() == name; });
    if (it == properties_.end()) return false;
    properties_.erase(it);
    return true;
}

void Device::add_equivalent(std::string ref) {
    if (std::find(equivalents_.begin(), equivalents_.end(), ref) == equivalents_.end()) {
        equivalents_.push_back(std::move(ref));
    }
}

bool Device::remove_equivalent(const std::string& ref) {
    auto it = std::find(equivalents_.begin(), equivalents_.end(), ref);
    if (it == equivalents_.end()) return false;
    equivalents_.erase(it);
    return true;
}

// ============================================================================
// 最小 TOML 子集 —— 序列化辅助
// ============================================================================
//
// 输出 schema：
//   schema_version = 1
//   uuid = "..."
//   name = "..."
//   description = "..."
//   category = "..."
//   keywords = "..."
//   datasheet_url = "..."
//
//   [binding]
//   symbol_ref = "res://..."
//   footprint_ref = "res://..."
//   model_3d_ref = "res://..."
//
//   [pin_map]                       # 仅当非空
//   "1" = "1"
//
//   [[properties]]                  # 按 name 字典序
//   name = "resistance"
//   type_name = "resistance"
//   unit = "Ω"
//   value = <Variant JSON 字符串>
//   visible = false
//   editable = true
//
//   [spice]                         # 仅当有 model_ref
//   model_ref = "..."
//   model_type = "SUBCKT"
//   subckt_name = "..."
//   enabled = true
//
//   [supply_chain]                  # 仅当有 mpn
//   mpn = "..."
//   manufacturer = "..."
//   supplier = "LCSC"
//   supplier_url = "..."
//   supplier_part_number = "..."
//   unit_price = 0.0
//   stock = 0
//   lifecycle = "active"
//   rohs = true
//
//   equivalents = ["...", "..."]    # 仅当非空

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

void append_toml_string_array(std::string& out, const std::vector<std::string>& items) {
    out += '[';
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += ", ";
        append_toml_string(out, items[i]);
    }
    out += ']';
}

}  // namespace

std::string Device::serialize() const {
    std::string out;
    out.reserve(640);

    out += "schema_version = 1\n";
    out += "uuid = "; append_toml_string(out, uuid_); out += '\n';
    out += "name = "; append_toml_string(out, name_); out += '\n';
    out += "description = "; append_toml_string(out, description_); out += '\n';
    out += "category = "; append_toml_string(out, category_); out += '\n';
    out += "keywords = "; append_toml_string(out, keywords_); out += '\n';
    out += "datasheet_url = "; append_toml_string(out, datasheet_url_); out += '\n';

    // [binding]：始终输出（即使引用为空——schema 完整性，缺失视为未绑定）
    out += "\n[binding]\n";
    out += "symbol_ref = "; append_toml_string(out, symbol_ref_); out += '\n';
    out += "footprint_ref = "; append_toml_string(out, footprint_ref_); out += '\n';
    out += "model_3d_ref = "; append_toml_string(out, model_3d_ref_); out += '\n';

    // [pin_map]：非空才输出，键字典序
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

    // [[properties]]：按 name 字典序
    if (!properties_.empty()) {
        std::vector<const Property*> sorted;
        sorted.reserve(properties_.size());
        for (const Property& p : properties_) sorted.push_back(&p);
        std::sort(sorted.begin(), sorted.end(),
                  [](const Property* a, const Property* b) { return a->name() < b->name(); });
        for (const Property* p : sorted) {
            out += "\n[[properties]]\n";
            out += "name = "; append_toml_string(out, p->name()); out += '\n';
            out += "type_name = "; append_toml_string(out, p->type_name()); out += '\n';
            out += "unit = "; append_toml_string(out, p->unit()); out += '\n';
            // Variant 编码为 JSON 字符串（"{\"x\":..}" / "1.5" / "\"abc\""）
            out += "value = ";
            append_toml_string(out, p->value().serialize());
            out += '\n';
            out += "visible = "; out += (p->visible() ? "true" : "false"); out += '\n';
            out += "editable = "; out += (p->editable() ? "true" : "false"); out += '\n';
        }
    }

    // [spice]：仅当有 model_ref
    if (has_spice()) {
        out += "\n[spice]\n";
        out += "model_ref = "; append_toml_string(out, spice_.model_ref()); out += '\n';
        out += "model_type = "; append_toml_string(out, spice_.model_type()); out += '\n';
        out += "subckt_name = "; append_toml_string(out, spice_.subckt_name()); out += '\n';
        out += "enabled = "; out += (spice_.enabled() ? "true" : "false"); out += '\n';
    }

    // [supply_chain]：仅当有 mpn
    if (has_supply_chain()) {
        out += "\n[supply_chain]\n";
        out += "mpn = "; append_toml_string(out, supply_.mpn()); out += '\n';
        out += "manufacturer = "; append_toml_string(out, supply_.manufacturer()); out += '\n';
        out += "supplier = "; append_toml_string(out, supply_.supplier()); out += '\n';
        out += "supplier_url = "; append_toml_string(out, supply_.supplier_url()); out += '\n';
        out += "supplier_part_number = ";
        append_toml_string(out, supply_.supplier_part_number()); out += '\n';
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf),
                          "unit_price = %.17g\nstock = %d\n",
                          supply_.unit_price(), supply_.stock());
            out += buf;
        }
        out += "lifecycle = "; append_toml_string(out, supply_.lifecycle()); out += '\n';
        out += "rohs = "; out += (supply_.rohs() ? "true" : "false"); out += '\n';
    }

    // equivalents：非空才输出
    if (!equivalents_.empty()) {
        std::vector<std::string> sorted_eq = equivalents_;
        std::sort(sorted_eq.begin(), sorted_eq.end());
        out += "\nequivalents = ";
        append_toml_string_array(out, sorted_eq);
        out += '\n';
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

bool parse_toml_string_array(std::string_view raw, std::vector<std::string>& out) {
    if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') return false;
    std::string_view body = raw.substr(1, raw.size() - 2);
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == ',' ||
               body[i] == '\n' || body[i] == '\r')) {
            ++i;
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
        ++i;
        std::string elem;
        if (!parse_toml_string(body.substr(start, i - start), elem)) return false;
        out.push_back(std::move(elem));
    }
    return true;
}

}  // namespace

bool Device::deserialize(const std::string& text) {
    

    enum class Ctx { ROOT, PROPERTY };
    Ctx ctx = Ctx::ROOT;
    std::string current_section;  // binding / pin_map / spice / supply_chain
    Property cur_prop;

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = strip_comment(line);
        std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;

        // 数组表头 [[...]]：只识别 [[properties]]
        if (trimmed.size() >= 2 && trimmed[0] == '[' && trimmed[1] == '[') {
            if (ctx == Ctx::PROPERTY) properties_.push_back(std::move(cur_prop));
            size_t end = trimmed.find("]]");
            if (end == std::string::npos) { ctx = Ctx::ROOT; current_section.clear(); continue; }
            std::string head = trim_ws(trimmed.substr(2, end - 2));
            current_section.clear();
            if (head == "properties") {
                ctx = Ctx::PROPERTY;
                cur_prop = Property();
            } else {
                ctx = Ctx::ROOT;
            }
            continue;
        }

        // 单表头 [...]
        if (!trimmed.empty() && trimmed[0] == '[') {
            if (ctx == Ctx::PROPERTY) properties_.push_back(std::move(cur_prop));
            size_t end = trimmed.find(']');
            if (end == std::string::npos) { ctx = Ctx::ROOT; current_section.clear(); continue; }
            current_section = trim_ws(trimmed.substr(1, end - 1));
            ctx = Ctx::ROOT;
            cur_prop = Property();
            continue;
        }

        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_ws(trimmed.substr(0, eq));
        std::string raw_val = trim_ws(trimmed.substr(eq + 1));
        if (key.empty() || raw_val.empty()) continue;

        // ROOT 上下文：先按 current_section 派发
        if (ctx == Ctx::ROOT && !current_section.empty()) {
            if (current_section == "binding") {
                if (key == "symbol_ref") {
                    std::string v; if (parse_toml_string(raw_val, v)) symbol_ref_ = std::move(v);
                } else if (key == "footprint_ref") {
                    std::string v; if (parse_toml_string(raw_val, v)) footprint_ref_ = std::move(v);
                } else if (key == "model_3d_ref") {
                    std::string v; if (parse_toml_string(raw_val, v)) model_3d_ref_ = std::move(v);
                }
                continue;
            } else if (current_section == "pin_map") {
                std::string k, v;
                if (parse_toml_string(key, k) && parse_toml_string(raw_val, v)) {
                    pin_map_[std::move(k)] = std::move(v);
                }
                continue;
            } else if (current_section == "spice") {
                if (key == "model_ref") {
                    std::string v; if (parse_toml_string(raw_val, v)) spice_.set_model_ref(std::move(v));
                } else if (key == "model_type") {
                    std::string v; if (parse_toml_string(raw_val, v)) spice_.set_model_type(std::move(v));
                } else if (key == "subckt_name") {
                    std::string v; if (parse_toml_string(raw_val, v)) spice_.set_subckt_name(std::move(v));
                } else if (key == "enabled") {
                    bool v; if (parse_toml_bool(raw_val, v)) spice_.set_enabled(v);
                }
                continue;
            } else if (current_section == "supply_chain") {
                if (key == "mpn") {
                    std::string v; if (parse_toml_string(raw_val, v)) supply_.set_mpn(std::move(v));
                } else if (key == "manufacturer") {
                    std::string v; if (parse_toml_string(raw_val, v)) supply_.set_manufacturer(std::move(v));
                } else if (key == "supplier") {
                    std::string v; if (parse_toml_string(raw_val, v)) supply_.set_supplier(std::move(v));
                } else if (key == "supplier_url") {
                    std::string v; if (parse_toml_string(raw_val, v)) supply_.set_supplier_url(std::move(v));
                } else if (key == "supplier_part_number") {
                    std::string v; if (parse_toml_string(raw_val, v)) supply_.set_supplier_part_number(std::move(v));
                } else if (key == "unit_price") {
                    double v; if (parse_toml_double(raw_val, v)) supply_.set_unit_price(v);
                } else if (key == "stock") {
                    int64_t v; if (parse_toml_int(raw_val, v)) supply_.set_stock(static_cast<int>(v));
                } else if (key == "lifecycle") {
                    std::string v; if (parse_toml_string(raw_val, v)) supply_.set_lifecycle(std::move(v));
                } else if (key == "rohs") {
                    bool v; if (parse_toml_bool(raw_val, v)) supply_.set_rohs(v);
                }
                continue;
            }
        }

        // ROOT 顶层字段
        if (ctx == Ctx::ROOT && current_section.empty()) {
            if (key == "uuid") {
                std::string v; if (parse_toml_string(raw_val, v)) uuid_ = std::move(v);
            } else if (key == "name") {
                std::string v; if (parse_toml_string(raw_val, v)) name_ = std::move(v);
            } else if (key == "description") {
                std::string v; if (parse_toml_string(raw_val, v)) description_ = std::move(v);
            } else if (key == "category") {
                std::string v; if (parse_toml_string(raw_val, v)) category_ = std::move(v);
            } else if (key == "keywords") {
                std::string v; if (parse_toml_string(raw_val, v)) keywords_ = std::move(v);
            } else if (key == "datasheet_url") {
                std::string v; if (parse_toml_string(raw_val, v)) datasheet_url_ = std::move(v);
            } else if (key == "equivalents") {
                parse_toml_string_array(raw_val, equivalents_);
            }
            continue;
        }

        // PROPERTY 上下文
        if (ctx == Ctx::PROPERTY) {
            if (key == "name") {
                std::string v; if (parse_toml_string(raw_val, v)) cur_prop.set_name(std::move(v));
            } else if (key == "type_name") {
                std::string v; if (parse_toml_string(raw_val, v)) cur_prop.set_type_name(std::move(v));
            } else if (key == "unit") {
                std::string v; if (parse_toml_string(raw_val, v)) cur_prop.set_unit(std::move(v));
            } else if (key == "value") {
                // value 是被 TOML 字符串包裹的 Variant JSON
                std::string json_str;
                if (parse_toml_string(raw_val, json_str)) {
                    cur_prop.set_value(Variant::deserialize(json_str));
                }
            } else if (key == "visible") {
                bool v; if (parse_toml_bool(raw_val, v)) cur_prop.set_visible(v);
            } else if (key == "editable") {
                bool v; if (parse_toml_bool(raw_val, v)) cur_prop.set_editable(v);
            }
        }
    }

    if (ctx == Ctx::PROPERTY) properties_.push_back(std::move(cur_prop));

    return true;
}

Ref<Device> Device::parse(const std::string& text) {
    Ref<Device> d(new Device());
    if (!d->deserialize(text)) return Ref<Device>();
    return d;
}

}  // namespace eda
