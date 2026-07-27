---
title: 01-01 平台抽象
lang: zh-CN
parent: 01-引擎地基
plan: P1
status: 待实现
tags: [EDA, P1, 平台抽象]
related: ["[[01-引擎地基]]"]
---

# 01-01 平台抽象

> 所属 [[01-引擎地基]]。定义后端无关的 `Window` 抽象与事件轮询接口，初期用 GLFW 引导实现 `GlfwWindow`，后续替换为自研 `DisplayServer`，对上层保持接口稳定。对齐 Godot `OS` / `DisplayServer`。

## 目标（要实现什么）
- 定义抽象接口 `platform/window.h`：`create(title,w,h,fullscreen,vsync)`、`poll_events()`、`swap_buffers()`、`get_client_size()`、`should_close()`、`set_vsync(mode)`
- 事件回调转发：键盘（按键 + 修饰键 + 字符输入）、鼠标（按键/移动/滚轮）、窗口尺寸/DPI 变化、关闭/最小化/最大化
- 首版 `GlfwWindow`（GLFW 3.4）实现并在 Windows 10/11 跑通；接口隔离保证 macOS/Linux 切换零业务代码改动
- HiDPI 支持：查询窗口缩放比（`get_content_scale()`），供上层决定字体/图标像素密度
- 输入事件以 `core/input::InputEvent` 值类型输出（不与具体窗口库耦合）
- 多显示器与多窗口的接口预留（首版实现单窗口即可）

## 关键点
- **接口位置**：`platform/window.h` 仅声明抽象，实现放 `platform/glfw/glfw_window.{h,cpp}`；include 路径从仓库根解析
- **零业务依赖**：Window 不引入 OpenGL/Vulkan 头，仅负责"窗口 + 输入"；上下文创建交给 driver 层
- **对齐 Godot `DisplayServer`**：分层为"窗口/事件"与"显示设备"两条职责，后续替换自研实现时仅替换 `platform/` 一层
- **GLFW 仅为引导**：参考 Godot 用 OS 抽象而非直接调 GLFW/SDL 的做法，避免业务层被特定库污染

## 输入 / 输出
- **入**：main 启动配置（窗口标题、尺寸、全屏、VSync 模式）；底层 OS 事件
- **出**：`InputEvent` 流（送 `core/input::InputState`）；窗口句柄与尺寸（送 `RenderingServer` 初始化）

## 依赖
- **上游**：无（地基层）
- **下游**：[[01-引擎地基/02-渲染服务器抽象]]（需要窗口句柄创建 GL/VK 上下文）、[[01-引擎地基/06-主循环]]（驱动 poll/swap）

## 状态
**待实现**。进入 Plan P1 时，经 brainstorming → writing-plans 细化为可执行任务。
