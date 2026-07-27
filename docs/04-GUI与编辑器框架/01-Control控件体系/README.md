---
title: 04-01 Control控件体系
parent: 04-GUI与编辑器框架
plan: P4
status: 待实现
tags: [EDA, P4, Control控件体系]
related: ["[[04-GUI与编辑器框架]]"]
---

# 04-01 Control控件体系

> 所属 [[04-GUI与编辑器框架]]。借鉴 Godot `scene/gui/control.h`，自研一套**不依赖系统控件**的自绘 UI 控件基类与内置控件集，作为整个编辑器（原理图/PCB/库/属性面板）的 UI 基石。

## 目标（要实现什么）
- 实现 `Control` 基类：继承自 `CanvasItem`，拥有 `Rect2`（位置+尺寸）、虚函数 `get_minimum_size()`、`queue_redraw()` + `_draw()` 自绘入口
- 锚点布局：`anchor_left/right/top/bottom`（0~1 相对父尺寸）+ `offset_*`（像素偏移）+ `grow_direction`，支持任意父容器内的相对定位
- `size_flags`：`SIZE_FILL / SIZE_EXPAND / SIZE_SHRINK_CENTER` 等，与容器协同决定伸缩行为
- 焦点链路：`set_focus_mode`(NONE/CLICK/ALL)、`focus_enter/focus_exit` 信号、Tab/Shift+Tab 遍历
- 输入分发：`_gui_input(event)` 虚函数、`gui_input` 信号、`mouse_filter`(STOP/PASS/IGNORE)
- 自绘内置控件集：Button、Label、LineEdit、TextEdit（含语法高亮基础）、Tree（多列树/表）、ItemList、ScrollContainer、TabContainer、SplitContainer、PopupMenu、ComboBox、CheckBox、SpinBox、ColorPicker——至少覆盖 EDA 编辑器所需
- 主题接入：`add_theme_*` 局部覆盖全局 `Theme` Resource（字号/颜色/贴图/常量）
- 自绘不闪烁：脏矩形 + 增量重绘，万级 UI 节点交互不卡顿

## 关键点
- **完全自绘**：所有控件经 `_draw()` 调 `RenderingServer.canvas_item_add_*`，不依赖系统按钮/输入框——保证跨平台 UI 完全一致
- **节点即控件**：Control 继承 Node/CanvasItem，UI 即场景树子集，可序列化、可挂信号
- **RTL 与 DPI**：锚点支持 RTL 翻转；`Scale` 属性支持高 DPI 缩放（无障碍前置）
- **裁剪**：`set_clip_contents`（滚动/列表必需）
- **IME 接入**：LineEdit/TextEdit 接入 DisplayServer 的 IME 回调（中日韩输入）

## 输入 / 输出
- **入**：`CanvasItem`（绘制基类）、`Theme` Resource、输入事件（DisplayServer 投递的鼠标/键盘）
- **出**：可实例化的控件节点 + 标准化事件/信号流，供编辑器面板/Dock/Inspector 直接组装

## 依赖
- **上游**：`servers/RenderingServer`（canvas_item RID）、`platform/DisplayServer`（事件）
- **下游**：[[02-布局与容器]]（Container 继承 Control）、[[04-可停靠Dock与面板]]、[[05-属性Inspector]]、[[06-命令面板]]、所有编辑器面板

## 状态
**待实现**。进入 Plan P4 时，经 brainstorming → writing-plans 细化为可执行任务。
