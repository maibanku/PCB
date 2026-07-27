---
title: 17-02 Git集成
parent: 17-协作与版本
plan: P17
status: 待实现
tags: [EDA, P17, Git, 版本控制, 二期]
related: ["[[17-协作与版本]]"]
---

# 17-02 Git集成

> 所属 [[17-协作与版本]]��集成 Git 进行版本控制，依托 P5 开放文本格式保证 diff 可读。

## 目标（要实现什么）
- 工程一键 Git 化：工程创建时可勾选"启用 Git 版本控制"，自动 `git init` + 生成 `.gitattributes`（区分文本与二进制）+ `.gitignore`（排除缓存/快照/临时文件）
- 提交流程：编辑器内 Stage/Unstage/Commit/Diff UI，提交消息模板（约定式提交 Conventional Commits：feat/fix/docs/refactor）
- 分支管理：可视化分支图（git graph），支持创建/切换/合并/rebase 分支，适配"feature 分支开发"与"release 分支发布"工作流
- 提交前钩子：可配置 pre-commit 自动跑 ERC/DRC（调用 [[19-工具链CLI]] 无头校验），失败阻止提交（可强制跳过并记录）
- 远程同步：HTTPS/SSH 双协议，凭据管理（系统钥匙串 / SSH Agent / PAT），push/pull/fetch 操作与冲突提示
- 文本格式友好：依托 [[05-工程与文件系统]] 开放文本格式（JSON/TOML）保证 diff 可读，二进制资源（3D/图片/PDF）以 Git LFS 管理

## 关键点
- Git 操作通过 libgit2（成熟 C 库）或调用 git CLI 实现；libgit2 更内聚但 LFS 支持有限，需评估
- 提交粒度：建议以"逻辑变更"为单位（一次 ECO、一次布局调整），而不是文件粒度；UI 引导用户合理拆分提交
- `.gitattributes` 关键：JSON/TOML 标记为 `text diff`，3D/图片标记为 `binary`，并配置 LFS 过滤（`.step`/`.wrl`/`.png` > 1MB）
- 提交前钩子与 [[19-工具链CLI]] 集成，DRC 失败时显示错误清单 + 跳转选项
- 与 [[17-01]] 快照协作：Git 提交前自动创建"提交前快照"，便于回退到 pre-commit 状态

## 输入 / 输出
- **入**：工程文件变更 + 用户提交操作 + ERC/DRC 钩子配置
- **出**：Git 仓库状态、提交历史、分支图、远程同步结果

## 依赖
- **上游**：[[05-工程与文件系统]]（文本格式）
- **可选/二期**：[[19-工具链CLI]]（pre-commit 钩子的无头 ERC/DRC，二期软依赖，核心 Git 集成不依赖）
- **下游**：[[17-03]]（diff 基于 Git 历史）、[[17-04]]（云同步可基于 Git remote）

## 状态
**待实现**（二期）。进入 Plan P17 时，经 brainstorming → writing-plans 细化为可执行任务。
