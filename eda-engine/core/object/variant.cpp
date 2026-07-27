// 故意不 include "core/object/object.h"——以解开 Variant ↔ Object 的循环依赖。
// as_object() 仅返回前向声明的 Object* 指针，无需 Object 的完整定义。
//
// P2 Task 3 扩展：ARRAY / DICTIONARY（COW 共享存储）+ VECTOR2 / COLOR + 手写 JSON 往返。

#include "core/object/variant.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>

namespace eda {

// =====================================================================
//  特殊成员函数（out-of-line：确保 shared_ptr<detail::ArrayData/DictData>
//  的析构/拷贝在 detail 类型完整可见处发生，规避不完整类型下的未定义行为）
// =====================================================================

Variant::Variant(const std::vector<Variant>& v)
    : type_(ARRAY), value_(make_array_storage(v)) {}

Variant::Variant(const std::unordered_map<std::string, Variant>& v)
    : type_(DICTIONARY), value_(make_dict_storage(v)) {}

Variant::~Variant() = default;

Variant::Variant(const Variant& o) : type_(o.type_), value_(o.value_) {}

Variant& Variant::operator=(const Variant& o) {
    if (this != &o) {
        type_ = o.type_;
        value_ = o.value_;  // shared_ptr 浅拷贝（refcount++），COW 在变更时分离
    }
    return *this;
}

// =====================================================================
//  内部工厂
// =====================================================================

std::shared_ptr<detail::ArrayData> Variant::make_array_storage(const std::vector<Variant>& v) {
    auto storage = std::make_shared<detail::ArrayData>();
    storage->items = v;
    return storage;
}

std::shared_ptr<detail::DictData> Variant::make_dict_storage(const std::unordered_map<std::string, Variant>& v) {
    auto storage = std::make_shared<detail::DictData>();
    storage->items = v;
    return storage;
}

void Variant::copy_on_write_array() {
    if (type_ != ARRAY) return;
    auto* sp = std::get_if<std::shared_ptr<detail::ArrayData>>(&value_);
    if (!sp) {
        value_ = std::make_shared<detail::ArrayData>();
        return;
    }
    if (!*sp) {
        *sp = std::make_shared<detail::ArrayData>();
        return;
    }
    if (sp->use_count() > 1) {
        // 被多个 Variant 共享——克隆一份独占副本，避免变更波及其余共享者
        *sp = std::make_shared<detail::ArrayData>(**sp);
    }
}

void Variant::copy_on_write_dict() {
    if (type_ != DICTIONARY) return;
    auto* sp = std::get_if<std::shared_ptr<detail::DictData>>(&value_);
    if (!sp) {
        value_ = std::make_shared<detail::DictData>();
        return;
    }
    if (!*sp) {
        *sp = std::make_shared<detail::DictData>();
        return;
    }
    if (sp->use_count() > 1) {
        *sp = std::make_shared<detail::DictData>(**sp);
    }
}

Variant Variant::deep_copy(const Variant& v) {
    Variant out;
    out.type_ = v.type_;
    switch (v.type_) {
        case ARRAY: {
            const auto* src = std::get_if<std::shared_ptr<detail::ArrayData>>(&v.value_);
            auto storage = std::make_shared<detail::ArrayData>();
            if (src && *src) {
                for (const auto& e : (*src)->items) {
                    storage->items.push_back(deep_copy(e));
                }
            }
            out.value_ = storage;
            break;
        }
        case DICTIONARY: {
            const auto* src = std::get_if<std::shared_ptr<detail::DictData>>(&v.value_);
            auto storage = std::make_shared<detail::DictData>();
            if (src && *src) {
                for (const auto& kv : (*src)->items) {
                    storage->items[kv.first] = deep_copy(kv.second);
                }
            }
            out.value_ = storage;
            break;
        }
        default:
            out.value_ = v.value_;  // 标量/几何/Object*：直接拷贝
            break;
    }
    return out;
}

// =====================================================================
//  标量取值（沿用 T1：类型不匹配返回零值，无异常）
// =====================================================================

bool Variant::as_bool() const {
    switch (type_) {
        case BOOL:   return std::get<bool>(value_);
        case INT:    return std::get<int64_t>(value_) != 0;
        case FLOAT:  return std::get<double>(value_) != 0.0;
        case STRING: return !std::get<std::string>(value_).empty();
        case ARRAY: {
            const auto* sp = std::get_if<std::shared_ptr<detail::ArrayData>>(&value_);
            return sp && *sp && !(*sp)->items.empty();
        }
        case DICTIONARY: {
            const auto* sp = std::get_if<std::shared_ptr<detail::DictData>>(&value_);
            return sp && *sp && !(*sp)->items.empty();
        }
        default:     return false;  // NIL / OBJECT / VECTOR2 / COLOR
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
        default:     return 0;  // NIL / OBJECT / ARRAY / DICTIONARY / VECTOR2 / COLOR
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
        default:     return std::string();  // NIL/OBJECT/ARRAY/DICT/VECTOR2/COLOR
    }
}

Object* Variant::as_object() const {
    return type_ == OBJECT ? std::get<Object*>(value_) : nullptr;
}

Vector2 Variant::as_vector2() const {
    if (type_ != VECTOR2) return Vector2{};
    return std::get<Vector2>(value_);
}

Color Variant::as_color() const {
    if (type_ != COLOR) return Color{};
    return std::get<Color>(value_);
}

// =====================================================================
//  集合只读视图
// =====================================================================

const std::vector<Variant>& Variant::as_array() const {
    static const std::vector<Variant> empty;
    if (type_ != ARRAY) return empty;
    const auto* sp = std::get_if<std::shared_ptr<detail::ArrayData>>(&value_);
    return (sp && *sp) ? (*sp)->items : empty;
}

const std::unordered_map<std::string, Variant>& Variant::as_dictionary() const {
    static const std::unordered_map<std::string, Variant> empty;
    if (type_ != DICTIONARY) return empty;
    const auto* sp = std::get_if<std::shared_ptr<detail::DictData>>(&value_);
    return (sp && *sp) ? (*sp)->items : empty;
}

// =====================================================================
//  ARRAY 编辑（COW）
// =====================================================================

size_t Variant::array_size() const {
    if (type_ != ARRAY) return 0;
    const auto* sp = std::get_if<std::shared_ptr<detail::ArrayData>>(&value_);
    return (sp && *sp) ? (*sp)->items.size() : 0;
}

const Variant& Variant::array_at(size_t i) const {
    static const Variant nil_instance;
    if (type_ != ARRAY) return nil_instance;
    const auto* sp = std::get_if<std::shared_ptr<detail::ArrayData>>(&value_);
    if (!sp || !*sp || i >= (*sp)->items.size()) return nil_instance;
    return (*sp)->items[i];
}

void Variant::set_array_element(size_t i, const Variant& v) {
    if (type_ != ARRAY) return;
    // 自引用防护：v 即 *this 时，先深拷贝快照，避免 COW 后写入共享自身存储的循环
    Variant snapshot;
    const Variant* src = &v;
    if (&v == this) {
        snapshot = deep_copy(v);
        src = &snapshot;
    }
    copy_on_write_array();
    auto* sp = std::get_if<std::shared_ptr<detail::ArrayData>>(&value_);
    if (sp && *sp && i < (*sp)->items.size()) {
        (*sp)->items[i] = *src;
    }
}

void Variant::append(const Variant& v) {
    if (type_ != ARRAY) return;
    Variant snapshot;
    const Variant* src = &v;
    if (&v == this) {
        snapshot = deep_copy(v);
        src = &snapshot;
    }
    copy_on_write_array();
    auto* sp = std::get_if<std::shared_ptr<detail::ArrayData>>(&value_);
    if (sp && *sp) {
        (*sp)->items.push_back(*src);
    }
}

// =====================================================================
//  DICTIONARY 编辑（COW）
// =====================================================================

size_t Variant::dict_size() const {
    if (type_ != DICTIONARY) return 0;
    const auto* sp = std::get_if<std::shared_ptr<detail::DictData>>(&value_);
    return (sp && *sp) ? (*sp)->items.size() : 0;
}

const Variant& Variant::dict_at(const std::string& key) const {
    static const Variant nil_instance;
    if (type_ != DICTIONARY) return nil_instance;
    const auto* sp = std::get_if<std::shared_ptr<detail::DictData>>(&value_);
    if (!sp || !*sp) return nil_instance;
    auto it = (*sp)->items.find(key);
    return it != (*sp)->items.end() ? it->second : nil_instance;
}

bool Variant::dict_has(const std::string& key) const {
    if (type_ != DICTIONARY) return false;
    const auto* sp = std::get_if<std::shared_ptr<detail::DictData>>(&value_);
    if (!sp || !*sp) return false;
    return (*sp)->items.find(key) != (*sp)->items.end();
}

void Variant::set_dict_element(const std::string& key, const Variant& v) {
    if (type_ != DICTIONARY) return;
    Variant snapshot;
    const Variant* src = &v;
    if (&v == this) {
        snapshot = deep_copy(v);
        src = &snapshot;
    }
    copy_on_write_dict();
    auto* sp = std::get_if<std::shared_ptr<detail::DictData>>(&value_);
    if (sp && *sp) {
        (*sp)->items[key] = *src;
    }
}

size_t Variant::dict_erase(const std::string& key) {
    if (type_ != DICTIONARY) return 0;
    copy_on_write_dict();
    auto* sp = std::get_if<std::shared_ptr<detail::DictData>>(&value_);
    if (!sp || !*sp) return 0;
    return (*sp)->items.erase(key);
}

// =====================================================================
//  JSON 序列化
// =====================================================================

namespace {

constexpr int kSerializeMaxDepth = 64;  // 与头文件 kMaxSerializeDepth 对齐（循环引用防护）

// 双精度 → JSON 数字字面量。使用 %.17g 保证可逆；若结果无 '.' / 'e' / 'E'
//（纯整数字面量），追加 ".0" 以保证反序列化时识别为 FLOAT 而非 INT。
void append_double(std::string& out, double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    bool looks_like_float = false;
    for (const char* p = buf; *p != '\0'; ++p) {
        char c = *p;
        if (c == '.' || c == 'e' || c == 'E' || c == 'n' /*nan/inf 防御*/) {
            looks_like_float = true;
            break;
        }
    }
    out += buf;
    if (!looks_like_float) out += ".0";
}

// 单精度 → JSON 数字字面量（VECTOR2/COLOR 分量）。%.9g 足以无损往返 float32。
void append_float(std::string& out, float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    out += buf;
}

// 字符串 → 带转义的 JSON 字符串字面量（含首尾引号）。
void append_escaped_string(std::string& out, const std::string& s) {
    out += '"';
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    // 控制字符：\u00XX
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    // 可打印 ASCII / UTF-8 多字节：原样输出
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    out += '"';
}

}  // namespace

void Variant::serialize_into(std::string& out, int depth) const {
    if (depth > kSerializeMaxDepth) {
        out += "null";  // 循环引用/超深嵌套防护
        return;
    }
    switch (type_) {
        case NIL:
            out += "null";
            return;
        case BOOL:
            out += (std::get<bool>(value_) ? "true" : "false");
            return;
        case INT:
            out += std::to_string(std::get<int64_t>(value_));
            return;
        case FLOAT:
            append_double(out, std::get<double>(value_));
            return;
        case STRING:
            append_escaped_string(out, std::get<std::string>(value_));
            return;
        case OBJECT:
            // OBJECT 指针不参与序列化（不可逆）
            out += "null";
            return;
        case VECTOR2: {
            const auto& v = std::get<Vector2>(value_);
            out += "{\"x\":";
            append_float(out, v.x);
            out += ",\"y\":";
            append_float(out, v.y);
            out += '}';
            return;
        }
        case COLOR: {
            const auto& c = std::get<Color>(value_);
            out += "{\"r\":";
            append_float(out, c.r);
            out += ",\"g\":";
            append_float(out, c.g);
            out += ",\"b\":";
            append_float(out, c.b);
            out += ",\"a\":";
            append_float(out, c.a);
            out += '}';
            return;
        }
        case ARRAY: {
            out += '[';
            const auto* sp = std::get_if<std::shared_ptr<detail::ArrayData>>(&value_);
            if (sp && *sp) {
                bool first = true;
                for (const auto& e : (*sp)->items) {
                    if (!first) out += ',';
                    first = false;
                    e.serialize_into(out, depth + 1);
                }
            }
            out += ']';
            return;
        }
        case DICTIONARY: {
            out += '{';
            const auto* sp = std::get_if<std::shared_ptr<detail::DictData>>(&value_);
            if (sp && *sp) {
                bool first = true;
                for (const auto& kv : (*sp)->items) {
                    if (!first) out += ',';
                    first = false;
                    append_escaped_string(out, kv.first);
                    out += ':';
                    kv.second.serialize_into(out, depth + 1);
                }
            }
            out += '}';
            return;
        }
    }
}

std::string Variant::serialize() const {
    std::string out;
    out.reserve(64);
    serialize_into(out, 0);
    return out;
}

// =====================================================================
//  JSON 反序列化（手写最小递归下降解析器）
// =====================================================================

namespace {

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : src_(src), pos_(0) {}

