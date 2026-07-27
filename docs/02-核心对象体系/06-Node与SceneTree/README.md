---
title: 02-06 Node与SceneTree
lang: zh-CN
parent: 02-核心对象体系
plan: P2
status: 待实现
tags: [EDA, P2, Node与SceneTree]
related: ["[[02-核心对象体系]]"]
---

# 02-06 Node与SceneTree

> 所属 [[02-核心对象体系]]。`Node` 构成父子树并提供生命周期回调（`_enter_tree`/`_ready`/`_process`/`_exit_tree`）；`SceneTree` 持有根节点并作为主循环驱动者，把帧时间、输入事件分发到节点树。对齐 Godot `Node` / `SceneTree`。

## 目标（要实现什么）
- 实现 `scene/main/node.{h,cpp}`：`Node : public Object`，持有 parent/children 列表；`add_child` / `remove_child` / `get_parent` / `get_children`
- 生命周期通知：`_enter_tree`（挂树时）/ `_ready`（子树全部就绪时，自底向上）/ `_process(delta)`（每帧）/ `_exit_tree`（卸载时）
- 输入分发：`_input(event)` / `_unhandled_input(event)`；事件自顶向下冒泡，被消费即停
- 实现 `scene/main/scene_tree.{h,cpp}`：单例持有 `root`；`process(delta)` 遍历树调 `_process`；`input(event)` 遍历调 `_input`
- 与 P1 主循环对接：[[01-引擎地基/06-主循环]] 把裸 update/draw 改为 `SceneTree::process(delta)` + 渲染驱动
- 暂停/分组：`process_mode`（`INHERIT`/`PAUSABLE`/`WHEN_PAUSED`/`ALWAYS`），支持 `pause()` 整树

## 关键点
- **图元即节点、工程树即场景树**：EDA 原理图的器件/导线、PCB 的器件/走线/铺铜未来都作为 Node 挂树（功能蓝图附录三第 3 点）
- **生命周期顺序**：`_enter_tree` 自顶向下、`_ready` 自底向上，与 Godot 一致
- **遍历缓存**：维护"所有 _process 节点"扁平列表，避免每帧递归遍历整树
- **对齐 Godot `SceneTree`**：消息分发顺序、暂停语义、`call_group` 等接口直接照搬

## 输入 / 输出
- **入**：[[01-引擎地基/06-主循环]] 每帧传入 delta；`InputState` 传入事件
- **出**：节点树的状态推进；渲染由各节点通过 Canvas2D 自驱（节点 `_process` 中提交绘制）

## 依赖
- **上游**：[[01-引擎地基/06-主循环]]（替换裸循环）、[[02-核心对象体系/01-Object基类]]（基类）、[[02-核心对象体系/04-Signal信号槽]]（`child_entered_tree` 等信号）
- **下游**：所有 EDA 业务对象（作为 Node 挂树）、编辑器框架（P4，dock/inspector 作为 Node）

## 状态
**待实现**。进入 Plan P2 时，经 brainstorming → writing-plans 细化为可执行任务。
