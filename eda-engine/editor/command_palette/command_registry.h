// editor/command_palette/command_registry.h
//
// P4 Task 6 · 命令面板（Command Registry + 模糊搜索）。
//
// Command 是编辑器动作的单一真相源：菜单项 / 工具栏按钮 / 命令面板条目 / 快捷键绑定
// 共用同一个 {id, name, description, category, shortcut, handler} 结构。业务代码
// （P7 原理图 / P8 PCB / P6 库等）通过 register_command 注册命令；命令面板
// Ctrl+Shift+P 通过 fuzzy_match 模糊搜索命令并执行 handler。
//
// CommandRegistry 是注册中心：
//   - register_command：按 id 插入或覆盖（业务后续接 handler 时复用同一路径覆盖）。
//   - unregister / get / all：常规 CRUD。
//   - register_builtins：注册 open / save / undo / redo / zoom 等占位命令（handler 空，
//     后续业务通过 register_command 同 id 覆盖以接入真实逻辑）。
//   - get()：进程级单例（Meyers singleton，与 ThemeManager 一致）；同时保留公有默认
//     构造以支持测试与多实例编辑器（命令面板经 set_registry 注入本地 registry）。

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eda {

// ===========================================================================
// 内置命令 id 占位（业务后续以同名 id 调 register_command 覆盖 handler）。
// 命名 "<域>.<动作>"，对齐 VS Code / Godot 命令 id 约定。
// ===========================================================================
namespace builtin {
inline constexpr const char* FILE_NEW        = "file.new";
inline constexpr const char* FILE_OPEN       = "file.open";
inline constexpr const char* FILE_SAVE       = "file.save";
inline constexpr const char* FILE_SAVE_AS    = "file.save_as";
inline constexpr const char* FILE_CLOSE      = "file.close";
inline constexpr const char* EDIT_UNDO       = "edit.undo";
inline constexpr const char* EDIT_REDO       = "edit.redo";
inline constexpr const char* VIEW_ZOOM_IN    = "view.zoom_in";
inline constexpr const char* VIEW_ZOOM_OUT   = "view.zoom_out";
inline constexpr const char* VIEW_ZOOM_FIT   = "view.zoom_fit";
inline constexpr const char* VIEW_ZOOM_RESET = "view.zoom_reset";
}  // namespace builtin

// ===========================================================================
// Command —— 单条用户可调用动作（聚合体，配合 designated initializers 使用）。
// ===========================================================================
//
// 字段语义：
//   - id          全局唯一键（"file.open"）；菜单 / 工具栏 / 快捷键经此关联 handler。
//   - name        显示名（"Open File..."），命令面板与菜单用此渲染 + 模糊匹配主字段。
//   - description 工具提示 / 帮助文案（可空）。
//   - category    分组（"File" / "Edit" / "View"），命令面板按类折叠。
//   - shortcut    快捷键显示串（"Ctrl+O"）；绑定逻辑在 Task 7 InputMap。
//   - handler     回调；可为空（占位命令），invoke 时跳过（不视为错误）。

struct Command {
    std::string                id;
    std::string                name;
    std::string                description;
    std::string                category;
    std::string                shortcut;
    std::function<void()>      handler;
};

// ===========================================================================
// CommandRegistry —— Command 注册中心（单一真相源）。
// ===========================================================================
//
// 生命周期：
//   - 默认构造为空 registry，便于测试与多实例编辑器各自维护命令集。
//   - get() 返回进程级单例（Meyers singleton），命令面板默认绑定此实例。
//   - 禁拷贝禁移动（内部 unordered_map 持有 Command 强引用；菜单 / 工具栏经
//     const Command* 直指 map 内地址，移动会使其失效）。
//
// 指针稳定性：register_command / unregister 可能触发 unordered_map rehash，使先前
// 由 get / all 返回的 const Command* 失效。命令面板在每次 set_query 重算时重新
// 经 all() 取指针，不跨 mutate 持有。

class CommandRegistry {
public:
    // 进程级单例（Meyers singleton，C++11 起线程安全初始化）。
    static CommandRegistry& get();

    CommandRegistry() = default;
    ~CommandRegistry() = default;

    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;
    CommandRegistry(CommandRegistry&&) = delete;
    CommandRegistry& operator=(CommandRegistry&&) = delete;

    // 按 command.id 插入或覆盖（业务后续接 handler 时复用同一路径覆盖）。
    void register_command(Command command);

    // 按 id 移除；返回是否命中。
    bool unregister(const std::string& id);

    // 按 id 查找；未找到返回 nullptr。
    const Command* get(const std::string& id) const;

    // 全部已注册命令（只读指针；指针在下次 mutate 后可能失效）。
    std::vector<const Command*> all() const;

    // 已注册命令数。
    std::size_t size() const { return commands_.size(); }

    // 注册内置占位命令（open / save / undo / redo / zoom 等；handler 空）。
    // 已存在同 id 的命令不覆盖（业务先注册的 handler 优先）。
    void register_builtins();

    // 清空全部命令（测试辅助 / teardown）。
    void clear();

private:
    std::unordered_map<std::string, Command> commands_;
};

}  // namespace eda
