#pragma once

// eda/schematic/schematic.h
//
// P7 原理图核心数据模型 · Schematic（07-01 原理图图元 / 07-04 网络表生成）。
//
// 对应 docs/07-原理图编辑器/01-原理图图元 + 04-网络表生成。
// 对齐 REQ-007 EDA Object Model：Schematic : Resource。
//
// 数据模型分层：
//   Schematic (Resource)
//     ├── components_   : Component[]（器件实例，每个持 Symbol/Device 引用 + 放置）
//     ├── wires_        : Wire[]（两点导线，连通性骨架）
//     ├── net_labels_   : NetLabel[]（显式命名节点：SDA/SCL/...）
//     ├── power_ports_  : PowerPort[]（电源/地符号：VCC/GND/+3V3/...）
//     └── notes_        : string[]（注释，07-01 Note 图元的简化文本占位）
//
// 网表生成（generate_netlist）：
//   - 把所有 Pin 连接点 / Wire 端点 / NetLabel 位置 / PowerPort 位置插入 Union-Find；
//   - Wire 的两端点做 union；
//   - 同坐标的点自动同根（隐式 union）；
//   - 按 root 分组 Pin 引用 → 形成 Net；
//   - Net 命名优先级：PowerPort > NetLabel > 自动 N0001。
//   复杂度 O(n·α(n))，万级引脚亚秒级（docs/07-04 关键点）。
//
// 序列化：手写最小 TOML 子集，输出 .sch.toml（确定性：字段顺序固定；列表稳定）。
//
// 命名规范：namespace eda::sch、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/object/ref.h"
#include "core/object/resource.h"
#include "eda/schematic/component.h"
#include "eda/schematic/net_label.h"
#include "eda/schematic/power_port.h"
#include "eda/schematic/wire.h"

namespace eda::sch {

// ============================================================================
// PinRef —— 网表中的引脚引用（器件位号 + 引脚号）
// ============================================================================
struct PinRef {
    std::string refdes;       // "R1"（对应 Component::refdes()）
    std::string pin_number;   // "1"（对应 Symbol::Pin::number()）

    bool operator==(const PinRef&) const = default;
};

// ============================================================================
// Net —— 网络（一组电气连通的引脚 + 网络名 + 分类）
// ============================================================================
struct Net {
    std::string name;                 // 网络名（"VCC" / "SDA" / "N0001"）
    std::vector<PinRef> pins;         // 成员引脚（按 refdes+pin_number 排序，确定性）
    bool is_power = false;            // 电源/地网络（含 PowerPort）

    bool operator==(const Net&) const = default;
};

// ============================================================================
// Netlist —— 网表（generate_netlist 的输出）
//   网表是 C2→C3 的咽喉数据（docs/07-04 关键点）：必须稳定。
//   nets 按 name 字典序排列，确保跨次运行的确定性输出。
// ============================================================================
class Netlist {
public:
    std::size_t net_count() const { return nets_.size(); }
    const std::vector<Net>& nets() const { return nets_; }

    // 按网络名查找（找不到返回 nullptr）。
    const Net* find_net(const std::string& name) const;

    // 全网表引脚总数（= Σ net.pins.size()）。
    std::size_t pin_count() const;

private:
    std::vector<Net> nets_;
    friend class Schematic;
};

// ============================================================================
// Schematic —— 原理图页（继承 Resource）
// ============================================================================
class Schematic : public Resource {
public:
    static std::string get_class_static() { return "Schematic"; }
    std::string get_class() const override { return "Schematic"; }
    bool is_class(const std::string& name) const override {
        return name == "Schematic" || Resource::is_class(name);
    }

    Schematic();  // 自动生成 UUID

    // ----- 身份 / 元信息 -----
    const std::string& uuid() const { return uuid_; }
    const std::string& name() const { return name_; }     // 显示名（如 "Main Sheet"）
    void set_name(std::string n) { name_ = std::move(n); }

    // ----- Component（值语义容器；按 UUID 管理）-----
    // 返回新增 Component 的引用，便于继续配置。
    Component& add_component(Component c);
    // 按 UUID 移除 Component；返回是否实际移除。
    bool remove_component_by_uuid(const std::string& uuid);
    // 按 UUID 查找；找不到返回 nullptr。
    Component* find_component_by_uuid(const std::string& uuid);
    const Component* find_component_by_uuid(const std::string& uuid) const;

    // 按 refdes 查找（首个匹配；找不到返回 nullptr）。
    Component* find_component_by_refdes(const std::string& refdes);
    const Component* find_component_by_refdes(const std::string& refdes) const;

    std::size_t component_count() const { return components_.size(); }
    const std::vector<Component>& components() const { return components_; }
    const Component& component(std::size_t i) const { return components_[i]; }
    Component& component(std::size_t i) { return components_[i]; }

    // ----- Wire / NetLabel / PowerPort（值语义容器；按索引管理）-----
    Wire& add_wire(Wire w);
    NetLabel& add_net_label(NetLabel l);
    PowerPort& add_power_port(PowerPort p);

    std::size_t wire_count() const { return wires_.size(); }
    std::size_t net_label_count() const { return net_labels_.size(); }
    std::size_t power_port_count() const { return power_ports_.size(); }

    const std::vector<Wire>& wires() const { return wires_; }
    const std::vector<NetLabel>& net_labels() const { return net_labels_; }
    const std::vector<PowerPort>& power_ports() const { return power_ports_; }

    const Wire& wire(std::size_t i) const { return wires_[i]; }
    const NetLabel& net_label(std::size_t i) const { return net_labels_[i]; }
    const PowerPort& power_port(std::size_t i) const { return power_ports_[i]; }

    // 按索引移除（值语义；越界返回 false）。
    bool remove_wire(std::size_t index);
    bool remove_net_label(std::size_t index);
    bool remove_power_port(std::size_t index);

    // 通用按 UUID 删除（仅 components 有 UUID；其它类型忽略并返回 false）。
    // 提供统一的 Schematic-level 删除入口，对接命令栈（B3 SetPropertyCommand 等）。
    bool remove_by_uuid(const std::string& uuid);
    // 通用按 UUID 查找（仅查 components；返回 Component* 或 nullptr）。
    Component* find_by_uuid(const std::string& uuid);
    const Component* find_by_uuid(const std::string& uuid) const;

    // ----- 注释（07-01 Note 图元的简化文本占位）-----
    void add_note(std::string note);
    const std::vector<std::string>& notes() const { return notes_; }
    bool remove_note(std::size_t index);

    // ----- 网表生成（04）-----
    // 遍历 components/wires/net_labels/power_ports 构建 Union-Find 等价类，输出 Net 列表。
    // 跳过无 Symbol 的 Component（has_symbol()==false，无法计算连接点）。
    Netlist generate_netlist() const;

    // ----- 测试专用：注入固定 UUID（生产代码勿用）-----
    void assign_uuid_for_test(const std::string& u) { uuid_ = u; }

    // ----- Resource 序列化接口（.sch.toml）-----
    std::string serialize() const override;
    bool deserialize(const std::string& text) override;
    static Ref<Schematic> parse(const std::string& text);

private:
    std::string uuid_;
    std::string name_;

    std::vector<Component> components_;
    std::vector<Wire> wires_;
    std::vector<NetLabel> net_labels_;
    std::vector<PowerPort> power_ports_;
    std::vector<std::string> notes_;
};

}  // namespace eda::sch
