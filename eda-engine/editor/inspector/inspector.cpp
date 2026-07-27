// Inspector 实现（P4 Task 5）。
//
// 反射枚举 + 插件链分派 + 编辑器子节点生命周期管理。

#include "editor/inspector/inspector.h"
#include "editor/inspector/property_editors.h"  // 内置插件 + PropertyEditorBase
#include "core/object/class_db.h"               // ClassInfo / get_class_info

#include <algorithm>
#include <cstddef>
#include <unordered_set>
#include <utility>

namespace eda {

// 内部：沿 target 的类链收集所有已注册属性（派生遮蔽基类）。
// 与 ClassDB::find_property 的查找顺序一致：从 obj->get_class() 起逐级向父类。
namespace {

std::vector<PropertyInfo> collect_properties_along_chain(Object* target) {
    std::vector<PropertyInfo> result;
    if (target == nullptr) return result;

    std::unordered_set<std::string> seen;
    std::string current = target->get_class();
    for (int i = 0; i < 1024 && !current.empty(); ++i) {
        const ClassInfo* info = ClassDB::get_class_info(current);
        if (info == nullptr) break;  // 类未注册：链断
        for (const auto& [name, bind] : info->properties) {
            if (bind == nullptr) continue;
            if (seen.insert(name).second) {  // 派生类已遮蔽则跳过
                result.push_back({name, bind->get_type()});
            }
        }
        if (info->parent.empty()) break;
        current = info->parent;
    }
    return result;
}

}  // namespace

// ====== 构造 / 析构 ======

Inspector::Inspector() {
    // 装配内置插件链：STRING / INT / FLOAT / BOOL / COLOR / VECTOR2。
    // 顺序不影响分派结果（取最高 can_handle），但保持稳定可读。
    builtin_plugins_.push_back(std::make_unique<LineEditEditorPlugin>());
    builtin_plugins_.push_back(std::make_unique<SpinBoxEditorPlugin>());
    builtin_plugins_.push_back(std::make_unique<BoolEditorPlugin>());
    builtin_plugins_.push_back(std::make_unique<ColorEditorPlugin>());
    builtin_plugins_.push_back(std::make_unique<Vector2EditorPlugin>());
}

Inspector::~Inspector() {
    clear();
}

// ====== 编辑目标 ======

void Inspector::edit(Object* target) {
    target_ = target;
    build_editors_();
}

void Inspector::refresh() {
    // 重建：销毁旧编辑器后按当前 target 重新枚举 + 分派。
    // 重建自然反映外部对 target 属性的改动（新编辑器 ctor 会 sync_from_target）。
    build_editors_();
}

void Inspector::clear() {
    destroy_editors_();
    target_ = nullptr;
}

void Inspector::add_plugin(InspectorPlugin* plugin) {
    if (plugin == nullptr) return;
    user_plugins_.push_back(plugin);
    // 当前已有 target：立即重建以使新插件生效（用户插件通常覆盖内置）。
    if (target_ != nullptr) build_editors_();
}

// ====== 查询 ======

Control* Inspector::find_editor_for(const std::string& property_name) const {
    for (const auto& [name, editor] : editors_) {
        if (name == property_name) return editor;
    }
    return nullptr;
}

Variant Inspector::get_property_value(const std::string& name) const {
    if (target_ == nullptr) return Variant();
    return target_->get(name);
}

bool Inspector::set_property_value(const std::string& name, const Variant& value) {
    if (target_ == nullptr) return false;
    return target_->set(name, value);
}

void Inspector::set_read_only(bool ro) {
    if (read_only_ == ro) return;
    read_only_ = ro;
    // 同步到现存编辑器：仅 PropertyEditorBase 派生类支持只读语义。
    for (auto& [name, editor] : editors_) {
        if (editor != nullptr && editor->is_class("PropertyEditorBase")) {
            static_cast<PropertyEditorBase*>(editor)->set_read_only(ro);
        }
    }
}

// ====== 内部：构建 / 销毁编辑器 ======

void Inspector::destroy_editors_() {
    // 取出旧编辑器后清空表，逐个 delete。
    // ~Node 会自动从 this->children_ 抹去自身，无需手动 remove_child。
    auto old = std::move(editors_);
    editors_.clear();
    property_names_.clear();
    for (auto& [name, editor] : old) {
        delete editor;  // nullptr delete 安全
    }
}

void Inspector::build_editors_() {
    destroy_editors_();
    if (target_ == nullptr) return;

    // 1) 反射枚举属性（派生遮蔽基类）。
    std::vector<PropertyInfo> properties = collect_properties_along_chain(target_);

    // 2) 组装候选插件链：用户插件 + 内置插件。
    //    等优先级时，链中靠前（先注册）者胜——用户插件因此天然优先。
    std::vector<InspectorPlugin*> chain;
    chain.insert(chain.end(), user_plugins_.begin(), user_plugins_.end());
    for (auto& up : builtin_plugins_) chain.push_back(up.get());

    // 3) 为每个属性挑选最高优先级插件并构造编辑器。
    for (const auto& prop_info : properties) {
        InspectorPlugin* best = nullptr;
        int best_score = 0;
        for (InspectorPlugin* p : chain) {
            int score = p->can_handle(target_, prop_info);
            if (score > best_score) {
                best_score = score;
                best = p;
            }
        }
        if (best == nullptr) continue;  // 无插件可处理：跳过

        Control* editor = best->create_editor(target_, prop_info);
        if (editor == nullptr) continue;

        // 应用只读标记（PropertyEditorBase 派生类才支持）。
        if (editor->is_class("PropertyEditorBase")) {
            static_cast<PropertyEditorBase*>(editor)->set_read_only(read_only_);
        }

        // adopt 为子节点（Inspector 拥有编辑器；~Node 级联释放）。
        add_child(editor);
        editors_.push_back({prop_info.name, editor});
        property_names_.push_back(prop_info.name);
    }
}

}  // namespace eda
