---
title: 13-01 Gerber输出
parent: 13-制造输出
plan: P13
status: 待实现
tags: [EDA, P13, Gerber输出]
related: ["[[13-制造输出]]"]
---

# 13-01 Gerber输出

> 所属 [[13-制造输出]]。按 RS-274X 标准分层输出 2D 矢量光绘文件，包含各铜层、阻焊、丝印、板框，是 PCB 制造最基础的工厂通用格式。

## 目标（要实现什么）
- 输出符合 **RS-274X**（RS-274D 已淘汰）的 Gerber 文件，每层一个 .gbr/.phd 文件
- 支持图层全集：Top/Bot 铜层、内层、阻焊（Top/Bot）、丝印（Top/Bot）、钢网、 Drill drawing、Board outline
- 光圈（Aperture / D-code）自动生成与优化：���并相同形状、最小化 D-code 数量、支持自定义光圈（圆/矩形/多边形）
- 单位（mm / inch）+ 坐标精度（3:3 / 3:4 / 4:4）可配置，零省略前导/后导（LZ/TZ）设置
- 自定义文件命名规则（如 `proj-GTL.gbr`、`proj-TOP_CU.gbr`），支持占位符模板（项目名/层名/版本）
- 镜像、旋转、极性（正片/负片）按层设定，支持平面层负片输出
- 与 [[13-制造输出/12-制造前检查]] 联动：输出前自动检查未连接元素、D 码冲突、单位混用
- 提供可视化预览器（参考 gerbv / KiCad GerbView）：导出前逐层查看避免出错

## 关键点
- RS-274X 是事实标准（Ucamco 维护），扩展 RS-274D 已淘汰，不支持嵌入光圈定义的旧格式
- Aperture D01-D03 = draw/flash/line，标准光圈定义块（AD）+ 光圈列表（AM）
- 多边形填充（Region, G36/G37）用于铺铜输出，避免海量 draw 命令
- 文件命名约定差异大（嘉立创 vs JLCPCB vs Multek），需可配置并预设主要工厂模板
- 输出后建议自动 round-trip 解析：自己导出 → 自己读回 → 与原设计比对，防止静默丢失

## 输入 / 输出
- **入**：PCB 设计数据（C3 编辑器）、输出配置（命名规则、精度、单位、命名预设）
- **出**：每层一个 Gerber 文件 + 配套 readme/manifest（说明各文件对应层）

## 依赖
- **上游**：C3-PCB 编辑器、输出配置（[[13-制造输出/11-输出模板批处理]]）
- **下游**：PCB 工厂（CAM 部门）、[[05-IPC-2581]] / [[06-ODB++]]（可选用 Gerber 作为图层源）

## 状态
**待实现**。进入 Plan P13 时，经 brainstorming → writing-plans 细化为可执行任务。
