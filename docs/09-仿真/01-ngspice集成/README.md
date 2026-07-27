---
title: 09-01 ngspice 集成
lang: zh-CN
parent: 09-仿真
plan: P9
status: 待实现
tags: [EDA, P9, ngspice集成]
related: ["[[09-仿真]]"]
---

# 09-01 ngspice 集成

> 所属 [[09-仿真]]。以共享库（libngspice）方式嵌入 ngspice 引擎，完成网表生成、引擎调用、仿真执行与原始向量数据回读，是整个仿真域的执行基座。

## 目标（要实现什么）
- 通过 libngspice 的 `ngSpice_Init` / `ngSpice_Command` / `ngGet_Vec_Info` C API 完成引擎初始化、命令注入与向量读取，**不依赖子进程**。
- 从内部网表（P7 原理图产出）生成 ngspice 可执行的 `.cir`/`.net` 文本（含 `.include`、`.lib`、`.param`、`.control` 块）。
- 支持异步执行 + 取消（`ngSpice_Command` 的 `bg` 后台模式 + `halt` 中断），超时阈值可配置。
- 解析 ngspice 输出的 rawfile（`write` / `ngget_cur_vec`）回填为带时间轴/扫描轴的向量结构。
- 捕获 stdout/stderr（`printf` 回调）与告警/错误码（`ngSpice_Init` 的 `SendStat`/`ControlledExit` 回调），归一到统一日志与诊断面板。
- 绑定 ngspice 版本（如 41+），记录兼容性矩阵，避免不同版本 `.measure` 语法差异导致结果漂移。

## 关键点
- **嵌入而非进程**：libngspice 同进程调用，避免 IPC 与文件中转，但需在独立线程跑仿真以不阻塞 UI（借鉴 Godot `WorkerThreadPool`）。
- **可复现性**：网表文本 + ngspice 版本号 + 模型库版本一并存入工程快照，保证 Git diff 友好与跨机复跑。
- **错误归一**：ngspice 报错（如 `singular matrix`、`timestep too small`）翻译为用户可读诊断 + 排查建议，而非裸字符串。

## 输入 / 输出
- **入**：P7 原理图网表（内部 Netlist 类）、P6 器件 SPICE 模型引用、用户分析配置（来自 09-03）。
- **出**：仿真向量数据集（Voltage/Current vs Time/Frequency），供 09-04 波形查看器；诊断日志供 UI 面板。

## 依赖
- **上游**：P7（原理图网表/位号/节点）、P6（SPICE 模型库）、09-03（分析类型配置）。
- **下游**：09-04（波形查看器消费向量）、09-05（探针绑定结果）、E1（AI 异常波形解读）。

## 状态
**待实现**。进入 Plan P9 时，经 brainstorming → writing-plans 细化为可执行任务。
