#pragma once

// eda/pcb/track.h
//
// P8 PCB 编辑器 · 走线图元（08-05 布线）。
//
// Track 是 PCB 上最具代表性的图元：中心折线 + 宽度（geometry::Path 本身就含 width）。
// 渲染/DRC 时由 P3 offset 偏移成多边形（Path → Polygon 走 eda/geometry/offset）。
//
// 六属性契约：
//   UUID       构造分配（generate_uuid）
//   Geometry   bounding_box = Path 点集 AABB 外扩 width/2
//   Properties width / layer / net / segment_count（Properties 系统的 value 通道）
//   Constraints 占位（默认 nullptr；P8-03 落地 NetClass width 约束后挂接）
//   History     基类 history() 挂点（UndoStack）
//   Serialize   Variant JSON 往返（type/uuid/layer/net/width/points）
//
// 术语对齐（04-术语表 D3）：英文 source=track，代码符号 Track（PascalCase）。
// 走线（track/trace）统一用 Track（不用 Path 命名，避免与 geometry::Path 冲突）。
//
// 命名规范：namespace eda::pcb、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <string>

#include "eda/geometry/types.h"
#include "eda/pcb/pcb_entity.h"

namespace eda::pcb {

class Track : public PcbEntity {
public:
    Track() { uuid_ = generate_uuid(); }

    Track(geometry::Path path, std::string layer_id, std::string net_uuid = "")
        : path_(std::move(path)),
          layer_id_(std::move(layer_id)),
          net_uuid_(std::move(net_uuid)) {
        uuid_ = generate_uuid();
    }

    // ---- Geometry ----
    geometry::Box bounding_box() const override;

    const geometry::Path& path() const { return path_; }
    void set_path(geometry::Path p) { path_ = std::move(p); }

    geometry::Coord width() const { return path_.width; }
    void set_width(geometry::Coord w) { path_.width = w; }

    const std::string& layer_id() const { return layer_id_; }
    void set_layer_id(std::string l) { layer_id_ = std::move(l); }

    const std::string& net_uuid() const { return net_uuid_; }
    void set_net_uuid(std::string n) { net_uuid_ = std::move(n); }

    // ---- Properties / Serialization ----
    PropertySet properties() const override;
    std::string serialize() const override;
    bool deserialize(const std::string& text) override;

    // ---- ClassDB ----
    static std::string get_class_static() { return "Track"; }
    std::string get_class() const override { return "Track"; }
    bool is_class(const std::string& n) const override {
        return n == "Track" || PcbEntity::is_class(n);
    }

private:
    geometry::Path path_;       // 中心线 + 宽度（width=0 表示零宽，仅几何占位）
    std::string layer_id_;      // 所属铜层 ID（如 "F.Cu"）
    std::string net_uuid_;      // 所属网络 UUID（可空——未分配网络）
};

}  // namespace eda::pcb
