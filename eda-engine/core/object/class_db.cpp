// ClassDB 非模板实现。模板成员（register_class / bind_method / bind_property /
// cast_to）在 class_db.h 内联。本文件只放运行期存储 + 静态查询逻辑。

#include "core/object/class_db.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace eda {

namespace {

// 函数局部 static：跨翻译单元初始化顺序确定；首次调用零初始化。
// 注册阶段单线程写入；运行期只读、无锁（plan Global Constraints）。
std::unordered_map<std::string, ClassInfo>& class_db_storage() {
    static std::unordered_map<std::string, ClassInfo> db;
    return db;
}

}  // namespace

ClassInfo& ClassDB::get_or_create_class_info(const std::string& name) {
    return class_db_storage()[name];  // 不存在则默认构造 ClassInfo
}

ClassInfo* ClassDB::find_class_info(const std::string& name) {
    auto& db = class_db_storage();
    auto it = db.find(name);
    return it == db.end() ? nullptr : &it->second;
}

const ClassInfo* ClassDB::get_class_info(const std::string& name) {
    return find_class_info(name);
}

bool ClassDB::class_exists(const std::string& name) {
    return find_class_info(name) != nullptr;
}

std::string ClassDB::get_parent_class(const std::string& name) {
    const ClassInfo* info = find_class_info(name);
    return info ? info->parent : std::string();
}

bool ClassDB::is_parent_class(const std::string& child_class,
                              const std::string& parent_class) {
    std::string current = child_class;
    // 防御性上限，避免意外环导致死循环；正常层级深度远小于 1024。
    for (int i = 0; i < 1024 && !current.empty(); ++i) {
        if (current == parent_class) return true;
        const ClassInfo* info = find_class_info(current);
        if (info == nullptr) return false;       // 链断裂：未注册
        if (info->parent.empty()) return false;  // 到根
        current = info->parent;
    }
    return false;
}

Object* ClassDB::instantiate(const std::string& class_name) {
    const ClassInfo* info = find_class_info(class_name);
    if (info == nullptr || info->create_func == nullptr) return nullptr;
    return info->create_func();
}

Variant ClassDB::call(Object* obj, const std::string& method,
                      const Variant* args, std::size_t arg_count) {
    if (obj == nullptr) return Variant();
    MethodBind* mb = get_method(obj, method);
    if (mb == nullptr) return Variant();
    return mb->call(obj, args, arg_count);
}

Variant ClassDB::call(Object* obj, const std::string& method,
                      const std::vector<Variant>& args) {
    return call(obj, method, args.empty() ? nullptr : args.data(), args.size());
}

bool ClassDB::set_property(Object* obj, const std::string& name,
                           const Variant& value) {
    if (obj == nullptr) return false;
    PropertyBind* pb = find_property(obj, name);
    if (pb == nullptr) return false;
    return pb->set(obj, value);
}

Variant ClassDB::get_property(Object* obj, const std::string& name) {
    if (obj == nullptr) return Variant();
    PropertyBind* pb = find_property(obj, name);
    if (pb == nullptr) return Variant();
    return pb->get(obj);
}

MethodBind* ClassDB::get_method(Object* obj, const std::string& name) {
    if (obj == nullptr) return nullptr;
    std::string current = obj->get_class();
    for (int i = 0; i < 1024 && !current.empty(); ++i) {
        ClassInfo* info = find_class_info(current);
        if (info == nullptr) break;  // 类未注册：链断
        auto it = info->methods.find(name);
        if (it != info->methods.end()) return it->second.get();
        if (info->parent.empty()) break;
        current = info->parent;
    }
    return nullptr;
}

PropertyBind* ClassDB::find_property(Object* obj, const std::string& name) {
    if (obj == nullptr) return nullptr;
    std::string current = obj->get_class();
    for (int i = 0; i < 1024 && !current.empty(); ++i) {
        ClassInfo* info = find_class_info(current);
        if (info == nullptr) break;
        auto it = info->properties.find(name);
        if (it != info->properties.end()) return it->second.get();
        if (info->parent.empty()) break;
        current = info->parent;
    }
    return nullptr;
}

void ClassDB::bind_signal(const std::string& class_name,
                          const std::string& signal_name) {
    ClassInfo* info = find_class_info(class_name);
    if (info == nullptr) return;
    if (std::find(info->signal_names.begin(), info->signal_names.end(),
                  signal_name) == info->signal_names.end()) {
        info->signal_names.push_back(signal_name);
    }
}

std::vector<std::string> ClassDB::get_class_list() {
    std::vector<std::string> result;
    auto& db = class_db_storage();
    result.reserve(db.size());
    for (const auto& [name, _] : db) result.push_back(name);
    return result;
}

std::vector<std::string> ClassDB::get_method_list(const std::string& class_name) {
    std::vector<std::string> result;
    const ClassInfo* info = find_class_info(class_name);
    if (info == nullptr) return result;
    result.reserve(info->methods.size());
    for (const auto& [name, _] : info->methods) result.push_back(name);
    return result;
}

std::vector<std::string> ClassDB::get_property_list(const std::string& class_name) {
    std::vector<std::string> result;
    const ClassInfo* info = find_class_info(class_name);
    if (info == nullptr) return result;
    result.reserve(info->properties.size());
    for (const auto& [name, _] : info->properties) result.push_back(name);
    return result;
}

std::vector<std::string> ClassDB::get_signal_list(const std::string& class_name) {
    const ClassInfo* info = find_class_info(class_name);
    if (info == nullptr) return {};
    return info->signal_names;
}

void ClassDB::cleanup() {
    class_db_storage().clear();
}

}  // namespace eda
