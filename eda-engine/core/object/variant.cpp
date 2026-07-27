// 故意不 include "core/object/object.h"——以解开 Variant ↔ Object 的循环依赖。
// as_object() 仅返回前向声明的 Object* 指针，无需 Object 的完整定义。
#include "core/object/variant.h"

#include <stdexcept>
#include <string>

namespace eda {

bool Variant::as_bool() const {
    switch (type_) {
        case BOOL:   return std::get<bool>(value_);
        case INT:    return std::get<int64_t>(value_) != 0;
        case FLOAT:  return std::get<double>(value_) != 0.0;
        case STRING: return !std::get<std::string>(value_).empty();
        default:     return false;
    }
}

int64_t Variant::as_int() const {
    switch (type_) {
        case INT:    return std::get<int64_t>(value_);
        case FLOAT:  return static_cast<int64_t>(std::get<double>(value_));
        case BOOL:   return std::get<bool>(value_) ? 1 : 0;
        case STRING: {
            try { return std::stoll(std::get<std::string>(value_)); }
            catch (...) { return 0; }
        }
        default:     return 0;
    }
}

double Variant::as_float() const {
    switch (type_) {
        case FLOAT:  return std::get<double>(value_);
        case INT:    return static_cast<double>(std::get<int64_t>(value_));
        case BOOL:   return std::get<bool>(value_) ? 1.0 : 0.0;
        case STRING: {
            try { return std::stod(std::get<std::string>(value_)); }
            catch (...) { return 0.0; }
        }
        default:     return 0.0;
    }
}

std::string Variant::as_string() const {
    switch (type_) {
        case STRING: return std::get<std::string>(value_);
        case INT:    return std::to_string(std::get<int64_t>(value_));
        case FLOAT:  return std::to_string(std::get<double>(value_));
        case BOOL:   return std::get<bool>(value_) ? "true" : "false";
        default:     return std::string();
    }
}

Object* Variant::as_object() const {
    return type_ == OBJECT ? std::get<Object*>(value_) : nullptr;
}

}  // namespace eda
