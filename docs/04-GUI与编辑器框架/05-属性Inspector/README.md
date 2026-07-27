---
title: 04-05 属性Inspector
lang: zh-CN
parent: 04-GUI与编辑器框架
plan: P4
status: 待实现
tags: [EDA, P4, 属性Inspector]
related: ["[[04-GUI与编辑器框架]]"]
---

# 04-05 属性Inspector

> 所属 [[04-GUI与编辑器框架]]。借鉴 Godot `EditorInspector` + `EditorInspectorPlugin`，实现通用的属性面板：根据选中对象（器件/网络/规则/叠层）反射出可编辑的属性表单，支持插件式扩展。

## 目标（要实现什么）
- **反射驱动**：基于 ClassDB 的 `get_property_list()` 自动生成属性编辑器（字符串/数值/枚举/颜色/资源/Vector2 等）
- **属性编辑器类型**：LineEdit、SpinBox、ColorPickerButton、ResourcePicker、Vector2Editor、EnumDropdown、CheckBox、曲线编辑器（用于规则曲线）
- **分类与分组**：属性按 `category` 分组折叠（如"基础属性""电气属性""供应链"）
- **`EditorInspectorPlugin` 机制**：插件可注入自定义属性编辑器（如为"器件"对象加"位号重排"按钮、为"网络"加"长度调谐"面板）
- **多选合并**：多选同类对象时显示公共属性，不同值显示为"---"，修改时批量应用
- **撤销集成**：所有属性修改经 `UndoRedo` 入命令栈，可撤销
- **编辑器适配**：原理图选中器件 → 显示符号属性；PCB 选中走线 → 显示网络/线宽/长度；规则选中 → 显示约束参数
- **搜索/过滤**：属性面板顶部搜索框，按属性名快速定位
- **只读模式**：库锁定或依赖锁定的属性显示为只读（与元件库 08-版本管理与依赖锁定 联动）

## 关键点
- **零样板代码**：业务对象只要在 ClassDB 注册属性，Inspector 自动生成编辑器，新加属性无需改 UI
- **插件化扩展**：EDA 各域（原理图/PCB/库）注册各自的 InspectorPlugin，避免硬编码
- **数值校验**：属性 setter 内置范围/类型校验，非法值不提交（红色高亮）
- **批量修改性能**：多选千级器件改属性时，UndoRedo 合并成单个事务

## 输入 / 输出
- **入**：当前选中的对象（节点/Resource）、对象的属性反射列表
- **出**：属性修改事件（经 UndoRedo 入命令栈）

## 依赖
- **上游**：[[01-Control控件体系]]、[[03-主题系统]]、`core/object/ClassDB`（反射）、`core/object/UndoRedo`（撤销）
- **下游**：所有编辑器（原理图、PCB、库编辑器都依赖 Inspector 编辑对象属性）

## 状态
**待实现**。进入 Plan P4 时，经 brainstorming → writing-plans 细化为可执行任务。
