// eda/pcb/board.cpp
//
// P8 PCB 编辑器 · 对象树骨架实现（REQ-008 六属性契约）。
//
// 本翻译单元聚合 PCB 模块的全部非平凡方法实现（headers 内联了简单 getter/setter）：
//   * UUID v4 生成 / 校验
//   * 枚举（LayerType / PadShape / ViaType）字符串互转
//   * Stackup 查询
//   * PcbEntity 基类：write_base/read_base/properties/set_property/get_property/
//                    serialize/deserialize
//   * Track / Pad / Via / Net：bounding_box / properties / serialize / deserialize
//   * Board：构造 / 增删查 / bounding_box / recompute_net_boxes / properties /
//           serialize / deserialize（递归子对象）
//
// 序列化策略：
//   - 全部走 Variant JSON 往返（P2 Task 3 手写最小 JSON）。
//   - 坐标点统一用 2 元素 ARRAY [x, y] 编码（避免 {x,y} → VECTOR2 启发式导致 int→float 精度损失）。
//   - Board 递归子对象：先调子对象 to_variant-equivalent（即 serialize 后 Variant::deserialize）
//     嵌入为结构化 DICTIONARY；反序列化时对每个子元素调用 deserialize 重建。
//
// 边界：仅实现骨架；不接 P6 元件库（FootprintInstance 仅占位）、不接 P8-03 设计规则
//       （rules_ref 字符串引用）、不接 P3 R-tree（bounding_box 提供，索引由上层挂接）。
//
// 命名规范：namespace eda::pcb、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include "eda/pcb/board.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/object/variant.h"

namespace eda::pcb {

// ============================================================================
// UUID v4 生成 / 校验
// ============================================================================

std::string generate_uuid() {
    // 进程级 mt19937 + random_device 播种。骨架版足够 PCB 对象主键用。
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<unsigned> dist(0, 15);
    static constexpr const char* kHex = "0123456789abcdef";

    auto hex_n = [&](int n) {
        std::string s;
        s.reserve(n);
        for (int i = 0; i < n; ++i) s.push_back(kHex[dist(gen)]);
        return s;
    };

    // 8-4-4-4-12，version=4，variant=10xx
    std::string s;
    s.reserve(36);
    s += hex_n(8);
    s.push_back('-');
    s += hex_n(4);
    s.push_back('-');
    s.push_back('4');  // version 4
    s += hex_n(3);
    s.push_back('-');
    s.push_back(kHex[(dist(gen) & 0x3) | 0x8]);  // variant 10xx
    s += hex_n(3);
    s.push_back('-');
    s += hex_n(12);
    return s;
}

bool is_valid_uuid(const std::string& s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else {
            const char c = s[i];
            const bool is_hex =
                (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!is_hex) return false;
        }
    }
    return true;
}

// ============================================================================
// 枚举 ↔ 字符串
// ============================================================================

std::string layer_type_string(LayerType t) {
    switch (t) {
        case LayerType::COPPER: return "copper";
        case LayerType::SOLDER_MASK: return "solder_mask";
        case LayerType::SILKSCREEN: return "silkscreen";
        case LayerType::PASTE: return "paste";
        case LayerType::DRILL: return "drill";
        case LayerType::DOCUMENT: return "document";
        case LayerType::USER: return "user";
    }
    return "user";
}

LayerType parse_layer_type(const std::string& s) {
    if (s == "copper") return LayerType::COPPER;
    if (s == "solder_mask") return LayerType::SOLDER_MASK;
    if (s == "silkscreen") return LayerType::SILKSCREEN;
    if (s == "paste") return LayerType::PASTE;
    if (s == "drill") return LayerType::DRILL;
    if (s == "document") return LayerType::DOCUMENT;
    return LayerType::USER;  // 前向兼容：未识别 → USER
}

std::string pad_shape_string(PadShape s) {
    switch (s) {
        case PadShape::CIRCLE: return "circle";
        case PadShape::RECT: return "rect";
        case PadShape::OVAL: return "oval";
        case PadShape::OCTAGON: return "octagon";
    }
    return "rect";
}

PadShape parse_pad_shape(const std::string& s) {
    if (s == "circle") return PadShape::CIRCLE;
    if (s == "oval") return PadShape::OVAL;
    if (s == "octagon") return PadShape::OCTAGON;
    return PadShape::RECT;
}