    Variant parse() {
        skip_ws();
        Variant v = parse_value();
        return v;
    }

private:
    const std::string& src_;
    size_t pos_;

    void skip_ws() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    char peek() {
        skip_ws();
        return pos_ < src_.size() ? src_[pos_] : '\0';
    }

    Variant parse_value() {
        skip_ws();
        if (pos_ >= src_.size()) return Variant();
        char c = src_[pos_];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return Variant(parse_string());
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        return parse_number();
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::string parse_string() {
        std::string out;
        if (pos_ >= src_.size() || src_[pos_] != '"') return out;
        ++pos_;  // 跳过起始引号
        while (pos_ < src_.size() && src_[pos_] != '"') {
            char c = src_[pos_++];
            if (c != '\\') {
                out += c;
                continue;
            }
            // 转义序列
            if (pos_ >= src_.size()) break;
            char esc = src_[pos_++];
            switch (esc) {
                case '"':  out += '"'; break;
                case '\\': out += '\\'; break;
                case '/':  out += '/'; break;
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'u': {
                    if (pos_ + 4 > src_.size()) break;
                    char buf[5] = {src_[pos_], src_[pos_ + 1], src_[pos_ + 2], src_[pos_ + 3], '\0'};
                    pos_ += 4;
                    char* end = nullptr;
                    uint32_t cp = static_cast<uint32_t>(std::strtoul(buf, &end, 16));
                    append_utf8(out, cp);
                    break;
                }
                default: out += esc; break;
            }
        }
        if (pos_ < src_.size() && src_[pos_] == '"') ++pos_;  // 跳过结束引号
        return out;
    }

    Variant parse_number() {
        size_t start = pos_;
        if (pos_ < src_.size() && (src_[pos_] == '-' || src_[pos_] == '+')) ++pos_;
        bool is_float = false;
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c >= '0' && c <= '9') { ++pos_; continue; }
            if (c == '.') { is_float = true; ++pos_; continue; }
            if (c == 'e' || c == 'E') { is_float = true; ++pos_; continue; }
            if ((c == '-' || c == '+') && pos_ > start &&
                (src_[pos_ - 1] == 'e' || src_[pos_ - 1] == 'E')) { ++pos_; continue; }
            break;
        }
        std::string num = src_.substr(start, pos_ - start);
        if (num.empty()) return Variant();
        try {
            if (is_float) return Variant(std::stod(num));
            return Variant(static_cast<int64_t>(std::stoll(num)));
        } catch (...) {
            return Variant();
        }
    }

