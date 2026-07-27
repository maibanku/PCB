---
title: 10-02 STEP/IGES/OBJ/VRML 导入导出
lang: zh-CN
parent: 10-3D与机械协同
plan: P10
status: 待实现
tags: [EDA, P10, STEP_IGES_OBJ_VRML导入导出]
related: ["[[10-3D与机械协同]]"]
---

# 10-02 STEP/IGES/OBJ/VRML 导入导出

> 所属 [[10-3D与机械协同]]。基于 Assimp + STEP 内核实现主流 3D 格式互操作，覆盖器件库模型导入与整机 STEP 导出。

## 目标（要实现什么）
- **导入**：OBJ、STL、VRML(`.wrl`)、Collada 走 Assimp（既定基线）；STEP/IGES 的 B-Rep 曲面导入作为 P10 启动时的独立架构决策（评估 OCCT 等 B-Rep 内核的许可证与构建成本），不在本子功能预设选型。
- **导出**：整机/单板 STEP AP203/AP214（B-Rep 体，机械 CAD 可直接编辑）、OBJ/STL（网格）、VRML（兼容 KiCad 旧格式）。
- 单位换算：mm / inch 一致映射（STEP 默认 mm，VRML 常用 inch/m），导入时自动校正板框坐标。
- 模型简化：网格 decimation（meshopt / Simplygon 算法）降低面数，适配万器件场景流畅度。
- 原点/旋转校准：导入后 UI 提供"三点对位"或"基准面对齐"工具，把模型局部坐标系对齐到封装原点。
- 批量库模型导入：从 ZIP/目录批量入库，自动关联到 P6 封装（按命名规则匹配）。
- 元数据保留：STEP 的装配树、层名、PMI 标注保留并可在 10-01 中显示。

## 关键点
- **STEP 是机械协同关键**：必须导出 B-Rep（而非三角网格）才能让 SolidWorks/Fusion 360 编辑——这是相对 KiCad（仅 VRML）的核心补强。
- **格式回环保真**：导入 STEP 再导出 STEP，几何应无损；给"导入保真度报告"（顶点/面/装配树统计）。
- **大文件性能**：整机 STEP 导出可能数百 MB，流式写入 + 内存上限保护。

## 输入 / 输出
- **入**：外部 3D 文件（厂商/机械工程师提供）、P6 封装原点定义、用户对位操作。
- **出**：入库的 3D 模型（关联 P6）、整机/单板 STEP（B-Rep）导出文件、OBJ/STL 网格导出、保真度报告。

## 依赖
- **上游**：P6（封装/3D 模型库）。（3D 格式适配器在本子功能自包含，不硬依赖 F3 总框架；P10 属 Phase 3，F3 在 Phase 4。）
- **下游**：10-01（3D 查看器渲染）、10-04（机械协同装配）。

## 状态
**待实现**。进入 Plan P10 时，经 brainstorming → writing-plans 细化为可执行任务。
