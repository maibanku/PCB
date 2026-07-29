#pragma once

// eda/schematic/power_port.h
//
// P7 原理图核心数据模型 · 电源/地端口 PowerPort（07-01 原理图图元）。
//
// 对应 docs/07-原理图编辑器/01-原理图图元 与 04-网络表生成。
//
// 数据模型：
//   PowerPort 是聚合值类型：网络名（VCC/GND/+3V3/AGND/...）+ 位置 + 种类（电源/地/其它）。
//   - 与 NetLabel 同属"显式命名节点"：位置与导线端点/引脚连接点重合时，整个连通分量
//     继承此网络名（且 is_power=true 标记进入网表，驱动 PCB 网络类分级 08-03）；
//   - 与 NetLabel 的差异：PowerPort 在网表生成中具有"高优先级命名"——同名同网络冲突时
//     PowerPort 名胜出（避免 NetLabel 偶然占用 VCC/GND 名）。
//
// 与 P3 geometry 的关系：位置用 geometry::Point（int64 nm）。
//
// 命名规范：namespace eda::sch、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include "eda/geometry/types.h"

#include <string>
#include <string_view>

namespace eda::sch {

// ============================================================================
// 枚举：电源端口种类（驱动 PCB 网络类分级 08-03 / ERC 检查 07-05）
// ============================================================================
enum class PowerPortKind {
    POWER,   // 电源（VCC / VDD / +3V3 / +5V / +12V / VBATT / ...）
    GROUND,  // 地（GND / AGND / DGND / PGND / ...）
    OTHER,   // 其它显式命名端口（如基准电压 VREF）
};

std::string_view to_string_view(PowerPortKind k);
PowerPortKind parse_power_port_kind(std::string_view s);

// ============================================================================
// PowerPort —— 电源/地符号（聚合值类型）
// ============================================================================
class PowerPort {
public:
    PowerPort() = default;
    PowerPort(std::string net_name, geometry::Point position,
              PowerPortKind kind = PowerPortKind::POWER)
        : net_name_(std::move(net_name)),
          position_(position),
          kind_(kind) {}

    const std::string& net_name() const { return net_name_; }
    void set_net_name(std::string n) { net_name_ = std::move(n); }
    bool has_net_name() const { return !net_name_.empty(); }

    geometry::Point position() const { return position_; }
    void set_position(geometry::Point p) { position_ = p; }

    PowerPortKind kind() const { return kind_; }
    void set_kind(PowerPortKind k) { kind_ = k; }

    // 值语义：默认 ==/!=
    bool operator==(const PowerPort&) const = default;

private:
    std::string net_name_;
    geometry::Point position_{};
    PowerPortKind kind_ = PowerPortKind::POWER;
};

}  // namespace eda::sch
