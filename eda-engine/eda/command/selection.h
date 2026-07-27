#pragma once

// eda/command/selection.h
//
// P3 命令栈 · Task 8：SelectionSet<T> —— 选择系统。
//
// 对应 plan「几何与命令栈实现计划.md」Task 8 / 子功能 README「08-命令栈与选择」。
//
// 设计要点：
//   * 泛化选择集，T 通常是图元 id（int / int64_t / 指针）。要求 T 可哈希且可相等
//     比较（std::unordered_set<T> 的约束）。选择本身只存 id，**不持有图元本体**。
//   * 与 eda::geometry 解耦：eda_command 不依赖 eda_geometry，故 box_select 的
//     "Box" 由调用方传入候选集（典型：R-tree 范围查询的结果），或通过模板化的
//     provider 把 Box 映射为候选 vector（Box 类型由调用方决定）。
//   * 选择变更可包装为 SelectionChangeCommand<T>，经 UndoStack 入栈即可撤销。
//   * on_changed 信号：增删/替换/过滤/清空均触发，UI 据此刷新高亮。
//   * SelectMode 四种语义：REPLACE（替换）/ ADD（Shift 追加）/ REMOVE（移除）
//     / TOGGLE（Ctrl 切换）。
//
// 不可拷贝、不可移动：内部持有 Signal<>，需稳定地址。

#include <cstddef>
#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/object/signal.h"
#include "eda/command/command.h"

namespace eda::command {

// 选择模式：对应编辑器的修饰键语义。
enum class SelectMode {
    REPLACE,  // 替换当前选择（单击 / 普通框选）
    ADD,      // 追加（Shift-点击 / Shift-框选）
    REMOVE,   // 移除（Alt-点击）
    TOGGLE,   // 切换存在性（Ctrl-点击）
};

// SelectMode 的字符串名（日志 / 调试用）。定义在 selection.cpp。
const char* select_mode_name(SelectMode mode);

// ===========================================================================
// SelectionSet<T> —— 泛化选择集。
// ===========================================================================

template <typename T>
class SelectionSet {
public:
    using Predicate = std::function<bool(const T&)>;

    SelectionSet() = default;
    ~SelectionSet() = default;

    SelectionSet(const SelectionSet&) = delete;
    SelectionSet& operator=(const SelectionSet&) = delete;
    SelectionSet(SelectionSet&&) = delete;
    SelectionSet& operator=(SelectionSet&&) = delete;

    // -------- 单元素 --------

    void add(T item);
    void remove(const T& item);
    void toggle(T item);
    bool contains(const T& item) const noexcept {
        return items_.find(item) != items_.end();
    }

    // -------- 批量 --------

    // 按指定模式应用一组元素。
    void select(const std::vector<T>& items, SelectMode mode = SelectMode::REPLACE);

    // 框选：candidates 是调用方预先用空间索引（R-tree 等）范围查询得到的候选 id 列表，
    // SelectionSet 不感知 Box 类型（保持与 eda::geometry 解耦）。
    void box_select(const std::vector<T>& candidates,
                    SelectMode mode = SelectMode::REPLACE) {
        select(candidates, mode);
    }

    // 模板便捷：调用方传入任意 Box 类型 + provider(box)->vector<T>。
    // provider 通常是 R-tree 的范围查询闭包；Box 类型参数化避免引入 geometry 头。
    template <typename Box, typename Provider>
    void box_select(const Box& box, Provider&& provider, SelectMode mode) {
        select(provider(box), mode);
    }

    // 全选：把全集并入当前选择（不替换已有，等价于 ADD 全集）。
    void select_all(const std::vector<T>& universe);

    // 反选：在给定全集内取补集（已选变未选，未选变已选）。
    void invert(const std::vector<T>& universe);

    // -------- 过滤 --------

    // 保留满足谓词的元素。
    void filter(Predicate pred);

    // 便捷：按属性过滤。getter(item) 返回属性值，与 value 相等则保留。
    template <typename Getter, typename Attr>
    void filter_by(Getter&& getter, const Attr& value) {
        filter([&](const T& t) { return getter(t) == value; });
    }

    // -------- 清空 / 查询 --------

    void clear();
    std::size_t size() const noexcept { return items_.size(); }
    bool empty() const noexcept { return items_.empty(); }
    const std::unordered_set<T>& items() const noexcept { return items_; }
    std::vector<T> to_vector() const;

    Signal<>& on_changed() noexcept { return on_changed_; }

private:
    std::unordered_set<T> items_;
    Signal<> on_changed_;

