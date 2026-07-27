---
title: 13-06 ODB++
parent: 13-制造输出
plan: P13
status: 待实现
tags: [EDA, P13, ODB++]
related: ["[[13-制造输出]]"]
---

# 13-06 ODB++

> 所属 [[13-制造输出]]。输出 ODB++ 制造数据包（目录结构或 .tgz 压缩），对接 Valor NPI / Camtek 等主流智能制造生态。

## 目标（要实现什么）
- 实现 ODB++ **Version 8.0+** 导出器：分层目录结构（`/steps`、`/layers`、`/symbols`、`/matrix`）
- 输出数据：所有图层（铜/阻焊/丝印/钢网）、钻孔矩阵、器件属性、网络表、封装符号、属性链（attribute chain）
- 步骤（Step）模型：单板 step、panel step（拼板）、array step（托盘），层级嵌套
- 元素属性：每个图元可挂载属性（如 net name、pin name、component ref），便于 CAM 提取
- 压缩格式：可选 zip 或 tar.gz，包含完整目录树
- 校验：导出后用 ODB++ Viewer（参考 Valor NPI 免费查看器）回读检查
- 与 BOM / 装配变体联动：变体作为 step 子集或独立 step 输出
- 与 [[05-IPC-2581]] 互补：根据工厂能力选用，国内工厂 ODB++ 接受度高

## 关键点
- ODB++ 由 Valor（现 Siemens EDA）发起，工业 CAM 系统的事实标准之一，需商业兼容（出口端无需授权）
- 目录结构优于单文件：可增量更新某层（如仅替换铜层），但传输需打包
- 属性（attributes）是 ODB++ 的精髓：标准化的 .attr 文件，让 CAM 工具能识别网络/器件关系
- Step 模型支持拼板嵌套：单板 → 拼板 step（step-repeat） → 阵列 step
- Version 8.x 引入 3D 物料体（3D solid model），可选输出器件 3D 几何
- 国内大厂（华虹、深南、景旺等）多配备 Valor NPI，ODB++ 是首选交付格式
- 与 IPC-2581 数据等价：可由 IPC-2581 转换或反之，工厂选其一即可

## 输入 / 输出
- **入**：完整 PCB 设计数据（C3）、BOM、装配变体、拼板定义（[[10-拼板面板化]]）
- **出**：ODB++ 目录或压缩包、内容校验报告

## 依赖
- **上游**：C3-PCB 编辑器、[[07-BOM输出]]、[[10-拼板面板化]]
- **下游**：智能制造工厂、CAM350 / Valor NPI

## 状态
**待实现**。进入 Plan P13 时，经 brainstorming → writing-plans 细化为可执行任务。
