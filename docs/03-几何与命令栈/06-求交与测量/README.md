---
title: 03-06 求交与测量
lang: zh-CN
parent: 03-几何与命令栈
plan: P3
status: 待实现
tags: [EDA, P3, 求交与测量]
related: ["[[03-几何与命令栈]]"]
---

# 03-06 求交与测量

> 所属 [[03-几何与命令栈]]。实现线-线/线-弧/弧-弧精确求交与自交检测，以及长度/面积/距离等几何测量，是 DRC 间距、布线等长匹配、连通性判定的计算基础。对应 `geometry-几何内核设计.md` §4 intersect.h / measure.h。

## 目标（要实现什么）
- 实现 `eda/geometry/intersect.{h,cpp}`：`segment_segment(a,b)`、`segment_arc(s,arc)`、`arc_arc(a,b)`、`polyline_self(p)`（Bentley-Ottmann 扫描线）
- 求交返回 `vector<Point>`（或多段参数 t），覆盖共线/重合/相切等退化情形
- 实现 `eda/geometry/measure.{h,cpp}`：`length(Polyline)`、`area(Polygon)`（含孔，外环面积减孔）、`distance(a,b)`、`point_to_segment(p,s)`
- 弧长/扇形面积：参数方程积分（`arc_length = r * Δθ`）；端点闭合校验
- 退化情形明确：平行线无交点、共线无交点或无穷多（返回段）、相切返回单点
- 与 R-tree 协同：上层先用 [[03-几何与命令栈/05-空间索引]] 预筛选 AABB，再调本模块精确计算

## 关键点
- **混合整数/浮点**：求交/距离用 `PointF`（double）中间量计算，结果点用 `Point`（nm）四舍五入返回（避免精度溢出）
- **弧的统一前置处理**：弧求交可选两条路径——直接参数方程求根（精确）或离散化后线-线（与布尔一致）；首版选离散化路径降低复杂度
- **EDA 语义对齐**：`distance` 直接对应 DRC 间距规则、`length` 直接对应蛇形等长匹配、`intersect` 直接对应连通性
- **解析公式优先**：避免无谓数值方法，保证可重复结果

## 输入 / 输出
- **入**：图元对（Segment/Arc/Polyline/Polygon）或点-图元
- **出**：交点列表 / 标量（长度 nm、面积 nm²、距离 nm）

## 依赖
- **上游**：[[03-几何与命令栈/01-图元与坐标]]、[[03-几何与命令栈/07-圆弧离散化]]（弧输入前置）、[[03-几何与命令栈/05-空间索引]]（预筛选）
- **下游**：P11 DRC 间距/短路/连通性、P8 PCB 走线长度匹配、P7 ERC 网络连通性

## 状态
**待实现**。进入 Plan P3 时，经 brainstorming → writing-plans 细化为可执行任务。
