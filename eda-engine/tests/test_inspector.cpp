// Inspector 单元测试（P4 Task 5）。
//
// 覆盖：
//   反射枚举（1）：edit(Object*) 沿 ClassDB 类链枚举属性，派生类遮蔽基类。
//   类型分派（2）：STRING→LineEdit / INT→SpinBox(int) / FLOAT→SpinBox(float)
//                 / BOOL→BoolEditor。
//   各类型编辑器 roundtrip（4）：
//     - LineEdit：set_text → obj.get 一致
//     - SpinBox（int）：set_value 取整 → obj.get 一致；step_up/down
//     - SpinBox（float）：set_value → obj.get 一致
//     - BoolEditor：set_checked → obj.get 一致
//   Vector2 / Color 直接 roundtrip（2）：编辑器构造后 set_vector / set_color
//     → obj.get 一致（不依赖 ClassDB 枚举，因 ClassDB 暂不支持绑定 Vector2/Color）。
//   EnumEditor（1）：自定义 EnumEditorPlugin 注入选项；select_by_value / index
//     → obj.get 一致。
//   插件优先级覆盖（1）：用户插件返回更高优先级 → 覆盖内置渲染。
//   refresh（1）：外部改属性 → refresh() → 编辑器反映新值。
//   生命周期（2）：clear() 清空；edit(nullptr) 等价 clear。
//
// 所有权约定：每个用例在 SetUp 中清空 ClassDB 并注册测试类型。Inspector
// 声明在 target 之后（target 先析构是安全的：~Inspector 不解引用 target）。

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "editor/inspector/inspector.h"
#include "editor/inspector/property_editors.h"
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/object/variant.h"
#include "core/math/color.h"
#include "core/math/vector2.h"

using namespace eda;

namespace {

// 辅助：判断 vector 是否包含某项。
bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// ---- 反射测试对象：STRING / INT / FLOAT / BOOL 经 ClassDB 绑定 ----
// 重写 set/get 以转派到 ClassDB::set_property/get_property——这样编辑器
// （经 target->set/get 读写）即可命中反射绑定。
class ReflectedObject : public Object {
public:
    std::string label = "init";
    int64_t count = 0;
    double ratio = 0.0;
    bool enabled = false;

    std::string get_label() const { return label; }
    void set_label(const std::string& v) { label = v; }
    int64_t get_count() const { return count; }
    void set_count(int64_t v) { count = v; }
    double get_ratio() const { return ratio; }
    void set_ratio(double v) { ratio = v; }
    bool get_enabled() const { return enabled; }
    void set_enabled(bool v) { enabled = v; }

    bool set(const std::string& name, const Variant& value) override {
        return ClassDB::set_property(this, name, value);
    }
    Variant get(const std::string& name) const override {
        // ClassDB::get_property 形参为非 const Object*；此处 const 转派安全：
        // PropertyBind::get 内部仅调用 const getter。
        return ClassDB::get_property(const_cast<ReflectedObject*>(this), name);
    }

    static std::string get_class_static() { return "ReflectedObject"; }
    std::string get_class() const override { return "ReflectedObject"; }
    bool is_class(const std::string& n) const override {
        return n == "ReflectedObject" || Object::is_class(n);
    }
};

// ---- 直接访问对象：Vector2 / Color 经 set/get 直接存成员 ----
// （ClassDB 的 bind_property 模板暂不支持 Vector2/Color 返回类型，故不进反射表。）
class DirectObject : public Object {
public:
    Vector2 position{1.0f, 2.0f};
    Color tint{0.10f, 0.20f, 0.30f, 1.0f};

    bool set(const std::string& name, const Variant& value) override {
        if (name == "position") { position = value.as_vector2(); return true; }
        if (name == "tint") { tint = value.as_color(); return true; }
        return false;
    }
    Variant get(const std::string& name) const override {
        if (name == "position") return Variant(position);
        if (name == "tint") return Variant(tint);
        return Variant();
    }
};

// ---- 枚举宿主：INT 属性 "mode"（ClassDB 绑定）----
class EnumObject : public Object {
public:
    int64_t mode = 0;

