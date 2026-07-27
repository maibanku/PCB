---
title: 02-03 Variant动态类型
parent: 02-核心对象体系
plan: P2
status: 待实现
tags: [EDA, P2, Variant动态类型]
related: ["[[02-核心对象体系]]"]
---

# 02-03 Variant动态类型

> 所属 [[02-核心对象体系]]。`Variant` 是承载多态值的联合类型容器，覆盖 nil/bool/int64/double/String/Vector2/Color/Object*/Array/Dictionary 等核心集合，是属性、脚本、序列化的统一值载体。对齐 Godot `Variant`。

## 目标（要实现什么）
- 实现 `core/variant/variant.{h,cpp}`：tagged union（`std::variant` 或手写 enum + union）承载 P2 核心类型集合
- 类型集合：`NIL` / `BOOL` / `INT`（int64） / `FLOAT`（double） / `STRING` / `VECTOR2` / `COLOR` / `OBJECT` / `ARRAY` / `DICTIONARY`（P2 范围）
- 隐式转换与比较：`int ↔ float`、`String ↔ int/float`、`==` / `<`；非法转换返回 fallback 或报错
- 类型查询：`get_type()` / `is_type<T>()` / `operator T()` 安全取值（类型不符返回默认）
- `Variant::call(method, args)`：若持有 Object*，借助 ClassDB 反射调用方法
- 序列化：`to_string()` / JSON 往返（`to_json()` / `Variant::from_json()`），支撑 .tres 文本格式

## 关键点
- **大对象走指针**：String / Array / Dictionary 内部用引用计数对象（Copy-on-Write 或共享），避免 Variant 拷贝昂贵
- **16 字节对齐目标**：常用标量（int/float/bool）走内联存储，避免堆分配；Object* 直接存裸指针
- **对齐 Godot `Variant`**：覆盖 Godot 核心类型集合；EDA 专用类型（如 Point/Polygon）通过 `Object*` 或注册为 `Variant::Object` 走反射
- **错误模型**：类型不匹配的取值不抛异常（引擎禁用异常），返回零值并打日志

## 输入 / 输出
- **入**：属性 set/get、脚本字面量、JSON 解析
- **出**：动态属性值、跨类型传递、序列化字节流

## 依赖
- **上游**：[[02-核心对象体系/01-Object基类]]（持有 Object* 时）、[[02-核心对象体系/02-ClassDB反射]]（`call` 走反射）、`core/math`（Vector2/Color）
- **下游**：[[02-核心对象体系/01-Object基类]] 属性系统、[[02-核心对象体系/05-Resource资源系统]] 序列化、Inspector（P4）、脚本（F2）

## 状态
**待实现**。进入 Plan P2 时，经 brainstorming → writing-plans 细化为可执行任务。
