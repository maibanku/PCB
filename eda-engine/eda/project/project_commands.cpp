// eda/project/project_commands.cpp
//
// P5 · 工程新建 / 保存 / 加载命令化实现。详见 project_commands.h 设计文档。
//
// 实现要点：
//   * NewProjectCommand 构造 1 Board(MAIN) + 2 Page(SCHEMATIC + PCB) 的空白树
//     —— 与 main 启动时的默认工程一致，避免空树状态导致后续面板/画布拿不到 root。
//   * SaveProjectCommand 经 ProjectSaver::save 落盘；tree 无效或父目录不可写时 emit failed。
//   * LoadProjectCommand 先 peek 文件 schema_version（行扫描状态机的极小子集，
//     不进入 [[boards]] 段），与 kExpectedSchemaVersion 比较；通过后再走 ProjectLoader。
//     这样可在新 schema 引入时（schema_version > 1）由旧版本解析器安全拒绝，
//     而不是被 ProjectLoader 的"忽略未知键"前向兼容悄悄降级。
//   * register_project_commands：四条 handler 经 lambda 捕获 current_tree 与
//     tree_changed（按引用）+ current_path（按 shared_ptr，跨 save/save_as/open
//     共享），覆写 register_builtins() 注册的同 id 占位命令。

#include "eda/project/project_commands.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

namespace eda {

// ============================================================================
// 内部辅助
// ============================================================================

namespace {

// 读取文件全部字节到字符串。文件不存在 / 不可读返回 false。
bool read_file_to_string(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream oss;
    oss << f.rdbuf();
    out = oss.str();
    return true;
}

// 简化 trim：仅去前后空白（空格 / 制表 / 回车）。
std::string trim_ws(const std::string& s) {
    auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    size_t b = 0, e = s.size();
    while (b < e && is_ws(s[b])) ++b;
    while (e > b && is_ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// 从 TOML 文本顶层扫描 schema_version = <int>。
//   - 遇到任何表头（[ ... ]，含 [[boards]] 等）即停止：schema_version 必在 ROOT 段。
//   - 注释（# ... 行尾）在解析前剥离；TOML 字符串内的 # 不会出现在 schema_version 行。
// 未找到 / 解析失败返回 -1。
int64_t peek_schema_version(const std::string& text) {
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        // 剥离行尾注释（schema_version 行不会在字符串字面量内含 #）。
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;
        // 表头出现：顶层段结束。
        if (trimmed.front() == '[') break;
        auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_ws(trimmed.substr(0, eq));
        if (key != "schema_version") continue;
        std::string raw = trim_ws(trimmed.substr(eq + 1));
        try {
            size_t pos = 0;
            long long v = std::stoll(raw, &pos);
            if (pos == raw.size()) return static_cast<int64_t>(v);
            return -1;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

}  // namespace

// ============================================================================
// NewProjectCommand
// ============================================================================

Ref<ProjectTree> NewProjectCommand::execute() {
    Ref<ProjectTree> tree(new ProjectTree());
    Project& root = tree->root();
    root.set_name("Untitled");
    Board& board = root.add_board("MainBoard", BoardType::MAIN);
    board.add_page("Sheet1", PageType::SCHEMATIC, "res://sheets/sheet1.sch.toml");
    board.add_page("PCB1",   PageType::PCB,       "res://boards/pcb1.pcb.toml");
    created_.emit();
    return tree;
}

// ============================================================================
// SaveProjectCommand
// ============================================================================

SaveProjectCommand::SaveProjectCommand(Ref<ProjectTree> tree)
    : tree_(std::move(tree)) {}

bool SaveProjectCommand::execute(const std::string& path) {
    if (!tree_.valid()) {
        failed_.emit();
        return false;
    }
    if (!ProjectSaver::save(path, tree_.get())) {
        failed_.emit();
        return false;
    }
    saved_.emit();
    return true;
}

// ============================================================================
// LoadProjectCommand
// ============================================================================

Ref<ProjectTree> LoadProjectCommand::execute(const std::string& path) {
    version_compatible_ = false;
    file_schema_version_ = -1;

    std::string text;
    if (!read_file_to_string(path, text)) {
        failed_.emit();
        return Ref<ProjectTree>();
    }

    file_schema_version_ = peek_schema_version(text);
    if (file_schema_version_ != kExpectedSchemaVersion) {
        // 版本不兼容（含未知 / 缺失 schema_version）：拒绝加载，避免旧解析器
        // 误读新版本结构后被"忽略未知键"前向兼容悄悄降级。
        failed_.emit();
        return Ref<ProjectTree>();
    }

    Ref<ProjectTree> tree = ProjectLoader::parse(text);
    if (!tree.valid()) {
        failed_.emit();
        return Ref<ProjectTree>();
    }

    version_compatible_ = true;
    loaded_.emit();
    return tree;
}

// ============================================================================
// register_project_commands
// ============================================================================

void register_project_commands(CommandRegistry& registry,
                               Ref<ProjectTree>& current_tree,
                               Signal<>& tree_changed) {
    // 当前工程路径：跨 save / save_as / open 共享。
    // shared_ptr 确保 lambda 捕获后，多次 handler 调用看到同一份状态，
    // 同时生命周期随 registry 内 Command 自洽（无需调用方维护）。
    auto current_path = std::make_shared<std::string>();

    // file.new → 新建空白工程 → 写回 current_tree → emit tree_changed
    registry.register_command({
        builtin::FILE_NEW, "New Project", "Create a new project",
        "File", "Ctrl+N",
        [&current_tree, &tree_changed]() {
            NewProjectCommand cmd;
            cmd.created().connect([&tree_changed]() { tree_changed.emit(); });
            current_tree = cmd.execute();
            // current_tree 赋值在 execute() 内部 emit created() 之前完成，
            // 故 tree_changed 触发时观察者看到的已是新树。
        }
    });

    // file.save → 已有路径则保存；否则回落 save_as（默认路径）
    registry.register_command({
        builtin::FILE_SAVE, "Save Project", "Save the current project",
        "File", "Ctrl+S",
        [&current_tree, current_path]() {
            if (!current_tree.valid()) return;
            const std::string path = current_path->empty()
                ? std::string(kDefaultProjectFileName)
                : *current_path;
            SaveProjectCommand cmd(current_tree);
            cmd.execute(path);
        }
    });

    // file.save_as → 固定默认路径（简化版，后续接 FileDialog 替换）
    registry.register_command({
        builtin::FILE_SAVE_AS, "Save Project As...", "Save to a new file",
        "File", "Ctrl+Shift+S",
        [&current_tree, current_path]() {
            if (!current_tree.valid()) return;
            const std::string path = kDefaultProjectFileName;
            *current_path = path;
            SaveProjectCommand cmd(current_tree);
            cmd.execute(path);
        }
    });

    // file.open → 从默认路径加载 → 写回 current_tree → 记录路径 → emit tree_changed
    registry.register_command({
        builtin::FILE_OPEN, "Open Project...", "Open an existing project",
        "File", "Ctrl+O",
        [&current_tree, &tree_changed, current_path]() {
            const std::string path = kDefaultProjectFileName;
            LoadProjectCommand cmd;
            Ref<ProjectTree> loaded = cmd.execute(path);
            if (!loaded.valid()) return;
            current_tree = loaded;
            *current_path = path;
            tree_changed.emit();
        }
    });
}

}  // namespace eda
