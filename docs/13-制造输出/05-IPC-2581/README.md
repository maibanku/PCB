---
title: 13-05 IPC-2581
lang: zh-CN
parent: 13-制造输出
plan: P13
status: 待实现
tags: [EDA, P13, IPC-2581]
related: ["[[13-制造输出]]"]
---

# 13-05 IPC-2581

> 所属 [[13-制造输出]]。输出 IPC-2581 一体化 XML 制造数据包，单文件（或 zip）聚合所有设计数据，工厂可直接导入无需逐层解析 Gerber。

## 目标（要实现什么）
- 实现 IPC-2581 **Rev C/D** 导出器：单一 XML 文件含所有制造数据
- 数据内容：各层铜/阻焊/丝印/钢网、钻孔（含盲埋孔）、器件位号/坐标/封装、网络表、BOM、装配变体
- 元数据：版本、修订记录（revision history）、设计单位、设计者、工程信息
- 多板（panel）支持：含 step-repeat、拼板信息、fiducial、tooling hole
- 增量导出：仅导出变更层/变更器件（减少文件体积，便于增量交付）
- 与 BOM（[[07-BOM输出]]）、PnP（[[03-PickAndPlace坐标]]）数据一致性保证（同源数据派生）
- 与供应链集成（E2）：可选嵌入供应链属性（MPN、批次、合规信息）
- 输出后做 schema 验证（XSD）+ 内容回读校验

## 关键点
- IPC-2581 是开放国际标准（IPC），相比 ODB++ 不需商业授权，国产 EDA 优先支持
- Rev C 是当前主流（Rev D 2018 加入 3D 与供应链扩展），工厂 CAM350 / Valor NPI 等支持
- 单文件 vs 分文件：单文件便于传输（zip），分文件便于增量；标准支持两种
- 数据完整性检查（BOM row count vs placement count vs netlist pin count）防止静默丢失
- 工厂接收端能力差异大：欧美工厂 IPC-2581 普及率高，国内多用 ODB++ 或纯 Gerber
- 与 [[06-ODB++]] 互补：根据工厂能力选用

## 输入 / 输出
- **入**：完整 PCB 设计数据（C3）、BOM、装配变体、工程元数据
- **出**：IPC-2581 XML 文件（或 zip 包）、schema 验证报告

## 依赖
- **上游**：C3-PCB 编辑器、[[07-BOM输出]]、C1 元件库
- **下游**：智能制造工厂（一站式接收）、供应链（E2）

## 状态
**待实现**。进入 Plan P13 时，经 brainstorming → writing-plans 细化为可执行任务。
