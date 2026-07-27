---
title: 04-07 快捷键体系
lang: zh-CN
parent: 04-GUI与编辑器框架
plan: P4
status: 待实现
tags: [EDA, P4, 快捷键体系]
related: ["[[04-GUI与编辑器框架]]"]
---

# 04-07 快捷键体系

> 所属 [[04-GUI与编辑器框架]]。实现全局可配置的快捷键映射系统，并提供 **KiCad / Altium / 立创EDA** 三套预设键位，降低老用户迁移成本。

## 目标（要实现什么）
- `InputMap`（借鉴 Godot）：将物理按键事件（`KeyModifierMask + Key`）映射到抽象 `Action` 名（如 `"pcb_route"`），业务代码只认 Action
- **三套预设**（开箱即用）：
  - KiCad 预设（开源用户迁移）
  - Altium 预设（商业用户迁移）
  - 立创EDA 预设（国内创客迁移）
  - 默认预设（自研推荐）
- **作用域**：全局快捷键 vs 编辑器内（原理图/PCB/库）独占快捷键，按焦点自动切换
- **冲突检测**：绑定重复时高亮警告，禁止覆盖关键动作（如保存/撤销）
- **可配置 UI**：设置面板可视化修改、搜索、重置、导入/导出 `.keymap.toml`
- **修饰键兼容**：跨平台 Ctrl/Cmd 自动映射（Mac 用 Cmd 替代 Ctrl）
- **鼠标手势 + 数值输入**：PCB 走线时按 `W` 弹出行内输入框改线宽（类 Altium）
- **快捷键提示**：菜单、工具栏 tooltip、命令面板统一显示当前绑定

## 关键点
- **业务与按键解耦**：业务代码订阅 Action（如 `Input.is_action_pressed("pcb_route")`），换键位零改动
- **降低迁移成本**：三套预设是 KiCad/Altium/嘉立创老用户的"0 摩擦"切入点，差异化卖点
- **作用域优先级**：编辑器局部 > 全局，避免误触
- **可分享**：键位映射导出 TOML，团队统一标准

## 输入 / 输出
- **入**：DisplayServer 投递的键盘事件、用户配置的 `.keymap.toml`
- **出**：抽象 `Action` 事件，分发给订阅者

## 依赖
- **上游**：`platform/DisplayServer`（键盘事件）、[[06-命令面板]]（共享 Action 注册）
- **下游**：所有编辑器交互（绘制、放置、删除、切换工具）

## 状态
**待实现**。进入 Plan P4 时，经 brainstorming → writing-plans 细化为可执行任务。