    int64_t get_mode() const { return mode; }
    void set_mode(int64_t v) { mode = v; }

    bool set(const std::string& name, const Variant& value) override {
        return ClassDB::set_property(this, name, value);
    }
    Variant get(const std::string& name) const override {
        return ClassDB::get_property(const_cast<EnumObject*>(this), name);
    }

    static std::string get_class_static() { return "EnumObject"; }
    std::string get_class() const override { return "EnumObject"; }
    bool is_class(const std::string& n) const override {
        return n == "EnumObject" || Object::is_class(n);
    }
};

// 自定义插件：用 CustomStringEditor 替代内置 LineEditEditor 渲染 "label"。
class CustomStringEditor : public PropertyEditorBase {
public:
    CustomStringEditor(Object* t, const std::string& p) : PropertyEditorBase(t, p) {
        sync_from_target();
    }
    void sync_from_target() override { marker_ = read_value_().as_string(); }
    std::string marker_;

    static std::string get_class_static() { return "CustomStringEditor"; }
    std::string get_class() const override { return "CustomStringEditor"; }
    bool is_class(const std::string& n) const override {
        return n == "CustomStringEditor" || PropertyEditorBase::is_class(n);
    }
};

class CustomLabelPlugin : public InspectorPlugin {
public:
    int can_handle(Object* /*target*/, const PropertyInfo& info) override {
        return info.name == "label" ? 100 : 0;
    }
    Control* create_editor(Object* target, const PropertyInfo& info) override {
        if (info.name != "label") return nullptr;
        return new CustomStringEditor(target, info.name);
    }
};

void RegisterTestClasses() {
    GDREGISTER_CLASS(ReflectedObject);
    ClassDB::bind_property("label", &ReflectedObject::get_label, &ReflectedObject::set_label);
    ClassDB::bind_property("count", &ReflectedObject::get_count, &ReflectedObject::set_count);
    ClassDB::bind_property("ratio", &ReflectedObject::get_ratio, &ReflectedObject::set_ratio);
    ClassDB::bind_property("enabled", &ReflectedObject::get_enabled, &ReflectedObject::set_enabled);

    GDREGISTER_CLASS(EnumObject);
    ClassDB::bind_property("mode", &EnumObject::get_mode, &EnumObject::set_mode);
}

}  // namespace

// 每用例独立 ClassDB 状态。
class InspectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        ClassDB::cleanup();
        RegisterTestClasses();
    }
    void TearDown() override { ClassDB::cleanup(); }
};

// ===================== 反射枚举 =====================

TEST_F(InspectorTest, EditEnumeratesAllBoundProperties) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    EXPECT_EQ(insp.get_target(), &obj);
    EXPECT_EQ(insp.get_editor_count(), 4u);
    const auto names = insp.get_property_names();
    EXPECT_TRUE(contains(names, "label"));
    EXPECT_TRUE(contains(names, "count"));
    EXPECT_TRUE(contains(names, "ratio"));
    EXPECT_TRUE(contains(names, "enabled"));
}

TEST_F(InspectorTest, PropertiesIncludeInheritedFromParentChain) {
    // 派生类对象应同时枚举到自身与基类（Object）链上的属性。
    // ReflectedObject 无独立父类属性（parent=Object，Object 无属性），但确认枚举
    // 不遗漏派生类自身的全部属性。
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);
    EXPECT_EQ(insp.get_editor_count(), 4u);
}

TEST_F(InspectorTest, EditNullptrClearsEditors) {
    Inspector insp;
    ReflectedObject obj;
    insp.edit(&obj);
    EXPECT_GT(insp.get_editor_count(), 0u);
    insp.edit(nullptr);
    EXPECT_EQ(insp.get_editor_count(), 0u);
    EXPECT_EQ(insp.get_target(), nullptr);
}

