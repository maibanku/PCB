// eda/schematic/schematic.cpp
//
// P7 原理图核心数据模型 · Schematic 实现 + 网表生成（Union-Find）+ .sch.toml 序列化。
//
// 网表生成算法（docs/07-04 关键点）：
//   1. 把所有电气节点（Component 引脚连接点 / Wire 两端 / NetLabel 位置 / PowerPort 位置）
//      插入 Union-Find（按 geometry::Point 去重——同坐标自动同根）。
//   2. 对每条 Wire，union 其两端点。
//   3. 按 root 收集 Pin 引用 → 同 root 的引脚组成一个 Net。
//   4. 网络命名优先级：PowerPort > NetLabel > 自动命名（N0001）。
//   5. Net 按 name 字典序输出；pins 按 (refdes, pin_number) 字典序输出（确定性）。
//
// 序列化风格与 symbol.cpp / device.cpp 一致：手写最小 TOML 子集，确定性输出。

#include "eda/schematic/schematic.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "eda/project/project.h"  // generate_uuid 复用

namespace eda::sch {

// ============================================================================
// 枚举字符串映射（LabelDirection / PowerPortKind）
// ============================================================================

std::string_view to_string_view(LabelDirection d) {
    switch (d) {
        case LabelDirection::HORIZONTAL: return "horizontal";
        case LabelDirection::VERTICAL:   return "vertical";
    }
    return "horizontal";
}

LabelDirection parse_label_direction(std::string_view s) {
    if (s == "vertical") return LabelDirection::VERTICAL;
    return LabelDirection::HORIZONTAL;  // 含 "horizontal" 与未识别值
}

std::string_view to_string_view(PowerPortKind k) {
    switch (k) {
        case PowerPortKind::POWER:  return "power";
        case PowerPortKind::GROUND: return "ground";
        case PowerPortKind::OTHER:  return "other";
    }
    return "power";
}

PowerPortKind parse_power_port_kind(std::string_view s) {
    if (s == "ground") return PowerPortKind::GROUND;
    if (s == "other")  return PowerPortKind::OTHER;
    return PowerPortKind::POWER;  // 含 "power" 与未识别值
}

// ============================================================================
// Schematic
// ============================================================================

Schematic::Schematic() : uuid_(generate_uuid()) {}

Component& Schematic::add_component(Component c) {
    components_.push_back(std::move(c));
    return components_.back();
}

bool Schematic::remove_component_by_uuid(const std::string& uuid) {
    auto it = std::find_if(components_.begin(), components_.end(),
                           [&](const Component& c) { return c.uuid() == uuid; });
    if (it == components_.end()) return false;
    components_.erase(it);
    return true;
}

Component* Schematic::find_component_by_uuid(const std::string& uuid) {
    auto it = std::find_if(components_.begin(), components_.end(),
                           [&](const Component& c) { return c.uuid() == uuid; });
    return it == components_.end() ? nullptr : &(*it);
}

const Component* Schematic::find_component_by_uuid(const std::string& uuid) const {
    auto it = std::find_if(components_.begin(), components_.end(),
                           [&](const Component& c) { return c.uuid() == uuid; });
    return it == components_.end() ? nullptr : &(*it);
}

Component* Schematic::find_component_by_refdes(const std::string& refdes) {
    auto it = std::find_if(components_.begin(), components_.end(),
                           [&](const Component& c) { return c.refdes() == refdes; });
    return it == components_.end() ? nullptr : &(*it);
}

const Component* Schematic::find_component_by_refdes(const std::string& refdes) const {
    auto it = std::find_if(components_.begin(), components_.end(),
                           [&](const Component& c) { return c.refdes() == refdes; });
    return it == components_.end() ? nullptr : &(*it);
}

Wire& Schematic::add_wire(Wire w) {
    wires_.push_back(std::move(w));
    return wires_.back();
}

NetLabel& Schematic::add_net_label(NetLabel l) {
    net_labels_.push_back(std::move(l));
    return net_labels_.back();
}

PowerPort& Schematic::add_power_port(PowerPort p) {
    power_ports_.push_back(std::move(p));
    return power_ports_.back();
}

bool Schematic::remove_wire(std::size_t index) {
    if (index >= wires_.size()) return false;
    wires_.erase(wires_.begin() + index);
    return true;
}

bool Schematic::remove_net_label(std::size_t index) {
    if (index >= net_labels_.size()) return false;
    net_labels_.erase(net_labels_.begin() + index);
    return true;
}

bool Schematic::remove_power_port(std::size_t index) {
    if (index >= power_ports_.size()) return false;
    power_ports_.erase(power_ports_.begin() + index);
    return true;
}

bool Schematic::remove_by_uuid(const std::string& uuid) {
    return remove_component_by_uuid(uuid);
}

Component* Schematic::find_by_uuid(const std::string& uuid) {
    return find_component_by_uuid(uuid);
}

const Component* Schematic::find_by_uuid(const std::string& uuid) const {
    return find_component_by_uuid(uuid);
}

void Schematic::add_note(std::string note) {
    notes_.push_back(std::move(note));
}

bool Schematic::remove_note(std::size_t index) {
    if (index >= notes_.size()) return false;
    notes_.erase(notes_.begin() + index);
    return true;
}

// ============================================================================
// 网表生成 —— Union-Find 等价类构建
// ============================================================================

namespace {

// Point 哈希（与 geometry::operator== 配套；不污染 geometry 命名空间）
struct PointHasher {
    std::size_t operator()(const geometry::Point& p) const {
        const std::size_t h1 = std::hash<geometry::Coord>{}(p.x);
        const std::size_t h2 = std::hash<geometry::Coord>{}(p.y);
        // boost::hash_combine 风格
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

// Union-Find（按点去重；路径压缩 + 按秩合并）
class UnionFind {
public:
    // 插入点（若已存在返回既有 id）；否则分配新 id。
    std::size_t insert(geometry::Point p) {
        auto it = point_to_id_.find(p);
        if (it != point_to_id_.end()) return it->second;
        std::size_t id = parents_.size();
        parents_.push_back(id);
        ranks_.push_back(0);
        point_to_id_[p] = id;
        return id;
    }

    // 查找根（路径压缩）。点不存在时自动插入。
    std::size_t find(geometry::Point p) {
        return find_by_id(insert(p));
    }

    // 合并两点所在集合。
    void unite(geometry::Point a, geometry::Point b) {
        std::size_t ra = find_by_id(insert(a));
        std::size_t rb = find_by_id(insert(b));
        if (ra == rb) return;
        if (ranks_[ra] < ranks_[rb]) std::swap(ra, rb);
        parents_[rb] = ra;
        if (ranks_[ra] == ranks_[rb]) ++ranks_[ra];
    }

    bool has(geometry::Point p) const { return point_to_id_.find(p) != point_to_id_.end(); }

private:
    std::size_t find_by_id(std::size_t x) {
        while (parents_[x] != x) {
            parents_[x] = parents_[parents_[x]];  // 路径压缩
            x = parents_[x];
        }
        return x;
    }

    std::vector<std::size_t> parents_;
    std::vector<std::size_t> ranks_;
    std::unordered_map<geometry::Point, std::size_t, PointHasher> point_to_id_;
};

// refdes 优先级排序：先按 refdes 字典序，再按 pin_number 字典序（自然排）。
// 注意：refdes 排序的"自然"语义（R1 < R2 < R10）需要解析数字段；首版采用字符串字典序
//       + 数字感知的简易比较，以避免 R10 < R2 这种尴尬（测试只验证 R1/R2 等单位数）。
int compare_refdes_natural(const std::string& a, const std::string& b) {
    // 简单实现：先比前缀字母段（字典序），再比数字段（数值），其余回落字典序。
    auto is_alpha = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; };
    auto is_digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };

    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (is_alpha(a[i]) && is_alpha(b[j])) {
            if (a[i] != b[j]) return a[i] < b[j] ? -1 : 1;
            ++i; ++j;
        } else if (is_digit(a[i]) && is_digit(b[j])) {
            // 提取数字段
            size_t ai = i, bj = j;
            while (ai < a.size() && is_digit(a[ai])) ++ai;
            while (bj < b.size() && is_digit(b[bj])) ++bj;
            int64_t na = 0, nb = 0;
            try {
                na = std::stoll(a.substr(i, ai - i));
                nb = std::stoll(b.substr(j, bj - j));
            } catch (...) {
                return a.substr(i) < b.substr(j) ? -1 : 1;
            }
            if (na != nb) return na < nb ? -1 : 1;
            i = ai; j = bj;
        } else {
            // 类型不匹配（一个字母一个数字），回落字符比较
            if (a[i] != b[j]) return a[i] < b[j] ? -1 : 1;
            ++i; ++j;
        }
    }
    if (i == a.size() && j == b.size()) return 0;
    return i == a.size() ? -1 : 1;  // a 是 b 的前缀 → a 较短 → a 较小
}

bool pin_ref_less(const PinRef& a, const PinRef& b) {
    int c = compare_refdes_natural(a.refdes, b.refdes);
    if (c != 0) return c < 0;
    return compare_refdes_natural(a.pin_number, b.pin_number) < 0;
}

}  // namespace