std::string via_type_string(ViaType t) {
    switch (t) {
        case ViaType::THROUGH: return "through";
        case ViaType::BLIND: return "blind";
        case ViaType::BURIED: return "buried";
    }
    return "through";
}

ViaType parse_via_type(const std::string& s) {
    if (s == "blind") return ViaType::BLIND;
    if (s == "buried") return ViaType::BURIED;
    return ViaType::THROUGH;
}

// ============================================================================
// Stackup
// ============================================================================

const Layer* Stackup::find(const std::string& layer_id) const {
    for (const auto& l : layers_) {
        if (l.layer_id() == layer_id) return &l;
    }
    return nullptr;
}

std::size_t Stackup::copper_count() const {
    std::size_t n = 0;
    for (const auto& l : layers_) {
        if (l.is_copper()) ++n;
    }
    return n;
}

// ============================================================================
// 内部工具：把 Point / Box 编码为 Variant（[x,y] 数组形式）
// ============================================================================

namespace {

Variant point_to_variant(geometry::Point p) {
    std::vector<Variant> coord;
    coord.reserve(2);
    coord.emplace_back(Variant(static_cast<int64_t>(p.x)));
    coord.emplace_back(Variant(static_cast<int64_t>(p.y)));
    return Variant(coord);
}

geometry::Point point_from_variant(const Variant& v) {
    geometry::Point p{};
    const auto& coord = v.as_array();
    if (coord.size() >= 2) {
        p.x = static_cast<geometry::Coord>(coord[0].as_int());
        p.y = static_cast<geometry::Coord>(coord[1].as_int());
    }
    return p;
}

}  // namespace

// ============================================================================
// PcbEntity 基类
// ============================================================================

void PcbEntity::write_base(std::unordered_map<std::string, Variant>& d) const {
    d["type"] = Variant(std::string(get_class()));
    d["uuid"] = Variant(uuid_);
    if (!custom_properties_.empty()) {
        d["custom"] = Variant(custom_properties_);
    }
}

bool PcbEntity::read_base(const Variant& v) {
    if (v.get_type() != Variant::DICTIONARY) return false;
    if (v.dict_has("uuid")) {
        uuid_ = v.dict_at("uuid").as_string();
    }
    if (v.dict_has("custom")) {
        const auto& cm = v.dict_at("custom").as_dictionary();
        for (const auto& [k, val] : cm) {
            custom_properties_[k] = val;
        }
    }
    return true;
}

PropertySet PcbEntity::properties() const { return custom_properties_; }

void PcbEntity::set_property(const std::string& name, const Variant& value) {
    custom_properties_[name] = value;
}

Variant PcbEntity::get_property(const std::string& name,
                                const Variant& default_value) const {
    const auto it = custom_properties_.find(name);
    if (it == custom_properties_.end()) return default_value;
    return it->second;
}

std::string PcbEntity::serialize() const {
    std::unordered_map<std::string, Variant> d;
    write_base(d);
    return Variant(d).serialize();
}

bool PcbEntity::deserialize(const std::string& text) {
    Variant v = Variant::deserialize(text);
    return read_base(v);
}

// ============================================================================
// Track
// ============================================================================

geometry::Box Track::bounding_box() const {
    if (path_.points.empty()) return geometry::Box{};
    auto box = geometry::Box::from_points(path_.points);
    const geometry::Coord half = path_.width / 2;
    box.min.x -= half;
    box.min.y -= half;
    box.max.x += half;
    box.max.y += half;
    return box;
}

PropertySet Track::properties() const {
    PropertySet out = PcbEntity::properties();
    out["layer"] = Variant(layer_id_);
    if (!net_uuid_.empty()) out["net"] = Variant(net_uuid_);
    out["width"] = Variant(static_cast<int64_t>(path_.width));
    const int64_t seg_count = path_.points.empty()
                                  ? 0
                                  : static_cast<int64_t>(path_.points.size()) - 1;
    out["segment_count"] = Variant(seg_count);
    return out;
}