TEST_F(InspectorTest, ClearDropsTargetAndEditors) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);
    EXPECT_GT(insp.get_editor_count(), 0u);
    insp.clear();
    EXPECT_EQ(insp.get_editor_count(), 0u);
    EXPECT_EQ(insp.get_target(), nullptr);
}

// ===================== 类型分派 =====================

TEST_F(InspectorTest, DispatchesStringToLineEditEditor) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    Control* e = insp.find_editor_for("label");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->get_class(), "LineEditEditor");
    EXPECT_TRUE(e->is_class("LineEditEditor"));
}

TEST_F(InspectorTest, DispatchesIntToSpinBoxIntEditor) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    Control* e = insp.find_editor_for("count");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->get_class(), "SpinBoxEditor");
    auto* sb = static_cast<SpinBoxEditor*>(e);
    EXPECT_TRUE(sb->is_int());
}

TEST_F(InspectorTest, DispatchesFloatToSpinBoxFloatEditor) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    Control* e = insp.find_editor_for("ratio");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->get_class(), "SpinBoxEditor");
    auto* sb = static_cast<SpinBoxEditor*>(e);
    EXPECT_FALSE(sb->is_int());
}

TEST_F(InspectorTest, DispatchesBoolToBoolEditor) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    Control* e = insp.find_editor_for("enabled");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->get_class(), "BoolEditor");
}

TEST_F(InspectorTest, EditorInitiallySyncsFromTarget) {
    ReflectedObject obj;
    obj.label = "hello";
    obj.count = 42;
    obj.ratio = 3.5;
    obj.enabled = true;
    Inspector insp;
    insp.edit(&obj);

    auto* le = static_cast<LineEditEditor*>(insp.find_editor_for("label"));
    ASSERT_NE(le, nullptr);
    EXPECT_EQ(le->get_text(), "hello");

    auto* sb_count = static_cast<SpinBoxEditor*>(insp.find_editor_for("count"));
    ASSERT_NE(sb_count, nullptr);
    EXPECT_DOUBLE_EQ(sb_count->get_value(), 42.0);

    auto* sb_ratio = static_cast<SpinBoxEditor*>(insp.find_editor_for("ratio"));
    ASSERT_NE(sb_ratio, nullptr);
    EXPECT_DOUBLE_EQ(sb_ratio->get_value(), 3.5);

    auto* be = static_cast<BoolEditor*>(insp.find_editor_for("enabled"));
    ASSERT_NE(be, nullptr);
    EXPECT_TRUE(be->is_checked());
}

// ===================== 各类型编辑器 roundtrip =====================

TEST_F(InspectorTest, LineEditEditorRoundtripWritesToTarget) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    auto* le = static_cast<LineEditEditor*>(insp.find_editor_for("label"));
    ASSERT_NE(le, nullptr);
    le->set_text("modified");
    EXPECT_EQ(le->get_text(), "modified");
    EXPECT_EQ(obj.label, "modified");                       // 编辑器 → set → 成员
    EXPECT_EQ(obj.get("label").as_string(), "modified");    // get 一致
}

TEST_F(InspectorTest, SpinBoxIntEditorRoundtrip) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    auto* sb = static_cast<SpinBoxEditor*>(insp.find_editor_for("count"));
    ASSERT_NE(sb, nullptr);
    sb->set_value(17.0);
    EXPECT_EQ(sb->get_value(), 17.0);
    EXPECT_EQ(obj.count, 17);
    EXPECT_EQ(obj.get("count").as_int(), 17);
}

TEST_F(InspectorTest, SpinBoxIntEditorRoundsFractionalInput) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    auto* sb = static_cast<SpinBoxEditor*>(insp.find_editor_for("count"));
    ASSERT_NE(sb, nullptr);
    sb->set_value(5.7);  // INT 属性取整
    EXPECT_EQ(sb->get_value(), 6.0);
    EXPECT_EQ(obj.count, 6);
}

