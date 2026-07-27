#pragma once

// Signal —— 模板化信号槽（对齐 Godot Signal<Args...>）。
//
// 核心：
//   - connect(Callable) / connect(&T::method, T*) / connect(lambda|free fn)
//   - emit(Args...) 触发所有连接（重入安全：emit 用快照保护迭代器）
//   - disconnect(Callable) / disconnect_all()
//
// 析构解绑（核心难点，需双向引用）：
//   方向 A：Object 死 → 通知 Signal 清理
//     Object 持有 connected_signals_（SignalBase* 列表）；析构时调用
//     _signal_disconnect_all()，遍历列表对每个 Signal 调 _disconnect_object(this)，
//     该 Signal 移除所有 target==this 的 Callable，并从 tracked_objects_ 去掉自己。
//   方向 B：Signal 死 → 通知 Object 清理
//     Signal 持有 tracked_objects_（Object* 列表）；析构时对每个 Object 调
//     _signal_unregister_connection(this)，避免 Object 持有悬空 SignalBase*。
//
// =====================================================================
//  集成契约（由主循环 / 集成步骤在 core/object/object.{h,cpp} 落地）：
// =====================================================================
//  object.h 需新增（forward-declare class SignalBase;）：
//    public:
//      void _signal_register_connection(SignalBase* s);     // 幂等 push_back
//      void _signal_unregister_connection(SignalBase* s);   // 存在则 erase
//    private:
//      std::vector<SignalBase*> connected_signals_;
//  object.cpp 修改：
//    - ~Object() 体内调用 _signal_disconnect_all();  // 替换原 = default
//    - _signal_disconnect_all() 实现：
//        for (SignalBase* s : connected_signals_) s->_disconnect_object(this);
//        connected_signals_.clear();
//    - _signal_register_connection / _signal_unregister_connection 实现
//  core/CMakeLists.txt 追加 object/signal.cpp
//  tests/CMakeLists.txt 追加 test_signal.cpp
// =====================================================================

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/object/callable.h"
#include "core/object/object.h"

namespace eda {

// ---------------------------------------------------------------------------
// SignalBase —— 非模板基类。Object 持有 SignalBase* 列表，无需知道模板参数。
// ---------------------------------------------------------------------------

class SignalBase {
public:
    virtual ~SignalBase() = default;
    // 移除所有 target==obj 的连接；同时把 obj 从 tracked_objects_ 去除。
    virtual void _disconnect_object(Object* obj) = 0;
};

// ---------------------------------------------------------------------------
// Signal<Args...> —— 模板信号
// ---------------------------------------------------------------------------

template <typename... Args>
class Signal : public SignalBase {
public:
    Signal() = default;
    ~Signal();

    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    // 不可拷贝亦不可移动：Object 的 connected_signals_ 按地址持有本 Signal，
    // 移动后旧地址将失效。Signal 应作为对象成员在稳定地址上长期持有。
    Signal(Signal&&) = delete;
    Signal& operator=(Signal&&) = delete;

    // 主入口：连接一个 Callable。幂等（同一 Callable 重复 connect 不重复触发）。
    void connect(Callable cb);

    // 便捷：成员函数 + 对象指针
    template <typename T, typename... MArgs>
    void connect(void (T::*method)(MArgs...), T* obj) {
        connect(Callable(method, obj));
    }

    // 便捷：lambda / 自由函数
    template <typename F,
              typename = std::enable_if_t<
                  !std::is_same_v<std::decay_t<F>, Callable>>>
    void connect(F&& fn) {
        connect(Callable(std::forward<F>(fn)));
    }

    // 移除与 cb 身份相等的连接（若有）。
    void disconnect(const Callable& cb);

    // 清空所有连接。
    void disconnect_all();

    // 触发：快照当前连接后逐个调用，重入安全（回调内 connect/disconnect 不影响本次）。
    void emit(Args... args);

    std::size_t connection_count() const { return connections_.size(); }

    // SignalBase 实现：移除所有 target==obj 的连接，并把 obj 从 tracked_objects_ 去除。
    void _disconnect_object(Object* obj) override;

private:
    std::vector<Callable> connections_;
    std::vector<Object*> tracked_objects_;  // 注册过本 Signal 的 Object（去重）
};

// ---------------------------------------------------------------------------
// Signal<Args...> 方法定义（模板，必须头文件内）
// ---------------------------------------------------------------------------

template <typename... Args>
Signal<Args...>::~Signal() {
    // 通知所有 tracked Object 把本 Signal 从其 connected_signals_ 移除，
    // 避免 Object 后续析构时持有悬空 SignalBase*。
    // 注意：tracked_objects_ 仅含仍存活的 Object —— 一旦某 Object 先析构，
    // 其 _signal_disconnect_all 会回调本 Signal 的 _disconnect_object，从
    // tracked_objects_ 中抹去自己，故此处不会触碰已销毁对象。
    for (Object* obj : tracked_objects_) {
        obj->_signal_unregister_connection(this);
    }
}

template <typename... Args>
void Signal<Args...>::connect(Callable cb) {
    // 幂等：身份相等则跳过。
    for (const auto& existing : connections_) {
        if (existing == cb) {
            return;
        }
    }
    if (Object* obj = cb.get_object()) {
        // 向 Object 注册本 Signal（若尚未注册）。
        if (std::find(tracked_objects_.begin(), tracked_objects_.end(), obj) ==
            tracked_objects_.end()) {
            tracked_objects_.push_back(obj);
        }
        obj->_signal_register_connection(this);
    }
    connections_.push_back(std::move(cb));
}

template <typename... Args>
void Signal<Args...>::disconnect(const Callable& cb) {
    auto it = std::find_if(connections_.begin(), connections_.end(),
                           [&cb](const Callable& x) { return x == cb; });
    if (it != connections_.end()) {
        connections_.erase(it);
    }
    // 注：不从 tracked_objects_ 中移除对应 Object。Object 死时会反向通知本 Signal，
    // 此时 _disconnect_object 会清理 tracked_objects_。Signal 死时则统一 unregistr。
}

template <typename... Args>
void Signal<Args...>::disconnect_all() {
    connections_.clear();
}

template <typename... Args>
void Signal<Args...>::emit(Args... args) {
    // 快照：回调内可能 connect/disconnect 同一 Signal，迭代快照避免迭代器失效。
    auto snapshot = connections_;
    if constexpr (sizeof...(Args) == 0) {
        for (auto& cb : snapshot) {
            cb.invoke(nullptr, 0);
        }
    } else {
        Variant boxed[] = {to_variant(args)...};
        for (auto& cb : snapshot) {
            cb.invoke(boxed, sizeof...(Args));
        }
    }
}

template <typename... Args>
void Signal<Args...>::_disconnect_object(Object* obj) {
    // 1) 清理所有 target==obj 的 Callable
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
                       [obj](const Callable& cb) { return cb.get_object() == obj; }),
        connections_.end());
    // 2) 从 tracked_objects_ 抹去 obj（本 Signal 析构时不再通知它）
    tracked_objects_.erase(
        std::remove(tracked_objects_.begin(), tracked_objects_.end(), obj),
        tracked_objects_.end());
}

}  // namespace eda
