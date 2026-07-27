---
title: 03-05 空间索引
lang: zh-CN
parent: 03-几何与命令栈
plan: P3
status: 待实现
tags: [EDA, P3, 空间索引]
related: ["[[03-几何与命令栈]]"]
---

# 03-05 空间索引

> 所属 [[03-几何与命令栈]]。自研轻量 R-tree，支持 STR（Sort-Tile-Recursive）bulk-load 批量构建与邻域/包围盒查询，服务于拾取、碰撞检测、DRC 邻域检索等高性能场景。对应 `geometry-几何内核设计.md` §4 index.h。

## 目标（要实现什么）
- 实现 `eda/geometry/index.{h,cpp}`：模板 `RTree<T>`，叶子存 `Box + T`（如图元 id 或 AABB 句柄）
- STR bulk-load：从已排序的 AABB 列表一次性建树（万级图元建图比逐插入快数倍），用于板加载后初始化
- 增量更新：`insert(Box, T)` / `remove(Box, T)`，支持走线/铺铜编辑后维护
- 查询 API：`query(Box) -> vector<T>`（范围查询）、`nearest(Point, k) -> vector<T>`（k 近邻）、`contains(Point) -> vector<T>`
- 不变量校验：与暴力查询结果一致（交叉验证单测的核心断言）
- 性能基准：10k 图元邻域查询时延达标（具体阈值在 P22 基准里固化）

## 关键点
- **D5 决策已定**：自研 R-tree（避免 Boost.Geometry 重依赖，符合自研引擎精神）
- **STR bulk-load 优先**：EDA 板载入即批量构建；编辑期少量 insert/remove
- **AABB + id 解耦**：R-tree 不持有图元本体，只持有 `Box` 与业务层 id，便于跨模块共享
- **风险**：自研正确性风险 → 充分单测 + 与暴力 O(n²) 结果交叉验证；万级图元查询性能纳入 P22 基准

## 输入 / 输出
- **入**：图元 AABB 集合（建树）/ 单个 AABB（增删）/ 查询 Box 或 Point
- **出**：候选图元 id 列表（小集合，交由精确求交/测量进一步过滤）

## 依赖
- **上游**：[[03-几何与命令栈/01-图元与坐标]]（Box 类型）
- **下游**：[[03-几何与命令栈/06-求交与测量]]（精确距离/交点的预筛选）；P11 DRC 邻域检索（核心消费方）、P7/P8 拾取与视图剔除

## 状态
**待实现**。进入 Plan P3 时，经 brainstorming → writing-plans 细化为可执行任务。
