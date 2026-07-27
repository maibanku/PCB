#pragma once

// ClassDB —— 运行时反射数据库（对齐 Godot core/class_db.{h,cpp}）。
//
// 设计要点（P2 Task 2）：
//   * 类型擦除走虚函数分派（MethodBind / PropertyBind 抽象基类 + 模板派生）。
//     不使用 typeid / dynamic_cast（核心 -fno-rtti），向下转换走 ClassDB 父类链
//     字符串比较（cast_to<T>）或编译期已知类型的 static_cast。
//   * 注册阶段在 main 启动单线程完成；运行期只读、无锁查询。
//   * 全局状态用函数局部 static，避免跨翻译单元初始化顺序问题；cleanup() 供测试清空。
//   * call() 在参数个数不足、方法不存在、obj 为空时返回 NIL（不抛异常）。

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/object/object.h"
#include "core/object/variant.h"

namespace eda {

// ===========================================================================
// detail：Variant ↔ C++ 类型互转辅助。
// MethodBind / PropertyBind 在分派时使用；扩展新参数类型时在此添加分支。
// ===========================================================================

namespace detail {

// Variant → 方法参数（按值返回，可绑定到 const T& / T 形参）。
template <typename T>
inline auto variant_to_arg(const Variant& v) {
    using D = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<D, bool>) {
        return v.as_bool();
    } else if constexpr (std::is_integral_v<D>) {
        return static_cast<D>(v.as_int());
    } else if constexpr (std::is_floating_point_v<D>) {
        return static_cast<D>(v.as_float());
    } else if constexpr (std::is_same_v<D, std::string>) {
        return v.as_string();
    } else if constexpr (std::is_pointer_v<D> &&
                         std::is_base_of_v<Object, std::remove_pointer_t<D>>) {
        // 调用方保证 Variant 内 Object* 实际指向 D（或派生）；-fno-rtti 无法运行期校验。
        return static_cast<D>(v.as_object());
    } else {
        static_assert(sizeof(T) == 0,
                      "ClassDB: unsupported argument type; extend variant_to_arg<T>");
        return D{};  // 不可达；仅安抚未启用完整 if constexpr 丢弃的编译器
    }
}

// 返回值 → Variant。
template <typename T>
inline Variant to_variant(const T& value) {
    using D = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<D, bool>) {
        return Variant(static_cast<bool>(value));
    } else if constexpr (std::is_integral_v<D>) {
        return Variant(static_cast<int64_t>(value));
    } else if constexpr (std::is_floating_point_v<D>) {
        return Variant(static_cast<double>(value));
    } else if constexpr (std::is_same_v<D, std::string>) {
        return Variant(static_cast<const std::string&>(value));
    } else if constexpr (std::is_pointer_v<D> &&
                         std::is_base_of_v<Object, std::remove_pointer_t<D>>) {
        return Variant(static_cast<Object*>(value));
    } else {
        static_assert(sizeof(T) == 0,
                      "ClassDB: unsupported return type; extend to_variant<T>");
        return Variant();
    }
}

// 由 C++ 类型推断 Variant::Type（bind_property 自动登记属性类型用）。
template <typename T>
constexpr Variant::Type variant_type_of() {
    using D = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<D, bool>) {
        return Variant::BOOL;
    } else if constexpr (std::is_integral_v<D>) {
        return Variant::INT;
    } else if constexpr (std::is_floating_point_v<D>) {
        return Variant::FLOAT;
    } else if constexpr (std::is_same_v<D, std::string>) {
        return Variant::STRING;
    } else if constexpr (std::is_pointer_v<D> &&
                         std::is_base_of_v<Object, std::remove_pointer_t<D>>) {
        return Variant::OBJECT;
    } else {
        return Variant::NIL;
    }
}

}  // namespace detail

// ===========================================================================
// MethodBind —— 成员方法的类型擦除绑定。
// ===========================================================================

class MethodBind {
public:
    MethodBind() = default;
    explicit MethodBind(std::string name) : name_(std::move(name)) {}
    virtual ~MethodBind() = default;

    MethodBind(const MethodBind&) = delete;
    MethodBind& operator=(const MethodBind&) = delete;

    const std::string& get_name() const { return name_; }
    int get_argument_count() const { return arg_count_; }

