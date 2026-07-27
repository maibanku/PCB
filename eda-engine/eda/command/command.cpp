// eda/command/command.cpp
//
// P3 命令栈 · Task 8：Command / MacroCommand / SetPropertyCommand 实现。
//
// 属性读写统一走 helper：先查 ClassDB::find_property（注册属性走 PropertyBind），
// 否则回落到 Object::set/get（子类可直接覆写）。两条路径对 SetPropertyCommand
// 透明，覆盖绝大多数业务对象的属性编辑场景。
//
// 注意：核心 -fno-rtti，向下转换走 ClassDB 字符串类名比较（cast_to 风格）。
// merge_with 中先比对 get_class() 字符串，命中后再 static_cast 安全。

#include "eda/command/command.h"

#include <utility>

#include "core/object/class_db.h"

namespace eda::command {

// ===========================================================================
// Command 基类
// ===========================================================================

bool Command::merge_with(const Command* /*other*/) {
    return false;  // 默认不合并；子类按需覆写
}

// ===========================================================================
// MacroCommand
// ===========================================================================

void MacroCommand::execute() {
    // 整体首次执行：正序触发每个子命令的 execute（各自负责捕获旧值 + 应用）。
    // 在 UndoStack::push 的 macro 流程中不会调用到这里（子命令在 push 时已 execute），
    // 该路径仅用于手动构造 MacroCommand 的独立使用场景。
    for (auto& child : children_) {
        child->execute();
    }
}

void MacroCommand::undo() {
    // 逆序撤销：保证 A→B→C 的执行序，撤销序为 C→B→A，状态严格回退。
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        (*it)->undo();
    }
}

void MacroCommand::redo() {
    // 正序重做。
    for (auto& child : children_) {
        child->redo();
    }
}

void MacroCommand::add_child(std::unique_ptr<Command> child) {
    if (child) {
        children_.push_back(std::move(child));
    }
}

// ===========================================================================
// 内部 helper：统一属性读写
// ===========================================================================

namespace {

// 写属性：先 ClassDB（注册属性），否则回落 Object::set。返回是否成功。
bool apply_property(Object* target, const std::string& key, const Variant& v) {
    if (target == nullptr) return false;
    auto* pb = ClassDB::find_property(target, key);
    if (pb != nullptr) {
        return pb->set(target, v);
    }
    return target->set(key, v);
}

// 读属性：先 ClassDB（注册属性），否则回落 Object::get。缺省返回 NIL。
Variant read_property(Object* target, const std::string& key) {
    if (target == nullptr) return Variant();
    auto* pb = ClassDB::find_property(target, key);
    if (pb != nullptr) {
        return pb->get(target);
    }
    return target->get(key);
}

}  // namespace

// ===========================================================================
// SetPropertyCommand
// ===========================================================================

SetPropertyCommand::SetPropertyCommand(Object* target, std::string key,
                                       Variant new_value, std::string name)
    : Command(std::move(name)),
      target_(target),
      key_(std::move(key)),
      old_value_(),
      new_value_(std::move(new_value)),
      captured_(false) {
    if (name_.empty()) {
        name_ = "Set " + key_;
    }
}

void SetPropertyCommand::execute() {
    if (!captured_) {
        // 首次执行：从 live 对象抓取旧值（之后 undo 用）。即便 target_ 为空也标记
        // captured_，避免下次 execute 重抓（语义上仍是"已应用"）。
        old_value_ = read_property(target_, key_);
        captured_ = true;
    }
    apply_property(target_, key_, new_value_);
}

void SetPropertyCommand::undo() {
    apply_property(target_, key_, old_value_);
}

void SetPropertyCommand::redo() {
    apply_property(target_, key_, new_value_);
}

bool SetPropertyCommand::merge_with(const Command* other) {
    if (other == nullptr) return false;
    // 不使用 RTTI；按类名字符串严格匹配，命中后 static_cast 安全。
    if (other->get_class() != SetPropertyCommand::get_class_static()) {
        return false;
    }
    const auto* sp = static_cast<const SetPropertyCommand*>(other);
    if (sp->target_ != target_ || sp->key_ != key_) {
        return false;  // 不同对象或不同属性：不合并
    }
    // 合并：保留最早旧值，新值取最新。live 对象已被 other->execute() 推进到
    // sp->new_value_，这里只需把本命令的 new_value_ 同步到最新即可。
    new_value_ = sp->new_value_;
    return true;
}

}  // namespace eda::command
