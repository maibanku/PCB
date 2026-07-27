---
title: 02-04 Signal信号槽
lang: zh-CN
parent: 02-核心对象体系
plan: P2
status: 待实现
tags: [EDA, P2, Signal信号槽]
related: ["[[02-核心对象体系]]"]
---

# 02-04 Signal信号槽

> 所属 [[02-核心对象体系]]。`Signal` 提供观察者模式的 connect/emit/disconnect，支持连接对象方法、lambda、`Callable` 抽象，是节点间通信与编辑器事件分发的基础。对齐 Godot `Signal`。

## 目标（要实现什么）
- 实现 `core/object/signal.{h,cpp}`：模板化 `Signal<Args...>`，存储 `Callable` 列表
- `Callable` 抽象：可包装 `Object* + method name`（走 ClassDB）、lambda、自由函数；统一 `invoke(args)` 接口
- API：`connect(callable)` / `disconnect(callable)` / `emit(args...)`；连接时可指定 `ConnectFlags`（one-shot、deferred）
- **生命周期安全**：`Object` 析构时通过其持有的"被连接 Signal 列表"反向解绑，避免悬空回调（核心难点）
- 延迟调用（deferred）：`emit` 可入队到 `MessageQueue`，由 SceneTree 在 idle 帧统一 flush（避免回调中重入修改信号列表）
- 类型安全：模板参数推导，`connect(&T::method, obj)` 在编译期校验签名

## 关键点
- **双向引用**：Signal 记"我连了谁"，Object 记"谁连了我"，析构双向清理
- **重入保护**：emit 期间若 connect/disconnect，用快照或写时复制避免迭代器失效
- **对齐 Godot `Signal`**：API 一致（`connect`/`emit`/`disconnect`），但首版不实现 Godot 的"连接目标自动转 Callable"
- **不依赖 RTTI**：`Callable` 内部用 `void* + type-erased invoker`，跨 DLL 安全

## 输入 / 输出
- **入**：业务对象 connect 注册回调；上游 emit 触发
- **出**：解耦的事件通知（属性变更、节点添加、UI 交互）

## 依赖
- **上游**：[[02-核心对象体系/01-Object基类]]（生命周期反向解绑）
- **下游**：[[02-核心对象体系/06-Node与SceneTree]]（父子通信、SceneTree 信号）、编辑器事件分发（P4）、属性变更通知

## 状态
**待实现**。进入 Plan P2 时，经 brainstorming → writing-plans 细化为可执行任务。
