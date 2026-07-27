---
title: 16-03 Eagle与OrCAD与PADS
parent: 16-格式兼容
plan: P16
status: 待实现
tags: [EDA, P16, Eagle, OrCAD, PADS, 二期]
related: ["[[16-格式兼容]]"]
---

# 16-03 Eagle与OrCAD与PADS

> 所属 [[16-格式兼容]]。Eagle、OrCAD、PADS 三家主流 EDA 格式的解析与导入。**二期实现**（MVP 仅 KiCad）。

## 目标（要实现什么）
- **Eagle**：解析 v6+ XML 格式 `.sch` / `.brd` / `.lbr`（基于 RapidJSON/pugixml），覆盖 Net/Part/Contact/Set/Element/Signal 等元素；v5 及更早二进制单独处理或提示用户先用 Eagle 升级
- **OrCAD**：解析 `.DSN` 设计文件与 `.OLB` 库（专有二进制），通过逆向工程 mapping 表还原原理图页结构与元件
- **PADS**：解析 ASCII `.pcb`（Layout）与 `.sch`（Logic）、`.lib`/`.dft`/`.p`（库与规则），覆盖 PADS9.x 与 VX 系列
- 各格式元数据归一化：图层名、单位（inch/mil/mm）、字体、网络属性、Pin Naming 等统一为内部规范，避免歧义
- 损坏/非标文件容错：缺字段、编码异常、版本混杂给出警告并尽量继续解析（best-effort），不影响后续保真度报告
- 导出（二期）：Eagle XML 导出可优先支持（开放格式，社区可读）；OrCAD/PADS 导出受格式专有限制，按用户需求评估

## 关键点
- Eagle v6+ XML 是开放的、文档化的格式，是最易支持的专有格式之一；Eagle 已停止更新（Autodesk 2026 EOL），存量工程迁移需求大
- OrCAD `.DSN` 二进制结构由 OpenAccess 或 dbChip 数据库存储，需独立逆向或借助 liboed 的开源尝试
- PADS ASCII 格式按行块组织（`*PART*`/`*NET*`/`*SIG*`/`*PAD*`/`*LINE*`），相对易解析
- Eagle 的 `<signal>` 与 OrCAD 的 net、PADS 的 `*NET*` 三套语义映射要清晰，避免网络名冲突（如 `GND` vs `GND_POWER`）
- 三家均有大量老工程存量（90s-2010s），测试样本需覆盖 Eagle 4-9、OrCAD 9-22、PADS 9-VX

## 输入 / 输出
- **入**：Eagle `.sch/.brd/.lbr`、OrCAD `.DSN/.OLB`、PADS `.pcb/.sch/.lib`
- **出**：内部工程/原理图/PCB/器件库 + 保真度报告

## 依赖
- **上游**：[[05-工程与文件系统]]、[[06-元件库]]、[[07-原理图编辑器]]、[[08-PCB编辑器]]
- **下游**：[[16-08]]、[[16-09]]、[[16-10]]、[[16-05]]（库互转）

## 状态
**待实现**（二期）。进入 Plan P16 时，经 brainstorming → writing-plans 细化为可执行任务。
