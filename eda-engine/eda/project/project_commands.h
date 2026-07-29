#pragma once

// eda/project/project_commands.h
//
// P5 · 工程新建 / 保存 / 加载命令化（接 CommandRegistry，供 main 菜单
// File→New/Save/Save As/Open 调用）。
//
// 三条命令封装工程级操作：
//   - NewProjectCommand  : 构造空白工程树（1 Board[MAIN] + 1 Page[SCHEMATIC]
//                                     + 1 Page[PCB]）→ Ref<ProjectTree>
//   - SaveProjectCommand : ProjectTree → ProjectSaver::save(path) → bool
//                          + Signal saved/failed
//   - LoadProjectCommand : path → ProjectLoader::load → Ref<ProjectTree>
//                          + Signal loaded/failed + 版本兼容检查
//
// register_project_commands 把上述命令接到 CommandRegistry（按 id 覆盖
// register_builtins() 留下的占位 handler）：
//   - file.new     → 新建工程树 → 写回 current_tree → emit tree_changed
//   - file.save    → 若已有路径则保存到现有路径，否则回落 save_as（默认路径）
//   - file.save_as → 保存到默认路径 "untitled.pcbproj"
//   - file.open    → 从默认路径加载 → 写回 current_tree → emit tree_changed
//
// main 的 MenuBar / ToolBar / InputMap 通过 command_id 路由到同一 handler，
// 无需直接耦合 ProjectTree。current_tree / tree_changed 由调用方持有，生命
// 周期需覆盖 registry 命令的使用期（典型为 main 主循环局部）。
//
// 依赖：eda_project（ProjectTree / ProjectLoader / ProjectSaver）
//       editor/command_palette/CommandRegistry
//       core/object/{Ref, Signal}

#include <cstdint>
#include <string>

#include "core/object/ref.h"
#include "core/object/signal.h"
#include "editor/command_palette/command_registry.h"
#include "eda/project/project.h"

namespace eda {

// ============================================================================
// 默认工程文件路径与版本兼容基准
// ============================================================================

// 简化版：固定默认文件名（实际路径相对于进程 CWD）。
// 后续接入 FileDialog 后替换为用户选择路径。
inline constexpr const char* kDefaultProjectFileName = "untitled.pcbproj";

// 当前实现支持的 schema_version。LoadProjectCommand 以此为基准做版本兼容检查
// （文件 schema_version 与此不等则视为不兼容，emit failed 并返回空 Ref）。
inline constexpr int64_t kExpectedSchemaVersion = 1;

// ============================================================================
// NewProjectCommand —— 构造空白工程树
// ============================================================================
//
// execute() 返回新构造的 ProjectTree：
//   Project(name = "Untitled")
//     └ Board(name = "MainBoard", type = MAIN)
//         ├ Page(name = "Sheet1", type = SCHEMATIC, path = "res://sheets/sheet1.sch.toml")
//         └ Page(name = "PCB1",   type = PCB,       path = "res://boards/pcb1.pcb.toml")
//
// created() 在树构造完毕后 emit 一次，便于外部观察者（ProjectTree 面板 /
// tree_changed 信号级联）响应。

class NewProjectCommand {
public:
    NewProjectCommand() = default;
    ~NewProjectCommand() = default;

    NewProjectCommand(const NewProjectCommand&) = delete;
    NewProjectCommand& operator=(const NewProjectCommand&) = delete;

    // 构造空白工程树；emit created() 后返回。
    Ref<ProjectTree> execute();

    Signal<>& created() { return created_; }

private:
    Signal<> created_;
};

// ============================================================================
// SaveProjectCommand —— ProjectTree → 磁盘
// ============================================================================

class SaveProjectCommand {
public:
    // 持有待保存的 tree（Ref 共享所有权；tree 无效时 execute 必然 failed）。
    explicit SaveProjectCommand(Ref<ProjectTree> tree);
    ~SaveProjectCommand() = default;

    SaveProjectCommand(const SaveProjectCommand&) = delete;
    SaveProjectCommand& operator=(const SaveProjectCommand&) = delete;

    // 序列化 tree 到 path（覆盖写）。
    //   成功：emit saved()，返回 true
    //   失败：emit failed()，返回 false（tree 为空 / 父目录不可写）
    bool execute(const std::string& path);

    Signal<>& saved() { return saved_; }
    Signal<>& failed() { return failed_; }

private:
    Ref<ProjectTree> tree_;
    Signal<> saved_;
    Signal<> failed_;
};

// ============================================================================
// LoadProjectCommand —— 磁盘 → ProjectTree
// ============================================================================

class LoadProjectCommand {
public:
    LoadProjectCommand() = default;
    ~LoadProjectCommand() = default;

    LoadProjectCommand(const LoadProjectCommand&) = delete;
    LoadProjectCommand& operator=(const LoadProjectCommand&) = delete;

    // 从 path 加载工程树。
    //   成功：emit loaded()，返回有效 Ref，version_compatible() == true
    //   失败：emit failed()，返回空 Ref（读不出 / schema 不兼容 / 解析失败）
    //
    // 版本兼容检查：先 peek 文件顶层 schema_version（不进入 [[boards]] 段），
    // 与 kExpectedSchemaVersion 比较；不等则视为不兼容，避免旧版本解析器误读
    // 新版本结构。peek 在完整 parse 之前发生，可拒绝尚未支持的 schema。
    Ref<ProjectTree> execute(const std::string& path);

    Signal<>& loaded() { return loaded_; }
    Signal<>& failed() { return failed_; }

    // 最近一次 execute 的版本兼容结果（成功加载且 schema_version 匹配）。
    bool version_compatible() const { return version_compatible_; }
    // 最近一次 execute 读到的文件 schema_version（未读到 / 文件缺失 为 -1）。
    int64_t file_schema_version() const { return file_schema_version_; }

private:
    Signal<> loaded_;
    Signal<> failed_;
    bool version_compatible_ = false;
    int64_t file_schema_version_ = -1;
};

// ============================================================================
// register_project_commands —— 把工程命令注册到 CommandRegistry
// ============================================================================

void register_project_commands(CommandRegistry& registry,
                               Ref<ProjectTree>& current_tree,
                               Signal<>& tree_changed);

}  // namespace eda
