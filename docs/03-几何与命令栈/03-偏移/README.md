---
title: 03-03 偏移
lang: zh-CN
parent: 03-几何与命令栈
plan: P3
status: 待实现
tags: [EDA, P3, 偏移]
related: ["[[03-几何与命令栈]]"]
---

# 03-03 偏移

> 所属 [[03-几何与命令栈]]。包装 Clipper2 `ClipperOffset` 实现多边形轮廓偏移 `inflate`/`shrink`/`courtyard`，用于铺铜收缩避让、间距扩张、courtyard 边界生成、走线宽度展开。对应 `geometry-几何内核设计.md` §4 offset.h。

## 目标（要实现什么）
- 实现 `eda/geometry/offset.{h,cpp}`：`offset(Polygon, delta_nm, opts) -> vector<Polygon>`，delta 正为外膨、负为内缩
- 连接类型 `JoinType`：`miter`（锐角）/ `round`（圆弧过渡）/ `square`（方角）；端点类型 `EndType`：`polygon` / `butt` / `square` / `round`
- `inflate(delta)` / `shrink(delta)` 语义化包装；`courtyard(pad, gap_nm)` 一键生成器件 courtyard
- `Path` 偏移：走线中心线 + width → 两侧 offset 生成 `Polygon` 轮廓（PCB 走线渲染/DRC 输入）
- 锐角限幅：`miter_limit`（默认 2.0，避免尖角飞出）；自交清理用 Clipper2 内置选项
- 含孔处理：外环膨胀 + 孔收缩（保持正确的"内/外"语义）

## 关键点
- **薄包装原则**：业务层只调 `eda::geometry::offset::*`，不直 include Clipper2
- **D4 决策已定**：偏移也走 Clipper2 `ClipperOffset`（与布尔同库，统一维护）
- **EDA 语义对齐**：`courtyard` 直接对应器件占用区（C1/C3），`shrink` 直接对应铺铜避让（C3）
- **风险**：自交 / 窄通道塌缩 → 依赖 Clipper2 清理；P3 单测覆盖矩形膨胀、含孔收缩、锐角 miter 限幅

## 输入 / 输出
- **入**：`Polygon`/`Polyline`/`Path` + delta（nm）+ 选项
- **出**：偏移结果 `Polygon` 列表（可能多环）

## 依赖
- **上游**：[[03-几何与命令栈/01-图元与坐标]]（类型）、Clipper2 ClipperOffset
- **下游**：P6 器件 courtyard（C1）、P8 PCB 走线宽度展开与铺铜避让（C3）、P11 DRC 间距分析

## 状态
**待实现**。进入 Plan P3 时，经 brainstorming → writing-plans 细化为可执行任务。