Netlist Schematic::generate_netlist() const {
    UnionFind uf;

    // ---- 1. 插入所有 Pin 连接点 + 收集 (point → pin refs) ----
    std::unordered_map<geometry::Point, std::vector<PinRef>, PointHasher> point_pins;
    for (const Component& comp : components_) {
        if (!comp.has_symbol()) continue;  // 无 Symbol 跳过
        // 若 refdes 为空，使用 "?-uuid前缀" 占位，确保不丢失引脚
        std::string refdes = comp.refdes();
        if (refdes.empty()) {
            refdes = "?";
        }
        for (const ConnectionPoint& cp : comp.connection_points()) {
            uf.insert(cp.position);
            point_pins[cp.position].push_back(PinRef{refdes, cp.pin_number});
        }
    }

    // ---- 2. Wire：插入两端 + union ----
    // 同时记录每条 wire 上声明的 net_ref（命名提示）。
    std::unordered_map<geometry::Point, std::string, PointHasher> point_net_hint;
    for (const Wire& w : wires_) {
        uf.unite(w.a(), w.b());
        if (w.has_net_ref()) {
            // 两端都登记命名提示（首个非空命中生效）
            if (point_net_hint.find(w.a()) == point_net_hint.end()) {
                point_net_hint[w.a()] = w.net_ref();
            }
            if (point_net_hint.find(w.b()) == point_net_hint.end()) {
                point_net_hint[w.b()] = w.net_ref();
            }
        }
    }

    // ---- 3. NetLabel：插入位置 + 收集 (point → name) ----
    std::unordered_map<geometry::Point, std::string, PointHasher> point_label;
    for (const NetLabel& lbl : net_labels_) {
        uf.insert(lbl.position());
        // 同点多个 NetLabel：首版取首个（生产可改冲突报告 ERC）
        if (point_label.find(lbl.position()) == point_label.end()) {
            point_label[lbl.position()] = lbl.net_name();
        }
    }

    // ---- 4. PowerPort：插入位置 + 收集 (point → name, kind) ----
    std::unordered_map<geometry::Point, std::pair<std::string, PowerPortKind>, PointHasher> point_power;
    for (const PowerPort& pp : power_ports_) {
        uf.insert(pp.position());
        if (point_power.find(pp.position()) == point_power.end()) {
            point_power[pp.position()] = {pp.net_name(), pp.kind()};
        }
    }

    // ---- 5. 按 root 收集 Pin 引用 ----
    // root → pins 列表
    std::unordered_map<std::size_t, std::vector<PinRef>> root_pins;
    for (const auto& [pt, pins] : point_pins) {
        std::size_t root = uf.find(pt);
        auto& vec = root_pins[root];
        for (const auto& pr : pins) vec.push_back(pr);
    }

    // ---- 6. 为每个 root 决定网络名（PowerPort > NetLabel > Wire.net_ref > 自动）----
    std::unordered_map<std::size_t, std::string> root_name;
    std::unordered_map<std::size_t, bool> root_is_power;

    // Wire 的 net_ref 提示（最低显式优先级）
    for (const auto& [pt, hint] : point_net_hint) {
        if (!uf.has(pt)) continue;
        std::size_t root = uf.find(pt);
        if (root_name.find(root) == root_name.end()) {
            root_name[root] = hint;
        }
    }
    // NetLabel（中等优先级）
    for (const auto& [pt, name] : point_label) {
        std::size_t root = uf.find(pt);
        root_name[root] = name;
    }
    // PowerPort（最高优先级）
    for (const auto& [pt, pair] : point_power) {
        std::size_t root = uf.find(pt);
        root_name[root] = pair.first;
        root_is_power[root] = true;
    }

    // ---- 7. 构建 Net 列表 + 自动命名未命名 root ----
    Netlist nl;
    std::vector<std::pair<std::size_t, std::vector<PinRef>>> unnamed;
    unnamed.reserve(root_pins.size());

    for (auto& [root, pins] : root_pins) {
        std::sort(pins.begin(), pins.end(), pin_ref_less);

        Net net;
        net.pins = std::move(pins);
        auto it = root_name.find(root);
        if (it != root_name.end()) {
            net.name = it->second;
            net.is_power = root_is_power[root];
            nl.nets_.push_back(std::move(net));
        } else {
            // 暂存，待显式命名 net 收集完后统一编号（避免与已命名冲突）
            unnamed.emplace_back(root, std::move(net.pins));
        }
    }

    // 自动命名（N0001, N0002, ...）—— 按 pins 首项 (refdes, pin_number) 排序保证确定性
    std::sort(unnamed.begin(), unnamed.end(),
              [](const auto& a, const auto& b) {
                  if (a.second.empty() || b.second.empty()) {
                      // 不应为空（root_pins 只在有点 pins 时才产生条目）；防御性处理
                      return a.first < b.first;
                  }
                  return pin_ref_less(a.second.front(), b.second.front());
              });

    // 收集已使用名字（避免自动名冲突）
    std::unordered_map<std::string, std::size_t> used_names;
    for (const Net& n : nl.nets_) used_names[n.name] = 1;

    int auto_id = 1;
    for (auto& [root, pins] : unnamed) {
        Net net;
        net.pins = std::move(pins);
        net.is_power = false;
        // 找一个未占用的 N000x
        std::string candidate;
        do {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "N%04d", auto_id++);
            candidate = buf;
        } while (used_names.find(candidate) != used_names.end());
        net.name = candidate;
        used_names[net.name] = 1;
        nl.nets_.push_back(std::move(net));
    }

    // ---- 8. Net 按 name 字典序输出（确定性）----
    std::sort(nl.nets_.begin(), nl.nets_.end(),
              [](const Net& a, const Net& b) { return a.name < b.name; });

    return nl;
}

