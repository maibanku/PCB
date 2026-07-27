// PropertyEditors —— Inspector 的内置属性编辑器控件集（P4 Task 5）。
//
// 提供：
//   * PropertyEditorBase：所有属性编辑器的共同基类。持 target+property 名，
//     读写经 target->set/get(name, Variant)。派生类覆盖 sync_from_target()
//     从当前值刷新显示；提交编辑时调用 write_value_() 回写。
//   * 内置编辑器（按 Variant::Type 分派）：
//       LineEditEditor  —— STRING
//       SpinBoxEditor   —— INT / FLOAT（含 step_up / step_down 步进）
//       ColorEditor     —— COLOR（色块，点击触发拾色器入口）
//       EnumEditor      —— INT 枚举（下拉；options 由外部提供，因 ClassDB
//                          暂不暴露枚举元信息，故不自动注册，由用户插件注入）
//       Vector2Editor   —— VECTOR2（x / y 输入）
//       BoolEditor      —— BOOL（勾选）
//   * 内置 InspectorPlugin：按 Variant::Type 分派到上述编辑器；优先级为 1
//     （用户插件返回更高值即可覆盖）。
//   * EnumEditorPlugin：把特定 INT 属性渲染为枚举下拉的便利插件。
//
// 设计要点：
//   * 所有编辑器派生自 PropertyEditorBase（进而 Control），可被 Inspector
//     adopt 为子节点。
//   * 读写在 write_value_ / read_value_ 集中转派，子类不直接碰 ClassDB。
//   * 只读模式（read_only_）在 write_value_ 与各 set_* 入口短路。
//   * 不使用 dynamic_cast：派生类覆盖 is_class 提供字符串判定。
//
// 对齐 Godot editor/editor_inspector.h 中的 EditorProperty* 系列。

#pragma once