std::string Track::serialize() const {
    std::unordered_map<std::string, Variant> d;
    write_base(d);
    d["layer"] = Variant(layer_id_);
    if (!net_uuid_.empty()) d["net"] = Variant(net_uuid_);
    d["width"] = Variant(static_cast<int64_t>(path_.width));
    std::vector<Variant> pts;
    pts.reserve(path_.points.size());
    for (const auto& p : path_.points) {
        pts.emplace_back(point_to_variant(p));
    }
    d["points"] = Variant(pts);
    return Variant(d).serialize();
}

bool Track::deserialize(const std::string& text) {
    Variant v = Variant::deserialize(text);
    if (!read_base(v)) return false;
    if (v.dict_has("layer")) layer_id_ = v.dict_at("layer").as_string();
    if (v.dict_has("net")) net_uuid_ = v.dict_at("net").as_string();
    if (v.dict_has("width")) {
        path_.width = static_cast<geometry::Coord>(v.dict_at("width").as_int());
    }
    path_.points.clear();
    if (v.dict_has("points")) {
        const auto& pts = v.dict_at("points").as_array();
        path_.points.reserve(pts.size());
        for (const auto& pt_v : pts) {
            path_.points.push_back(point_from_variant(pt_v));
        }
    }
    return true;
}

// ============================================================================
// Pad
// ============================================================================

geometry::Box Pad::bounding_box() const {
    const geometry::Coord half_w = width_ / 2;
    const geometry::Coord half_h = height_ / 2;
    geometry::Box b;
    b.min = geometry::Point{position_.x - half_w, position_.y - half_h};
    b.max = geometry::Point{position_.x + half_w, position_.y + half_h};
    return b;
}

PropertySet Pad::properties() const {
    PropertySet out = PcbEntity::properties();
    out["layer"] = Variant(layer_id_);
    out["shape"] = Variant(pad_shape_string(shape_));
    out["width"] = Variant(static_cast<int64_t>(width_));
    out["height"] = Variant(static_cast<int64_t>(height_));
    if (!number_.empty()) out["number"] = Variant(number_);
    if (!net_uuid_.empty()) out["net"] = Variant(net_uuid_);
    return out;
}

std::string Pad::serialize() const {
    std::unordered_map<std::string, Variant> d;
    write_base(d);
    d["layer"] = Variant(layer_id_);
    d["shape"] = Variant(pad_shape_string(shape_));
    d["width"] = Variant(static_cast<int64_t>(width_));
    d["height"] = Variant(static_cast<int64_t>(height_));
    if (!number_.empty()) d["number"] = Variant(number_);
    if (!net_uuid_.empty()) d["net"] = Variant(net_uuid_);
    d["position"] = point_to_variant(position_);
    return Variant(d).serialize();
}

bool Pad::deserialize(const std::string& text) {
    Variant v = Variant::deserialize(text);
    if (!read_base(v)) return false;
    if (v.dict_has("layer")) layer_id_ = v.dict_at("layer").as_string();
    if (v.dict_has("shape")) shape_ = parse_pad_shape(v.dict_at("shape").as_string());
    if (v.dict_has("width")) width_ = static_cast<geometry::Coord>(v.dict_at("width").as_int());
    if (v.dict_has("height")) height_ = static_cast<geometry::Coord>(v.dict_at("height").as_int());
    if (v.dict_has("number")) number_ = v.dict_at("number").as_string();
    if (v.dict_has("net")) net_uuid_ = v.dict_at("net").as_string();
    if (v.dict_has("position")) position_ = point_from_variant(v.dict_at("position"));
    return true;
}

// ============================================================================
// Via
// ============================================================================

void Via::infer_via_type() {
    const bool from_outer = (layer_from_ == "F.Cu" || layer_from_ == "B.Cu");
    const bool to_outer = (layer_to_ == "F.Cu" || layer_to_ == "B.Cu");
    if (from_outer && to_outer) {
        via_type_ = ViaType::THROUGH;
    } else if (from_outer || to_outer) {
        via_type_ = ViaType::BLIND;
    } else {
        via_type_ = ViaType::BURIED;
    }
}

geometry::Box Via::bounding_box() const {
    const geometry::Coord half = pad_diameter_ / 2;
    geometry::Box b;
    b.min = geometry::Point{position_.x - half, position_.y - half};
    b.max = geometry::Point{position_.x + half, position_.y + half};
    return b;
}