    // 在 target 上调用绑定方法。参数个数不足时返回 NIL。
    virtual Variant call(Object* target, const Variant* args,
                         std::size_t arg_count) = 0;

protected:
    std::string name_;
    int arg_count_ = 0;
};

// 非 const 成员方法绑定：R (T::*)(Args...)
template <typename T, typename R, typename... Args>
class MethodBindT : public MethodBind {
public:
    using MethodPtr = R (T::*)(Args...);

    MethodBindT(std::string name, MethodPtr method)
        : MethodBind(std::move(name)), method_(method) {
        arg_count_ = static_cast<int>(sizeof...(Args));
    }

    Variant call(Object* target, const Variant* args,
                 std::size_t arg_count) override {
        if (arg_count < sizeof...(Args)) return Variant();
        auto* derived = static_cast<T*>(target);
        return invoke(derived, args, std::make_index_sequence<sizeof...(Args)>{});
    }

private:
    template <std::size_t... Is>
    Variant invoke(T* obj, [[maybe_unused]] const Variant* args,
                   std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<R>) {
            (obj->*method_)(detail::variant_to_arg<Args>(args[Is])...);
            return Variant();
        } else {
            return detail::to_variant(
                (obj->*method_)(detail::variant_to_arg<Args>(args[Is])...));
        }
    }

    MethodPtr method_;
};

// const 成员方法绑定：R (T::*)(Args...) const
template <typename T, typename R, typename... Args>
class MethodBindConstT : public MethodBind {
public:
    using MethodPtr = R (T::*)(Args...) const;

    MethodBindConstT(std::string name, MethodPtr method)
        : MethodBind(std::move(name)), method_(method) {
        arg_count_ = static_cast<int>(sizeof...(Args));
    }

    Variant call(Object* target, const Variant* args,
                 std::size_t arg_count) override {
        if (arg_count < sizeof...(Args)) return Variant();
        const auto* derived = static_cast<const T*>(target);
        return invoke(derived, args, std::make_index_sequence<sizeof...(Args)>{});
    }

private:
    template <std::size_t... Is>
    Variant invoke(const T* obj, [[maybe_unused]] const Variant* args,
                   std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<R>) {
            (obj->*method_)(detail::variant_to_arg<Args>(args[Is])...);
            return Variant();
        } else {
            return detail::to_variant(
                (obj->*method_)(detail::variant_to_arg<Args>(args[Is])...));
        }
    }

    MethodPtr method_;
};

// ===========================================================================
// PropertyBind —— 属性（getter/setter 对）的类型擦除绑定。
// ===========================================================================

class PropertyBind {
public:
    PropertyBind() = default;
    PropertyBind(std::string name, Variant::Type type)
        : name_(std::move(name)), type_(type) {}
    virtual ~PropertyBind() = default;

    PropertyBind(const PropertyBind&) = delete;
    PropertyBind& operator=(const PropertyBind&) = delete;

    const std::string& get_name() const { return name_; }
    Variant::Type get_type() const { return type_; }

    virtual Variant get(Object* target) = 0;
    // 返回 false 表示设置失败（属性不存在 / 类型不兼容）。当前实现总是 true。
    virtual bool set(Object* target, const Variant& value) = 0;

protected:
    std::string name_;
    Variant::Type type_ = Variant::NIL;
};

// const getter + setter：GetR (T::*)() const / void (T::*)(SetArg)
template <typename T, typename GetR, typename SetArg>
class PropertyBindT : public PropertyBind {
public:
    using GetterPtr = GetR (T::*)() const;
    using SetterPtr = void (T::*)(SetArg);

    PropertyBindT(std::string name, Variant::Type type,
                  GetterPtr getter, SetterPtr setter)
        : PropertyBind(std::move(name), type),
          getter_(getter),
          setter_(setter) {}

    Variant get(Object* target) override {
        const auto* derived = static_cast<const T*>(target);
        return detail::to_variant((derived->*getter_)());
    }

    bool set(Object* target, const Variant& value) override {
        auto* derived = static_cast<T*>(target);
        (derived->*setter_)(detail::variant_to_arg<SetArg>(value));
        return true;
    }

private:
    GetterPtr getter_;
    SetterPtr setter_;
};

// 非 const getter + setter（少数场景，如懒计算属性的 getter）。
template <typename T, typename GetR, typename SetArg>
class PropertyBindNonConstT : public PropertyBind {
public:
    using GetterPtr = GetR (T::*)();
    using SetterPtr = void (T::*)(SetArg);