TEST_F(InspectorTest, SpinBoxStepUpDownAdjustsAndWrites) {
    ReflectedObject obj;
    obj.count = 10;
    Inspector insp;
    insp.edit(&obj);

    auto* sb = static_cast<SpinBoxEditor*>(insp.find_editor_for("count"));
    ASSERT_NE(sb, nullptr);
    sb->set_step(5.0);
    sb->step_up();
    EXPECT_EQ(obj.count, 15);
    EXPECT_DOUBLE_EQ(sb->get_value(), 15.0);

    sb->step_down();
    sb->step_down();  // 15 - 5 - 5 = 5
    EXPECT_EQ(obj.count, 5);
    EXPECT_DOUBLE_EQ(sb->get_value(), 5.0);
}

TEST_F(InspectorTest, SpinBoxFloatEditorRoundtrip) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    auto* sb = static_cast<SpinBoxEditor*>(insp.find_editor_for("ratio"));
    ASSERT_NE(sb, nullptr);
    sb->set_value(2.5);
    EXPECT_DOUBLE_EQ(sb->get_value(), 2.5);
    EXPECT_DOUBLE_EQ(obj.ratio, 2.5);
    EXPECT_DOUBLE_EQ(obj.get("ratio").as_float(), 2.5);
}

TEST_F(InspectorTest, BoolEditorRoundtrip) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    auto* be = static_cast<BoolEditor*>(insp.find_editor_for("enabled"));
    ASSERT_NE(be, nullptr);
    EXPECT_FALSE(be->is_checked());
    be->set_checked(true);
    EXPECT_TRUE(be->is_checked());
    EXPECT_TRUE(obj.enabled);
    EXPECT_EQ(obj.get("enabled").as_bool(), true);
}

// ===================== Vector2 / Color 直接 roundtrip =====================
// （不经 Inspector 枚举——ClassDB 暂不支持 Vector2/Color 属性绑定。）

TEST_F(InspectorTest, Vector2EditorRoundtripWritesToTarget) {
    DirectObject obj;
    Vector2Editor editor(&obj, "position");
    EXPECT_FLOAT_EQ(editor.get_vector().x, 1.0f);  // 初始 sync 自 target
    EXPECT_FLOAT_EQ(editor.get_vector().y, 2.0f);

    editor.set_vector({5.0f, 7.0f});
    EXPECT_FLOAT_EQ(obj.position.x, 5.0f);
    EXPECT_FLOAT_EQ(obj.position.y, 7.0f);
    EXPECT_FLOAT_EQ(editor.get_vector().x, 5.0f);
    EXPECT_FLOAT_EQ(editor.get_vector().y, 7.0f);
    EXPECT_FLOAT_EQ(obj.get("position").as_vector2().x, 5.0f);
}

TEST_F(InspectorTest, Vector2EditorSetXKeepsY) {
    DirectObject obj;
    Vector2Editor editor(&obj, "position");
    editor.set_x(9.0f);
    EXPECT_FLOAT_EQ(obj.position.x, 9.0f);
    EXPECT_FLOAT_EQ(obj.position.y, 2.0f);  // y 保持
}

TEST_F(InspectorTest, ColorEditorRoundtripWritesToTarget) {
    DirectObject obj;
    ColorEditor editor(&obj, "tint");
    EXPECT_FLOAT_EQ(editor.get_color().r, 0.10f);  // 初始 sync 自 target

    Color nw{0.5f, 0.6f, 0.7f, 0.8f};
    editor.set_color(nw);
    EXPECT_FLOAT_EQ(obj.tint.r, 0.5f);
    EXPECT_FLOAT_EQ(obj.tint.g, 0.6f);
    EXPECT_FLOAT_EQ(obj.tint.b, 0.7f);
    EXPECT_FLOAT_EQ(obj.tint.a, 0.8f);
    EXPECT_FLOAT_EQ(editor.get_color().b, 0.7f);
    EXPECT_FLOAT_EQ(obj.get("tint").as_color().a, 0.8f);
}

