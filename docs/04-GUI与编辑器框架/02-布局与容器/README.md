---
title: 04-02 布局与容器
parent: 04-GUI与编辑器框架
plan: P4
status: 待实现
tags: [EDA, P4, 布局与容器]
related: ["[[04-GUI与编辑器框架]]"]
---

# 04-02 布局与容器

> 所属 [[04-GUI与编辑器框架]]。借鉴 Godot `scene/gui/container.h` + `box_container.h`，自研一套**接管子控件几何**的容器体系，让编辑器外壳（Dock 拆分 / 属性面板滚动 / 多页签）只需"加容器"即可自动排布，无需手算坐标。

## 目标（要实现什么）
- 实现 `Container` 基类（继承 `Control`）：接管所有子节点的位置与尺寸；子控件 `_draw` 仍自绘，但 `Rect2` 由容器决定
- 排布入口：`_notification(SORT_CHILDREN)` 虚函数；触发时机为 `queue_sort()`（尺寸变/子控件增删/最小尺寸变）
- 标准接口 `fit_child_in_rect(child, rect)`：把子控件塞入指定矩形（含 margin 处理），所有内置容器复用
- 最小尺寸冒泡：`get_combined_minimum_size()` 汇总子控件最小尺寸 → 父容器最小尺寸 → 直到窗口，保证布局不自相矛盾
- `BoxContainer`（`HBoxContainer` / `VBoxContainer`）：按 `size_flags`（FILL/EXPAND/SHRINK_CENTER）一维排列、`separation` 间距、弹性分配剩余空间
- `SplitContainer`（`HSplit` / `VSplit`）：可拖拽分隔条、`drag_area`、`collapsed`、子区比例持久化（Dock 左右/上下拆分的基础）
- `ScrollContainer`：内容超框自动出现滚动条、`set_clip_contents` 裁剪、`follow_focus`（焦点控件自动滚入视图，无障碍必备）
- `TabContainer`：多页签切换、`tab_changed` 信号、页签标题/图标、可关闭页签（编辑器多文档/多面板切换的基础）
- `GridContainer`：`columns` 网格排列（属性 Inspector 表单）
- 辅助容器：`MarginContainer`（统一内边距）、`CenterContainer`（居中）、`AspectRatioContainer`（保比例缩放，3D 预览窗口）、`FlowContainer`（自动换行流式布局）
- 主题接入：`separation` / `h_separation` / `v_separation` 等布局间距走 `Theme` 常量，支持全局/局部覆盖

## 关键点
- **布局即约定**：子控件放进容器后位置不再由 `anchor/offset` 决定，而由容器 `SORT_CHILDREN` 统一计算——避免编辑器面板手算坐标的维护地狱
- **size_flags 双向契约**：子控件声明"我要 FILL/EXPAND/SHRINK"，容器据此分配空间；这是 [[01-Control控件体系]] size_flags 的消费端
- **最小尺寸递归**：必须正确合并（max + separation + padding），否则出现"控件被挤成 0 / 滚动条乱跳"
- **批量重排节流**：一帧内多次 `queue_sort` 只排一次（`NOTIFICATION_SORT_CHILDREN` 去重），万级 UI 节点重排不卡顿
- **EDA 场景映射**：`HSplitContainer`=左右 Dock 拆分、`ScrollContainer`=属性 Inspector 滚动、`TabContainer`=多原理图/PCB 文档页签、`GridContainer`=规则编辑器表单

## 输入 / 输出
- **入**：[[01-Control控件体系]]（`Control` 基类、`size_flags`、`get_minimum_size`）、`Theme`（布局间距常量）
- **出**：可实例化的容器节点集 + `Container` 扩展点，供 [[04-可停靠Dock与面板]]、[[05-属性Inspector]] 直接嵌套组装编辑器外壳

## 依赖
- **上游**：[[01-Control控件体系]]（容器是 `Control` 子类）、`servers/RenderingServer`（裁剪用 `canvas_item_set_clip`）
- **下游**：[[04-可停靠Dock与面板]]（拆分/停靠核心依赖 `SplitContainer`）、[[05-属性Inspector]]（`ScrollContainer` + `GridContainer`）、所有编辑器多文档页签

## 状态
**待实现**。进入 Plan P4 时，经 brainstorming → writing-plans 细化为可执行任务。