    PropertyBindNonConstT(std::string name, Variant::Type type,
                          GetterPtr getter, SetterPtr setter)
        : PropertyBind(std::move(name), type),
          getter_(getter),
          setter_(setter) {}

    Variant get(Object* target) override {
        auto* derived = static_cast<T*>(target);
        return detail::to_variant((derived->*getter_)());
    }

    bool set(Object* target, const Variant& value) override {
        auto* derived = static_cast<T*>(target);
        (derived->*setter_)(detail::variant_to_arg<SetArg>(value));
        return true;
    }

private:
    GetterPtr getter_;
    SetterPtr setter_;
};

// ===========================================================================
// ClassInfo —— 单个类的反射元数据。
// ===========================================================================

struct ClassInfo {
    std::string name;
    std::string parent;  // 父类名；Object 本身为空，其他类至少为 "Object"
    Object* (*create_func)() = nullptr;  // 默认工厂；为 nullptr 表示不可实例化
    std::unordered_map<std::string, std::unique_ptr<MethodBind>> methods;
    std::unordered_map<std::string, std::unique_ptr<PropertyBind>> properties;
    std::vector<std::string> signal_names;  // Task 4 接 SignalAdaptor；此处仅登记
    bool registered = false;
};

// ===========================================================================
// ClassDB —— 反射数据库（静态方法集 + 内部全局存储）。
// ===========================================================================

class ClassDB {
public:
    // ---- 注册 ----

    // 注册类型 T，父类为 Parent（默认 Object）。要求：
    //   * T 派生自 eda::Object
    //   * T 提供 static std::string get_class_static()
    //   * T 可默认构造（用于 instantiate）
    template <typename T, typename Parent = Object>
    static void register_class();

    // 绑定非 const 成员方法。类 T 须先 register_class。
    template <typename T, typename R, typename... Args>
    static void bind_method(const std::string& name, R (T::*method)(Args...));

    // 绑定 const 成员方法。
    template <typename T, typename R, typename... Args>
    static void bind_method(const std::string& name, R (T::*method)(Args...) const);

    // 绑定属性（const getter 版）。Variant::Type 由 getter 返回类型自动推断。
    template <typename T, typename GetR, typename SetArg>
    static void bind_property(const std::string& name,
                              GetR (T::*getter)() const,
                              void (T::*setter)(SetArg));

    // 绑定属性（非 const getter 版）。
    template <typename T, typename GetR, typename SetArg>
    static void bind_property(const std::string& name,
                              GetR (T::*getter)(),
                              void (T::*setter)(SetArg));

    // 登记信号名（Task 4 接 SignalAdaptor；此处仅存储供枚举）。
    static void bind_signal(const std::string& class_name,
                            const std::string& signal_name);

    // ---- 调用 ----

    // 按名调用 obj 的方法，沿父类链查找。未找到 / 参数不足 / obj 为空 → NIL。
    static Variant call(Object* obj, const std::string& method,
                        const Variant* args, std::size_t arg_count);

    // 便利重载：参数打包在 vector 中。
    static Variant call(Object* obj, const std::string& method,
                        const std::vector<Variant>& args);

    // 便利重载：零参。
    static Variant call(Object* obj, const std::string& method) {
        return call(obj, method, static_cast<const Variant*>(nullptr),
                    std::size_t(0));
    }

    // ---- 属性直连 ----
    // Object::set/get 的 ClassDB 接入由 Task 2 整合阶段统一修改 object.cpp；
    // 在此之前通过 ClassDB::set_property / get_property 直接访问。

    static bool set_property(Object* obj, const std::string& name,
                             const Variant& value);
    static Variant get_property(Object* obj, const std::string& name);

    // ---- 实例化 ----

    // 按类名默认构造。未注册 / 无工厂 → nullptr。
    static Object* instantiate(const std::string& class_name);

    // ---- 父类链查询 ----

    static bool class_exists(const std::string& class_name);
    static std::string get_parent_class(const std::string& class_name);

    // parent_class == child_class 或为其祖先时返回 true。
    // 链中节点未注册时按已知 parent 链接继续走，匹配靠字符串 ==。
    static bool is_parent_class(const std::string& child_class,
                                const std::string& parent_class);

    // ---- 枚举 ----

