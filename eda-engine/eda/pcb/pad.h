#pragma once

// eda/pcb/pad.h
//
// P8 PCB 编辑器 · 焊盘图元（Pad，04-术语表 D3）。
//
// Pad 是器件 footprint 上的可焊接点，亦可独立放置（孤立焊盘）。本期为骨架数据模型，
// 不强绑定 footprint（FootprintInstance 在 Board 中持有位置占位，Pad 可作为独立图元存在）。
//
// 六属性契约：
//   UUID       构造分配
//   Geometry   bounding_box = position ± (width/2, height/2)
//   Properties position / size / shape / layer / number / net
//   Constraints 占位
//   History     基类挂点
//   Serialize   Variant JSON 往返
//
// 形状枚举（PadShape）覆盖主流封装类型（IPC-7351）：
//   CIRCLE / RECT / OVAL / OCTAGON
//
// 命名规范：namespace eda::pcb、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <string>

#include "eda/geometry/types.h"
#include "eda/pcb/pcb_entity.h"

namespace eda::pcb {

// 焊盘形状枚举。序列化字符串见 pad_shape_string / parse_pad_shape。
enum class PadShape {
    CIRCLE,
    RECT,
    OVAL,
    OCTAGON,
};

std::string pad_shape_string(PadShape s);
PadShape parse_pad_shape(const std::string& s);

class Pad : public PcbEntity {
public:
    Pad() { uuid_ = generate_uuid(); }

    Pad(geometry::Point position, geometry::Coord width, geometry::Coord height,
        PadShape shape, std::string layer_id, std::string number = "")
        : position_(position),
          width_(width),
          height_(height),
          shape_(shape),
          layer_id_(std::move(layer_id)),
          number_(std::move(number)) {
        uuid_ = generate_uuid();
    }

    // ---- Geometry ----
    geometry::Box bounding_box() const override;

    geometry::Point position() const { return position_; }
    void set_position(geometry::Point p) { position_ = p; }

    geometry::Coord width() const { return width_; }
    void set_width(geometry::Coord w) { width_ = w; }

    geometry::Coord height() const { return height_; }
    void set_height(geometry::Coord h) { height_ = h; }

    PadShape shape() const { return shape_; }
    void set_shape(PadShape s) { shape_ = s; }

    const std::string& layer_id() const { return layer_id_; }
    void set_layer_id(std::string l) { layer_id_ = std::move(l); }

    // 焊盘号（如 "1" / "A1" / "GND"）；与 footprint 引脚号对应。
    const std::string& number() const { return number_; }
    void set_number(std::string n) { number_ = std::move(n); }

    const std::string& net_uuid() const { return net_uuid_; }
    void set_net_uuid(std::string n) { net_uuid_ = std::move(n); }

    // ---- Properties / Serialization ----
    PropertySet properties() const override;
    std::string serialize() const override;
    bool deserialize(const std::string& text) override;

    // ---- ClassDB ----
    static std::string get_class_static() { return "Pad"; }
    std::string get_class() const override { return "Pad"; }
    bool is_class(const std::string& n) const override {
        return n == "Pad" || PcbEntity::is_class(n);
    }

private:
    geometry::Point position_{};
    geometry::Coord width_ = 0;     // X 方向尺寸（CIRCLE 时为直径）
    geometry::Coord height_ = 0;    // Y 方向尺寸（CIRCLE 时为直径）
    PadShape shape_ = PadShape::RECT;
    std::string layer_id_;
    std::string number_;            // 焊盘号 / 引脚号
    std::string net_uuid_;          // 所属网络 UUID（可空）
};

}  // namespace eda::pcb
