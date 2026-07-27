---
title: 16-02 Altium格式
lang: zh-CN
parent: 16-格式兼容
plan: P16
status: 待实现
tags: [EDA, P16, Altium, 二期]
related: ["[[16-格式兼容]]"]
---

# 16-02 Altium格式

> 所属 [[16-格式兼容]]。Altium Designer 工程/库/原理图/PCB 的解析与导入。**二期实现**（MVP 仅 KiCad）。

## 目标（要实现什么）
- 解析 Altium Designer 工程文件 `.PrjPcb`（XML 格式）+ `.SchDoc` / `.PcbDoc`（支持二进制 OLE 复合文档与 ASCII/XML 4.0+ 双形态）
- OLE 复合文档读取：复用 libolecf/FreeODF 或自研 OLE 解析器，提取 SchDoc/PcbDoc 内部存储的多段 Record（按 RecordType 字段分块）
- Record 流解析：按 Altium 公开的 RecordType（如 Component=12、Pad=15、Track=9、Net=22）逐一还原为内部对象，丢失字段记录到保真度报告
- 库导入：`.SchLib` 符号库 + `.PcbLib` 封装库，含焊盘栈（Pad/Via Stack）、3D Body（STEP / Extruded）、Designator 规则
- 设计规则映射：Altium Rule（Width/Clearance/ShortCircuit/RoutingLayers 等）映射为内部约束管理器（[[08-PCB编辑器]] 规则系统）
- 工程变量与文档属性：Altium 工程变量（Project Variables）、参数、文档属性（Title Block/图框）保留映射

## 关键点
- SchDoc/PcbDoc 二进制为 OLE 复合文档（结构化存储），内含 `FileHeader` + 多个 `Storage`/`Stream`，每条记录以 4 字节长度 + RecordType + 字段块组成
- Altium v4.0+ 提供纯 XML 导出格式（Save As ASCII），优先支持；二进制 OLE 形态作为兜底
- 设计规则数量大（一份 PCB 可有数百条 Rule），需提供"规则归并"策略避免内部约束管理器膨胀
- 多部分器件（Component Part 1/2/3）与 Altium Pin Symbol（IEEE 符号）需正确还原
- OutputJob 文件 `.OutJob` 可解析制造输出预设（[[13-制造输出]] 复用）

## 输入 / 输出
- **入**：Altium 工程包（`.PrjPcb` + `.SchDoc`/`.PcbDoc`/`.SchLib`/`.PcbLib`/`.OutJob`/`.SchPrj`）
- **出**：内部工程树 + 原理图 + PCB + 器件库 + 设计规则 + 保真度报告

## 依赖
- **上游**：[[05-工程与文件系统]]、[[06-元件库]]、[[07-原理图编辑器]]、[[08-PCB编辑器]]
- **下游**：[[16-格式兼容/08-迁移向导]]、[[16-格式兼容/09-保真度报告]]、[[16-格式兼容/10-适配器插件化]]；与 [[16-格式兼容/04-立创EDA与Protel与DesignSpark与gEDA]] 共享（DesignSpark 基于 Altium OEM）

## 状态
**待实现**（二期）。进入 Plan P16 时，经 brainstorming → writing-plans 细化为可执行任务。
