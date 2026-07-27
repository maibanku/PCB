#pragma once

// Variant —— tagged union 值载体（对齐 Godot Variant），P2 Task 3 扩展。
//
// 类型集合（10 种）：
//   标量    : NIL / BOOL / INT(int64_t) / FLOAT(double) / STRING(std::string)
//   对象    : OBJECT(Object*) —— 不参与 JSON 序列化（输出 null）
//   集合    : ARRAY(vector<Variant>) / DICTIONARY(unordered_map<string,Variant>)
//   几何    : VECTOR2(eda::Vector2) / COLOR(eda::Color)
//
// 存储策略：
//   - 标量/几何直接内嵌（值语义）。
//   - STRING 直接内嵌（SSO 友好）。
//   - ARRAY/DICTIONARY 通过 std::shared_ptr<detail::ArrayData/DictData> 持有，
//     实现 COW（写时复制）：拷贝共享底层存储（廉价），变更时分离（copy_on_write_*）。
//     这同时解决 std::variant 无法内嵌递归类型（vector<Variant>）的完整性问题。
//
// 错误模型（沿用 T1）：
//   类型不匹配的取值不抛异常（引擎核心禁用异常），返回零值/默认视图；
//   越界/缺键访问返回 NIL 静态实例。core/os 日志接入后可追加 warning。

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "core/math/color.h"
#include "core/math/vector2.h"

namespace eda {

class Object;  // 前向声明，避免循环（variant.cpp 不 include object.h）

namespace detail {

// 集合内部存储（前向声明；完整定义位于 Variant 类之后，此时 Variant 已完整，
// 才能实例化 vector<Variant> / unordered_map<string, Variant>）。
// 通过 shared_ptr 持有，因此可在 std::variant 中作为完整类型使用。
struct ArrayData;
struct DictData;

}  // namespace detail

class Variant {
public:
    enum Type {
        NIL,
        BOOL,        // bool
        INT,         // int64_t
        FLOAT,       // double
        STRING,      // std::string
        OBJECT,      // Object*（不参与 JSON 序列化）
        ARRAY,       // shared_ptr<detail::ArrayData>
        DICTIONARY,  // shared_ptr<detail::DictData>
        VECTOR2,     // eda::Vector2
        COLOR,       // eda::Color
    };

    // ----- 构造（标量沿用 T1）-----
    Variant() : type_(NIL), value_(std::monostate{}) {}
    explicit Variant(bool v) : type_(BOOL), value_(v) {}
    explicit Variant(int64_t v) : type_(INT), value_(v) {}
    explicit Variant(double v) : type_(FLOAT), value_(v) {}
    explicit Variant(const char* v) : type_(STRING), value_(std::string(v)) {}
    explicit Variant(const std::string& v) : type_(STRING), value_(v) {}
    explicit Variant(Object* v) : type_(OBJECT), value_(v) {}
    explicit Variant(const Vector2& v) : type_(VECTOR2), value_(v) {}
    explicit Variant(const Color& v) : type_(COLOR), value_(v) {}

    // 集合构造：拷贝入共享存储（定义在 .cpp，避免在头文件内要求集合元素类型完整）
    explicit Variant(const std::vector<Variant>& v);
    explicit Variant(const std::unordered_map<std::string, Variant>& v);

    // 特殊成员：~Variant / 拷贝在 .cpp 定义（确保 shared_ptr 操作在集合类型完整可见处发生）
    ~Variant();
    Variant(const Variant& o);
    Variant& operator=(const Variant& o);
    Variant(Variant&& o) noexcept = default;
    Variant& operator=(Variant&& o) noexcept = default;

    // ----- 类型查询 -----
    Type get_type() const { return type_; }
    bool is_nil() const { return type_ == NIL; }

    // ----- 标量取值（类型不匹配返回零值，无异常）-----
    bool as_bool() const;
    int64_t as_int() const;
    double as_float() const;
    std::string as_string() const;
    Object* as_object() const;
    Vector2 as_vector2() const;
    Color as_color() const;

    // ----- 集合只读视图（非集合类型返回空静态视图）-----
    const std::vector<Variant>& as_array() const;
    const std::unordered_map<std::string, Variant>& as_dictionary() const;

    // ----- ARRAY 编辑（COW：变更前分离共享存储）-----
    size_t array_size() const;
    // 越界返回静态 NIL 实例（调用方不可持久存储返回值的地址跨过下一次变更）
    const Variant& array_at(size_t i) const;
    void set_array_element(size_t i, const Variant& v);  // 越界忽略
    void append(const Variant& v);

    // ----- DICTIONARY 编辑（COW）-----
    size_t dict_size() const;
    // 缺键返回静态 NIL 实例
    const Variant& dict_at(const std::string& key) const;
    bool dict_has(const std::string& key) const;
    void set_dict_element(const std::string& key, const Variant& v);
    size_t dict_erase(const std::string& key);  // 返回移除数（0 或 1）

    // ----- JSON 往返（手写最小 JSON，不引入 RapidJSON）-----
    // 编码：
    //   NIL→null | BOOL→true|false | INT→整数字面量 | FLOAT→浮点字面量（保精度）
    //   STRING→带转义字符串（" \\ \n \t \r \b \f \/ \uXXXX）
    //   ARRAY→[e, e, ...] | DICTIONARY→{"k": v, ...}
    //   VECTOR2→{"x":..,"y":..} | COLOR→{"r":..,"g":..,"b":..,"a":..}
    //   OBJECT→null（不可逆；OBJECT 指针不参与序列化）
    // 反序列化启发：JSON 对象键集恰好为 {x,y} → VECTOR2；恰好为 {r,g,b,a} → COLOR；
    //              其余 JSON 对象 → DICTIONARY。
    // 循环引用防护：serialize 内置深度上限（kMaxSerializeDepth），超限输出 null，
    //              保证自引用结构不会无限递归。append/set_* 在传入 *this 时做深拷贝，
    //              从源头避免构造出共享自身存储的循环。
    std::string serialize() const;
    static Variant deserialize(const std::string& json);

private:
    static constexpr int kMaxSerializeDepth = 64;

    Type type_;
    std::variant<
        std::monostate,
        bool,
        int64_t,
        double,
        std::string,
        Object*,
        Vector2,
        Color,
        std::shared_ptr<detail::ArrayData>,
        std::shared_ptr<detail::DictData>>
        value_;

    // 内部工厂：构造共享存储（定义在 .cpp，ArrayData/DictData 完整可见处）
    static std::shared_ptr<detail::ArrayData> make_array_storage(const std::vector<Variant>& v);
    static std::shared_ptr<detail::DictData> make_dict_storage(const std::unordered_map<std::string, Variant>& v);

    // COW 分离：若存储被多个 Variant 共享（use_count > 1）则克隆一份独占副本
    void copy_on_write_array();
    void copy_on_write_dict();

    // 递归深拷贝（用于 append/set_* 传入 *this 时打破自引用）
    static Variant deep_copy(const Variant& v);

    // 序列化辅助（depth 用于循环引用防护）
    void serialize_into(std::string& out, int depth) const;
};

namespace detail {

// 完整定义（此时 Variant 已完整，可实例化递归容器）
struct ArrayData {
    std::vector<Variant> items;
};

struct DictData {
    std::unordered_map<std::string, Variant> items;
};

}  // namespace detail

}  // namespace eda
