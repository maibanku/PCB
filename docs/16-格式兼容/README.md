---
title: 格式兼容
lang: zh-CN
plan: P16
phase: Phase 4 · 生态与协作
priority: MVP
status: 待实现
tags: [EDA, P16, 格式兼容]
related: ["[[00-总览/01-功能设计目录]]", "[[00-总览/02-实现路线图]]", "[[00-总览/03-项目目录结构]]"]
---

# 16-格式兼容

> 对应 **Plan P16**（Phase 4 · 生态与协作）。第三方 EDA 格式的导入导出与互操作。

## 定位
- **做什么**：支持 KiCad/Altium/Eagle/OrCAD/PADS/立创EDA 等主流 EDA 格式的工程、库、原理图、PCB 导入导出，以及 DXF/DWG、ODB++/IPC/Gerber 的反向解析，并提供迁移向导与保真度评估。
- **不做什么**：**MVP 仅做 KiCad 工程导入子集**，其余格式（Altium、Eagle、OrCAD、PADS、立创EDA、Protel、DesignSpark、gEDA 等）推迟到二期；导出能力亦视格式按阶段推进。
- **代码位置**：`eda/formats`（见 [[00-总览/03-项目目录结构]]）

## 子功能清单
| 编号 | 子功能 | 说明 | 状态 |
|------|--------|------|------|
| 01 | [KiCad格式](01-KiCad格式/) | 工程/库/原理图/PCB 导入导出（MVP 优先） | 待实现 |
| 02 | [Altium格式](02-Altium格式/) | AD 工程/库导入导出 | 待实现 |
| 03 | [Eagle与OrCAD与PADS](03-Eagle与OrCAD与PADS/) | 各格式导入导出 | 待实现 |
| 04 | [立创EDA与Protel与DesignSpark与gEDA](04-立创EDA与Protel与DesignSpark与gEDA/) | 各格式导入导出 | 待实现 |
| 05 | [元件库格式](05-元件库格式/) | 各库格式互转 | 待实现 |
| 06 | [DXF与DWG](06-DXF与DWG/) | 机械图导入导出 | 待实现 |
| 07 | [ODB与IPC与Gerber反向解析](07-ODB与IPC与Gerber反向解析/) | 制造数据反向 | 待实现 |
| 08 | [迁移向导](08-迁移向导/) | 导入迁移流程 | 待实现 |
| 09 | [保真度报告](09-保真度报告/) | 导入保真度评估 | 待实现 |
| 10 | [适配器插件化](10-适配器插件化/) | 格式适配器接口 | 待实现 |

## 依赖
- **上游**：P5（工程文件系统）、P6（元件库模型）、P7（原理图模型）、P8（PCB 几何/封装与设计数据）
- **下游**：P16 产物可直接被上述功能域加载编辑

## 关联
- 功能蓝图：[[00-总览/01-功能设计目录]] F3（格式兼容）
- 实现计划：`plan/`（进入 Plan P16 时经 brainstorming → writing-plans 产出）
