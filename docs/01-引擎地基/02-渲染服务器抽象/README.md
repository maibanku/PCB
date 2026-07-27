---
title: 01-02 渲染服务器抽象
parent: 01-引擎地基
plan: P1
status: 待实现
tags: [EDA, P1, 渲染服务器抽象]
related: ["[[01-引擎地基]]"]
---

# 01-02 渲染服务器抽象

> 所属 [[01-引擎地基]]。定义后端无关的 `RenderingServer` 接口（绘制命令、资源句柄、状态机），把上层（Canvas2D/编辑器）与具体图形 API 彻底隔离。对齐 Godot `RenderingServer`。

## 目标（要实现什么）
- 声明 `servers/rendering_server.h` 抽象基类：`begin_frame()` / `end_frame()` / `clear(color)` / `draw_triangles(verts, transform, blend)` / `set_viewport(rect)` / `set_clip(rect)`
- 定义 `CanvasVertex { Vector2 pos; Color color; }`（首版仅位置 + 颜色，后续扩展 uv/属性）
- `RenderingDriver` 枚举：`OPENGL` / `VULKAN`（/ `METAL` 预留）；`create_rendering_server(driver, window)` 工厂函数按枚举实例化具体实现
- 资源句柄 API（首版最小）：`vertex_buffer_create/upload/destroy`；为后续 texture、shader、pipeline 预留扩展点
- 状态机：混合模式（`OPAQUE` / `ALPHA_BLEND` / `ADDITIVE`）、深度测试开关、裁剪矩形栈
- 设计成"立即模式 + 内部命令缓冲"混合：上层每帧调命令，下层可批合并

## 关键点
- **零 GL/VK 头泄漏**：`rendering_server.h` 不 include 任何图形 API 头，全部用抽象类型（句柄即 `uint64_t`）
- **对齐 Godot `RenderingServer` 思路**：服务化的"绘制 API + 资源 RID"，业务层永不直接碰 driver
- **驱动选择策略**：运行期可切换（启动参数 `--renderer=vulkan`），便于在 GL 调试与 VK 高性能间切换
- **批合并契约**：相同 transform/blend 的连续 `draw_triangles` 可由实现合并为一个 draw call（万焊盘性能关键）

## 输入 / 输出
- **入**：[[01-引擎地基/05-2D画布]] 与编辑器提交的顶点/命令；[[01-引擎地基/01-平台抽象]] 的窗口句柄与尺寸
- **出**：把帧绘制到窗口默认 framebuffer（具体由 driver 实现）

## 依赖
- **上游**：[[01-引擎地基/01-平台抽象]]（窗口/上下文）
- **下游**：[[01-引擎地基/03-OpenGL驱动]] / [[01-引擎地基/04-Vulkan驱动]]（实现此接口）、[[01-引擎地基/05-2D画布]] 与所有编辑器（消费 API）

## 状态
**待实现**。进入 Plan P1 时，经 brainstorming → writing-plans 细化为可执行任务。
