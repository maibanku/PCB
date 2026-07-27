#pragma once

// eda/geometry/types.h
//
// P3 几何内核 · 图元值类型层（Task 1）
//
// 内部坐标统一为 int64_t（别名 Coord），单位 nm（纳毫米）——见 D1。
// 浮点坐标（PointF）仅用于显示、测量中间量、圆弧离散化中间量，
// 永不参与布尔/求交/索引（避免万级图元累积浮点误差）。
//
// 全部为 POD 值类型 + 自由函数（D2），缓存友好、序列化简单、SOA 友好。

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace eda::geometry {

// 内部坐标类型，单位 nm（D1）。量程 ±9.2×10⁹ nm ≈ ±9.2 m，分辨率 0.001μm。
using Coord = int64_t;

// 整数点：参与布尔/索引/求交的所有运算。
struct Point {
    Coord x = 0, y = 0;
    bool operator==(const Point&) const = default;
};

// 浮点点：仅用于显示、测量中间量、圆弧离散化中间量（D1）。
struct PointF {
    double x = 0, y = 0;
};

// 线段。
struct Segment {
    Point a, b;
};

// 圆弧（D3）：参数化表示——中心 + 半径 + 起止角（弧度）+ 方向。
// 布尔/求交前由 arc_tessellate 离散为 Polyline；不预离散以保留精确参数。
struct Arc {
    Point center{};
    Coord radius = 0;
    double start_angle = 0.0;  // 弧度
    double end_angle = 0.0;    // 弧度
    bool ccw = true;           // 逆时针为正
};

// 折线（开路径：导线、走线轮廓、未闭合板框段）。至少 2 点。
struct Polyline {
    std::vector<Point> points;
};

// 多边形（可含孔：外环 + 内环列表）。环方向约定：外环 CCW，孔 CW。
struct Polygon {
    std::vector<Point> outer;
    std::vector<std::vector<Point>> holes;
};

// 带宽路径（走线本质：中心线 + 宽度）。渲染/DRC 时偏移成 Polygon。
struct Path {
    std::vector<Point> points;
    Coord width = 0;
};

// 轴对齐包围盒（空间索引用）。
struct Box {
    Point min{}, max{};

    // 从点集构造 AABB。空输入返回默认 Box（min==max=={0,0}）。
    static Box from_points(std::span<const Point> pts) {
        Box b;
        if (pts.empty()) return b;
        b.min = b.max = pts[0];
        for (const auto& p : pts) {
            b.min.x = std::min(b.min.x, p.x);
            b.min.y = std::min(b.min.y, p.y);
            b.max.x = std::max(b.max.x, p.x);
            b.max.y = std::max(b.max.y, p.y);
        }
        return b;
    }

    // 两个 AABB 是否相交（闭区间判定）。
    bool intersects(const Box& o) const {
        return !(o.min.x > max.x || o.max.x < min.x ||
                 o.min.y > max.y || o.max.y < min.y);
    }

    // 是否包含点 p（左闭右开：含下界、不含上界，便于无缝拼接）。
    bool contains(Point p) const {
        return p.x >= min.x && p.x < max.x &&
               p.y >= min.y && p.y < max.y;
    }

    // 返回两个 AABB 的并集包围盒。
    Box merge(const Box& o) const {
        return Box{
            {std::min(min.x, o.min.x), std::min(min.y, o.min.y)},
            {std::max(max.x, o.max.x), std::max(max.y, o.max.y)}};
    }
};

}  // namespace eda::geometry
