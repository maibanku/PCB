#pragma once

// eda/schematic/wire.h
//
// P7 原理图核心数据模型 · 导线 Wire（07-01 原理图图元）。
//
// 对应 docs/07-原理图编辑器/01-原理图图元 与 04-网络表生成。
//
// 数据模型：
//   Wire 是聚合值类型（POD-like）：两点 + 线宽 + 可选网络 ref。
//   - 端点重合即视为同网络节点（网表生成的连通性基石）；
//   - 线宽仅作视觉/工艺参考，不参与连通性判定（首版：仅端点连通，不含 T 型/十字中点穿透；
//     多段折线由调用方拆为多段两点 Wire 或未来升级为 Polyline Wire）；
//   - net_ref 可选：若给定，所有与此导线端点重合的引脚/标号都归属该网络（命名提示）；
//     若未给定，网络名由 NetLabel/PowerPort 决定，缺失时自动命名（Net0001）。
//
// 与 P3 geometry 的关系：
//   - 端点用 geometry::Point（int64 nm）；
//   - 线宽用 geometry::Coord（int64 nm），默认 152'400nm = 6mil（典型原理图线宽）。
//
// 命名规范：namespace eda::sch、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include "eda/geometry/types.h"

#include <string>

namespace eda::sch {

// ============================================================================
// Wire —— 两点导线（聚合值类型）
// ============================================================================
class Wire {
public:
    Wire() = default;
    Wire(geometry::Point a, geometry::Point b) : a_(a), b_(b) {}
    Wire(geometry::Point a, geometry::Point b, geometry::Coord width_nm)
        : a_(a), b_(b), width_nm_(width_nm) {}

    geometry::Point a() const { return a_; }
    geometry::Point b() const { return b_; }
    void set_a(geometry::Point p) { a_ = p; }
    void set_b(geometry::Point p) { b_ = p; }
    // 同时设置两端点。
    void set_endpoints(geometry::Point a, geometry::Point b) { a_ = a; b_ = b; }

    geometry::Coord width_nm() const { return width_nm_; }
    void set_width_nm(geometry::Coord w) { width_nm_ = w; }

    // 可选网络绑定（命名提示）；空表示由连通性推导。
    const std::string& net_ref() const { return net_ref_; }
    void set_net_ref(std::string n) { net_ref_ = std::move(n); }
    bool has_net_ref() const { return !net_ref_.empty(); }

    // 端点是否重合（零长度导线，连通性无意义；ERC 检查时报告）。
    bool is_degenerate() const { return a_ == b_; }

    // 是否经过点 p（仅端点判定：含 p 当且仅当 a==p 或 b==p）。
    bool has_endpoint(geometry::Point p) const { return a_ == p || b_ == p; }

    // 值语义：默认 ==/!=
    bool operator==(const Wire&) const = default;

private:
    geometry::Point a_{};
    geometry::Point b_{};
    geometry::Coord width_nm_ = 152'400;  // 默认 6mil（≈0.15mm）
    std::string net_ref_;                 // 可选网络名提示
};

}  // namespace eda::sch