#include "scene/gui/control.h"
#include "core/object/object.h"
#include "core/object/variant.h"
#include "core/math/color.h"
#include "core/math/vector2.h"
#include "editor/inspector/inspector.h"  // InspectorPlugin / PropertyInfo

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace eda {

// 属性编辑器基类。
class PropertyEditorBase : public Control {
public:
    using ChangeCallback = std::function<void()>;

    PropertyEditorBase(Object* target, std::string property);

    void set_target(Object* t) { target_ = t; }
    Object* get_target() const { return target_; }
    const std::string& get_property_name() const { return property_; }

    // Inspector / 外部挂载变更回调：每次成功 write 后触发。Task 5 中 Inspector
    // 不强依赖此回调（编辑器已立即回写 target），但保留钩子供后续 UndoRedo / 联动刷新。
    void set_change_callback(ChangeCallback cb) { on_changed_ = std::move(cb); }

    // 只读开关（库锁定等场景）。
    void set_read_only(bool ro) { read_only_ = ro; }
    bool is_read_only() const { return read_only_; }

    // 从 target 当前值刷新本地显示。派生类实现。
    virtual void sync_from_target() = 0;

    static std::string get_class_static() { return "PropertyEditorBase"; }
    std::string get_class() const override { return "PropertyEditorBase"; }
    bool is_class(const std::string& n) const override {
        return n == "PropertyEditorBase" || Control::is_class(n);
    }

protected:
    // 读 target 的当前属性值。
    Variant read_value_() const;
    // 回写属性值。只读模式或 target 缺失返回 false；成功则触发 on_changed_。
    bool write_value_(const Variant& v);
    void notify_changed_();

    Object* target_;
    std::string property_;
    ChangeCallback on_changed_;
    bool read_only_ = false;
};

// ============ LineEditEditor（STRING）============

class LineEditEditor : public PropertyEditorBase {
public:
    LineEditEditor(Object* target, const std::string& property);

    void set_text(const std::string& text);  // 模拟用户输入并提交
    const std::string& get_text() const { return text_; }

    void sync_from_target() override;

    static std::string get_class_static() { return "LineEditEditor"; }
    std::string get_class() const override { return "LineEditEditor"; }
    bool is_class(const std::string& n) const override {
        return n == "LineEditEditor" || PropertyEditorBase::is_class(n);
    }

private:
    std::string text_;
};

// ============ SpinBoxEditor（INT / FLOAT）============

class SpinBoxEditor : public PropertyEditorBase {
public:
    // is_int=true 表示属性是整数（提交时取整并按 INT 写回）。
    SpinBoxEditor(Object* target, const std::string& property, bool is_int);

    void set_value(double v);  // 模拟用户输入数值并提交
    double get_value() const { return value_; }

    void step_up();     // value += step
    void step_down();   // value -= step
    void set_step(double step) { step_ = step; }
    double get_step() const { return step_; }

    bool is_int() const { return is_int_; }

    void sync_from_target() override;

    static std::string get_class_static() { return "SpinBoxEditor"; }
    std::string get_class() const override { return "SpinBoxEditor"; }
    bool is_class(const std::string& n) const override {
        return n == "SpinBoxEditor" || PropertyEditorBase::is_class(n);
    }

private:
    double value_ = 0.0;
    double step_ = 1.0;
    bool is_int_;
};

// ============ ColorEditor（COLOR）============

class ColorEditor : public PropertyEditorBase {
public:
    ColorEditor(Object* target, const std::string& property);

    void set_color(Color c);  // 模拟拾色后提交
    Color get_color() const { return color_; }

    // 模拟点击色块唤起拾色器。Task 5 中无实际 UI，仅作未来集成接入点。
    void activate_picker();

    void sync_from_target() override;

    static std::string get_class_static() { return "ColorEditor"; }
    std::string get_class() const override { return "ColorEditor"; }
    bool is_class(const std::string& n) const override {
        return n == "ColorEditor" || PropertyEditorBase::is_class(n);
    }

private:
    Color color_;
};

// ============ EnumEditor（INT 枚举下拉）============

class EnumEditor : public PropertyEditorBase {
public:
    // 选项列表：(整数值, 显示标签)，按 vector 顺序呈现为下拉。
    using OptionList = std::vector<std::pair<int64_t, std::string>>;

    EnumEditor(Object* target, const std::string& property, OptionList options);

    void select_by_value(int64_t value);  // 按值选中并提交
    void select_by_index(int idx);         // 按下拉索引选中并提交
    int64_t get_value() const { return value_; }
    int get_selected_index() const;        // 未匹配返回 -1
    const OptionList& get_options() const { return options_; }

    void sync_from_target() override;

    static std::string get_class_static() { return "EnumEditor"; }
    std::string get_class() const override { return "EnumEditor"; }
    bool is_class(const std::string& n) const override {
        return n == "EnumEditor" || PropertyEditorBase::is_class(n);
    }

private:
    OptionList options_;
    int64_t value_ = 0;
};

// ============ Vector2Editor（VECTOR2）============

class Vector2Editor : public PropertyEditorBase {
public:
    Vector2Editor(Object* target, const std::string& property);

    void set_x(float x);                  // 仅改 x，保持 y
    void set_y(float y);                  // 仅改 y，保持 x
    void set_vector(Vector2 v);           // 整体提交
    Vector2 get_vector() const { return vec_; }

    void sync_from_target() override;

    static std::string get_class_static() { return "Vector2Editor"; }
    std::string get_class() const override { return "Vector2Editor"; }
    bool is_class(const std::string& n) const override {
        return n == "Vector2Editor" || PropertyEditorBase::is_class(n);
    }

private:
    Vector2 vec_;
};

// ============ BoolEditor（BOOL）============

class BoolEditor : public PropertyEditorBase {
public:
    BoolEditor(Object* target, const std::string& property);

    void set_checked(bool c);  // 模拟勾选并提交
    bool is_checked() const { return checked_; }

    void sync_from_target() override;

    static std::string get_class_static() { return "BoolEditor"; }
    std::string get_class() const override { return "BoolEditor"; }
    bool is_class(const std::string& n) const override {
        return n == "BoolEditor" || PropertyEditorBase::is_class(n);
    }

private:
    bool checked_ = false;
};

// ============ 内置 InspectorPlugin（按 Variant::Type 分派）============
//
// can_handle 返回固定优先级 1；用户插件返回更高值即可覆盖。

class LineEditEditorPlugin : public InspectorPlugin {
public:
    int can_handle(Object* target, const PropertyInfo& info) override;
    Control* create_editor(Object* target, const PropertyInfo& info) override;
};

class SpinBoxEditorPlugin : public InspectorPlugin {
public:
    int can_handle(Object* target, const PropertyInfo& info) override;
    Control* create_editor(Object* target, const PropertyInfo& info) override;
};

class ColorEditorPlugin : public InspectorPlugin {
public:
    int can_handle(Object* target, const PropertyInfo& info) override;
    Control* create_editor(Object* target, const PropertyInfo& info) override;
};

class Vector2EditorPlugin : public InspectorPlugin {
public:
    int can_handle(Object* target, const PropertyInfo& info) override;
    Control* create_editor(Object* target, const PropertyInfo& info) override;
};

class BoolEditorPlugin : public InspectorPlugin {
public:
    int can_handle(Object* target, const PropertyInfo& info) override;
    Control* create_editor(Object* target, const PropertyInfo& info) override;
};

// EnumEditorPlugin：把指定 INT 属性渲染为枚举下拉。
// 因 ClassDB 不暴露枚举元信息，由用户实例化此插件并传入选项。
// can_handle 优先级为 10（高于内置 SpinBox 的 1）以覆盖默认渲染。
class EnumEditorPlugin : public InspectorPlugin {
public:
    EnumEditorPlugin(std::string property_name, EnumEditor::OptionList options);

    int can_handle(Object* target, const PropertyInfo& info) override;
    Control* create_editor(Object* target, const PropertyInfo& info) override;

private:
    std::string property_name_;
    EnumEditor::OptionList options_;
};

}  // namespace eda
