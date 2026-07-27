// editor/command_palette/command_registry.cpp
//
// CommandRegistry 实现：CRUD + 内置占位命令。
//
// register_command 注意：先拷 id 到本地 key，再 std::move(command) 入 map，避免
// "commands_[c.id] = std::move(c)" 中 c.id 与 std::move(c) 求值顺序导致的 UAF
// 风险（move 后 c.id 处于 valid-but-unspecified 状态）。

#include "editor/command_palette/command_registry.h"

#include <utility>

namespace eda {

// Meyers singleton：函数内 static，首次调用构造；C++11 起线程安全。
CommandRegistry& CommandRegistry::get() {
    static CommandRegistry instance;
    return instance;
}

void CommandRegistry::register_command(Command command) {
    // 先拷 id 出来作 key，再移动 command（避免 move 后读 id）。
    std::string id = command.id;
    commands_[std::move(id)] = std::move(command);
}

bool CommandRegistry::unregister(const std::string& id) {
    return commands_.erase(id) > 0;
}

const Command* CommandRegistry::get(const std::string& id) const {
    auto it = commands_.find(id);
    return it == commands_.end() ? nullptr : &it->second;
}

std::vector<const Command*> CommandRegistry::all() const {
    std::vector<const Command*> out;
    out.reserve(commands_.size());
    for (const auto& entry : commands_) {
        out.push_back(&entry.second);
    }
    return out;
}

void CommandRegistry::register_builtins() {
    // 已存在同 id 则不覆盖（业务先注册的真实 handler 优先于占位）。
    auto add = [this](Command c) {
        auto it = commands_.find(c.id);
        if (it != commands_.end()) return;
        std::string id = c.id;
        commands_[std::move(id)] = std::move(c);
    };

    // 字段顺序对齐 Command 聚合体：id / name / description / category / shortcut / handler。
    // handler 留空（{}）——占位命令，业务后续以同 id 调 register_command 覆盖接入。
    add({"file.new",        "New Project",        "Create a new project",     "File", "Ctrl+N",       {}});
    add({"file.open",       "Open File...",       "Open an existing file",    "File", "Ctrl+O",       {}});
    add({"file.save",       "Save File",          "Save the current file",    "File", "Ctrl+S",       {}});
    add({"file.save_as",    "Save As...",         "Save to a new file",       "File", "Ctrl+Shift+S", {}});
    add({"file.close",      "Close File",         "Close the current file",   "File", "Ctrl+W",       {}});
    add({"edit.undo",       "Undo",               "Undo the last action",     "Edit", "Ctrl+Z",       {}});
    add({"edit.redo",       "Redo",               "Redo the last undo",       "Edit", "Ctrl+Y",       {}});
    add({"view.zoom_in",    "Zoom In",            "Increase zoom level",      "View", "Ctrl++",       {}});
    add({"view.zoom_out",   "Zoom Out",           "Decrease zoom level",      "View", "Ctrl+-",       {}});
    add({"view.zoom_fit",   "Zoom to Fit",        "Fit content to viewport",  "View", "",             {}});
    add({"view.zoom_reset", "Reset Zoom",         "Reset zoom to 100%",       "View", "Ctrl+0",       {}});
}

void CommandRegistry::clear() {
    commands_.clear();
}

}  // namespace eda
