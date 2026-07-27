---
title: 14-01 原理图PCB双向同步
lang: zh-CN
parent: 14-ECO工程变更
plan: P14
status: 待实现
tags: [EDA, P14, 原理图PCB双向同步]
related: ["[[14-ECO工程变更]]"]
---

# 14-01 原理图PCB双向同步

> 所属 [[14-ECO工程变更]]。原理图 ↔ PCB 双向同步：正向（Forward Annotation）把原理图变更推到 PCB；反向（Back Annotation）把 PCB 改动（封装变更/位号互换/门交换）回写原理图。

## 目标（要实现什么）
- **Forward Annotation（正向）**：原理图改器件/网络 → 检测差异 → 生成 ECO → 应用到 PCB（新增器件、删除器件、网络重命名）
- **Back Annotation（反向）**：PCB 改动 → 回写原理图，支持：
  - 封装变更（PCB 改封装 → 原理图更新 footprint 属性）
  - 位号互换（PCB 重新编号 R1↔R2 → 原理图同步）
  - 门交换（gate swap，多门 IC 内部门互换）
  - 引脚交换（pin swap，如 FPGA/逻辑门等价引脚互换）
- 同步方向显式选择：用户每次明确"原理图 → PCB"或"PCB → 原理图"，避免双向混乱
- 拓扑变更（如新增层次块、改层次引脚）通过 ECO 链式同步
- 与 [[02-差异检测]]、[[03-ECO列表]] 联动：检测→生成 ECO→应用
- 与原理图/PCB 编辑器深度集成：右键菜单"Update PCB from Schematic"

## 关键点
- Forward Annotation 是高频操作（设计阶段原理图频繁修改），需低摩擦
- Back Annotation 是高风险操作（PCB 改了不该回写时易污染原理图），需用户明确确认
- 门交换（gate swap）是 PCB 布线优化常用功能（74xx 多门 IC），需 IC 库支持"等价门"标记
- 引脚交换（pin swap）需器件库声明 pin-swap-pair（如 NAND 的两输入端可互换）
- 位号互换（re-annotation）的"参考方案"必须保存：否则反向同步无法恢复对应关系
- 与 [[04-选择性应用]] 配合：用户选择性应用 ECO，部分接受部分拒绝

## 输入 / 输出
- **入**：原理图设计（C2）、PCB 设计（C3）、同步方向选择
- **出**：ECO 列表（[[03-ECO列表]]）、应用结果（修改后的 PCB 或原理图）

## 依赖
- **上游**：C2 原理图编辑器、C3 PCB 编辑器
- **下游**：[[02-差异检测]]、[[03-ECO列表]]、[[04-选择性应用]]

## 状态
**待实现**。进入 Plan P14 时，经 brainstorming → writing-plans 细化为可执行任务。
