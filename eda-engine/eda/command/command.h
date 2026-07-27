#pragma once

// eda/command/command.h
//
// P3 命令栈 · Task 8：Command 接口与泛用命令。
//
// 对应 plan「几何与命令栈实现计划.md」Task 8 / 子功能 README「08-命令栈与选择」。
//
// 设计要点：
//   * Command 派生自 P2 的 eda::Object（注册 ClassDB 可选；至少重写
//     get_class / is_class / get_class_static 以便调试与 cast_to）。
//   * 三段式：execute()（首次应用，push 时由 UndoStack 调一次）/ undo()（回滚）
//     / redo()（重做，默认转发到 execute()，子类可覆写以区分"首次"与"重放"）。
//   * merge_with(other)：把连续的同类微编辑（如逐帧拖动）合并为单个撤销单元。
//     默认返回 false（不合并）。配合 UndoStack::set_merge_enabled 使用。
//   * 泛用 SetPropertyCommand：借助 P2 的 Object + Variant + ClassDB 的
//     PropertyBind 通道落地 set/get，覆盖绝大多数"改属性"类编辑。
//   * MacroCommand：事务化命令组，UndoStack::begin_macro / end_macro 内部使用，
//     亦可单独使用——子命令在 push 时已各自 execute，整体 undo 逆序、redo 正序。
//
// 与 P2 集成：属性读写统一走 helper（先查 ClassDB::find_property，回落 Object::set/get），
// 兼容"ClassDB 注册属性"与"子类直接覆写 set/get"两条路径。属性值用 Variant 承载。
//
// 命名规范：类型 PascalCase、方法 snake_case、成员变量尾下划线 name_、
// 命名空间 eda::command、include 从仓库根（#include "eda/command/command.h"）。

#include <memory>
#include <string>
#include <vector>

#include "core/object/object.h"
#include "core/object/variant.h"

namespace eda::command {

// ===========================================================================
// Command —— 命令抽象基类（继承 eda::Object）。
// ===========================================================================

class Command : public Object {
public:
    Command() = default;
    explicit Command(std::string name) : name_(std::move(name)) {}
    ~Command() override = default;

    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;

    // 首次执行：应用编辑动作并捕获 undo 所需旧状态。由 UndoStack::push 调一次。
    virtual void execute() = 0;

    // 撤销：恢复 execute() 前的状态。
    virtual void undo() = 0;

    // 重做：再次应用编辑动作。默认转发到 execute()；若"首次执行"与"重放"
    // 语义不同（例如 execute 需要捕获旧值，redo 只写新值），子类应覆写。
    virtual void redo() { execute(); }

    // 合并：将 other 的编辑并入本命令，返回 true 表示已吸收（UndoStack 不再
    // 单独 push other）。默认不合并。仅在 UndoStack 开启 merge 且栈顶为本命令时
    // 触发。典型场景：连续拖动产生的 SetPropertyCommand 序列合并为单步撤销。
    virtual bool merge_with(const Command* other);

    // 合并分组 id：相同 id 的相邻命令才有资格合并（-1 = 永不按 id 合并）。
    // 默认实现让所有"返回非 -1 id"的命令落在同一合并桶；具体合并仍由 merge_with
    // 决定。UndoStack 当前只在开启 merge 时调用 merge_with，不直接读 merge_id。
    virtual int merge_id() const { return -1; }

    const std::string& get_name() const { return name_; }
    void set_name(std::string name) { name_ = std::move(name); }

    // Object 元信息（不强制 ClassDB 注册；提供类名以便 cast_to 与调试）。
    static std::string get_class_static() { return "Command"; }
    std::string get_class() const override { return "Command"; }
    bool is_class(const std::string& n) const override {
        return n == "Command" || Object::is_class(n);
    }

protected:
    std::string name_;
};

// ===========================================================================
// MacroCommand —— 事务化命令组。
// ===========================================================================
//
// 子命令按 add 顺序保存。execute() / redo() 正序触发子命令，undo() 逆序。
// UndoStack 的 macro 流程中，子命令在 push 时已各自 execute（由 UndoStack 调用），
// macro 本体被压入主栈时不再触发 execute；后续 undo/redo 经 macro 转发到子命令。

class MacroCommand : public Command {
public:
    explicit MacroCommand(std::string name = "Macro") : Command(std::move(name)) {}

    void execute() override;
    void undo() override;
    void redo() override;

    void add_child(std::unique_ptr<Command> child);
    std::size_t child_count() const noexcept { return children_.size(); }
    const std::vector<std::unique_ptr<Command>>& children() const noexcept {
        return children_;
    }

    static std::string get_class_static() { return "MacroCommand"; }
    std::string get_class() const override { return "MacroCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "MacroCommand" || Command::is_class(n);
    }

private:
    std::vector<std::unique_ptr<Command>> children_;
};

// ===========================================================================
// SetPropertyCommand —— 泛用属性变更命令。
// ===========================================================================
//
// 持有 target_（非拥有）+ key_ + new_value_；execute() 首次调用时从 live 对象
// 抓取 old_value_，之后 undo 写旧值、redo 写新值。
//
// 合并策略：同 target + 同 key 的相邻命令合并——old 保留最早，new 取最新。

class SetPropertyCommand : public Command {
public:
    // name 为空时自动生成 "Set <key>"。
    SetPropertyCommand(Object* target, std::string key, Variant new_value,
                       std::string name = "");

    ~SetPropertyCommand() override = default;

    void execute() override;
    void undo() override;
    void redo() override;

    bool merge_with(const Command* other) override;
    int merge_id() const override { return merge_id_constant(); }
    static int merge_id_constant() { return 1; }

    Object* target() const noexcept { return target_; }
    const std::string& key() const noexcept { return key_; }
    const Variant& old_value() const noexcept { return old_value_; }
    const Variant& new_value() const noexcept { return new_value_; }

    static std::string get_class_static() { return "SetPropertyCommand"; }
    std::string get_class() const override { return "SetPropertyCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "SetPropertyCommand" || Command::is_class(n);
    }

private:
    Object* target_;
    std::string key_;
    Variant old_value_;
    Variant new_value_;
    bool captured_ = false;  // execute() 首次调用时才抓取 old_value_
};

}  // namespace eda::command
