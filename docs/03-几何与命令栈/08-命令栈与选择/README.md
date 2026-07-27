---
title: 03-08 命令栈与选择
lang: zh-CN
parent: 03-几何与命令栈
plan: P3
status: 待实现
tags: [EDA, P3, 命令栈与选择]
related: ["[[03-几何与命令栈]]"]
---

# 03-08 命令栈与选择

> 所属 [[03-几何与命令栈]]。提供事务化的 Undo/Redo 命令栈与选择系统，所有编辑器对文档的修改经命令栈落地（"图元即节点"原子操作），保证可撤销/可重做。对应功能目录 B3「命令栈 / Undo/Redo」与「选择系统」。

## 目标（要实现什么）
- 实现 `eda/command/command.{h,cpp}`：`Command` 接口（`undo()` / `redo()` / `get_name()` / `merge(other) -> bool`）
- 实现 `eda/command/undo_stack.{h,cpp}`：`push(cmd)` / `undo()` / `redo()` / `can_undo()` / `can_redo()` / `clear()` / `clean_index()`（脏检测）
- 事务化宏 API：`begin_macro(name)` → 多次 push → `end_macro()`，合并为一个撤销单元（移动器件 = 一次拖拽，撤销一次回退）
- 内存上限：栈超阈值（默认 100 步或 N MB）drop 最旧；可配置
- 选择系统 `eda/command/selection.{h,cpp}`：模板 `SelectionSet<T>`，单选 / 框选 / 过滤（按类型/图层/网络）/ 按属性选；选择变更亦命令化（可撤销）
- 命令合并：连续同类命令（如同属性多次拖拽）可合并为一条，减少栈深度

## 关键点
- **图元即节点、原子入栈**：对齐功能目录 B3 关键设计——每条编辑都是命令栈上的原子操作
- **借鉴 Godot `UndoRedo`**：`merge` 对应 `merge_action`，事务化对应 `create_action` / `commit_action`，do/undo 对应 `add_do_method` / `add_undo_method`（项目不引入 Qt，参见功能目录附录三）
- **与 P2 对象体系集成**：Command 本身是 `Object`（注册到 ClassDB），属性变更命令复用 `set(String, Variant)`（泛用 `SetPropertyCommand`）
- **编辑器集成预留**：P4 编辑器框架（`editor/undo/`）封装 UI 层（菜单项"撤销/重做"快捷键 Ctrl+Z/Ctrl+Y）
- **线程模型**：单线程使用（编辑器主线程），多线程写入场景由子任务（如批量 DRC 修复）显式 post 到主线程

## 输入 / 输出
- **入**：编辑器（P7 原理图/P8 PCB）产生的编辑动作；选择操作请求
- **出**：文档的可撤销变更历史；当前选择集（供 Inspector、高亮、批量操作消费）

## 依赖
- **上游**：[[02-核心对象体系/01-Object基类]]（Command 是 Object）、[[02-核心对象体系/02-ClassDB反射]]（命令注册）、[[02-核心对象体系/03-Variant动态类型]]（属性变更命令的值载体）
- **下游**：P7 原理图编辑器、P8 PCB 编辑器、P4 通用编辑器框架（`editor/undo/`）、P14 ECO 双向同步（变更日志可追溯）

## 状态
**待实现**。进入 Plan P3 时，经 brainstorming → writing-plans 细化为可执行任务。