PropertySet Via::properties() const {
    PropertySet out = PcbEntity::properties();
    out["drill"] = Variant(static_cast<int64_t>(drill_diameter_));
    out["pad"] = Variant(static_cast<int64_t>(pad_diameter_));
    out["from"] = Variant(layer_from_);
    out["to"] = Variant(layer_to_);
    out["type"] = Variant(via_type_string(via_type_));
    if (!net_uuid_.empty()) out["net"] = Variant(net_uuid_);
    return out;
}

std::string Via::serialize() const {
    std::unordered_map<std::string, Variant> d;
    write_base(d);
    d["drill"] = Variant(static_cast<int64_t>(drill_diameter_));
    d["pad"] = Variant(static_cast<int64_t>(pad_diameter_));
    d["from"] = Variant(layer_from_);
    d["to"] = Variant(layer_to_);
    d["type"] = Variant(via_type_string(via_type_));
    if (!net_uuid_.empty()) d["net"] = Variant(net_uuid_);
    d["position"] = point_to_variant(position_);
    return Variant(d).serialize();
}

bool Via::deserialize(const std::string& text) {
    Variant v = Variant::deserialize(text);
    if (!read_base(v)) return false;
    if (v.dict_has("drill")) drill_diameter_ = static_cast<geometry::Coord>(v.dict_at("drill").as_int());
    if (v.dict_has("pad")) pad_diameter_ = static_cast<geometry::Coord>(v.dict_at("pad").as_int());
    if (v.dict_has("from")) {
        layer_from_ = v.dict_at("from").as_string();
    }
    if (v.dict_has("to")) {
        layer_to_ = v.dict_at("to").as_string();
    }
    if (v.dict_has("type")) via_type_ = parse_via_type(v.dict_at("type").as_string());
    if (v.dict_has("net")) net_uuid_ = v.dict_at("net").as_string();
    if (v.dict_has("position")) position_ = point_from_variant(v.dict_at("position"));
    infer_via_type();
    return true;
}

// ============================================================================
// Net
// ============================================================================

void Net::add_member(std::string uuid) {
    if (std::find(members_.begin(), members_.end(), uuid) == members_.end()) {
        members_.push_back(std::move(uuid));
    }
}

bool Net::remove_member(const std::string& uuid) {
    const auto it = std::find(members_.begin(), members_.end(), uuid);
    if (it == members_.end()) return false;
    members_.erase(it);
    return true;
}

bool Net::has_member(const std::string& uuid) const {
    return std::find(members_.begin(), members_.end(), uuid) != members_.end();
}

PropertySet Net::properties() const {
    PropertySet out = PcbEntity::properties();
    out["name"] = Variant(name_);
    if (!net_class_.empty()) out["net_class"] = Variant(net_class_);
    out["member_count"] = Variant(static_cast<int64_t>(members_.size()));
    return out;
}

std::string Net::serialize() const {
    std::unordered_map<std::string, Variant> d;
    write_base(d);
    d["name"] = Variant(name_);
    if (!net_class_.empty()) d["net_class"] = Variant(net_class_);
    std::vector<Variant> members_v;
    members_v.reserve(members_.size());
    for (const auto& m : members_) members_v.emplace_back(Variant(m));
    if (!members_v.empty()) d["members"] = Variant(members_v);
    return Variant(d).serialize();
}

bool Net::deserialize(const std::string& text) {
    Variant v = Variant::deserialize(text);
    if (!read_base(v)) return false;
    if (v.dict_has("name")) name_ = v.dict_at("name").as_string();
    if (v.dict_has("net_class")) net_class_ = v.dict_at("net_class").as_string();
    members_.clear();
    if (v.dict_has("members")) {
        const auto& mv = v.dict_at("members").as_array();
        members_.reserve(mv.size());
        for (const auto& m : mv) {
            members_.push_back(m.as_string());
        }
    }
    return true;
}

// ============================================================================
// Board
// ============================================================================

Board::Board() : name_("Board") { uuid_ = generate_uuid(); }

Board::Board(std::string name) : name_(std::move(name)) { uuid_ = generate_uuid(); }

