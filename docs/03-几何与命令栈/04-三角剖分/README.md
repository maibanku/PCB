---
title: 03-04 三角剖分
parent: 03-几何与命令栈
plan: P3
status: 待实现
tags: [EDA, P3, 三角剖分]
related: ["[[03-几何与命令栈]]"]
---

# 03-04 三角剖分

> 所属 [[03-几何与命令栈]]。包装 earcut（耳切算法）对多边形进行三角剖分，为铺铜填充、GPU 渲染、Gerber 输出提供三角形流。对应 `geometry-几何内核设计.md` §4 triangulate.h。

## 目标（要实现什么）
- 实现 `eda/geometry/triangulate.{h,cpp}`：`triangulate(Polygon) -> vector<uint32_t>`（索引三元组）
- 适配 earcut 输入格式：`outer + holes` 平铺成 `vector<vector<Point>>`，外环在前、孔依次
- 顶点输出：剖分同时返回去重的顶点列表（`points`）+ 索引（`indices`），可直接喂 `RenderingServer`
- 面积守恒校验：`Σ |triangle_area| == |area(Polygon)|`，作为单测不变量
- 含孔多边形正确处理：earcut 原生支持多环，洞边界自动连接
- 弧输入前置：含弧的 Polygon 先经 [[03-几何与命令栈/07-圆弧离散化]] 离散

## 关键点
- **D4 决策已定**：选 earcut（mapbox/earcut.hpp，header-only，C++11 兼容），不保证 Delaunay（铺铜渲染够用）
- **不保证 Delaunay**：铺铜填充、GPU 渲染对网格质量要求低；若未来 SI/PI 需要更优网格再评估（YAGNI 原则）
- **索引输出**：避免每三角形复制顶点，节省内存与上传带宽（万焊盘场景关键）
- **薄包装原则**：业务层只调 `eda::geometry::triangulate`，不直 include earcut.hpp

## 输入 / 输出
- **入**：`Polygon`（可能含孔，nm 坐标）
- **出**：三角形索引列表 + 顶点列表（送渲染或 Gerber 填充）

## 依赖
- **上游**：[[03-几何与命令栈/01-图元与坐标]]（Polygon 类型）、[[03-几何与命令栈/07-圆弧离散化]]（弧输入前置）
- **下游**：[[01-引擎地基/05-2D画布]]（铺铜 GPU 填充）、P8 PCB 铺铜渲染、P13 Gerber/IPC-2581 多边形填充输出

## 状态
**待实现**。进入 Plan P3 时，经 brainstorming → writing-plans 细化为可执行任务。
