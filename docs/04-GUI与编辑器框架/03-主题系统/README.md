---
title: 04-03 主题系统
lang: zh-CN
parent: 04-GUI与编辑器框架
plan: P4
status: 待实现
tags: [EDA, P4, 主题系统]
related: ["[[04-GUI与编辑器框架]]"]
---

# 04-03 主题系统

> 所属 [[04-GUI与编辑器框架]]。实现统一的 `Theme` Resource（借鉴 Godot `Theme`）：颜色、字号、字体、图标、常量统一管理，支持明/暗/自定义三套预设与全局换肤。

## 目标（要实现什么）
- `Theme` Resource：键值存储 `(控件类型, 属性名) → Variant`，包含 colors/font_colors/font_sizes/constants/icons/styleboxes/stylebox_flat
- **StyleBox**：可绘制的外观盒（扁平/线框/贴图/九宫格缩放），替代系统按钮样式
- 三套内置预设：**明色（Light）/ 暗色（Dark）/ 高对比度（High-Contrast，无障碍前置）**
- 主题覆盖层级：默认主题 → 工程主题 → 控件 `add_theme_*` 局部覆盖（三级）
- **图标包可换**：SVG 矢量图标为主，按主题前景色染色（`modulate`）；支持图标包目录扫描替换
- 字体管理：HarfBuzz/FreeType 集成，主题可指定默认字体/等宽字体（原理图/PCB 代码字体）
- 实时切换：切换主题时所有控件 `queue_redraw()`，无需重启
- **DPI 适配**：主题尺寸支持按 `Scale` 缩放（高分辨率屏不糊）
- **用户自定义**：导出/导入主题为 `.theme.toml`，社区可分享

## 关键点
- **不硬编码颜色**：业务代码只查主题键（如 `get_theme_color("error_color", "LineEdit")`），换肤零改动
- **EDA 专用色板**：原理图/PCB 的图层色（铜层/阻焊/丝印）与 UI 主题色分离管理
- **可访问性**：高对比度主题满足 WCAG 文本/背景对比度 ≥ 4.5:1
- **图标染色**：单色 SVG 图标按主题前景色 modulate，暗色模式自动反色

## 输入 / 输出
- **入**：主题预设文件（TOML/JSON）、SVG 图标包、字体文件
- **出**：所有控件统一的查表 API（`get_theme_*`）

## 依赖
- **上游**：`core/io/Resource`（主题作为 Resource）
- **下游**：[[01-Control控件体系]]（消费主题）及所有 GUI 控件、[[08-i18n与无障碍]]（高对比度主题）

## 状态
**待实现**。进入 Plan P4 时，经 brainstorming → writing-plans 细化为可执行任务。