    static std::vector<std::string> get_class_list();
    static std::vector<std::string> get_method_list(const std::string& class_name);
    static std::vector<std::string> get_property_list(const std::string& class_name);
    static std::vector<std::string> get_signal_list(const std::string& class_name);

    // ---- 直接查询 ----

    static const ClassInfo* get_class_info(const std::string& class_name);
    // 沿 obj 的类链查找名为 name 的方法绑定。
    static MethodBind* get_method(Object* obj, const std::string& name);
    // 沿 obj 的类链查找名为 name 的属性绑定。
    static PropertyBind* find_property(Object* obj, const std::string& name);

    // ---- cast_to<T>（替代 dynamic_cast） ----
    // 基于 obj->get_class() 与 T::get_class_static() 及 ClassDB 父类链判断；
    // 不使用 typeid / dynamic_cast。要求 T 提供 get_class_static()。
    template <typename T>
    static T* cast_to(Object* obj);

    // ---- 清理（测试用：清空全部注册项） ----
    static void cleanup();

private:
    // 内部存储访问。
    static ClassInfo& get_or_create_class_info(const std::string& name);
    static ClassInfo* find_class_info(const std::string& name);
};

// Godot 对齐宏。GDREGISTER_CLASS(Foo) 注册 Foo（父类默认 Object）。
#define GDREGISTER_CLASS(T) ::eda::ClassDB::register_class<T, ::eda::Object>()
// 指定父类版本：GDREGISTER_CLASS_WITH(Dog, Animal)
#define GDREGISTER_CLASS_WITH(T, Parent) ::eda::ClassDB::register_class<T, Parent>()

}  // namespace eda

// ===========================================================================
// 模板成员实现（须头文件可见）。
// ===========================================================================

namespace eda {

template <typename T, typename Parent>
inline void ClassDB::register_class() {
    static_assert(std::is_base_of_v<Object, T>,
                  "register_class<T>: T must derive from eda::Object");
    static_assert(std::is_base_of_v<Object, Parent>,
                  "register_class<T, Parent>: Parent must derive from eda::Object");
    ClassInfo& info = get_or_create_class_info(T::get_class_static());
    info.name = T::get_class_static();
    info.parent = Parent::get_class_static();
    info.create_func = []() -> Object* { return new T(); };
    info.registered = true;
}

template <typename T, typename R, typename... Args>
inline void ClassDB::bind_method(const std::string& name,
                                 R (T::*method)(Args...)) {
    ClassInfo* info = find_class_info(T::get_class_static());
    if (info == nullptr) return;  // 类未注册：静默忽略（日志接入后打 warning）
    info->methods[name] =
        std::make_unique<MethodBindT<T, R, Args...>>(name, method);
}

template <typename T, typename R, typename... Args>
inline void ClassDB::bind_method(const std::string& name,
                                 R (T::*method)(Args...) const) {
    ClassInfo* info = find_class_info(T::get_class_static());
    if (info == nullptr) return;
    info->methods[name] =
        std::make_unique<MethodBindConstT<T, R, Args...>>(name, method);
}

template <typename T, typename GetR, typename SetArg>
inline void ClassDB::bind_property(const std::string& name,
                                   GetR (T::*getter)() const,
                                   void (T::*setter)(SetArg)) {
    ClassInfo* info = find_class_info(T::get_class_static());
    if (info == nullptr) return;
    info->properties[name] = std::make_unique<PropertyBindT<T, GetR, SetArg>>(
        name, detail::variant_type_of<GetR>(), getter, setter);
}

template <typename T, typename GetR, typename SetArg>
inline void ClassDB::bind_property(const std::string& name,
                                   GetR (T::*getter)(),
                                   void (T::*setter)(SetArg)) {
    ClassInfo* info = find_class_info(T::get_class_static());
    if (info == nullptr) return;
    info->properties[name] =
        std::make_unique<PropertyBindNonConstT<T, GetR, SetArg>>(
            name, detail::variant_type_of<GetR>(), getter, setter);
}

template <typename T>
inline T* ClassDB::cast_to(Object* obj) {
    if (obj == nullptr) return nullptr;
    const std::string target = T::get_class_static();
    const std::string actual = obj->get_class();
    if (actual == target) return static_cast<T*>(obj);
    if (is_parent_class(actual, target)) return static_cast<T*>(obj);
    return nullptr;
}

}  // namespace eda
