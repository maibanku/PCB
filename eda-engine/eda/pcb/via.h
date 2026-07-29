#pragma once

// eda/pcb/via.h
//
// P8 PCB 编辑器 · 过孔图元（Via，04-术语表 D3）。
//
// Via 是连接不同铜层的导电通孔： drilled hole + outer pad（padstack 简化）。
// 类型（ViaType）：THROUGH（通孔，Top→Bottom）/ BLIND（盲孔，外层→内层）/
//                 BURIED（埋孔，内层→内层）—— 08-07 盲埋孔与背钻。
//
// 六属性契约：
//   UUID       构造分配
//   Geometry   bounding_box = position ± pad_diameter/2
//   Properties position / drill / pad / layer pair / type / net
//   Constraints 占位
//   History     基类挂点
//   Serialize   Variant JSON 往返
//
// 层对（layer_from / layer_to）：用 layer_id 字符串表达（如 "F.Cu" → "B.Cu"）；
// Stackup 提供层 ID 校验（08-02 Layer Pair）。
//
// 命名规范：namespace eda::pcb、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <string>

#include "eda/geometry/types.h"
#include "eda/pcb/pcb_entity.h"

namespace eda::pcb {

// 过孔类型枚举。序列化字符串见 via_type_string / parse_via_type。
enum class ViaType {
    THROUGH,  // 通孔（Top - Bottom）
    BLIND,    // 盲孔（外层 - 内层）
    BURIED,   // 埋孔（内层 - 内层）
};

std::string via_type_string(ViaType t);
ViaType parse_via_type(const std::string& s);

class Via : public PcbEntity {
public:
    Via() { uuid_ = generate_uuid(); }

    Via(geometry::Point position, geometry::Coord drill_diameter,
        geometry::Coord pad_diameter, std::string layer_from,
        std::string layer_to, std::string net_uuid = "")
        : position_(position),
          drill_diameter_(drill_diameter),
          pad_diameter_(pad_diameter),
          layer_from_(std::move(layer_from)),
          layer_to_(std::move(layer_to)),
          net_uuid_(std::move(net_uuid)) {
        uuid_ = generate_uuid();
        infer_via_type();
    }

    // ---- Geometry ----
    geometry::Box bounding_box() const override;

    geometry::Point position() const { return position_; }
    void set_position(geometry::Point p) { position_ = p; }

    geometry::Coord drill_diameter() const { return drill_diameter_; }
    void set_drill_diameter(geometry::Coord d) { drill_diameter_ = d; }

    geometry::Coord pad_diameter() const { return pad_diameter_; }
    void set_pad_diameter(geometry::Coord d) { pad_diameter_ = d; }

    const std::string& layer_from() const { return layer_from_; }
    void set_layer_from(std::string l) {
        layer_from_ = std::move(l);
        infer_via_type();
    }

    const std::string& layer_to() const { return layer_to_; }
    void set_layer_to(std::string l) {
        layer_to_ = std::move(l);
        infer_via_type();
    }

    ViaType via_type() const { return via_type_; }

    const std::string& net_uuid() const { return net_uuid_; }
    void set_net_uuid(std::string n) { net_uuid_ = std::move(n); }

    // ---- Properties / Serialization ----
    PropertySet properties() const override;
    std::string serialize() const override;
    bool deserialize(const std::string& text) override;

    // ---- ClassDB ----
    static std::string get_class_static() { return "Via"; }
    std::string get_class() const override { return "Via"; }
    bool is_class(const std::string& n) const override {
        return n == "Via" || PcbEntity::is_class(n);
    }

private:
    geometry::Point position_{};
    geometry::Coord drill_diameter_ = 0;  // 孔径（NPTH/PTH 钻头尺寸）
    geometry::Coord pad_diameter_ = 0;    // 盘径（外环焊盘尺寸）
    std::string layer_from_;              // 起始铜层 ID
    std::string layer_to_;                // 终止铜层 ID
    ViaType via_type_ = ViaType::THROUGH;
    std::string net_uuid_;

    // 根据层对推断过孔类型：F.Cu/B.Cu 任一缺席 → BLIND/BURIED；两端皆外层 → THROUGH。
    // 骨架版判定：from==F.Cu && to==B.Cu（或反之）→ THROUGH；其余 → BURIED。
    // 完整判定需 Stackup 上下文（外层 vs 内层），待 08-07 接入。
    void infer_via_type();
};

}  // namespace eda::pcb
