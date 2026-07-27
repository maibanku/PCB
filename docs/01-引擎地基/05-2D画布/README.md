---
title: 01-05 2D画布
parent: 01-引擎地基
plan: P1
status: 待实现
tags: [EDA, P1, 2D画布]
related: ["[[01-引擎地基]]"]
---

# 01-05 2D画布

> 所属 [[01-引擎地基]]。`Canvas2D` 立即模式画布，提供矢量图元 API 并在 CPU 侧栅格化为三角形顶点流，是 EDA 原理图/PCB 视图的渲染基础。对齐 Godot `CanvasItem` 体系（首版不含节点树，仅核心绘制 API）。

## 目标（要实现什么）
- 实现 `scene/2d/canvas_2d.{h,cpp}`，提供立即模式 API：`draw_line(p0,p1,color,width)` / `draw_rect(rect,color,filled)` / `draw_polygon(points,color)` / `draw_polyline(points,color,width,closed)` / `draw_circle(center,radius,color,segments)` / `draw_arc(...)`
- 线宽处理：CPU 侧把粗线膨胀为三角形带（miter/round 端点，简化版）；填充多边形首版要求凸（凹多边形走 P3 三角剖分）
- 视图变换：`set_camera_transform(Transform2D)`（pan/zoom 由 main 更新）；所有绘制经此矩阵变换
- 批合并：相同状态（颜色、blend、线宽展开规则）的连续图元合并为一次 `RenderingServer::draw_triangles`
- 网格绘制 API（EDA 专属）：`draw_grid(extent, grid_nm, major_every, color_minor, color_major, origin_color)`
- 文本绘制 API 预留（首版 stub，字体系统在后续 plan）

## 关键点
- **立即模式**：上层每帧重画整图，简单可靠；万级图元下后续再切"保留模式 + 脏区刷新"（B2 增量渲染目标）
- **图元→三角形**：Canvas2D 只懂图元几何与 CPU 栅格化，不懂 GPU；输出顶点流给 `RenderingServer`
- **LOD 雏形**：zoom 越小，圆/弧段数自动降低（首版可固定段数，预留 API `set_lod_bias`）
- **对齐 Godot CanvasItem**：未来作为 Node 化的"图元节点"基类（P2 节点树就绪后）

## 输入 / 输出
- **入**：[[01-引擎地基/06-主循环]] 与编辑器的绘制请求；`InputState` 的 pan/zoom（经 camera transform）
- **出**：每帧顶点流送 [[01-引擎地基/02-渲染服务器抽象]]

## 依赖
- **上游**：[[01-引擎地基/02-渲染服务器抽象]]（draw API）、`core/math`（Vector2/Transform2D/Color）
- **下游**：[[01-引擎地基/06-主循环]]（首版 EDA 风格 Demo 渲染）、后续原理图/PCB 编辑器所有视图

## 状态
**待实现**。进入 Plan P1 时，经 brainstorming → writing-plans 细化为可执行任务。