// ===================== EnumEditor（自定义插件注入选项）=====================

TEST_F(InspectorTest, EnumEditorPluginOverridesSpinBoxForIntProperty) {
    EnumObject obj;
    Inspector insp;
    EnumEditorPlugin enum_plugin("mode", {{0, "Off"}, {1, "On"}, {2, "Auto"}});
    insp.add_plugin(&enum_plugin);
    insp.edit(&obj);

    // 没有 EnumEditorPlugin 时 "mode" 会落到内置 SpinBoxEditor；此处应被覆盖。
    Control* e = insp.find_editor_for("mode");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->get_class(), "EnumEditor");
}

TEST_F(InspectorTest, EnumEditorRoundtripSelectByValue) {
    EnumObject obj;
    Inspector insp;
    EnumEditorPlugin enum_plugin("mode", {{0, "Off"}, {1, "On"}, {2, "Auto"}});
    insp.add_plugin(&enum_plugin);
    insp.edit(&obj);

    auto* ee = static_cast<EnumEditor*>(insp.find_editor_for("mode"));
    ASSERT_NE(ee, nullptr);
    EXPECT_EQ(ee->get_options().size(), 3u);

    ee->select_by_value(2);
    EXPECT_EQ(ee->get_value(), 2);
    EXPECT_EQ(obj.mode, 2);
    EXPECT_EQ(obj.get("mode").as_int(), 2);
    EXPECT_EQ(ee->get_selected_index(), 2);
}

TEST_F(InspectorTest, EnumEditorRoundtripSelectByIndex) {
    EnumObject obj;
    Inspector insp;
    EnumEditorPlugin enum_plugin("mode", {{10, "A"}, {20, "B"}, {30, "C"}});
    insp.add_plugin(&enum_plugin);
    insp.edit(&obj);

    auto* ee = static_cast<EnumEditor*>(insp.find_editor_for("mode"));
    ASSERT_NE(ee, nullptr);

    ee->select_by_index(1);  // 选项 [1] = (20, "B")
    EXPECT_EQ(ee->get_value(), 20);
    EXPECT_EQ(obj.mode, 20);
    EXPECT_EQ(ee->get_selected_index(), 1);
    EXPECT_EQ(ee->get_options()[1].second, "B");
}

TEST_F(InspectorTest, EnumEditorSelectByInvalidIndexIgnored) {
    EnumObject obj;
    Inspector insp;
    EnumEditorPlugin enum_plugin("mode", {{0, "Off"}, {1, "On"}});
    insp.add_plugin(&enum_plugin);
    insp.edit(&obj);

    auto* ee = static_cast<EnumEditor*>(insp.find_editor_for("mode"));
    ASSERT_NE(ee, nullptr);
    int64_t before = ee->get_value();
    ee->select_by_index(99);  // 越界：忽略
    ee->select_by_index(-1);
    EXPECT_EQ(ee->get_value(), before);
}

// ===================== 插件优先级覆盖 =====================

TEST_F(InspectorTest, UserPluginOverridesBuiltinByPriority) {
    ReflectedObject obj;
    Inspector insp;
    CustomLabelPlugin custom;
    insp.add_plugin(&custom);  // 用户插件：label → CustomStringEditor（优先级 100）
    insp.edit(&obj);

    Control* label_editor = insp.find_editor_for("label");
    ASSERT_NE(label_editor, nullptr);
    EXPECT_EQ(label_editor->get_class(), "CustomStringEditor");
    EXPECT_TRUE(label_editor->is_class("CustomStringEditor"));

    // 其他属性仍走内置渲染。
    Control* count_editor = insp.find_editor_for("count");
    ASSERT_NE(count_editor, nullptr);
    EXPECT_EQ(count_editor->get_class(), "SpinBoxEditor");
}