void Board::add_footprint(FootprintInstance fp) {
    footprints_.push_back(std::move(fp));
}

FootprintInstance* Board::find_footprint_by_uuid(const std::string& uuid) {
    for (auto& fp : footprints_) {
        if (fp.uuid == uuid) return &fp;
    }
    return nullptr;
}

Track& Board::add_track(geometry::Path path, std::string layer_id, std::string net_uuid) {
    auto t = std::make_unique<Track>(std::move(path), std::move(layer_id), std::move(net_uuid));
    t->set_history(history_);
    Track& ref = *t;
    tracks_.push_back(std::move(t));
    return ref;
}

Pad& Board::add_pad(geometry::Point position, geometry::Coord width, geometry::Coord height,
                    PadShape shape, std::string layer_id, std::string number) {
    auto p = std::make_unique<Pad>(position, width, height, shape, std::move(layer_id),
                                   std::move(number));
    p->set_history(history_);
    Pad& ref = *p;
    pads_.push_back(std::move(p));
    return ref;
}

Via& Board::add_via(geometry::Point position, geometry::Coord drill_diameter,
                    geometry::Coord pad_diameter, std::string layer_from,
                    std::string layer_to, std::string net_uuid) {
    auto v = std::make_unique<Via>(position, drill_diameter, pad_diameter,
                                   std::move(layer_from), std::move(layer_to),
                                   std::move(net_uuid));
    v->set_history(history_);
    Via& ref = *v;
    vias_.push_back(std::move(v));
    return ref;
}

Net& Board::add_net(std::string name) {
    auto n = std::make_unique<Net>(std::move(name));
    n->set_history(history_);
    Net& ref = *n;
    nets_.push_back(std::move(n));
    return ref;
}

Track* Board::find_track(const std::string& uuid) {
    for (auto& t : tracks_) {
        if (t->uuid() == uuid) return t.get();
    }
    return nullptr;
}

Pad* Board::find_pad(const std::string& uuid) {
    for (auto& p : pads_) {
        if (p->uuid() == uuid) return p.get();
    }
    return nullptr;
}

Via* Board::find_via(const std::string& uuid) {
    for (auto& v : vias_) {
        if (v->uuid() == uuid) return v.get();
    }
    return nullptr;
}

Net* Board::find_net(const std::string& uuid) {
    for (auto& n : nets_) {
        if (n->uuid() == uuid) return n.get();
    }
    return nullptr;
}

PcbEntity* Board::find_by_uuid(const std::string& uuid) {
    if (uuid == uuid_) return this;
    if (auto* t = find_track(uuid)) return t;
    if (auto* p = find_pad(uuid)) return p;
    if (auto* v = find_via(uuid)) return v;
    if (auto* n = find_net(uuid)) return n;
    return nullptr;
}

const PcbEntity* Board::find_by_uuid(const std::string& uuid) const {
    if (uuid == uuid_) return this;
    for (const auto& t : tracks_) {
        if (t->uuid() == uuid) return t.get();
    }
    for (const auto& p : pads_) {
        if (p->uuid() == uuid) return p.get();
    }
    for (const auto& v : vias_) {
        if (v->uuid() == uuid) return v.get();
    }
    for (const auto& n : nets_) {
        if (n->uuid() == uuid) return n.get();
    }
    return nullptr;
}

geometry::Box Board::bounding_box() const {
    if (!outline_.outer.empty()) {
        return geometry::Box::from_points(outline_.outer);
    }
    // 空板框：合并子图元包围盒
    std::vector<geometry::Point> pts;
    auto push_box = [&pts](const geometry::Box& b) {
        pts.push_back(b.min);
        pts.push_back(b.max);
    };
    for (const auto& t : tracks_) push_box(t->bounding_box());
    for (const auto& p : pads_) push_box(p->bounding_box());
    for (const auto& v : vias_) push_box(v->bounding_box());
    if (pts.empty()) return geometry::Box{};
    return geometry::Box::from_points(pts);
}

