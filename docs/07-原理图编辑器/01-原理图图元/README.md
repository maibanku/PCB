---
title: 07-01 原理图图元
parent: 07-原理图编辑器
plan: P7
status: 待实现
tags: [EDA, P7, 原理图图元]
related: ["[[07-原理图编辑器]]"]
---

# 07-01 原理图图元

> 所属 [[07-原理图编辑器]]。定义原理图上所有可视图元类型及其数据结构：器件、导线、网络标号、总线、电源端口、层次块、子图、端口、注释、图片。

## 目标（要实现什么）
- 定义 10 类原子图元的统一数据结构（SchematicItem 基类 + 派生）：器件 Component、导线 Wire（Polyline）、网络标号 NetLabel、总线 Bus、总线入口 BusEntry、电源端口 PowerPort、层次块 HierBlock、子图 SheetInstance、端口 Port、注释 Note、图片 Image。
- 每个图元以节点形式挂接到原理图页节点树（借鉴 Godot Node 体系），支持父子层级与信号通知。
- 图元属性可序列化（JSON 文本格式，Git diff 友好），schema 版本化、向后兼容。
- 图元坐标采用 mil/mm 双单位存储（内部 nm 整数），原点可切换。
- 引脚 Pin 作为器件子节点：编号、名称、位置、电气类型（Input/Output/Power/Bidirectional/Passive）。
- 所有图元支持任意角度旋转、镜像（X/Y）、锁定、属性透传。

## 关键点
- 图元 = 节点；操作以原子粒度入命令栈（B3），保证 Undo/Redo 一致性。
- 导线 Wire 用 Polyline（多段折线）表达，节点吸附后构成连通图；端点重合即视为同网络。
- 总线 Bus 仅作视觉聚合，不直接参与网表；通过 BusEntry + NetLabel 解析为具体网络。
- 引脚电气类型用于 ERC 冲突判定（05）。
- 渲染走 CanvasItem 式批渲染（B2），图元自带脏区标记，支持增量刷新。

## 输入 / 输出
- **入**：符号库（C1）提供器件符号 / 引脚定义；用户绘制操作。
- **出**：图元树（页面数据结构），供 04-网表生成 / 05-ERC / 06-Annotate / 09-PDF 消费。

## 依赖
- **上游**：C1 元件库体系（符号/引脚）、B2 几何引擎、B3 数据模型与命令栈。
- **下游**：02-绘制与放置（编辑操作）、04-网络表生成、05-ERC、09-原理图PDF输出。

## 状态
**待实现**。进入 Plan P7 时，经 brainstorming → writing-plans 细化为可执行任务。
