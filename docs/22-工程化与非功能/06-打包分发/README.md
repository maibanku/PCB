---
title: 22-06 打包分发
lang: zh-CN
parent: 22-工程化与非功能
plan: P22
status: 待实现
tags: [EDA, P22, 打包分发]
related: ["[[22-工程化与非功能]]"]
---

# 22-06 打包分发

> 所属 [[22-工程化与非功能]]。提供 Win/Mac/Linux 三平台安装包构建、代码签名、公证（notarization）与自动更新机制，覆盖个人用户与企业分发场景。

## 目标（要实现什么）
- 三平台原生安装包：Windows MSI/EXE、macOS DMG（含 Universal Binary x86_64+arm64）、Linux AppImage/deb/rpm
- 代码签名与公证：Windows Authenticode、macOS Notarization（必需否则 Gatekeeper 拦截）、Linux 签名（GPG）
- 自动更新：版本通道（稳定/测试/夜间），增量更新优先，回滚机制
- 离线分发包：企业内网/隔离环境可用，包含完整依赖
- 可重现构建：相同 commit + 相同工具链 → 字节级一致产物，便于安全审计
- 分发元数据：版本号、changelog、最低系统要求、已知问题，自动同步到下载页与市场

## 关键点
- 自研引擎不依赖 Qt6/Tauri，依赖体积小，但 GPU 驱动/Vulkan runtime 须打包或检测
- 签名证书与公证凭据须安全管理（CI secret），不在产物中泄漏
- 自动更新须防中间人：更新包签名校验、回滚机制、断点续传
- Linux 跨发行版兼容：优先 AppImage（无需安装），deb/rpm 作为补充
- macOS Universal Binary 兼顾 Apple Silicon 与 Intel，避免双份下载

## 输入 / 输出
- **入**：构建产物、签名证书、版本元数据、changelog
- **出**：三平台安装包、签名文件、自动更新 manifest、SHA-256 校验值

## 依赖
- **上游**：[[22-工程化与非功能/05-测试策略]]（测试通过才打包）、[[22-工程化与非功能/04-安全]]（签名凭据）
- **下游**：终端用户、[[18-扩展生态/04-市场]]（市场可能引用版本通道）

## 状态
**待实现**。进入 Plan P22 时，经 brainstorming → writing-plans 细化为可执行任务。