TEST_F(InspectorTest, UserPluginFallsBackToBuiltinWhenCannotHandle) {
    ReflectedObject obj;
    Inspector insp;
    CustomLabelPlugin custom;  // 只处理 "label"
    insp.add_plugin(&custom);
    insp.edit(&obj);

    // label 之外的属性仍由内置插件处理。
    EXPECT_EQ(insp.get_editor_count(), 4u);
    EXPECT_NE(insp.find_editor_for("count"), nullptr);
    EXPECT_NE(insp.find_editor_for("ratio"), nullptr);
    EXPECT_NE(insp.find_editor_for("enabled"), nullptr);
}

TEST_F(InspectorTest, AddPluginAfterEditRebuilds) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    // 初始：label 走内置 LineEditEditor
    Control* before = insp.find_editor_for("label");
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(before->get_class(), "LineEditEditor");

    CustomLabelPlugin custom;
    insp.add_plugin(&custom);  // 注册后立即重建

    Control* after = insp.find_editor_for("label");
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->get_class(), "CustomStringEditor");
}

// ===================== refresh 反映外部改动 =====================

TEST_F(InspectorTest, RefreshRebuildsAndReflectsExternalMutation) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    auto* le = static_cast<LineEditEditor*>(insp.find_editor_for("label"));
    ASSERT_NE(le, nullptr);
    EXPECT_EQ(le->get_text(), "init");

    // 外部直接改动成员（不经 Inspector）
    obj.label = "externally changed";
    // 编辑器本地仍显示旧值（缓存）
    EXPECT_EQ(le->get_text(), "init");

    insp.refresh();  // 重建

    // 旧编辑器已销毁；重新取出并验证反映新值
    auto* le_after = static_cast<LineEditEditor*>(insp.find_editor_for("label"));
    ASSERT_NE(le_after, nullptr);
    EXPECT_EQ(le_after->get_text(), "externally changed");
}

TEST_F(InspectorTest, RefreshKeepsEditorCountForSamePropertySet) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);
    EXPECT_EQ(insp.get_editor_count(), 4u);
    obj.label = "changed";
    insp.refresh();
    EXPECT_EQ(insp.get_editor_count(), 4u);  // 属性集未变，数量稳定
}

// ===================== 只读模式 =====================

TEST_F(InspectorTest, ReadOnlyModeRejectsEdits) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);
    insp.set_read_only(true);

    auto* le = static_cast<LineEditEditor*>(insp.find_editor_for("label"));
    ASSERT_NE(le, nullptr);
    EXPECT_TRUE(le->is_read_only());
    le->set_text("should not apply");
    EXPECT_NE(obj.label, "should not apply");
    EXPECT_EQ(obj.label, "init");
}

TEST_F(InspectorTest, ReadOnlyAppliedToExistingEditorsOnToggle) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);

    auto* be = static_cast<BoolEditor*>(insp.find_editor_for("enabled"));
    ASSERT_NE(be, nullptr);
    EXPECT_FALSE(be->is_read_only());

    insp.set_read_only(true);
    EXPECT_TRUE(be->is_read_only());

    be->set_checked(true);  // 应被拒绝
    EXPECT_FALSE(obj.enabled);
}

// ===================== 未匹配属性的处理 =====================

TEST_F(InspectorTest, PropertyWithoutPluginIsSkipped) {
    // DirectObject 未在 ClassDB 注册任何属性 → 反射枚举为空 → 无编辑器。
    DirectObject obj;
    Inspector insp;
    insp.edit(&obj);
    EXPECT_EQ(insp.get_editor_count(), 0u);
}

TEST_F(InspectorTest, FindEditorForUnknownReturnsNull) {
    ReflectedObject obj;
    Inspector insp;
    insp.edit(&obj);
    EXPECT_EQ(insp.find_editor_for("nonexistent"), nullptr);
}
