---
title: 03-01 图元与坐标
parent: 03-几何与命令栈
plan: P3
status: 待实现
tags: [EDA, P3, 图元与坐标]
related: ["[[03-几何与命令栈]]"]
---

# 03-01 图元与坐标

> 所属 [[03-几何与命令栈]]。定义值类型图元 Point/Segment/Arc/Polyline/Polygon/Path/Box，采用 `int64`/nm 定点坐标并提供 mm/mil/inch 单位转换，作为几何内核的数据根基。对应 `geometry-几何内核设计.md` §3 数据模型。

## 目标（要实现什么）
- 定义 `eda/geometry/types.h`：`Coord = int64_t`（内部单位 nm）；`Point`/`PointF`（整数 / 浮点）
- 值类型图元集合：`Segment`、`Arc`（center + radius + 起止角 + ccw）、`Polyline`（开路径）、`Polygon`（outer + holes）、`Path`（中心线 + width）、`Box`（AABB）
- 多边形含孔约定：外环 CCW、孔 CW；`Polygon` 持有 `outer` + `std::vector<std::vector<Point>> holes`
- 单位转换 `units.h`：`mm_to_nm` / `mil_to_nm` / `inch_to_nm` 及反函数，全部 `constexpr`，边界处转换、内部全用 nm
- `Box` 接口：`from_points(span)` / `intersects(Box)` / `contains(Point)` / `merge(Box)`
- `snap.h` 网格吸附：`snap_to_grid(Point, grid_nm)` / `snap_point(目标, 容差)` / `snap_angle(角度, 步进)`（对应功能目录 B2「网格与吸附」、路线图 P3 范围）
- `transform.h` 几何变换：对 Point/Polyline/Polygon/Arc 的 `translate` / `rotate` / `mirror` / `scale`（header-only，复用 `core/math` 的 `Transform2D`，对应几何设计笔记 §4）
- 自由函数风格 + POD 结构（不搞深继承）；提供 `operator==` / 哈希

## 关键点
- **D1 决策已定**：内部 `int64_t/nm`（参考 KiCad；Clipper2 原生 int64，布尔零精度损失；nm 量程 ±9.2 m + 0.001μm 分辨率覆盖任何板尺寸）
- **D2 决策已定**：值类型 + 自由函数（缓存友好、序列化简单、万级图元 SOA 友好）
- **D3 决策已定**：圆弧保留参数（不预离散），仅在布尔/求交前调 `tessellate`
- **走线用 `Path` 而非 `Polygon`**：避免每条走线都存完整多边形，需要时由 `offset` 生成轮廓
- 业务层永远 `#include "eda/geometry/types.h"`，不直 include 第三方

## 输入 / 输出
- **入**：上层业务（原理图/PCB）传入用户层坐标（mm/mil）+ 图元参数
- **出**：布尔/偏移/剖分/索引/求交/测量模块统一消费的值类型图元

## 依赖
- **上游**：`core/math`（Vector2/Transform2D 复用）
- **下游**：本域全部其他子功能（02~07）以这些类型为输入输出；`snap`/`transform` 被 P7/P8 编辑器消费（光标吸附、移动/旋转/镜像器件）；P7/P8 业务直接持有这些图元

## 状态
**待实现**。进入 Plan P3 时，经 brainstorming → writing-plans 细化为可执行任务。
