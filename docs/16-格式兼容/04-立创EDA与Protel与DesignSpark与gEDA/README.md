---
title: 16-04 立创EDA与Protel与DesignSpark与gEDA
lang: zh-CN
parent: 16-格式兼容
plan: P16
status: 待实现
tags: [EDA, P16, 立创EDA, Protel, DesignSpark, gEDA, 二期]
related: ["[[16-格式兼容]]"]
---

# 16-04 立创EDA与Protel与DesignSpark与gEDA

> 所属 [[16-格式兼容]]。立创EDA、Protel、DesignSpark、gEDA 各格式的解析与导入。**二期实现**（MVP 仅 KiCad）。

## 目标（要实现什么）
- **立创EDA（嘉立创）**：导入 JSON 导出包（标准版 / 专业版），含工程结构、原理图、PCB、器件库；处理立创器件 ID 到立创商城供应链信息的映射（直供 [[21-供应链集成]]）
- **Protel 99SE**：解析 `.Ddb` 设计数据库（基于 OLE 复合文档结构），含 `.Sch`/`.Pcb`/`.Lib`，覆盖经典 90s/2000s 老工程迁移
- **DesignSpark PCB / Mechanical**：基于 Altium OEM 内核，复用 [[16-格式兼容/02-Altium格式]] Altium 适配器主链路，差异部分（菜单/输出预设）单独适配
- **gEDA**：开源 gschem `.sch` 文本格式与 PCB `.fp` 脚印库、`.pcb` 文件，纯文本/SED 友好易解析
- 各格式版本识别：自动判定版本/变体（立创标准/专业、Protel 99SE/98、DesignSpark v8/v9、gEDA 1.10+），路由到对应解析路径
- 导出（二期）：开放格式（gEDA、立创 JSON）优先支持导出；专有格式（Protel、DesignSpark）仅按需求支持

## 关键点
- 立创EDA 是国产主流，导入直供供应链是差异化重点；JSON 格式开放但字段较多，需维护 schema 兼容表
- Protel 99SE 工程老但国内存量巨大，与 Altium 二进制格式相通但更老；可考虑先用 Protel → Altium 转换工具中转
- DesignSpark 与 Altium 共享 `.SchDoc/.PcbDoc` 文件，但工程扩展名与变量定义不同（`.EPrj`/`.PcbDoc`）
- gEDA `gschem` 文本格式基于 lisp-like 语法（`(object ...)`），与 KiCad S-expr 类似，可复用部分解析器基础
- 立创EDA 标准版与专业版（Pro）JSON 结构差异较大，需分别适配

## 输入 / 输出
- **入**：立创 JSON 工程包、Protel `.Ddb`、DesignSpark 工程文件、gEDA `.sch/.pcb/.fp`
- **出**：内部工程/原理图/PCB/器件库 + 保真度报告（立创导入附加供应链属性）

## 依赖
- **上游**：[[05-工程与文件系统]]、[[06-元件库]]、[[07-原理图编辑器]]、[[08-PCB编辑器]]
- **下游**：[[16-格式兼容/08-迁移向导]]、[[16-格式兼容/09-保真度报告]]、[[16-格式兼容/10-适配器插件化]]、[[21-供应链集成]]（立创 ID 作为附加属性直供供应链，非阻塞）；[[16-格式兼容/02-Altium格式]]（DesignSpark 复用）

## 状态
**待实现**（二期）。进入 Plan P16 时，经 brainstorming → writing-plans 细化为可执行任务。