// ============================================================================
// Netlist
// ============================================================================

const Net* Netlist::find_net(const std::string& name) const {
    auto it = std::find_if(nets_.begin(), nets_.end(),
                           [&](const Net& n) { return n.name == name; });
    return it == nets_.end() ? nullptr : &(*it);
}

std::size_t Netlist::pin_count() const {
    std::size_t total = 0;
    for (const Net& n : nets_) total += n.pins.size();
    return total;
}

// ============================================================================
// 最小 TOML 子集 —— 序列化辅助（与 symbol.cpp / device.cpp 一致风格）
// ============================================================================
//
// 输出 schema（.sch.toml）：
//   schema_version = 1
//   uuid = "..."
//   name = "Main Sheet"
//
//   [[components]]
//   uuid = "..."
//   refdes = "R1"
//   symbol_ref = "res://lib/r.sym.toml"
//   device_ref = "res://lib/r.dev.toml"
//   position = [x, y]
//   rotation = "0"               # 字符串以与 Component.serialize 一致
//   [components.attributes]      # 嵌套表（仅当非空）
//   "value" = "10k"
//
//   [[wires]]
//   a = [x, y]
//   b = [x, y]
//   width_nm = 152400
//   net_ref = "SDA"              # 仅当非空
//
//   [[net_labels]]
//   net_name = "SDA"
//   position = [x, y]
//   direction = "horizontal"
//
//   [[power_ports]]
//   net_name = "VCC"
//   position = [x, y]
//   kind = "power"
//
//   notes = ["...", "..."]       # 仅当非空（TOML 字符串数组）

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

