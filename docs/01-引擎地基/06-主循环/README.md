---
title: 01-06 主循环
parent: 01-引擎地基
plan: P1
status: 待实现
tags: [EDA, P1, 主循环]
related: ["[[01-引擎地基]]"]
---

# 01-06 主循环

> 所属 [[01-引擎地基]]。`main/main.cpp` 组装平台、输入、渲染服务器、Canvas2D 各层，驱动"轮询事件 → 更新 → 渲染 → 交换缓冲"的主循环，并在 P1 终态交付一个可交互的 EDA 风格 Demo 窗口。

## 目标（要实现什么）
- 实现 `main/main.cpp`：实例化 `Window` → `InputState` → `RenderingServer`（按命令行 `--renderer` 选 GL/VK，默认 GL）→ `Canvas2D`
- 主循环：`while (!window.should_close()) { poll_events → input_state.update → on_update(delta) → canvas.begin/draw/end → server.end_frame → swap_buffers }`
- 固定步长 update（首版可同步 60Hz；预留 `accumulated_time` 解耦渲染与逻辑）
- EDA Demo：绘制焊盘矩形阵列、走线折线、mm/mil 双栅格、原点十字；鼠标滚轮 zoom、中键拖拽 pan
- 命令行参数解析：`--renderer={gl|vk}`、`--width`、`--height`、`--vsync={0|1}`
- 清理顺序由 RAII 保证（unique_ptr 析构逆序）；崩溃落日志（首版 `stderr`，正式崩溃上报在后续 plan）

## 关键点
- **组装而非实现**：main 只负责"把各层拼起来"，不写业务逻辑；EDA Demo 仅作 P1 可视化验证，P7/P8 编辑器就绪后替换
- **delta 计算**：高精度计时器（`std::chrono::steady_clock`）；钳制最大 delta（避免断点恢复后跳大步）
- **退出条件**：窗口关闭、ESC 键（首版硬编码，后续走快捷键体系）、致命错误
- **Vulkan/GL 差异吸收**：通过 `create_rendering_server(driver, window)` 工厂屏蔽，main 不写 `#ifdef USE_VULKAN`

## 输入 / 输出
- **入**：启动配置（命令行参数、窗口尺寸、driver 选择）
- **出**：可运行的程序窗口（显示 EDA 风格矢量图形并响应 pan/zoom）

## 依赖
- **上游**：[[01-引擎地基/01-平台抽象]] / [[01-引擎地基/02-渲染服务器抽象]] / [[01-引擎地基/05-2D画布]]（被组装）
- **下游**：P2（`SceneTree::process` 替换裸循环，主循环改造为驱动节点树）、后续编辑器（替换 EDA Demo 为真实编辑器入口）

## 状态
**待实现**。进入 Plan P1 时，经 brainstorming → writing-plans 细化为可执行任务。
