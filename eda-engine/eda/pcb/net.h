#pragma once

// eda/pcb/net.h
//
// P8 PCB 编辑器 · 网络（Net，04-术语表）。
//
// Net 表达电气连通性：把一组物理图元（Track/Pad/Via）归属于同一电气节点。
// Net 本身不是 placed primitive（不占几何），但 REQ-008 将其与 Track/Pad/Via/Footprint
// 同列实现六属性契约——bounding_box 取所有成员包围盒的并集（空时退化为默认 Box）。
//
// 六属性契约：
//   UUID       构造分配（= 电气节点的稳定主键）
//   Geometry   bounding_box = 成员包围盒并集
//   Properties name / net_class / member_count
//   Constraints 占位（NetClass 约束挂接点，REQ-011）
//   History     基类挂点
//   Serialize   Variant JSON 往返（含成员 UUID 列表）
//
// 关联：
//   - net_class：网络类引用（命名或 UUID），驱动 08-03 设计规则分级。
//   - members：成员图元的 UUID 列表（Track/Pad/Via）；ERC/DRC 阶段校验连通性。
//
// 命名规范：namespace eda::pcb、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <string>
#include <vector>

#include "eda/geometry/types.h"
#include "eda/pcb/pcb_entity.h"

namespace eda::pcb {

class Net : public PcbEntity {
public:
    Net() { uuid_ = generate_uuid(); }

    explicit Net(std::string name) : name_(std::move(name)) {
        uuid_ = generate_uuid();
    }

    Net(std::string name, std::string net_class)
        : name_(std::move(name)), net_class_(std::move(net_class)) {
        uuid_ = generate_uuid();
    }

    // ---- 标识 / 分类 ----
    const std::string& name() const { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

    const std::string& net_class() const { return net_class_; }
    void set_net_class(std::string c) { net_class_ = std::move(c); }

    // ---- 成员图元 UUID 列表 ----
    const std::vector<std::string>& member_uuids() const { return members_; }
    std::size_t member_count() const { return members_.size(); }

    void add_member(std::string uuid);
    bool remove_member(const std::string& uuid);
    bool has_member(const std::string& uuid) const;

    // ---- Geometry（成员包围盒并集）----
    // 注意：本方法不持有成员指针，需由 Board 提供解析器（按 UUID 查成员 bounding_box）。
    // 当前实现返回 last_computed_box_（由 Board::recompute_net_boxes 刷新），
    // 默认为空 Box。骨架阶段不阻塞接口；DRC 阶段补完整。
    geometry::Box bounding_box() const override { return cached_box_; }

    // Board 在 add_member 后调用以刷新缓存（避免 Net 反向持有 Board 指针）。
    void set_cached_box(geometry::Box b) { cached_box_ = b; }

    // ---- Properties / Serialization ----
    PropertySet properties() const override;
    std::string serialize() const override;
    bool deserialize(const std::string& text) override;

    // ---- ClassDB ----
    static std::string get_class_static() { return "Net"; }
    std::string get_class() const override { return "Net"; }
    bool is_class(const std::string& n) const override {
        return n == "Net" || PcbEntity::is_class(n);
    }

private:
    std::string name_;                       // 网络名（如 "VCC", "GND", "/SDA"）
    std::string net_class_;                  // 网络类引用（命名/UUID，08-03）
    std::vector<std::string> members_;       // 成员图元 UUID 列表
    geometry::Box cached_box_;               // 成员包围盒并集缓存（Board 刷新）
};

}  // namespace eda::pcb
