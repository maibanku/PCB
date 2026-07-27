---
title: 13-03 PickAndPlace坐标
lang: zh-CN
parent: 13-制造输出
plan: P13
status: 待实现
tags: [EDA, P13, PickAndPlace坐标]
related: ["[[13-制造输出]]"]
---

# 13-03 PickAndPlace坐标

> 所属 [[13-制造输出]]。输出 SMT 贴片机坐标文件（CSV/TXT），含位号、中心坐标、旋转角、层别、封装与供料器信息，直供贴片机程序。

## 目标（要实现什么）
- 输出 CSV / TXT 格式坐标文件，列：Designator、X（mm）、Y（mm）、Rotation（°）、Layer（Top/Bot）、Footprint、Value、MPN
- 坐标原点可配置：板框左下角、板框中心、自定义基准点（fiducial）
- 旋转角规则：0/90/180/270（90° 步进），与封装原点定义一致（标准：器件 1 脚朝向）
- 顶层 / 底层分开导出，或合并并加 Side 列（Top / Bottom，Bottom 通常镜像 X）
- 排除 DNF（Do Not Fit）器件 / 装配变体（assembly variant），支持多变体 BOM-PnP 联动
- 贴片机厂商格式预设：富士 NXT、雅马哈 YS 系列、松下 NPM、Europlacer、Mydata 等
- 包含供料器信息（feeder slot）：与 BOM 的 MPN 对应，便于贴片机程序生成
- 输出前自动校验：坐标是否在板框内、旋转是否为有效角度、是否有重复位号

## 关键点
- 贴片机以板框左下角或第一基准点（fiducial）为原点，原点选错会导致整板偏移
- 底层器件（Bottom Side）需镜像 X 坐标（绕板框 Y 轴对称），不是简单负号
- 旋转角是封装定义相关的：封装原点 + 方向（1 脚位置）必须与坐标文件一致，否则贴反
- 与 [[07-BOM输出]] 强联动：BOM 决定哪些器件装配，PnP 决定位置，两者必须一致（位号集合相等）
- 装配变体（variant）：同一板有多个 SKU 时，按变体导出不同 PnP 文件
- fiducial（基准点）：全局 2-3 个 + 局部（精细器件如 QFP/BGA），是贴片对位关键

## 输入 / 输出
- **入**：PCB 器件布局（C3）、装配变体、贴片机格式预设
- **出**：CSV/TXT 坐标文件（每变体/每层）、坐标可视化图

## 依赖
- **上游**：C3-PCB 编辑器、[[07-BOM输出]]（装配变体）
- **下游**：SMT 工厂（贴片机程序）、[[08-装配图与制造图PDF]]（一致性核对）

## 状态
**待实现**。进入 Plan P13 时，经 brainstorming → writing-plans 细化为可执行任务。
