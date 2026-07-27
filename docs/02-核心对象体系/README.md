---
title: 核心对象体系
plan: P2
phase: Phase 1 · 引擎内核
priority: MVP
status: 待实现
tags: [EDA, P2, 核心对象体系]
related: ["[[00-总览/01-功能设计目录]]", "[[00-总览/02-实现路线图]]", "[[00-总览/03-项目目录结构]]"]
---

# 02-核心对象体系

> 对应 **Plan P2**（Phase 1 · 引擎内核）。借鉴 Godot 的对象-反射-Variant-Signal-Resource-Node 体系，为 EDA 所有业务对象提供运行时类型系统与节点树驱动。

## 定位
- **做什么**：实现 Object 基类与引用计数、ClassDB 反射（GDREGISTER_CLASS/bind_method/signal/property）、Variant 动态类型、Signal 信号槽、Resource 资源系统（res:///.tres/.res 序列化）、Node 与 SceneTree 节点树主循环驱动。
- **不做什么**：不实现渲染与窗口（属 P1）；不实现几何与命令栈（属 P3）；不实现具体 EDA 文档/编辑器（属 P7/P8）。
- **代码位置**：`core/object, core/variant, scene/main/`（见 [[00-总览/03-项目目录结构]]）

## 子功能清单
| 编号 | 子功能 | 说明 | 状态 |
|------|--------|------|------|
| 01 | [Object基类](01-Object基类/) | notification 生命周期、属性 set/get/meta、派生 RefCounted | 待实现 |
| 02 | [ClassDB反射](02-ClassDB反射/) | GDREGISTER_CLASS、bind_method/signal/property、instantiate | 待实现 |
| 03 | [Variant动态类型](03-Variant动态类型/) | 承载 nil/int/float/String/Vector2/Color/Array/Dictionary 等、Variant::call | 待实现 |
| 04 | [Signal信号槽](04-Signal信号槽/) | connect/emit/disconnect 观察者解耦 | 待实现 |
| 05 | [Resource资源系统](05-Resource资源系统/) | RefCounted 共享、res://、.tres/.res 序列化、ResourceLoader/Saver | 待实现 |
| 06 | [Node与SceneTree](06-Node与SceneTree/) | 节点树、_enter_tree/_ready/_process、SceneTree 主循环驱动 | 待实现 |

## 依赖
- **上游**：P1（引擎地基提供主循环、渲染服务器、Canvas2D，Node/SceneTree 跑在其上）
- **下游**：P3（几何内核的命令栈与选择以 Object/Variant 为基）、P7/P8（编辑器业务对象全部继承 Object/Node）

## 关联
- 功能蓝图：[[00-总览/01-功能设计目录]]（核心对象体系章节）
- 实现路线图：[[00-总览/02-实现路线图]]
- 目录结构：[[00-总览/03-项目目录结构]]
- 实现计划：`plan/`（进入 Plan P2 时经 brainstorming → writing-plans 产出）
