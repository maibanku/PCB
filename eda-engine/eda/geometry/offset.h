#pragma once

// eda/geometry/offset.h
//
// P3 几何内核 · Task 4：偏移（Clipper2 ClipperOffset 薄包装）。
//
// 对应 plan「几何与命令栈实现计划.md」Task 4 与子功能 README「03-偏移」。
//
// 包装 Clipper2 ClipperOffset 提供多边形 / 路径轮廓偏移：
//   - inflate(Polygon, delta)        外扩（外环外推 + 孔内收 → 材料增加）
//   - shrink(Polygon, delta)         内缩（外环内收 + 孔外扩 → 材料减少）
//   - offset_polygon(Polygon, delta) 通用偏移原语（delta 符号决定方向）
//   - offset(Polyline, delta, ...)   线条偏移（courtyard 生成、走线外膨）
//   - courtyard(Polygon, gap)        器件 courtyard（外轮廓 + 间距，C1/C3 语义）
//   - path_to_polygon(Path)          走线中心线 + 宽度 → 多边形（PCB 走线渲染 / DRC）
//
// 薄包装原则（D4）：业务层只 include 本头，**永不直接 include clipper2.h**。
// 所有 Clipper2 类型（Path64 / PolyTree64 / ClipperOffset …）仅出现在 offset.cpp
// 内部，业务层只见 eda::geometry::* 自有类型。底层实现可替换而不影响业务。
//
// 含孔语义：外环与孔在 ClipperOffset 中按方向自动处理——
//   外环 CCW + delta > 0 → 外推；孔 CW + delta > 0 → 内缩（孔变小）。
//   即 inflate 一键同时外推外环、收缩孔；shrink 反之（外环收缩、孔扩张）。
//   即便调用方传入方向不一致的环，实现内部会按 Area 符号强制校正。
//
// 锐角限幅：miter_limit（默认 2.0）限制 miter 连接的最大臂长 = miter_limit × |delta|，
// 避免尖角处 miter 顶点飞出过远。圆角连接的离散精度由 arc_tolerance 控制。
//
// 决策对齐：D1（nm 整数内部坐标，零精度损失偏移）、D4（Clipper2 薄包装）。

#include "eda/geometry/types.h"

#include <vector>

namespace eda::geometry {

// 拐角连接类型（相邻偏移段的拐角过渡方式）。
enum class JoinType {
    Miter,  // 直角延伸（默认；锐角受 miter_limit 限幅防飞出）
    Round,  // 圆弧过渡（用 arc_tolerance 控制离散弦高）
    Square, // 方角截断（45° 切角）
};

// 端点类型（仅影响 Polyline 开路径的端部成型；Polygon 偏移恒用 Polygon）。
enum class EndType {
    Polygon, // 闭合多边形（首尾相连；Polygon 偏移专用）
    Butt,    // 平头（端部沿法线垂直截断）
    Square,  // 方头（端部外延半个 delta）
    Round,   // 圆头（端部半圆，PCB 走线 / courtyard 标准）
};

// 偏移选项（仅多边形偏移使用；线条偏移直接在签名里传 JoinType / EndType）。
struct OffsetOptions {
    JoinType join_type = JoinType::Miter;  // 拐角连接（默认 miter）
    double miter_limit = 2.0;               // miter 限幅系数（>=1；越大越尖）
    double arc_tolerance_mm = 0.0;          // 圆弧离散弦高（mm；<=0 让 Clipper2 自适应）
};

// 通用多边形偏移：delta > 0 外膨、delta < 0 内缩。
// 含孔自动处理（外环与孔按方向各自偏移；结果自动清理自交与窄通道塌缩）。
// 结果可能为 0、1 或多个独立 Polygon（自交 / 多块分裂后由 Clipper2 清理）。
// delta == 0 或外环 < 3 点时原样返回（空外环返回空向量）。
std::vector<Polygon> offset_polygon(const Polygon& polygon, Coord delta_nm,
                                    const OffsetOptions& opts = {});

// 外扩：传入正 delta_nm（>0），外环外推、孔内收（材料增加）。
// delta_nm <= 0 时按 0 处理（原样返回副本）。
std::vector<Polygon> inflate(const Polygon& polygon, Coord delta_nm,
                             const OffsetOptions& opts = {});

// 内缩：传入正 delta_nm（>0），外环内收、孔外扩（材料减少）。
// delta_nm <= 0 时按 0 处理（原样返回副本）。
// 完全塌缩（如 2mm 方形内缩 1mm）返回空向量。
std::vector<Polygon> shrink(const Polygon& polygon, Coord delta_nm,
                            const OffsetOptions& opts = {});

// 线条偏移（开路径）：返回偏移生成的闭合 Polyline 边界（每条为闭合环）。
// delta_nm > 0 两侧外膨 + 端部按 end_type 成型（courtyard 生成、走线外膨）。
// delta_nm < 0 两侧内缩。少于 2 点或 delta == 0 时返回空。
//
//   join_type  拐角过渡（默认 Round，EDA 通用）
//   end_type   端部成型（默认 Round，器件 courtyard / PCB 走线标准）
std::vector<Polyline> offset(const Polyline& polyline, Coord delta_nm,
                             JoinType join_type = JoinType::Round,
                             EndType end_type = EndType::Round);

// 器件 courtyard：device 外轮廓外加 gap_nm 间距（C1/C3 语义：器件占用区）。
// 本质是 inflate(device, gap_nm) 的语义化别名，便于调用方表达意图。
std::vector<Polygon> courtyard(const Polygon& device_outline, Coord gap_nm,
                               const OffsetOptions& opts = {});

// Path（走线中心线 + width）→ 多边形：两侧各偏移 width/2，圆角连接 + 圆头端点。
// PCB 走线渲染 / DRC 输入：避免每条走线存完整多边形（plan D2：Path 只存中心线 + 宽）。
// 中心线 < 2 点或 width <= 0 时返回空。
std::vector<Polygon> path_to_polygon(const Path& path,
                                     const OffsetOptions& opts = {});

}  // namespace eda::geometry