    Variant parse_bool() {
        if (src_.compare(pos_, 4, "true") == 0) { pos_ += 4; return Variant(true); }
        if (src_.compare(pos_, 5, "false") == 0) { pos_ += 5; return Variant(false); }
        return Variant();
    }

    Variant parse_null() {
        if (src_.compare(pos_, 4, "null") == 0) { pos_ += 4; return Variant(); }
        return Variant();
    }

    Variant parse_array() {
        ++pos_;  // 跳过 [
        std::vector<Variant> items;
        skip_ws();
        if (pos_ < src_.size() && src_[pos_] == ']') { ++pos_; return Variant(items); }
        while (pos_ < src_.size()) {
            items.push_back(parse_value());
            skip_ws();
            if (pos_ < src_.size() && src_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < src_.size() && src_[pos_] == ']') { ++pos_; break; }
            break;
        }
        return Variant(items);
    }

    Variant parse_object() {
        ++pos_;  // 跳过 {
        std::unordered_map<std::string, Variant> items;
        bool has_x = false, has_y = false;
        bool has_r = false, has_g = false, has_b = false, has_a = false;
        skip_ws();
        if (pos_ < src_.size() && src_[pos_] == '}') { ++pos_; return Variant(items); }
        while (pos_ < src_.size()) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            if (pos_ < src_.size() && src_[pos_] == ':') ++pos_;
            Variant val = parse_value();
            items[key] = val;
            // 累计键集用于结尾处的 VECTOR2/COLOR 启发式判定（size 校验已能排除
            // 含额外键的情形，这里仅记录键的存在性）
            if (key == "x") has_x = true;
            else if (key == "y") has_y = true;
            else if (key == "r") has_r = true;
            else if (key == "g") has_g = true;
            else if (key == "b") has_b = true;
            else if (key == "a") has_a = true;
            skip_ws();
            if (pos_ < src_.size() && src_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < src_.size() && src_[pos_] == '}') { ++pos_; break; }
            break;
        }
        // 启发式：键集恰好为 {x, y} → VECTOR2；恰好为 {r, g, b, a} → COLOR
        if (items.size() == 2 && has_x && has_y && !has_r && !has_g && !has_b && !has_a) {
            Vector2 v;
            v.x = static_cast<float>(items["x"].as_float());
            v.y = static_cast<float>(items["y"].as_float());
            return Variant(v);
        }
        if (items.size() == 4 && has_r && has_g && has_b && has_a && !has_x && !has_y) {
            Color c;
            c.r = static_cast<float>(items["r"].as_float());
            c.g = static_cast<float>(items["g"].as_float());
            c.b = static_cast<float>(items["b"].as_float());
            c.a = static_cast<float>(items["a"].as_float());
            return Variant(c);
        }
        return Variant(items);
    }
};

}  // namespace

Variant Variant::deserialize(const std::string& json) {
    JsonParser parser(json);
    return parser.parse();
}

}  // namespace eda
