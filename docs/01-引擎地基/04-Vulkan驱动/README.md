---
title: 01-04 Vulkan驱动
parent: 01-引擎地基
plan: P1
status: 待实现
tags: [EDA, P1, Vulkan驱动]
related: ["[[01-引擎地基]]"]
---

# 01-04 Vulkan驱动

> 所属 [[01-引擎地基]]。`RenderingServer` 的 Vulkan 1.2 后端实现 `RasterizerVK`，面向高性能与现代图形管线，对标嘉立创 WebGPU 万焊盘能力的原生路径。对齐 Godot `RasterizerVK` / `RenderingDevice`。

## 目标（要实现什么）
- 实现 `drivers/vulkan/rasterizer_vk.{h,cpp}`，继承 `RenderingServer` 全部接口
- Vulkan 实例 + 物理设备选择 + 逻辑设备 + 队列族（graphics family）
- Swapchain（VkSurfaceKHR 由 GLFW 创建）+ 图像获取 + 呈现
- 渲染通道（color attachment，无深度首版）+ framebuffer + pipeline（动态 viewport/scissor）
- SPIR-V 着色器：glslang 编译内嵌 GLSL 或预编译嵌入；统一 color 属性
- 命令缓冲录制 + 同步（image-available / render-finished semaphore + frame fence），双缓冲帧
- 描述符集最小布局（push constants 传 transform/blend，免 descriptor 池复杂度）

## 关键点
- **首版只跑通最小管线**：与 GL 等价的"清屏 + 三角形"烟雾测试通过即可，复杂特性（compute/raytrace/multi-subpass）延后
- **验证层 (Validation Layers)**：开发期强制开启 `VK_LAYER_KHRONOS_validation`，所有 VUID 错误归零才能合并
- **Vulkan Memory Allocator (VMA)**：引入 VMA 管理 vertex buffer 内存，避免手写内存类型选择
- **帧并行**：FRAMES_IN_FLIGHT=2，避免 GPU 等待 CPU；对应 `RasterizerVK` 内部维护每帧命令池
- **与 Godot `RenderingDevice` 对齐**：长远为 compute shader / GPU picking / GPU DRC 留扩展点（首版不实现）

## 输入 / 输出
- **入**：`RenderingServer` 接口调用（顶点、变换、状态）
- **出**：在 [[01-引擎地基/01-平台抽象]] 提供的 VkSurface 上呈现一帧

## 依赖
- **上游**：[[01-引擎地基/01-平台抽象]]（GLFW 创建 VkSurface）、[[01-引擎地基/02-渲染服务器抽象]]（接口契约）、Vulkan SDK + VMA
- **下游**：[[01-引擎地基/06-主循环]]（高性能路径默认 driver）、未来 GPU 加速 DRC/拾取复用此设备

## 状态
**待实现**。进入 Plan P1 时，经 brainstorming → writing-plans 细化为可执行任务。
