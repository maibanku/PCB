#pragma once

// Callable —— 类型擦除的可调用对象包装（对齐 Godot Callable）。
//
// 支持三种目标的统一包装：
//   1) 自由函数指针  void(Args...)
//   2) 成员函数指针 + 对象指针  void(T::method)(Args...) 调用于 obj
//   3) 任意 lambda / 仿函数（operator() 签名推导）
//
// 调用入口统一为 invoke(const Variant* args, size_t argc) —— Signal<Args...>::emit
// 把模板参数装箱为 Variant 数组后传入；Model 在调用点按已知参数类型拆箱回原类型。
// 这样 Callable 是单一非模板类型，可在容器中按值存放与比较。
//
// 生命周期：当 Callable 包装的是 "成员函数 + 对象指针" 且该对象派生自 eda::Object
// 时，get_object() 返回该 Object*；Signal 据此向 Object 注册反向解绑（详见
// signal.h 的集成契约）。否则 get_object() 返回 nullptr，调用方自行保证目标存活。
//
// 相等语义：operator== 比较底层 Concept 实例身份（shared_ptr 同一性）。即同一
// Callable 变量或其拷贝视为相等；两个独立构造、包装相同底层目标的 Callable 不视为
// 相等（不支持结构化相等）—— 调用方应保存 connect 时使用的 Callable 以便 disconnect。

#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "core/object/object.h"  // 完整 Object 定义（用于 static_cast<Object*>）
#include "core/object/variant.h"

namespace eda {

class Object;

// ---------------------------------------------------------------------------
// 辅助：Variant → 具体类型 的拆箱。覆盖常见标量；其余类型特化自行扩展。
// ---------------------------------------------------------------------------

template <typename T>
inline T unpack_arg(const Variant& v) = delete;  // 未特化的类型编译期报错

template <> inline bool unpack_arg<bool>(const Variant& v) { return v.as_bool(); }
template <> inline signed char unpack_arg<signed char>(const Variant& v) { return static_cast<signed char>(v.as_int()); }
template <> inline unsigned char unpack_arg<unsigned char>(const Variant& v) { return static_cast<unsigned char>(v.as_int()); }
template <> inline short unpack_arg<short>(const Variant& v) { return static_cast<short>(v.as_int()); }
template <> inline unsigned short unpack_arg<unsigned short>(const Variant& v) { return static_cast<unsigned short>(v.as_int()); }
template <> inline int unpack_arg<int>(const Variant& v) { return static_cast<int>(v.as_int()); }
template <> inline unsigned int unpack_arg<unsigned int>(const Variant& v) { return static_cast<unsigned int>(v.as_int()); }
template <> inline long unpack_arg<long>(const Variant& v) { return static_cast<long>(v.as_int()); }
template <> inline unsigned long unpack_arg<unsigned long>(const Variant& v) { return static_cast<unsigned long>(v.as_int()); }
template <> inline long long unpack_arg<long long>(const Variant& v) { return static_cast<long long>(v.as_int()); }
template <> inline unsigned long long unpack_arg<unsigned long long>(const Variant& v) { return static_cast<unsigned long long>(v.as_int()); }
template <> inline float unpack_arg<float>(const Variant& v) { return static_cast<float>(v.as_float()); }
template <> inline double unpack_arg<double>(const Variant& v) { return v.as_float(); }
template <> inline std::string unpack_arg<std::string>(const Variant& v) { return v.as_string(); }

// ---------------------------------------------------------------------------
// 辅助：具体类型 → Variant 的装箱。
//
// 必要性：Variant 同时存在 Variant(bool) / Variant(int64_t) / Variant(double) 三个
// explicit 构造函数；直接 Variant(int_value) 会在 int→int64_t / int→double / int→bool
// 之间产生"同为 conversion 等级"的二义。to_variant 用重载显式选定一个构造函数。
// ---------------------------------------------------------------------------

inline Variant to_variant(bool v) { return Variant(v); }
inline Variant to_variant(int v) { return Variant(static_cast<int64_t>(v)); }
inline Variant to_variant(long v) { return Variant(static_cast<int64_t>(v)); }
inline Variant to_variant(long long v) { return Variant(static_cast<int64_t>(v)); }
inline Variant to_variant(unsigned int v) { return Variant(static_cast<int64_t>(v)); }
inline Variant to_variant(unsigned long v) { return Variant(static_cast<int64_t>(v)); }
inline Variant to_variant(unsigned long long v) { return Variant(static_cast<int64_t>(v)); }
inline Variant to_variant(float v) { return Variant(static_cast<double>(v)); }
inline Variant to_variant(double v) { return Variant(v); }
inline Variant to_variant(const std::string& v) { return Variant(v); }
inline Variant to_variant(const char* v) { return Variant(v); }
inline Variant to_variant(Object* v) { return Variant(v); }

// ---------------------------------------------------------------------------
// 辅助：从 lambda/仿函数推导 operator() 的参数列表
// ---------------------------------------------------------------------------

namespace detail {

template <typename F>
struct lambda_args;

template <typename R, typename C, typename... Args>
struct lambda_args<R (C::*)(Args...) const> {
    using tuple_type = std::tuple<Args...>;
};

template <typename R, typename C, typename... Args>
struct lambda_args<R (C::*)(Args...)> {
    using tuple_type = std::tuple<Args...>;
};

}  // namespace detail

// ---------------------------------------------------------------------------
// Callable —— 对外非模板类型
// ---------------------------------------------------------------------------

class Callable {
public:
    Callable() = default;

