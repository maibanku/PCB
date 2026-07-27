// PropertyEditors 实现（P4 Task 5）。
//
// 内置编辑器 + 内置 InspectorPlugin 实现。

#include "editor/inspector/property_editors.h"

#include <cmath>
#include <utility>

namespace eda {

// ====== PropertyEditorBase ======

PropertyEditorBase::PropertyEditorBase(Object* target, std::string property)
    : target_(target), property_(std::move(property)) {
    // 注意：不在基类构造函数中调用 sync_from_target()——此时派生类尚未完成
    // 构造，虚分派到不了派生层。各派生类的构造函数末尾自行调用。
}

Variant PropertyEditorBase::read_value_() const {
    if (target_ == nullptr) return Variant();
    return target_->get(property_);
}

bool PropertyEditorBase::write_value_(const Variant& v) {
    if (read_only_) return false;
    if (target_ == nullptr) return false;
    bool ok = target_->set(property_, v);
    if (ok) notify_changed_();
    return ok;
}

void PropertyEditorBase::notify_changed_() {
    if (on_changed_) on_changed_();
}

// ====== LineEditEditor ======

LineEditEditor::LineEditEditor(Object* target, const std::string& property)
    : PropertyEditorBase(target, property) {
    sync_from_target();
}

void LineEditEditor::sync_from_target() {
    text_ = read_value_().as_string();
}

void LineEditEditor::set_text(const std::string& text) {
    if (is_read_only()) return;
    text_ = text;
    write_value_(Variant(text));
}

// ====== SpinBoxEditor ======

SpinBoxEditor::SpinBoxEditor(Object* target, const std::string& property, bool is_int)
    : PropertyEditorBase(target, property), is_int_(is_int) {
    sync_from_target();
}

void SpinBoxEditor::sync_from_target() {
    // INT/FLOAT 都经 as_float 取值（INT 会隐式转 double）。
    value_ = read_value_().as_float();
}

void SpinBoxEditor::set_value(double v) {
    if (is_read_only()) return;
    if (is_int_) {
        // 整数属性：向最近的整数取整（与 GUI SpinBox 的常见行为一致）。
        value_ = static_cast<double>(static_cast<int64_t>(std::round(v)));
        write_value_(Variant(static_cast<int64_t>(value_)));
    } else {
        value_ = v;
        write_value_(Variant(value_));
    }
}

void SpinBoxEditor::step_up() {
    set_value(value_ + step_);
}

void SpinBoxEditor::step_down() {
    set_value(value_ - step_);
}

// ====== ColorEditor ======

ColorEditor::ColorEditor(Object* target, const std::string& property)
    : PropertyEditorBase(target, property) {
    sync_from_target();
}

void ColorEditor::sync_from_target() {
    color_ = read_value_().as_color();
}

void ColorEditor::set_color(Color c) {
    if (is_read_only()) return;
    color_ = c;
    write_value_(Variant(c));
}

void ColorEditor::activate_picker() {
    // Task 5 占位：未来这里弹出 ColorPicker（Task 2/3 widgets）。
}

// ====== EnumEditor ======

EnumEditor::EnumEditor(Object* target, const std::string& property, OptionList options)
    : PropertyEditorBase(target, property), options_(std::move(options)) {
    sync_from_target();
}

void EnumEditor::sync_from_target() {
    value_ = read_value_().as_int();
}

void EnumEditor::select_by_value(int64_t value) {
    if (is_read_only()) return;
    value_ = value;
    write_value_(Variant(value_));
}

void EnumEditor::select_by_index(int idx) {
    if (idx < 0 || idx >= static_cast<int>(options_.size())) return;
    select_by_value(options_[idx].first);
}

int EnumEditor::get_selected_index() const {
    for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
        if (options_[i].first == value_) return i;
    }
    return -1;
}

// ====== Vector2Editor ======

Vector2Editor::Vector2Editor(Object* target, const std::string& property)
    : PropertyEditorBase(target, property) {
    sync_from_target();
}

void Vector2Editor::sync_from_target() {
    vec_ = read_value_().as_vector2();
}

void Vector2Editor::set_x(float x) {
    set_vector({x, vec_.y});
}

void Vector2Editor::set_y(float y) {
    set_vector({vec_.x, y});
}

void Vector2Editor::set_vector(Vector2 v) {
    if (is_read_only()) return;
    vec_ = v;
    write_value_(Variant(v));
}

// ====== BoolEditor ======

BoolEditor::BoolEditor(Object* target, const std::string& property)
    : PropertyEditorBase(target, property) {
    sync_from_target();
}

void BoolEditor::sync_from_target() {
    checked_ = read_value_().as_bool();
}

void BoolEditor::set_checked(bool c) {
    if (is_read_only()) return;
    checked_ = c;
    write_value_(Variant(c));
}

// ====== 内置 InspectorPlugin ======

int LineEditEditorPlugin::can_handle(Object* /*target*/, const PropertyInfo& info) {
    return info.type == Variant::STRING ? 1 : 0;
}

Control* LineEditEditorPlugin::create_editor(Object* target, const PropertyInfo& info) {
    if (info.type != Variant::STRING) return nullptr;
    return new LineEditEditor(target, info.name);
}

int SpinBoxEditorPlugin::can_handle(Object* /*target*/, const PropertyInfo& info) {
    return (info.type == Variant::INT || info.type == Variant::FLOAT) ? 1 : 0;
}

Control* SpinBoxEditorPlugin::create_editor(Object* target, const PropertyInfo& info) {
    if (info.type == Variant::INT) {
        return new SpinBoxEditor(target, info.name, /*is_int=*/true);
    }
    if (info.type == Variant::FLOAT) {
        return new SpinBoxEditor(target, info.name, /*is_int=*/false);
    }
    return nullptr;
}

int ColorEditorPlugin::can_handle(Object* /*target*/, const PropertyInfo& info) {
    return info.type == Variant::COLOR ? 1 : 0;
}

Control* ColorEditorPlugin::create_editor(Object* target, const PropertyInfo& info) {
    if (info.type != Variant::COLOR) return nullptr;
    return new ColorEditor(target, info.name);
}

int Vector2EditorPlugin::can_handle(Object* /*target*/, const PropertyInfo& info) {
    return info.type == Variant::VECTOR2 ? 1 : 0;
}

Control* Vector2EditorPlugin::create_editor(Object* target, const PropertyInfo& info) {
    if (info.type != Variant::VECTOR2) return nullptr;
    return new Vector2Editor(target, info.name);
}

int BoolEditorPlugin::can_handle(Object* /*target*/, const PropertyInfo& info) {
    return info.type == Variant::BOOL ? 1 : 0;
}

Control* BoolEditorPlugin::create_editor(Object* target, const PropertyInfo& info) {
    if (info.type != Variant::BOOL) return nullptr;
    return new BoolEditor(target, info.name);
}

// ====== EnumEditorPlugin（便利插件：特定 INT 属性 → 枚举下拉）======

EnumEditorPlugin::EnumEditorPlugin(std::string property_name, EnumEditor::OptionList options)
    : property_name_(std::move(property_name)), options_(std::move(options)) {}

int EnumEditorPlugin::can_handle(Object* /*target*/, const PropertyInfo& info) {
    if (info.name != property_name_) return 0;
    if (info.type != Variant::INT) return 0;
    return 10;  // 高于内置 SpinBox（1），覆盖默认渲染
}

Control* EnumEditorPlugin::create_editor(Object* target, const PropertyInfo& info) {
    if (info.name != property_name_ || info.type != Variant::INT) return nullptr;
    return new EnumEditor(target, info.name, options_);
}

}  // namespace eda
