#pragma once

// eda/pcb/board.h
//
// P8 PCB 编辑器 · Board（板）—— PCB 对象树根节点（REQ-008）。
//
// 对象树层级：
//   Board
//     ├── outline      : geometry::Polygon（闭合板框，08-01）
//     ├── layers[]     : Layer（08-02 图层 / Stackup）
//     ├── footprints[] : FootprintInstance（器件实例，引用 P6 元件库的 Footprint）
//     │     ├── footprint_ref  (lib://standard@1.2.0/SOIC-8)
//     │     ├── position / rotation / flipped
//     │     └── pads（Footprint 内部 Pad 列表——本期由 footprint 库承载，Board 仅占位）
//     ├── tracks[]     : Track（08-05 布线）
//     ├── pads[]       : Pad（孤立焊盘 / 自由焊盘）
//     ├── vias[]       : Via（08-07）
//     ├── nets[]       : Net（连通性，REQ-008 网络对象）
//     └── rules_ref    : 设计规则引用（08-03，命名/UUID）
//
// Board 自身实现六属性契约：
//   UUID       构造分配
//   Geometry   bounding_box = 板框 outline 的 AABB；空板框时退化为子图元并集
//   Properties name / outline_area / counts
//   Constraints 占位
//   History     基类挂点（UndoStack 经 Board 顶层分发到子对象编辑命令）
//   Serialize   Variant JSON 往返（递归子对象）
//
// 拥有语义：tracks/pads/vias/nets 用 unique_ptr 持有（PcbEntity 不可拷贝、需多态查找）。
// layers/footprints 为值类型（Layer/FootprintInstance 是 POD 定义/记录）。
//
// 命名规范：namespace eda::pcb、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "eda/geometry/types.h"
#include "eda/pcb/layer.h"
#include "eda/pcb/net.h"
#include "eda/pcb/pad.h"
#include "eda/pcb/pcb_entity.h"
#include "eda/pcb/track.h"
#include "eda/pcb/via.h"

namespace eda::pcb {

// ============================================================================
// FootprintInstance —— 板上的器件实例（Footprint 占位）。
// Footprint 完整定义在 P6 元件库（footprint + 3D + courtyard）；
// 本结构只记录板上的"放置"信息（引用 + 位置 + 朝向 + 镜像）。
// ============================================================================
struct FootprintInstance {
    std::string uuid;            // 实例 UUID（板上唯一）
    std::string footprint_ref;   // 引用，如 "lib://standard@1.2.0/SOIC-8"
    std::string refdes;          // 位号（R1/C2/U3），可空
    geometry::Point position{};  // 板上坐标（nm）
    double rotation = 0.0;       // 弧度，逆时针正
    bool flipped = false;        // true=镜像到底层（Bottom）
};

// ============================================================================
// Board —— PCB 对象树根节点。
// ============================================================================
class Board : public PcbEntity {
public:
    Board();                     // 自动生成 UUID + 默认名称
    explicit Board(std::string name);

    ~Board() override = default;
    Board(const Board&) = delete;
    Board& operator=(const Board&) = delete;

    // ---- 标识 ----
    const std::string& name() const { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

    // ---- 板框（08-01）----
    const geometry::Polygon& outline() const { return outline_; }
    void set_outline(geometry::Polygon p) { outline_ = std::move(p); }

    // ---- 图层 / 叠层（08-02）----
    void add_layer(Layer l) { stackup_.add_layer(std::move(l)); }
    std::size_t layer_count() const { return stackup_.layer_count(); }
    const Layer& layer(std::size_t i) const { return stackup_.layer(i); }
    const Stackup& stackup() const { return stackup_; }
    Stackup& stackup() { return stackup_; }

    // ---- 设计规则引用（08-03）----
    const std::string& rules_ref() const { return rules_ref_; }
    void set_rules_ref(std::string r) { rules_ref_ = std::move(r); }

    // ---- 器件实例（Footprint）----
    void add_footprint(FootprintInstance fp);
    std::size_t footprint_count() const { return footprints_.size(); }
    const FootprintInstance& footprint(std::size_t i) const { return footprints_[i]; }
    const std::vector<FootprintInstance>& footprints() const { return footprints_; }
    FootprintInstance* find_footprint_by_uuid(const std::string& uuid);

    // ---- Track / Pad / Via / Net：增删查（拥有所有权）----
    Track& add_track(geometry::Path path, std::string layer_id, std::string net_uuid = "");
    Pad& add_pad(geometry::Point position, geometry::Coord width, geometry::Coord height,
                 PadShape shape, std::string layer_id, std::string number = "");
    Via& add_via(geometry::Point position, geometry::Coord drill_diameter,
                 geometry::Coord pad_diameter, std::string layer_from,
                 std::string layer_to, std::string net_uuid = "");
    Net& add_net(std::string name);

    std::size_t track_count() const { return tracks_.size(); }
    std::size_t pad_count() const { return pads_.size(); }
    std::size_t via_count() const { return vias_.size(); }
    std::size_t net_count() const { return nets_.size(); }

    const std::vector<std::unique_ptr<Track>>& tracks() const { return tracks_; }
    const std::vector<std::unique_ptr<Pad>>& pads() const { return pads_; }
    const std::vector<std::unique_ptr<Via>>& vias() const { return vias_; }
    const std::vector<std::unique_ptr<Net>>& nets() const { return nets_; }

    // ---- UUID 查找（递归所有 PcbEntity 子对象）----
    // 命中 Board 自身 uuid 返回 this；否则遍历 track/pad/via/net。
    PcbEntity* find_by_uuid(const std::string& uuid);
    const PcbEntity* find_by_uuid(const std::string& uuid) const;

    // 便捷强类型查找（找不到返回 nullptr）。
    Track* find_track(const std::string& uuid);
    Pad* find_pad(const std::string& uuid);
    Via* find_via(const std::string& uuid);
    Net* find_net(const std::string& uuid);

    // ---- Geometry（板框 AABB；空板框时合并子图元包围盒）----
    geometry::Box bounding_box() const override;

    // 刷新所有 Net 的 cached_box（成员包围盒并集）。增删成员后调用。
    void recompute_net_boxes();

    // ---- Properties / Serialization ----
    PropertySet properties() const override;
    std::string serialize() const override;
    bool deserialize(const std::string& text) override;

    // ---- ClassDB ----
    static std::string get_class_static() { return "Board"; }
    std::string get_class() const override { return "Board"; }
    bool is_class(const std::string& n) const override {
        return n == "Board" || PcbEntity::is_class(n);
    }

private:
    std::string name_;
    geometry::Polygon outline_;                         // 板框（闭合多边形，含孔）
    Stackup stackup_;                                   // 图层列表
    std::vector<FootprintInstance> footprints_;         // 器件实例
    std::vector<std::unique_ptr<Track>> tracks_;        // 走线
    std::vector<std::unique_ptr<Pad>> pads_;            // 焊盘
    std::vector<std::unique_ptr<Via>> vias_;            // 过孔
    std::vector<std::unique_ptr<Net>> nets_;            // 网络
    std::string rules_ref_;                             // 设计规则引用
};

}  // namespace eda::pcb
