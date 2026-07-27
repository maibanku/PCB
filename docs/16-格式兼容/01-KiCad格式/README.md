---
title: 16-01 KiCad格式
parent: 16-格式兼容
plan: P16
status: 待实现
tags: [EDA, P16, KiCad, S-expression, MVP]
related: ["[[16-格式兼容]]"]
---

# 16-01 KiCad格式

> 所属 [[16-格式兼容]]。KiCad v6/v7/v8 工程/库/原理图/PCB 的解析与导入导出。**MVP 优先实现**（仅导入子集）。

## 目标（要实现什么）
- 解析 KiCad v6+ S-expression 文本格式：`.kicad_pro`（工程 JSON）、`.kicad_sch`（原理图）、`.kicad_pcb`（PCB）、`.kicad_sym`（符号库）、`.kicad_dru`（设计规则）、`.kicad_wks`（标题栏）
- S-expression 解析器：自研词法/语法分析（手写递归下降或 boost::spirit），保留 token 位置信息以利错误定位与原文件回写
- 工程树完整导入：`.kicad_pro` 映射为内部工程树（[[05-工程与文件系统]]），原理图页、PCB 板、库依赖、规则、文本变量全部还原
- 符号/封装库导入：`.kicad_sym` 符号库 + `.pretty` 封装目录（`.kicad_mod`）映射为一体化器件库（[[06-元件库]]）
- 几何对象完整还原：线段、圆弧、多边形、文字、尺寸标注、层次块、电源端口、铺铜区域、设计规则约束（`.kicad_dru`）
- 双向导出（二期）：将内部模型回写 KiCad 格式，保持与 KiCad 主线版本兼容（最小目标 v7+），支撑与 KiCad 用户协作

## 关键点
- S-expression 是 KiCad v6+ 统一存储格式（之前的二进制/旧版文本另作遗留处理），原子 token 加引号字符串区分
- KiCad 的图层体系、单位（mm/mil 内部并存）、坐标系（PCB 原点可调）需准确映射到内部规范
- 层次化原理图：SubSheet、SheetPin、Hierarchical Label 的层级关系要还原为内部层次块结构
- v6/v7/v8 字段差异：版本号嗅探 + 字段缺失默认值表（如 v8 新增的 thermals/teardrop 字段在 v6 不存在）
- 测试基准：使用 KiCad 官方 demo 库（如 ` demos/`）作为回归测试样本，确保跨版本兼容

## 输入 / 输出
- **入**：KiCad 工程文件（`.kicad_pro` + 关联 `.kicad_sch` / `.kicad_pcb` / `.kicad_sym` / `.pretty`）
- **出**：内部工程树（P5）+ 原理图模型（P7）+ PCB 模型（P8）+ 器件库（P6）+ 保真度报告（[[16-09]]）

## 依赖
- **上游**：[[05-工程与文件系统]]、[[06-元件库]]、[[07-原理图编辑器]]、[[08-PCB编辑器]]（内部数据模型）
- **下游**：[[16-08]]（迁移向导调用本适配器）、[[16-09]]（保真度评估）、[[16-10]]（适配器接口规范化）

## 状态
**待实现**。进入 Plan P16 时，经 brainstorming → writing-plans 细化为可执行任务。**MVP 范围**：导入子集（工程结构 + 原理图基础图元 + PCB 几何 + 符号/封装库），导出与高级特性（如复杂规则、脚本）二期。
