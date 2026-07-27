---
title: 02-05 Resource资源系统
lang: zh-CN
parent: 02-核心对象体系
plan: P2
status: 待实现
tags: [EDA, P2, Resource资源系统]
related: ["[[02-核心对象体系]]"]
---

# 02-05 Resource资源系统

> 所属 [[02-核心对象体系]]。`Resource` 继承 `RefCounted`，支持 `res://` 虚拟路径、`.tres`（文本）/`.res`（二进制）序列化，由 `ResourceLoader`/`ResourceSaver` 加载保存，承载元件库、设计文件、字体、主题等共享资源。对齐 Godot `Resource`。

## 目标（要实现什么）
- 实现 `core/object/resource.{h,cpp}`：`Resource : public RefCounted`，含 `path`（res://）与 `scene_instance_count`；`Ref<Resource>` 共享
- `res://` 虚拟路径解析：将 `res://foo.tres` 映射到工程根或资源包；为后续 P5 工程系统铺路（首版可映射到可执行目录同级 `res/`）
- 序列化：`.tres` 文本（类 Godot t format，可读可 Git diff）、`.res` 二进制（紧凑快速）；首版至少实现 `.tres`
- `ResourceFormatLoader` / `ResourceFormatSaver`：按扩展名分发；插件化支持后续格式（图片、字体）
- 缓存：`ResourceCache` 按路径唯一缓存，相同 `res://` 路径多次 `load` 返回同一实例（引用计数共享）
- 资源依赖：序列化时记录子资源 path（`SubResource`），加载时递归重建

## 关键点
- **共享语义**：多个引用同一资源的对象共用 `Ref<Resource>`，零拷贝；通过引用计数自动释放
- **对齐 Godot `.tres`**：文本格式人可读、可 diff，契合 F1 "Git 友好的文本格式"原则
- **eda-engine 适配**：资源根目录 = 工程根（P5 工程就绪后接工程树）；首版用启动期注册的全局根
- **路径规范**：统一 `/`，区分大小写（Linux 安全），禁止绝对路径入库（便于工程迁移）

## 输入 / 输出
- **入**：`ResourceLoader::load("res://foo.tres")` 请求；`ResourceSaver::save(res, path)` 写入请求
- **出**：`Ref<Resource>` 实例供业务层共享；`.tres`/`.res` 文件字节流

## 依赖
- **上游**：[[02-核心对象体系/01-Object基类]]（RefCounted）、[[02-核心对象体系/02-ClassDB反射]]（按类名重建）、[[02-核心对象体系/03-Variant动态类型]]（属性读写）
- **下游**：元件库（P6 C1）、原理图/PCB 文档（P7/P8）、字体/主题（P4）、所有需要持久化的 EDA 数据

## 状态
**待实现**。进入 Plan P2 时，经 brainstorming → writing-plans 细化为可执行任务。
