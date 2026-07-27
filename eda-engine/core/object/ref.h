#pragma once
#include "core/object/ref_counted.h"

#include <type_traits>
#include <utility>

namespace eda {

// Ref<T> —— 引用计数智能指针（对齐 Godot Ref<T>）。
// 要求 T 派生自 RefCounted。
//   - 构造 / 拷贝：ref++
//   - 析构 / reset：unref，归零自动销毁
//   - 移动：所有权转移，不增减计数
//   - 支持 Ref<Derived> -> Ref<Base> 上转
template <typename T>
class Ref {
    static_assert(std::is_base_of_v<RefCounted, T>,
                  "Ref<T> requires T to derive from eda::RefCounted");

public:
    Ref() = default;
    explicit Ref(T* p) : ptr_(p) { if (ptr_) ptr_->ref(); }
    Ref(const Ref& o) : ptr_(o.ptr_) { if (ptr_) ptr_->ref(); }
    Ref(Ref&& o) noexcept : ptr_(o.ptr_) { o.ptr_ = nullptr; }

    // 上转：Ref<Derived> -> Ref<Base>（仅当 Derived 派生自 T）
    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>>
    Ref(const Ref<U>& o) : ptr_(o.get()) { if (ptr_) ptr_->ref(); }

    ~Ref() { reset(); }

    Ref& operator=(const Ref& o) {
        if (this != &o && ptr_ != o.ptr_) {
            if (ptr_) ptr_->unref();
            ptr_ = o.ptr_;
            if (ptr_) ptr_->ref();
        }
        return *this;
    }
    Ref& operator=(Ref&& o) noexcept {
        if (this != &o) {
            if (ptr_) ptr_->unref();
            ptr_ = o.ptr_;
            o.ptr_ = nullptr;
        }
        return *this;
    }
    Ref& operator=(T* p) {
        if (ptr_ != p) {
            if (ptr_) ptr_->unref();
            ptr_ = p;
            if (ptr_) ptr_->ref();
        }
        return *this;
    }

    void reset() {
        if (ptr_) {
            ptr_->unref();
            ptr_ = nullptr;
        }
    }

    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    bool valid() const { return ptr_ != nullptr; }
    explicit operator bool() const { return ptr_ != nullptr; }

    bool operator==(const Ref& o) const { return ptr_ == o.ptr_; }
    bool operator!=(const Ref& o) const { return ptr_ != o.ptr_; }
    bool operator==(T* p) const { return ptr_ == p; }
    bool operator!=(T* p) const { return ptr_ != p; }

private:
    T* ptr_ = nullptr;

    template <typename U> friend class Ref;
};

}  // namespace eda