    // 自由函数指针
    template <typename... Args>
    Callable(void (*fn)(Args...)) {
        impl_ = std::make_shared<Model<Args...>>(
            std::function<void(Args...)>([fn](Args... a) { fn(a...); }),
            nullptr);
    }

    // 成员函数指针 + 对象指针
    template <typename T, typename... Args>
    Callable(void (T::*method)(Args...), T* obj) {
        Object* obj_as_object = nullptr;
        if constexpr (std::is_base_of_v<Object, T>) {
            obj_as_object = static_cast<Object*>(obj);
        }
        impl_ = std::make_shared<Model<Args...>>(
            std::function<void(Args...)>([method, obj](Args... a) { (obj->*method)(a...); }),
            obj_as_object);
    }

    // 任意 lambda / 仿函数（排除 Callable 自身 / 函数指针 / 成员函数指针 / 指针）
    template <typename F,
              typename = std::enable_if_t<
                  !std::is_same_v<std::decay_t<F>, Callable> &&
                  !std::is_function_v<std::remove_reference_t<F>> &&
                  !std::is_member_function_pointer_v<std::remove_reference_t<F>> &&
                  !std::is_pointer_v<std::remove_reference_t<F>>>>
    Callable(F&& fn) {
        using ArgsTuple = typename detail::lambda_args<
            decltype(&std::decay_t<F>::operator())>::tuple_type;
        init_from_tuple(std::forward<F>(fn), static_cast<ArgsTuple*>(nullptr));
    }

    // 调用：按 Variant 数组传入。未持有目标（impl_ 为空）时为空操作。
    void invoke(const Variant* args, std::size_t argc) const;

    // 返回包装的目标 Object*（lambda/自由函数返回 nullptr）。
    Object* get_object() const;

    bool valid() const { return impl_ != nullptr; }
    explicit operator bool() const { return impl_ != nullptr; }

    bool operator==(const Callable& other) const;
    bool operator!=(const Callable& other) const { return !(*this == other); }

    // ----- 类型擦除接口（Concept） + 模板实现（Model） -----

    struct Concept {
        virtual ~Concept() = default;
        virtual void invoke(const Variant* args, std::size_t argc) = 0;
        virtual Object* get_object() const = 0;
    };

    template <typename... Args>
    struct Model : Concept {
        std::function<void(Args...)> fn_;
        Object* object_;

        Model(std::function<void(Args...)> fn, Object* obj)
            : fn_(std::move(fn)), object_(obj) {}

        void invoke(const Variant* args, std::size_t /*argc*/) override {
            invoke_impl(args, std::index_sequence_for<Args...>{});
        }

        template <std::size_t... Is>
        void invoke_impl(const Variant* args, std::index_sequence<Is...>) {
            fn_(unpack_arg<Args>(args[Is])...);
        }

        Object* get_object() const override { return object_; }
    };

private:
    template <typename F, typename... Args>
    void init_from_tuple(F&& fn, std::tuple<Args...>*) {
        impl_ = std::make_shared<Model<Args...>>(
            std::function<void(Args...)>(std::forward<F>(fn)), nullptr);
    }

    std::shared_ptr<Concept> impl_;
};

}  // namespace eda
