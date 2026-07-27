---
title: 13-07 BOM输出
parent: 13-制造输出
plan: P13
status: 待实现
tags: [EDA, P13, BOM输出]
related: ["[[13-制造输出]]"]
---

# 13-07 BOM输出

> 所属 [[13-制造输出]]。输出 CSV/Excel 物料清单（BOM），含位号、数量、参数、供应链属性，支持多变体、合规标签与替代料，直供采购/SMT 厂。

## 目标（要实现什么）
- 输出 CSV / Excel（.xlsx）/ XML 多种格式，列：Designator、Qty、Value、Footprint、Manufacturer、MPN、Description
- 按列分组聚合：同 MPN/同 Value+Footprint 合并为一行，位号列表用逗号分隔
- **供应链属性**（与 E2 联动）：库存、价格、交期、供应商、替代料、生命周期（NRND/EOL/Active）
- 装配变体（assembly variant）：同一板多 SKU（如基础版/高配版），按变体筛选输出
- 替代料（alternative parts）：主 MPN + 多个 pin 兼容替代料，便于采购决策
- 合规标记：RoHS、REACH、无铅（Pb-Free）、冲突矿物（Conflict Minerals）声明
- 输出 BOM 校验：DNF（Do Not Fit）器件可选保留并标记、重复 MPN、缺失 MPN 警告
- 与 PnP（[[03-PickAndPlace坐标]]）一致性：装配器件位号集合 == PnP 坐标集合

## 关键点
- BOM 是供应链的入口：从设计阶段就带 MPN（设计即采购），避免到采购时才发现料不齐
- 装配变体：消费类电子常见（同一主板不同 SKU），需明确按变体输出
- "Cold BOM"（仅 MPN）vs "Hot BOM"（含库存/价格/替代），前者是设计输出，后者是采购输入
- 替代料：需要在器件库中维护"pin-compatible"关系（如 LM324 多家替代），E2 供应链集成提供
- 合规：进入欧盟/美国市场的 RoHS/REACH 是强制要求，冲突矿物（DRC）需声明
- 与 [[03-PickAndPlace坐标]] 强一致性：BOM 行数（按 MPN 聚合后）vs PnP 坐标数，需互相校验

## 输入 / 输出
- **入**：原理图/PCB 器件清单（C2/C3）、装配变体、E2 供应链数据、合规库
- **出**：BOM CSV/Excel、变体对比表、合规报告、与 PnP 一致性报告

## 依赖
- **上游**：C2 原理图、C3 PCB、E2 供应链集成（库存/价格/MPN）
- **下游**：[[03-PickAndPlace坐标]]（一致性）、采购/SMT 厂、供应链下单（E2）

## 状态
**待实现**。进入 Plan P13 时，经 brainstorming → writing-plans 细化为可执行任务。
