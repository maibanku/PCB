// Inspector —— 反射驱动的属性检查器（P4 Task 5）。
//
// 在 Control（P4 Task 1）之上叠加：
//   - 反射枚举：edit(Object*) 沿 obj 的 ClassDB 类链收集全部已注册属性，
//     派生类同名属性遮蔽基类（与 ClassDB::find_property 的查找语义一致）。
//   - 插件链分发：对每个属性询问所有 InspectorPlugin（用户注册 + 内置），
//     取 can_handle 返回值最大者（>0）生成编辑器；用户插件优先级高于内置
//     即可覆盖默认渲染。
//   - 属性回写：编辑器经 target->set/get(name, Variant) 读写（plan 指定）。
//     目标对象的 set/get 可转派到 ClassDB（set_property/get_property）或
//     自定义存储——Inspector 不直接依赖 ClassDB 的 setter/getter。
//   - refresh()：销毁旧编辑器后按当前 target 重建，自动反映外部改动。
//
// 设计要点：
//   * Inspector 是 Control，编辑器作为子节点（add_child）被拥有；rebuild
//     时直接 delete 旧编辑器（~Node 自动从父的 children_ 抹去自身）。
//   * 插件分两类：用户插件（add_plugin 注册，非拥有）与内置插件（构造时
//     装配，unique_ptr 拥有）。两者在分发链中平等比较优先级。
//   * 不使用 dynamic_cast（-fno-rtti）：编辑器若派生自 PropertyEditorBase，
//     通过 is_class("PropertyEditorBase") 字符串判定后 static_cast 下转。
//
// 对齐 Godot editor/editor_inspector.h（EditorInspector + EditorInspectorPlugin）。

#pragma once

#include "scene/gui/control.h"
#include "core/object/object.h"
#include "core/object/variant.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eda {

// 反射属性元信息：传给插件链判定 / 构造编辑器时使用。
struct PropertyInfo {
    std::string name;
    Variant::Type type = Variant::NIL;
};

// 插件虚接口：为指定 (target, property) 提供自定义编辑器。
//   * can_handle 返回优先级：>0 表示可处理，值越大优先级越高；0 表示不能。
//   * create_editor 返回 new 出的编辑器 Control（调用方 adopt 为子节点并拥有）；
//     返回 nullptr 表示本次不构造（交由链中下一个插件）。
class InspectorPlugin {
public:
    virtual ~InspectorPlugin() = default;
    virtual int can_handle(Object* target, const PropertyInfo& info) = 0;
    virtual Control* create_editor(Object* target, const PropertyInfo& info) = 0;
};

// Inspector：属性检查器面板。
class Inspector : public Control {
public:
    Inspector();
    ~Inspector() override;

    Inspector(const Inspector&) = delete;
    Inspector& operator=(const Inspector&) = delete;

    // 编辑单一目标。传 nullptr 等价于 clear()。多次调用会先清掉旧编辑器。
    void edit(Object* target);

    // 按当前 target 重建编辑器（反映外部改动；plan 指定语义：重建）。
    void refresh();

    // 清空 target 与全部编辑器。
    void clear();

    // 注册用户插件（非拥有；调用方保证生命周期长于 Inspector 的使用）。
    // 若当前已有 target，注册后会立即重建以使新插件生效。
    void add_plugin(InspectorPlugin* plugin);

    // —— 查询 ——
    Object* get_target() const { return target_; }
    std::size_t get_editor_count() const { return editors_.size(); }
    const std::vector<std::string>& get_property_names() const { return property_names_; }

    // 按属性名查编辑器；不存在返回 nullptr。
    Control* find_editor_for(const std::string& property_name) const;

    // 属性读写（编辑器经此；转派到 target->set/get）。
    Variant get_property_value(const std::string& name) const;
    bool set_property_value(const std::string& name, const Variant& value);

    // 只读模式（库锁定等）：编辑器拒绝写入。切换后同步到现存编辑器。
    void set_read_only(bool ro);
    bool is_read_only() const { return read_only_; }

    // —— 类型查询（无 RTTI）——
    static std::string get_class_static() { return "Inspector"; }
    std::string get_class() const override { return "Inspector"; }
    bool is_class(const std::string& n) const override {
        return n == "Inspector" || Control::is_class(n);
    }

private:
    void build_editors_();
    void destroy_editors_();

    Object* target_ = nullptr;
    std::vector<InspectorPlugin*> user_plugins_;                 // 非拥有
    std::vector<std::unique_ptr<InspectorPlugin>> builtin_plugins_;  // 拥有
    std::vector<std::pair<std::string, Control*>> editors_;      // property → 编辑器
    std::vector<std::string> property_names_;
    bool read_only_ = false;
};

}  // namespace eda
