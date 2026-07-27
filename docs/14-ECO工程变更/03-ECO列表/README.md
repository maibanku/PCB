---
title: 14-03 ECO列表
lang: zh-CN
parent: 14-ECO工程变更
plan: P14
status: 待实现
tags: [EDA, P14, ECO列表]
related: ["[[14-ECO工程变更]]"]
---

# 14-03 ECO列表

> 所属 [[14-ECO工程变更]]。将检测到的差异组织为结构化的 ECO（Engineering Change Order）变更项列表，每项含类型、对象、影响与状态，供用户审查与选择性应用。

## 目标（要实现什么）
- 自动从差异（[[02-差异检测]]）生成 ECO 项，每项含：
  - 唯一 ID（ECO-001、ECO-002...）
  - 类型（Add/Remove/Modify/Rename）
  - 对象类别（Component / Net / Pin / Footprint / Property）
  - 详情（前值→后值，如 `R1 Footprint: 0603 → 0805`）
  - 影响范围（影响哪些层、网络、器件）
  - 状态（Pending / Approved / Applied / Rejected / Skipped）
- 列表 UI：分组（按类型/按对象类别/按状态）、排序、过滤、搜索
- ECO 元数据：生成时间、生成者、来源（schematic → PCB / PCB → schematic）
- ECO 依赖关系：某些 ECO 必须先于其他应用（如先删除网络才能删除器件）
- 批量操作：全选/反选/按规则选择（如"接受所有封装变更"）
- 与 [[04-选择性应用]]、[[05-变更预览与日志]] 联动
- 导出 ECO 列表（CSV/PDF）作为变更文档

## 关键点
- ECO 列表是 Altium-style 的 ECO 系统（Vault / Supply Chain）的核心数据结构
- 每项 ECO 应是原子操作（不可再分），便于选择性应用与回滚
- 依赖关系：如"新增网络 NET_X" 必须先于"在 NET_X 上加器件 R5"
- 状态机：Pending → Approved → Applied（成功）/ Failed（错误回滚）/ Skipped（跳过）
- 元数据用于审计（[[06-回滚与审计]]）：谁、何时、为什么接受/拒绝
- 与命令栈（B3）联动：每个 ECO 应用对应一个命令栈条目，便于 Undo

## 输入 / 输出
- **入**：差异列表（[[02-差异检测]]）
- **出**：结构化 ECO 列表（含状态）、ECO 报告（CSV/PDF）

## 依赖
- **上游**：[[02-差异检测]]
- **下游**：[[04-选择性应用]]、[[05-变更预览与日志]]、[[06-回滚与审计]]

## 状态
**待实现**。进入 Plan P14 时，经 brainstorming → writing-plans 细化为可执行任务。
