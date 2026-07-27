---
title: 12-03 反射与眼图
lang: zh-CN
parent: 12-SI-PI分析
plan: P12
status: 待实现
tags: [EDA, P12, 反射与眼图]
related: ["[[12-SI-PI分析]]"]
---

# 12-03 反射与眼图

> 所属 [[12-SI-PI分析]]。基于网络拓扑与 IBIS 模型，仿真高速信号的反射/振铃、生成眼图，评估 SI 余量并推荐端接方案。

## 目标（要实现什么）
- 提取网络拓扑（点对点、多 drop、T 分支、菊花链），支持在拓扑编辑器中可视化
- 用 IBIS 驱动器/接收器模型 + 损耗传输线模型（RLGC 频变），做瞬态仿真（与 C4 ngspice 协同或独立引擎）
- 计算反射系数（ρ = (Zload−Z0)/(Zload+Z0)），定位阻抗失配点
- 生成眼图（含抖动分解：DDJ/ISI/RJ），输出眼高/眼宽/眼开度、BER 估算（Q 因子法）
- 端接建议：串联（源端）、并联（终端）、戴维南、AC 端接（RC），并给推荐值
- 预扫描：跑随机激励 + PRBS 码型，自动发现振铃/过冲 > 阈值的网络

## 关键点
- IBIS（I/O Buffer Information Specification，v1.3~7.0）是行业标准，非 SPICE 模型；需解析 V/I、V/t、波形曲线
- 损耗线建模：频变 RLGC + 趋肤效应（√f）+ 介质损耗（tanδ·f），低频 R、高频 L' 表皮深度修正
- 眼图掩模（eye mask）：按协议（USB3/PCIe/DDR4/HDMI）预置，自动判 PASS/FAIL
- DDR 等多负载网络：拓扑 + 等长 + ODT（On-Die Termination）配置共同决定 SI，需联合仿真
- 反射引起的振铃 → 过冲可能损坏器件栅极，是高速设计首要风险

## 输入 / 输出
- **入**：网络拓扑（C3 PCB）、IBIS 模型（器件库）、协议眼图掩模（预设库）
- **出**：瞬态波形、眼图、反射系数表、BER 估算、端接建议、SI 报告

## 依赖
- **上游**：[[01-SI阻抗分析]]、[[04-过孔stub分析]]（via 影响）、IBIS 模型库
- **下游**：SI 综合报告、AI 审查（E1）输入

## 状态
**待实现**。进入 Plan P12 时，经 brainstorming → writing-plans 细化为可执行任务。
