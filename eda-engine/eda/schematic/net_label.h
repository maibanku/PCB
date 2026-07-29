#pragma once

// eda/schematic/net_label.h
//
// P7 原理图核心数据模型 · 网络标号 NetLabel（07-01 原理图图元）。
//
// 对应 docs/07-原理图编辑器/01-原理图图元 与 04-网络表生成。
//
// 数据模型：
//   NetLabel 是聚合值类型：网络名 + 位置 + 方向（水平/垂直）。
//   - 位置（Point）与导线端点/引脚连接点重合时，整个连通分量继承此网络名；
//   - 同一连通分量上有多个 NetLabel 时，首版策略：取首个（生产可改为冲突报告，ERC 05）；
//   - 跨页网络通过同名 NetLabel 串联（层次化设计 03 由后续 Task 接入）。
//
// 方向仅影响渲染朝向（水平/垂直摆放），不参与电气连通性。
//
// 与 P3 geometry 的关系：位置用 geometry::Point（int64 nm）。
//
// 命名规范：namespace eda::sch、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include "eda/geometry/types.h"

#include <string>
#include <string_view>

namespace eda::sch {

// ============================================================================
// 枚举：标号方向（渲染朝向，不影响电气）
// ============================================================================
enum class LabelDirection {
    HORIZONTAL,  // 水平摆放
    VERTICAL,    // 垂直摆放
};

std::string_view to_string_view(LabelDirection d);
LabelDirection parse_label_direction(std::string_view s);

// ============================================================================
// NetLabel —— 网络标号（聚合值类型）
// ============================================================================
class NetLabel {
public:
    NetLabel() = default;
    NetLabel(std::string net_name, geometry::Point position,
             LabelDirection direction = LabelDirection::HORIZONTAL)
        : net_name_(std::move(net_name)),
          position_(position),
          direction_(direction) {}

    const std::string& net_name() const { return net_name_; }
    void set_net_name(std::string n) { net_name_ = std::move(n); }
    bool has_net_name() const { return !net_name_.empty(); }

    geometry::Point position() const { return position_; }
    void set_position(geometry::Point p) { position_ = p; }

    LabelDirection direction() const { return direction_; }
    void set_direction(LabelDirection d) { direction_ = d; }

    // 值语义：默认 ==/!=
    bool operator==(const NetLabel&) const = default;

private:
    std::string net_name_;
    geometry::Point position_{};
    LabelDirection direction_ = LabelDirection::HORIZONTAL;
};

}  // namespace eda::sch