    void notify_changed() { on_changed_.emit(); }
};

// ===========================================================================
// SelectionChangeCommand<T> —— 选择变更命令（可撤销）。
// ===========================================================================
//
// execute() 首次调用时抓取当前选择作为 old_items_，应用 new_items_（REPLACE 模式）；
// undo() 还原 old_items_；redo() 再次应用 new_items_。配合 UndoStack 即可让
// "选择变更"成为可撤销操作。

template <typename T>
class SelectionChangeCommand : public Command {
public:
    SelectionChangeCommand(SelectionSet<T>* target, std::vector<T> new_items,
                           std::string name = "Change Selection");

    void execute() override;
    void undo() override;
    void redo() override;

    static std::string get_class_static() { return "SelectionChangeCommand"; }
    std::string get_class() const override { return "SelectionChangeCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "SelectionChangeCommand" || Command::is_class(n);
    }

private:
    SelectionSet<T>* target_;
    std::vector<T> old_items_;
    std::vector<T> new_items_;
    bool captured_ = false;
};

// ===========================================================================
// 模板实现（须头文件可见）
// ===========================================================================

template <typename T>
void SelectionSet<T>::add(T item) {
    if (items_.insert(std::move(item)).second) {
        notify_changed();
    }
}

template <typename T>
void SelectionSet<T>::remove(const T& item) {
    if (items_.erase(item) > 0) {
        notify_changed();
    }
}

template <typename T>
void SelectionSet<T>::toggle(T item) {
    auto it = items_.find(item);
    if (it == items_.end()) {
        items_.insert(std::move(item));
    } else {
        items_.erase(it);
    }
    notify_changed();
}

template <typename T>
void SelectionSet<T>::select(const std::vector<T>& items, SelectMode mode) {
    bool changed = false;
    switch (mode) {
        case SelectMode::REPLACE: {
            // 仅在内容确实变化时通知：构造临时集做一次比较。
            std::unordered_set<T> next(items.begin(), items.end());
            if (items_ != next) {
                items_ = std::move(next);
                changed = true;
            }
            break;
        }
        case SelectMode::ADD:
            for (const auto& i : items) {
                if (items_.insert(i).second) changed = true;
            }
            break;
        case SelectMode::REMOVE:
            for (const auto& i : items) {
                if (items_.erase(i) > 0) changed = true;
            }
            break;
        case SelectMode::TOGGLE:
            for (const auto& i : items) {
                auto it = items_.find(i);
                if (it == items_.end()) {
                    items_.insert(i);
                } else {
                    items_.erase(it);
                }
                changed = true;
            }
            break;
    }
    if (changed) notify_changed();
}

template <typename T>
void SelectionSet<T>::select_all(const std::vector<T>& universe) {
    bool changed = false;
    for (const auto& i : universe) {
        if (items_.insert(i).second) changed = true;
    }
    if (changed) notify_changed();
}

template <typename T>
void SelectionSet<T>::invert(const std::vector<T>& universe) {
    std::unordered_set<T> next;
    next.reserve(universe.size());
    for (const auto& i : universe) {
        if (items_.find(i) == items_.end()) {
            next.insert(i);
        }
    }
    // 内容是否变化：尺寸不同必然变化；尺寸相同则逐一比对。
    bool changed = next.size() != items_.size();
    if (!changed) {
        for (const auto& i : next) {
            if (items_.find(i) == items_.end()) {
                changed = true;
                break;
            }
        }
    }
    if (changed) {
        items_ = std::move(next);
        notify_changed();
    }
}

template <typename T>
void SelectionSet<T>::filter(Predicate pred) {
    const std::size_t before = items_.size();
    for (auto it = items_.begin(); it != items_.end();) {
        if (!pred(*it)) {
            it = items_.erase(it);
        } else {
            ++it;
        }
    }
    if (items_.size() != before) notify_changed();
}

template <typename T>
void SelectionSet<T>::clear() {
    if (items_.empty()) return;
    items_.clear();
    notify_changed();
}

template <typename T>
std::vector<T> SelectionSet<T>::to_vector() const {
    return std::vector<T>(items_.begin(), items_.end());
}

template <typename T>
SelectionChangeCommand<T>::SelectionChangeCommand(SelectionSet<T>* target,
                                                  std::vector<T> new_items,
                                                  std::string name)
    : Command(std::move(name)),
      target_(target),
      old_items_(),
      new_items_(std::move(new_items)),
      captured_(false) {}

template <typename T>
void SelectionChangeCommand<T>::execute() {
    if (!captured_) {
        old_items_ = target_->to_vector();
        captured_ = true;
    }
    target_->select(new_items_, SelectMode::REPLACE);
}

template <typename T>
void SelectionChangeCommand<T>::undo() {
    target_->select(old_items_, SelectMode::REPLACE);
}

template <typename T>
void SelectionChangeCommand<T>::redo() {
    target_->select(new_items_, SelectMode::REPLACE);
}

}  // namespace eda::command
