---
title: 引擎地基
plan: P1
phase: Phase 0 · 引擎地基
priority: MVP
status: 待实现
tags: [EDA, P1, 引擎地基]
related: ["[[00-总览/01-功能设计目录]]", "[[00-总览/02-实现路线图]]", "[[00-总览/03-项目目录结构]]"]
---

# 01-引擎地基

> 对应 **Plan P1**（Phase 0 · 引擎地基）。自研 EDA 的底层引擎骨架：窗口、渲染服务器、双图形驱动、2D 画布与主循环，借鉴 Godot 分层架构而保持后端无关。

## 定位
- **做什么**：提供平台抽象（窗口/事件）、后端无关的 `RenderingServer` 接口、OpenGL 与 Vulkan 两套光栅化驱动、Canvas2D 矢量批渲染节点，以及组装各层的主循环入口。
- **不做什么**：不实现 EDA 业务逻辑（原理图/PCB 编辑器属 P7/P8）；不实现对象体系与反射（属 P2）；不实现几何内核与命令栈（属 P3）。
- **代码位置**：`core/math, core/input, servers/, drivers/opengl, drivers/vulkan, platform/glfw, scene/2d, main/`（见 [[00-总览/03-项目目录结构]]）

## 子功能清单
| 编号 | 子功能 | 说明 | 状态 |
|------|--------|------|------|
| 01 | [平台抽象](01-平台抽象/) | 窗口与事件循环（GLFW 引导，后替换自研 DisplayServer） | 待实现 |
| 02 | [渲染服务器抽象](02-渲染服务器抽象/) | RenderingServer 接口（后端无关，draw_triangles 等命令） | 待实现 |
| 03 | [OpenGL驱动](03-OpenGL驱动/) | RasterizerGL 后端实现 | 待实现 |
| 04 | [Vulkan驱动](04-Vulkan驱动/) | RasterizerVK 后端实现 | 待实现 |
| 05 | [2D画布](05-2D画布/) | Canvas2D 节点（矢量图元批渲染） | 待实现 |
| 06 | [主循环](06-主循环/) | main.cpp 组装各层、跑渲染+输入循环 | 待实现 |

## 依赖
- **上游**：无（地基层，整个项目的根）
- **下游**：P2（Object/Node/SceneTree 跑在主循环与渲染服务器之上）、P3（几何内核的渲染与交互依赖 Canvas2D）

## 关联
- 功能蓝图：[[00-总览/01-功能设计目录]]（引擎地基章节）
- 实现计划：[[01-引擎地基/plan/引擎地基实现计划.md]]（已有详细实现笔记，**勿修改**）
- 目录结构：[[00-总览/03-项目目录结构]]
- 进入 Plan P1 时经 brainstorming → writing-plans 产出 `plan/` 下可执行任务