void Board::recompute_net_boxes() {
    for (auto& net : nets_) {
        std::vector<geometry::Point> pts;
        auto push_box = [&pts](const geometry::Box& b) {
            pts.push_back(b.min);
            pts.push_back(b.max);
        };
        for (const auto& uuid : net->member_uuids()) {
            if (auto* t = find_track(uuid)) push_box(t->bounding_box());
            if (auto* p = find_pad(uuid)) push_box(p->bounding_box());
            if (auto* v = find_via(uuid)) push_box(v->bounding_box());
        }
        net->set_cached_box(pts.empty() ? geometry::Box{}
                                        : geometry::Box::from_points(pts));
    }
}

PropertySet Board::properties() const {
    PropertySet out = PcbEntity::properties();
    out["name"] = Variant(name_);
    out["track_count"] = Variant(static_cast<int64_t>(tracks_.size()));
    out["pad_count"] = Variant(static_cast<int64_t>(pads_.size()));
    out["via_count"] = Variant(static_cast<int64_t>(vias_.size()));
    out["net_count"] = Variant(static_cast<int64_t>(nets_.size()));
    out["layer_count"] = Variant(static_cast<int64_t>(stackup_.layer_count()));
    out["footprint_count"] = Variant(static_cast<int64_t>(footprints_.size()));
    return out;
}

std::string Board::serialize() const {
    std::unordered_map<std::string, Variant> d;
    write_base(d);
    d["name"] = Variant(name_);
    if (!rules_ref_.empty()) d["rules"] = Variant(rules_ref_);

    // 板框 outline（outer + holes）
    if (!outline_.outer.empty()) {
        std::vector<Variant> outer;
        outer.reserve(outline_.outer.size());
        for (const auto& p : outline_.outer) outer.emplace_back(point_to_variant(p));
        d["outline_outer"] = Variant(outer);
        if (!outline_.holes.empty()) {
            std::vector<Variant> holes;
            holes.reserve(outline_.holes.size());
            for (const auto& hole : outline_.holes) {
                std::vector<Variant> h;
                h.reserve(hole.size());
                for (const auto& p : hole) h.emplace_back(point_to_variant(p));
                holes.emplace_back(Variant(h));
            }
            d["outline_holes"] = Variant(holes);
        }
    }

    // 图层
    if (stackup_.layer_count() > 0) {
        std::vector<Variant> layers_v;
        layers_v.reserve(stackup_.layer_count());
        for (std::size_t i = 0; i < stackup_.layer_count(); ++i) {
            const auto& l = stackup_.layer(i);
            std::unordered_map<std::string, Variant> ld;
            ld["id"] = Variant(l.layer_id());
            ld["type"] = Variant(layer_type_string(l.type()));
            ld["copper_index"] = Variant(static_cast<int64_t>(l.copper_index()));
            layers_v.emplace_back(Variant(ld));
        }
        d["layers"] = Variant(layers_v);
    }

    // 器件实例（Footprint 占位）
    if (!footprints_.empty()) {
        std::vector<Variant> fps;
        fps.reserve(footprints_.size());
        for (const auto& fp : footprints_) {
            std::unordered_map<std::string, Variant> fd;
            fd["uuid"] = Variant(fp.uuid);
            fd["footprint_ref"] = Variant(fp.footprint_ref);
            if (!fp.refdes.empty()) fd["refdes"] = Variant(fp.refdes);
            fd["position"] = point_to_variant(fp.position);
            fd["rotation"] = Variant(fp.rotation);
            fd["flipped"] = Variant(fp.flipped);
            fps.emplace_back(Variant(fd));
        }
        d["footprints"] = Variant(fps);
    }

    // 递归嵌入子对象：解析子对象 serialize() 输出 → 嵌入为结构化 Variant
    auto embed_all = [](const auto& entities) {
        std::vector<Variant> out;
        out.reserve(entities.size());
        for (const auto& e : entities) {
            out.emplace_back(Variant::deserialize(e->serialize()));
        }
        return out;
    };

    if (!tracks_.empty()) d["tracks"] = Variant(embed_all(tracks_));
    if (!pads_.empty()) d["pads"] = Variant(embed_all(pads_));
    if (!vias_.empty()) d["vias"] = Variant(embed_all(vias_));
    if (!nets_.empty()) d["nets"] = Variant(embed_all(nets_));

    return Variant(d).serialize();
}

