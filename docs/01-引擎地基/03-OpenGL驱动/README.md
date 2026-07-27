---
title: 01-03 OpenGL驱动
parent: 01-引擎地基
plan: P1
status: 待实现
tags: [EDA, P1, OpenGL驱动]
related: ["[[01-引擎地基]]"]
---

# 01-03 OpenGL驱动

> 所属 [[01-引擎地基]]。`RenderingServer` 的 OpenGL 后端实现 `RasterizerGL`，作为优先落地的参考驱动，确保业务层跑通后再切 Vulkan。对齐 Godot `RasterizerGL`。

## 目标（要实现什么）
- 实现 `drivers/opengl/rasterizer_gl.{h,cpp}`，继承 `RenderingServer` 全部接口
- 用 glad 加载 OpenGL 4.3 Core 函数；窗口上下文由 GLFW 创建（GLFW_OPENGL_CORE_PROFILE）
- 内嵌 GLSL 着色器（顶点：`proj * pos`；片段：直通 `color`），统一 color 属性，免纹理
- VAO/VBO 管理��`draw_triangles` 上传顶点 → `glDrawArrays(GL_TRIANGLES)`；静态批可缓存到持久 VBO
- 状态切换：混合模式映射到 `glBlendFunc`（ALPHA_BLEND = SRC_ALPHA,ONE_MINUS_SRC_ALPHA）、裁剪映射到 `glScissor`
- 烟雾测试：初始化 → 清屏 → 一帧绘制三角形 → 清理，全程无 GL 报错（KHR_debug 输出）

## 关键点
- **定位为"参考驱动"**：功能正确优先，性能优化次之；用于业务联调与 CI 烟雾测试
- **GL 版本固定**：OpenGL 4.3 Core（覆盖 Win10+ 默认驱动；macOS 限 4.1，后续用 GL ES 3.0 兼容）
- **_KHR_debug**：开启 `GL_DEBUG_OUTPUT`，所有 GL 错误落到日志，便于 P1 排错
- **VSync**：通过 `glfwSwapInterval(1)` 控制，不直连 GL 扩展

## 输入 / 输出
- **入**：`RenderingServer` 接口调用（顶点、变换、状态）
- **出**：在 [[01-引擎地基/01-平台抽象]] 提供的 GL 上下文上呈现一帧

## 依赖
- **上游**：[[01-引擎地基/01-平台抽象]]（GLFW 创建 GL 上下文）、[[01-引擎地基/02-渲染服务器抽象]]（接口契约）
- **下游**：[[01-引擎地基/06-主循环]]（首版默认 driver）、后续编辑器在无 Vulkan 环境下的回退

## 状态
**待实现**。进入 Plan P1 时，经 brainstorming → writing-plans 细化为可执行任务。