void serialize_component_into(std::string& out, const Component& c) {
    // 复用 Component 自身的 serialize()，但其 schema_version 头部在嵌套场景下冗余。
    // 这里手写扁平字段 + [components.attributes] 子表，保证 [[components]] 段落整洁。
    out += "[[components]]\n";
    out += "uuid = "; append_toml_string(out, c.uuid()); out += '\n';
    out += "refdes = "; append_toml_string(out, c.refdes()); out += '\n';
    out += "symbol_ref = "; append_toml_string(out, c.symbol_ref()); out += '\n';
    out += "device_ref = "; append_toml_string(out, c.device_ref()); out += '\n';
    out += "position = "; append_toml_point(out, c.position()); out += '\n';
    out += "rotation = ";
    append_toml_string(out, std::string(to_string_view(c.rotation())));
    out += '\n';

    if (!c.attributes().empty()) {
        std::vector<std::pair<std::string, std::string>> entries(
            c.attributes().begin(), c.attributes().end());
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        out += "[components.attributes]\n";
        for (const auto& [k, v] : entries) {
            append_toml_string(out, k);
            out += " = ";
            append_toml_string(out, v);
            out += '\n';
        }
    }
}

}  // namespace

std::string Schematic::serialize() const {
    std::string out;
    out.reserve(512);

    out += "schema_version = 1\n";
    out += "uuid = "; append_toml_string(out, uuid_); out += '\n';
    out += "name = "; append_toml_string(out, name_); out += '\n';

    for (const Component& c : components_) {
        out += '\n';
        serialize_component_into(out, c);
    }

    for (const Wire& w : wires_) {
        out += "\n[[wires]]\n";
        out += "a = "; append_toml_point(out, w.a()); out += '\n';
        out += "b = "; append_toml_point(out, w.b()); out += '\n';
        out += "width_nm = "; out += std::to_string(w.width_nm()); out += '\n';
        if (w.has_net_ref()) {
            out += "net_ref = "; append_toml_string(out, w.net_ref()); out += '\n';
        }
    }

    for (const NetLabel& lbl : net_labels_) {
        out += "\n[[net_labels]]\n";
        out += "net_name = "; append_toml_string(out, lbl.net_name()); out += '\n';
        out += "position = "; append_toml_point(out, lbl.position()); out += '\n';
        out += "direction = ";
        append_toml_string(out, std::string(to_string_view(lbl.direction())));
        out += '\n';
    }

    for (const PowerPort& pp : power_ports_) {
        out += "\n[[power_ports]]\n";
        out += "net_name = "; append_toml_string(out, pp.net_name()); out += '\n';
        out += "position = "; append_toml_point(out, pp.position()); out += '\n';
        out += "kind = ";
        append_toml_string(out, std::string(to_string_view(pp.kind())));
        out += '\n';
    }

    if (!notes_.empty()) {
        out += "notes = [";
        for (std::size_t i = 0; i < notes_.size(); ++i) {
            if (i > 0) out += ", ";
            append_toml_string(out, notes_[i]);
        }
        out += "]\n";
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

// 解析 ["a", "b"] 字符串数组（用于 notes 字段）。
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
        size_t end = body.find('"', i + 1);
        // 处理转义：找到真正的闭合引号
        while (end != std::string_view::npos && body[end - 1] == '\\') {
            end = body.find('"', end + 1);
        }
        if (end == std::string_view::npos) return false;
        std::string s;
        if (!parse_toml_string(body.substr(i, end - i + 1), s)) return false;
        out.push_back(std::move(s));
        i = end + 1;
    }
    return true;
}

}  // namespace

