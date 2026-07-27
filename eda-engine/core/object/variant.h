#pragma once
#include <cstdint>
#include <string>
#include <variant>

namespace eda {

class Object;  // 前向声明，避免循环（variant.cpp 不 include object.h）

// Variant（Task 1 子集）：承载 P2 核心标量与 Object*。
// Task 3 扩展：ARRAY / DICTIONARY / VECTOR2 / COLOR + Variant::call + JSON 往返。
//
// 错误模型：类型不匹配的取值不抛异常（引擎核心禁用异常），返回零值/默认值；
//          Task 3 接入 core/os 日志后追加 warning。
class Variant {
public:
    enum Type {
        NIL,
        BOOL,     // bool
        INT,      // int64_t
        FLOAT,    // double
        STRING,   // std::string
        OBJECT,   // Object*
        // Task 3 待扩展：ARRAY, DICTIONARY, VECTOR2, COLOR
    };

    Variant() : type_(NIL), value_(std::monostate{}) {}
    explicit Variant(bool v) : type_(BOOL), value_(v) {}
    explicit Variant(int64_t v) : type_(INT), value_(v) {}
    explicit Variant(double v) : type_(FLOAT), value_(v) {}
    explicit Variant(const char* v) : type_(STRING), value_(std::string(v)) {}
    explicit Variant(const std::string& v) : type_(STRING), value_(v) {}
    explicit Variant(Object* v) : type_(OBJECT), value_(v) {}

    Type get_type() const { return type_; }
    bool is_nil() const { return type_ == NIL; }

    // 类型不匹配返回零值/默认值（无异常抛出）
    bool as_bool() const;
    int64_t as_int() const;
    double as_float() const;
    std::string as_string() const;
    Object* as_object() const;

private:
    Type type_;
    std::variant<std::monostate, bool, int64_t, double, std::string, Object*> value_;
};

}  // namespace eda
