---
title: 02-01 Object基类
parent: 02-核心对象体系
plan: P2
status: 待实现
tags: [EDA, P2, Object基类]
related: ["[[02-核心对象体系]]"]
---

# 02-01 Object基类

> 所属 [[02-核心对象体系]]。`Object` 是所有业务对象的根基类，提供 notification 生命周期分发、属性 set/get/meta 元数据，并派生 `RefCounted` 实现引用计数。对齐 Godot `Object` / `RefCounted`。

## 目标（要实现什么）
- 实现 `core/object/object.h`：`Object` 抽象基类；`notification(int what)` 接收生命周期消息（`NOTIFICATION_PREDELETE` 等）
- 属性系统：`set(String, Variant)` / `get(String) -> Variant`，配合 ClassDB 查 property 列表；`set_meta(key, value)` / `get_meta(key)` 元数据（任意附加数据，编辑器用）
- 派生 `RefCounted`：内置原子引用计数（`ref.inc()/dec()`），引用归零自动 `delete`；`Ref<T>` 智能指针封装
- 信号槽容器：`Object` 持有"被连接的 Signal 列表"，析构时通知所有 Signal 解绑，避免悬空
- 类型查询：`get_class()` / `is_class(name)` / `get_class_static()`，对接 ClassDB
- `cast_to<T>()` 安全向下转换（基于 ClassDB 父类链）

## 关键点
- **不引入虚函数表爆炸**：核心多态限于 `notification` / `dtor` / ClassDB 钩子；属性访问走 String + Variant
- **引用计数可选**：`Object` 默认手动管理（编辑器节点挂树由 SceneTree 管理），`RefCounted` 才是自动
- **对齐 Godot `Object`**：避免重发明 Godot 已验证的对象模型；属性/meta/信号三件套直接照搬语义
- **不依赖 RTTI**：不用 `dynamic_cast`，类型查询走 ClassDB 字符串比较，便于跨 dynamic library 边界

## 输入 / 输出
- **入**：[[02-核心对象体系/02-ClassDB反射]] 注册的类型信息；属性 `set` 接收 `Variant` 值
- **出**：所有业务对象（Node/Resource/EDA 文档对象）的统一根基类

## 依赖
- **上游**：[[02-核心对象体系/02-ClassDB���射]]（类型注册）、[[02-核心对象体系/03-Variant动态类型]]（属性值载体）、[[02-核心对象体系/04-Signal信号槽]]（信号生命周期）
- **下游**：[[02-核心对象体系/05-Resource资源系统]]、[[02-核心对象体系/06-Node与SceneTree]]，以及全部 EDA 业务对象

## 状态
**待实现**。进入 Plan P2 时，经 brainstorming → writing-plans 细化为可执行任务。
