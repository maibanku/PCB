---
title: 16-06 DXF与DWG
lang: zh-CN
parent: 16-格式兼容
plan: P16
status: 待实现
tags: [EDA, P16, DXF, DWG, 机械图, 二期]
related: ["[[16-格式兼容]]"]
---

# 16-06 DXF与DWG

> 所属 [[16-格式兼容]]。机械图（DXF/DWG）的导入导出，承载板框、定位孔、禁布区、丝印等机械协同数据。**二期实现**。

## 目标（要实现什么）
- DXF（ASCII / 二进制）解析：支持 AutoCAD R12~R2024 主流版本，覆盖 `LINE`/`CIRCLE`/`ARC`/`POLYLINE`/`SPLINE`/`HATCH`/`TEXT`/`INSERT`/`LWPOLYLINE` 等核心图元
- DWG 解析：基于 libredwg 或 ODA File Converter（外部转换）读取 `.dwg` 二进制（覆盖 R2018+）
- 图层语义映射：DXF/DWG 图层名映射 EDA 语义（如 `BoardOutline` → 板框、`Keepout` → 禁布区、`Silkscreen` → 丝印、`Route` → 走线层），通过可配置映射表
- 单位与坐标系：DWG/DXF 原始单位（英寸/mm）与坐标系（Y 向上）正确变换为 EDA 坐标系（Y 向下），避免镜像/翻转错误
- 导入路径：板框导入（[[08-PCB编辑器]]）、定位孔、禁布区、丝印贴图、装配图叠加
- 导出（二期）：PCB 装配图 / 制造图导出 DXF（与 [[13-制造输出]] 集成），供机械 CAD 协同

## 关键点
- DXF 文本格式开放、文档化（AutoCAD DXF Reference），优先支持；DWG 专有需借助 libredwg（GPL，需评估许可证）或外部转换
- SPLINE（B 样条/NURBS）需拟合为多段线或圆弧才能用于 EDA 板框（EDA 几何引擎不支持任意样条），保真度报告标注
- HATCH（图案填充）映射到 EDA 铺铜区域，需处理边界环、孤岛、填充图案
- 图层映射是关键：保留可配置映射表（`layer_map.json`），用户可自定义语义规则
- 坐标系翻转：AutoCAD 默认 Y 向上、原点左下；EDA PCB 编辑器 Y 向下、原点可调（左下或中心）

## 输入 / 输出
- **入**：DXF/DWG 文件 + 图层映射配置
- **出**：EDA 几何对象（板框 / 孔 / 禁布区 / 丝印 / 文字）+ 映射日志

## 依赖
- **上游**：[[03-几何与命令栈]]（几何模型）、[[08-PCB编辑器]]（PCB 图层）、[[10-3D与机械协同]]（机械协同）
- **下游**：[[16-格式兼容/08-迁移向导]]（迁移向导调用）、[[13-制造输出]]（装配图/制造图导出 DXF）

## 状态
**待实现**（二期）。进入 Plan P16 时，经 brainstorming → writing-plans 细化为可执行任务。