bool Schematic::deserialize(const std::string& text) {
      // 重置；保留新 UUID（若文本含 uuid 则覆盖）

    enum class Ctx { ROOT, COMPONENT, COMPONENT_ATTR, WIRES, NET_LABELS, POWER_PORTS };
    Ctx ctx = Ctx::ROOT;

    Component cur_comp;
    Wire cur_wire;
    NetLabel cur_label;
    PowerPort cur_port;

    auto flush_array_element = [&]() {
        switch (ctx) {
            case Ctx::COMPONENT:
            case Ctx::COMPONENT_ATTR:
                components_.push_back(std::move(cur_comp));
                cur_comp 
                break;
            case Ctx::WIRES:           wires_.push_back(std::move(cur_wire)); cur_wire = Wire(); break;
            case Ctx::NET_LABELS:      net_labels_.push_back(std::move(cur_label)); cur_label = NetLabel(); break;
            case Ctx::POWER_PORTS:     power_ports_.push_back(std::move(cur_port)); cur_port = PowerPort(); break;
            default: break;
        }
        if (ctx == Ctx::COMPONENT_ATTR) ctx = Ctx::COMPONENT;
    };

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = strip_comment(line);
        std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;

        // 数组表头 [[...]]
        if (trimmed.size() >= 2 && trimmed[0] == '[' && trimmed[1] == '[') {
            flush_array_element();
            // 离开 [components.attributes] 子表
            if (ctx == Ctx::COMPONENT_ATTR) ctx = Ctx::ROOT;

            size_t end = trimmed.find("]]");
            if (end == std::string::npos) { ctx = Ctx::ROOT; continue; }
            std::string head = trim_ws(trimmed.substr(2, end - 2));

            if (head == "components") {
                ctx = Ctx::COMPONENT;
                cur_comp 
            } else if (head == "wires") {
                ctx = Ctx::WIRES;
                cur_wire = Wire();
            } else if (head == "net_labels") {
                ctx = Ctx::NET_LABELS;
                cur_label = NetLabel();
            } else if (head == "power_ports") {
                ctx = Ctx::POWER_PORTS;
                cur_port = PowerPort();
            } else {
                ctx = Ctx::ROOT;
            }
            continue;
        }

        // 单表头 [...]（含 [components.attributes] 嵌套）
        if (!trimmed.empty() && trimmed[0] == '[') {
            flush_array_element();
            size_t end = trimmed.find(']');
            if (end == std::string::npos) { ctx = Ctx::ROOT; continue; }
            std::string head = trim_ws(trimmed.substr(1, end - 1));
            if (head == "components.attributes") {
                ctx = Ctx::COMPONENT_ATTR;
                // 注意：上一步 flush 已 push cur_comp；需要恢复以追加属性
                if (!components_.empty()) {
                    cur_comp = std::move(components_.back());
                    components_.pop_back();
                }
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

        switch (ctx) {
            case Ctx::ROOT: {
                if (key == "uuid") {
                    std::string v;
                    if (parse_toml_string(raw_val, v)) uuid_ = std::move(v);
                } else if (key == "name") {
                    std::string v;
                    if (parse_toml_string(raw_val, v)) name_ = std::move(v);
                } else if (key == "notes") {
                    parse_toml_string_array(raw_val, notes_);
                }
                break;
            }
            case Ctx::COMPONENT:
            case Ctx::COMPONENT_ATTR: {
                // COMPONENT_ATTR 仅处理 attributes 的 "k" = "v"
                if (ctx == Ctx::COMPONENT_ATTR) {
                    std::string k, v;
                    if (parse_toml_string(key, k) && parse_toml_string(raw_val, v)) {
                        cur_comp.set_attribute(std::move(k), std::move(v));
                    }
                    break;
                }
                if (key == "uuid") {
                    std::string v; if (parse_toml_string(raw_val, v)) {
                        cur_comp.assign_uuid_for_test(std::move(v));
                    }
                } else if (key == "refdes") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_comp.set_refdes(std::move(v));
                } else if (key == "symbol_ref") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_comp.set_symbol_ref(std::move(v));
                } else if (key == "device_ref") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_comp.set_device_ref(std::move(v));
                } else if (key == "position") {
                    geometry::Point p; if (parse_toml_point(raw_val, p)) cur_comp.set_position(p);
                } else if (key == "rotation") {
                    std::string v; if (parse_toml_string(raw_val, v))
                        cur_comp.set_rotation(parse_rotation(v));
                }
                break;
            }
            case Ctx::WIRES: {
                if (key == "a") {
                    geometry::Point p; if (parse_toml_point(raw_val, p)) cur_wire.set_a(p);
                } else if (key == "b") {
                    geometry::Point p; if (parse_toml_point(raw_val, p)) cur_wire.set_b(p);
                } else if (key == "width_nm") {
                    int64_t v; if (parse_toml_int(raw_val, v)) cur_wire.set_width_nm(v);
                } else if (key == "net_ref") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_wire.set_net_ref(std::move(v));
                }
                break;
            }
            case Ctx::NET_LABELS: {
                if (key == "net_name") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_label.set_net_name(std::move(v));
                } else if (key == "position") {
                    geometry::Point p; if (parse_toml_point(raw_val, p)) cur_label.set_position(p);
                } else if (key == "direction") {
                    std::string v; if (parse_toml_string(raw_val, v))
                        cur_label.set_direction(parse_label_direction(v));
                }
                break;
            }
            case Ctx::POWER_PORTS: {
                if (key == "net_name") {
                    std::string v; if (parse_toml_string(raw_val, v)) cur_port.set_net_name(std::move(v));
                } else if (key == "position") {
                    geometry::Point p; if (parse_toml_point(raw_val, p)) cur_port.set_position(p);
                } else if (key == "kind") {
                    std::string v; if (parse_toml_string(raw_val, v))
                        cur_port.set_kind(parse_power_port_kind(v));
                }
                break;
            }
        }
    }

    flush_array_element();

    return true;
}

Ref<Schematic> Schematic::parse(const std::string& text) {
    Ref<Schematic> s(new Schematic());
    if (!s->deserialize(text)) return Ref<Schematic>();
    return s;
}

}  // namespace eda::sch
