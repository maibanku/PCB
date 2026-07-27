---
title: 02-02 ClassDB反射
parent: 02-核心对象体系
plan: P2
status: 待实现
tags: [EDA, P2, ClassDB反射]
related: ["[[02-核心对象体系]]"]
---

# 02-02 ClassDB反射

> 所属 [[02-核心对象体系]]。`ClassDB` 是运行时反射数据库：注册类（名字、父类、构造）、绑定方法/信号/属性，支撑脚本调用、序列化、Inspector 属性面板、命令模式。对齐 Godot `ClassDB`。

## 目标（要实现什么）
- 实现单例 `core/object/class_db.{h,cpp}`，存储类元信息表（`ClassInfo`：name、parent、`Object*(*constructor)()`、methods、signals、properties）
- 注册宏 `GDREGISTER_CLASS(T)`：在引擎启动时把类加入表，自动建立父类链
- 绑定 API：`bind_method(name, &T::method)` / `bind_signal(name, params)` / `bind_property(name, getter, setter, type)`
- 运行时调用：`ClassDB::call(object, method_name, args) -> Variant`；按 `T::method` 签名分派
- 工厂：`instantiate(class_name) -> Object*`，按字符串名构造（脚本/序列化按类名重建对象）
- 反查 API：`get_property_list(obj)` / `get_method_list(obj)`，供 Inspector 与脚本枚举
- 父类链查询：`is_parent_class(A, B)` / `get_parent_class(name)`

## 关键点
- **零开销非反射路径**：编译期已知类型时直接 `cast_to<T>` + 直接调方法；反射仅用于 Inspector、脚本、序列化
- **对齐 Godot `ClassDB`**：API 形态（`bind_method` 等）与 Godot 一致，降低学习曲线、便于对照源码
- **构造函数表**：每个注册类存 `Object*(*)()` 函数指针，避免 RTTI，支持跨 DLL 边界（为 F2 插件铺路）
- **线程模型**：注册阶段在 main 启动单线程完成；运行期只读，无锁查询

## 输入 / 输出
- **入**：各模块在启动期调用 `GDREGISTER_CLASS` / `bind_*` 提供的元信息
- **出**：类型查询、按名构造、按名调用，供 Inspector / 序列化 / 脚本使用

## 依赖
- **上游**：[[02-核心对象体系/01-Object基类]]（被注册对象的根基）、[[02-核心对象体系/03-Variant动态类型]]（参数与返回值载体）
- **下游**：[[02-核心对象体系/05-Resource资源系统]]（序列化按类名重建）、Inspector（P4）、脚本系统（F2）、ECO 审计（按名记录变更）

## 状态
**待实现**。进入 Plan P2 时，经 brainstorming → writing-plans 细化为可执行任务。
