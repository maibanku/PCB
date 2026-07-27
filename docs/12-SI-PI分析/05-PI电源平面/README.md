---
title: 12-05 PI电源平面
parent: 12-SI-PI分析
plan: P12
status: 待实现
tags: [EDA, P12, PI电源平面]
related: ["[[12-SI-PI分析]]"]
---

# 12-05 PI电源平面

> 所属 [[12-SI-PI分析]]。分析电源/地平面的 AC 阻抗（目标阻抗法）、平面谐振模态和 SSN 同步开关噪声，找出 PDN（电源分配网络）薄弱点。

## 目标（要实现什么）
- 计算 PDN 目标阻抗：Ztarget = ΔV_allowed / ΔI_transient（如 50mV/1A = 50mΩ），按频段分目标
- 平面 AC 阻抗扫描（Z11 vs 频率，1MHz-1GHz）：识别阻抗峰值频点
- 平面腔体谐振（cavity resonance）模态分析：基于平面尺寸 + 介质厚度 → 谐振频率序列
- 同步开关噪声（SSN / ΔI 噪声）：估算 IC 同时翻转时的地弹/电源弹
- DC + AC 联合分析：DC 压降（[[07-IR-Drop分析]]）→ AC 余量
- 输出阻抗地图（彩色 heatmap），高亮平面谐振风险区，建议加 decoupling / 改平面分割

## 关键点
- 目标阻抗法是工业主流：在 IC 电源引脚处测得的 PDN 阻抗应 < Ztarget，整个频段
- 平面谐振由平行平面腔体形成（f = c/(2·L·√Dk) 等），在 GHz 段会放大噪声
- decoupling 电容抑制低频（< 100MHz），平面电容（planar capacitance）与 MLCC 抑制高频
- 平面分割（split plane）会让回流路径突变，引入额外电感，是 PI 与 EMI 的常见坑
- 平面谐振模态与平面尺寸直接相关：电源平面长边 100mm 时，第一谐振点约 600MHz

## 输入 / 输出
- **入**：平面几何（来自 C3 铺铜）、叠层、IC 瞬态电流（datasheet 或估算）、目标电压纹波
- **出**：Z11 vs 频率曲线、平面谐振模态表、阻抗 heatmap、SSN 估算、改进建议

## 依赖
- **上游**：[[08-叠层规则联动]]、C3-PCB 编辑器（铺铜）、器件库（IC 模型）
- **下游**：[[06-去耦优化]]、[[07-IR-Drop分析]]（DC 部分）、PI 综合报告

## 状态
**待实现**。进入 Plan P12 时，经 brainstorming → writing-plans 细化为可执行任务。