bool Board::deserialize(const std::string& text) {
    Variant v = Variant::deserialize(text);
    if (!read_base(v)) return false;

    if (v.dict_has("name")) name_ = v.dict_at("name").as_string();
    if (v.dict_has("rules")) rules_ref_ = v.dict_at("rules").as_string();

    // 板框
    outline_.outer.clear();
    outline_.holes.clear();
    if (v.dict_has("outline_outer")) {
        const auto& outer = v.dict_at("outline_outer").as_array();
        outline_.outer.reserve(outer.size());
        for (const auto& pt_v : outer) {
            outline_.outer.push_back(point_from_variant(pt_v));
        }
        if (v.dict_has("outline_holes")) {
            const auto& holes = v.dict_at("outline_holes").as_array();
            outline_.holes.reserve(holes.size());
            for (const auto& h_v : holes) {
                const auto& h_arr = h_v.as_array();
                std::vector<geometry::Point> hole;
                hole.reserve(h_arr.size());
                for (const auto& pt_v : h_arr) {
                    hole.push_back(point_from_variant(pt_v));
                }
                outline_.holes.push_back(std::move(hole));
            }
        }
    }

    // 图层
    stackup_ = Stackup{};
    if (v.dict_has("layers")) {
        const auto& layers_v = v.dict_at("layers").as_array();
        for (const auto& lv : layers_v) {
            Layer l;
            if (lv.dict_has("id")) l.set_layer_id(lv.dict_at("id").as_string());
            if (lv.dict_has("type")) l.set_type(parse_layer_type(lv.dict_at("type").as_string()));
            if (lv.dict_has("copper_index")) {
                l.set_copper_index(static_cast<int>(lv.dict_at("copper_index").as_int()));
            }
            stackup_.add_layer(std::move(l));
        }
    }

    // 器件实例
    footprints_.clear();
    if (v.dict_has("footprints")) {
        const auto& fps = v.dict_at("footprints").as_array();
        footprints_.reserve(fps.size());
        for (const auto& fv : fps) {
            FootprintInstance fp;
            if (fv.dict_has("uuid")) fp.uuid = fv.dict_at("uuid").as_string();
            if (fv.dict_has("footprint_ref")) fp.footprint_ref = fv.dict_at("footprint_ref").as_string();
            if (fv.dict_has("refdes")) fp.refdes = fv.dict_at("refdes").as_string();
            if (fv.dict_has("position")) fp.position = point_from_variant(fv.dict_at("position"));
            if (fv.dict_has("rotation")) fp.rotation = fv.dict_at("rotation").as_float();
            if (fv.dict_has("flipped")) fp.flipped = fv.dict_at("flipped").as_bool();
            footprints_.push_back(std::move(fp));
        }
    }

    // 子对象：每个元素是 DICTIONARY Variant；re-serialize 为 JSON 交由子对象自身解析。
    tracks_.clear();
    pads_.clear();
    vias_.clear();
    nets_.clear();
    if (v.dict_has("tracks")) {
        const auto& arr = v.dict_at("tracks").as_array();
        tracks_.reserve(arr.size());
        for (const auto& elem : arr) {
            auto t = std::make_unique<Track>();
            t->deserialize(elem.serialize());
            t->set_history(history_);
            tracks_.push_back(std::move(t));
        }
    }
    if (v.dict_has("pads")) {
        const auto& arr = v.dict_at("pads").as_array();
        pads_.reserve(arr.size());
        for (const auto& elem : arr) {
            auto p = std::make_unique<Pad>();
            p->deserialize(elem.serialize());
            p->set_history(history_);
            pads_.push_back(std::move(p));
        }
    }
    if (v.dict_has("vias")) {
        const auto& arr = v.dict_at("vias").as_array();
        vias_.reserve(arr.size());
        for (const auto& elem : arr) {
            auto via = std::make_unique<Via>();
            via->deserialize(elem.serialize());
            via->set_history(history_);
            vias_.push_back(std::move(via));
        }
    }
    if (v.dict_has("nets")) {
        const auto& arr = v.dict_at("nets").as_array();
        nets_.reserve(arr.size());
        for (const auto& elem : arr) {
            auto n = std::make_unique<Net>();
            n->deserialize(elem.serialize());
            n->set_history(history_);
            nets_.push_back(std::move(n));
        }
    }

    return true;
}

}  // namespace eda::pcb
