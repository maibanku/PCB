---
title: 03-02 布尔运算
parent: 03-几何与命令栈
plan: P3
status: 待实现
tags: [EDA, P3, 布尔运算]
related: ["[[03-几何与命令栈]]"]
---

# 03-02 布尔运算

> 所属 [[03-几何与命令栈]]。包装 Clipper2（Vatti 扫描线）提供多边形布尔运算 `unite`/`subtract`/`intersect`/`xor`，服务于铺铜合并、挖空避让、板框叠加、拼板等场景。对应 `geometry-几何内核设计.md` §4 boolean.h。

## 目标（要实现什么）
- 实现 `eda/geometry/boolean.{h,cpp}`：`unite(span<Polygon>)`、`subtract(a, span<Polygon> b)`、`intersect(a, b)`、`xor(a, b)`，均返回 `std::vector<Polygon>`
- 输入输出统一为 nm 整数坐标，零精度损失（Clipper2 原生 int64）
- 支持 Clipper2 的 `ClipType` 与 `FillRule`（EvenOdd / NonZero / Positive / Negative）透传
- 含孔多边形正确处理：外环与内环分别送 Clipper2，结果自动保留孔结构
- **短路检测**：`intersects(a, b) -> bool`，结果非空即相交（DRC 短路检查高速路径）
- 大型铺铜分块接口预留：`unite_with_progress(span, on_chunk)` 便于增量重建（首版可不实现）

## 关键点
- **薄包装原则**：业务层只调 `eda::geometry::boolean::*`，永不直 include Clipper2；底层可换实现
- **D4 决策已定**：选 Clipper2（不是自研），鲁棒性靠 Clipper2 保证
- **弧→折线前置**：含弧的输入先经 [[03-几何与命令栈/07-圆弧离散化]] 离散成 Polyline 再送 Clipper2
- **风险**：大型铺铜（万级顶点）扫描线可能慢 → 长远走分块 + 增量重建（P8 接入时验证）

## 输入 / 输出
- **入**：`Polygon`/`Polyline` 列表（nm 坐标）
- **出**：布尔结果 `Polygon` 列表（可能多环含孔）

## 依赖
- **上游**：[[03-几何与命令���/01-图元与坐标]]（类型）、[[03-几何与命令栈/07-圆弧离散化]]（弧输入前置）
- **下游**：[[03-几何与命令栈/04-三角剖分]]（结果送剖分）；P8 PCB 铺铜生成、板框叠加；P11 DRC 短路检查；P13 拼板

## 状态
**待实现**。进入 Plan P3 时，经 brainstorming → writing-plans 细化为可执行任务。
