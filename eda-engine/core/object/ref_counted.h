#pragma once
#include "core/object/object.h"

#include <atomic>

namespace eda {

// RefCounted —— 引用计数对象（对齐 Godot RefCounted）。
// 引用计数可选：Object 默认手动管理（如 Node 由 SceneTree 持有）；
// 只有继承 RefCounted 才走自动引用计数（如 Resource 跨对象共享）。
//
// 语义：
//   - ref()         原子增 1
//   - unref()       原子减 1；归零时先发 NOTIFICATION_PREDELETE（对象仍存活），
//                   再 delete this。返回 true 表示对象已被销毁，调用方不得再访问。
//   - get_reference_count() 当前计数
class RefCounted : public Object {
public:
    static std::string get_class_static() { return "RefCounted"; }
    std::string get_class() const override { return "RefCounted"; }
    bool is_class(const std::string& name) const override {
        return name == "RefCounted" || Object::is_class(name);
    }

    // 引用计数操作。ref 必须配对 unref。
    void ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

    bool unref();

    int get_reference_count() const {
        return ref_count_.load(std::memory_order_relaxed);
    }

    // 注：计划原稿将 ctor/dtor 置于 protected 以强制走 Ref<T> 生命周期，
    //     但 Task 1 测试用例（RefCountedTest）直接 new/栈构造 RefCounted 基类，
    //     故公开默认构造与析构以使测试可编译。生产代码仍建议通过 Ref<T> 管理。
    RefCounted() = default;
    ~RefCounted() override = default;

private:
    std::atomic<int> ref_count_{0};
};

}  // namespace eda
