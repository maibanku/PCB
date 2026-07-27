// P2 Task 2 —— ClassDB 运行时反射测试。
//
// 覆盖：注册 → 类名查询 → 按名调用方法 → instantiate → 父类链判断 →
//       属性 set/get → 方法/属性枚举 → cast_to<T>（替代 dynamic_cast）。
//
// 依赖 Task 1（Object + Variant）按 plan 提供的接口；ClassDB 自身不使用
// RTTI / dynamic_cast（编译选项 -fno-rtti），全靠类名字符串 + 父类链。

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/object/variant.h"

using namespace eda;

namespace {

// ---- 测试用的业务类型 ----
// 每个类都按 plan 约定重写 get_class / is_class / get_class_static，
// 以便与 ClassDB 字符串注册名一致。

// Animal : Object —— 继承链的中间基类。
class Animal : public Object {
public:
    std::string name;
    int legs = 0;

    // 属性访问器
    std::string get_name() const { return name; }
    void set_name(const std::string& v) { name = v; }
    int get_legs() const { return legs; }
    void set_legs(int v) { legs = v; }

    // 方法
    std::string speak() { return "?"; }
    int area() const { return 0; }

    // 类型元信息（Task 1 硬编码风格；类名与 ClassDB 注册名一致）
    static std::string get_class_static() { return "Animal"; }
    std::string get_class() const override { return "Animal"; }
    bool is_class(const std::string& n) const override {
        return n == "Animal" || Object::is_class(n);
    }
};

// Dog : Animal —— 子类，覆盖部分方法以验证分派优先走派生类。
class Dog : public Animal {
public:
    int bark_count = 0;

    // 覆盖父类方法
    std::string speak() { return "woof"; }
    int area() const { return legs * 2; }

    // Dog 独有方法
    void bark_n(int times) { bark_count += times; }
    int get_bark_count() const { return bark_count; }

    static std::string get_class_static() { return "Dog"; }
    std::string get_class() const override { return "Dog"; }
    bool is_class(const std::string& n) const override {
        return n == "Dog" || Animal::is_class(n);
    }
};

// Standalone : Object —— 无关类，用于验证 cast_to 对不相关类型返回 nullptr。
class Standalone : public Object {
public:
    static std::string get_class_static() { return "Standalone"; }
    std::string get_class() const override { return "Standalone"; }
};

// 注册全部测试类型 + 绑定方法/属性。每次 SetUp 调用以保证状态干净。
void RegisterTestClasses() {
    GDREGISTER_CLASS(Animal);
    ClassDB::bind_property("name", &Animal::get_name, &Animal::set_name);
    ClassDB::bind_property("legs", &Animal::get_legs, &Animal::set_legs);
    ClassDB::bind_method("speak", &Animal::speak);
    ClassDB::bind_method("area", &Animal::area);

    GDREGISTER_CLASS_WITH(Dog, Animal);
    // Dog 覆盖 speak / area；ClassDB 在 Dog 类下登记同名方法，分派时优先命中。
    ClassDB::bind_method("speak", &Dog::speak);
    ClassDB::bind_method("area", &Dog::area);
    ClassDB::bind_method("bark_n", &Dog::bark_n);
    ClassDB::bind_method("get_bark_count", &Dog::get_bark_count);

    GDREGISTER_CLASS(Standalone);
}

// 辅助：判断列表是否包含某项。
bool Contains(const std::vector<std::string>& list, const std::string& key) {
    return std::find(list.begin(), list.end(), key) != list.end();
}

}  // namespace

// 每个用例独立 ClassDB 状态：SetUp 注册，TearDown 清空。
class ClassDBTest : public ::testing::Test {
protected:
    void SetUp() override {
        ClassDB::cleanup();
        RegisterTestClasses();
    }
    void TearDown() override { ClassDB::cleanup(); }
};

// ===================== 注册与查询 =====================

TEST_F(ClassDBTest, RegisterClassAppearsInClassList) {
    auto list = ClassDB::get_class_list();
    EXPECT_TRUE(Contains(list, "Animal"));
    EXPECT_TRUE(Contains(list, "Dog"));
    EXPECT_TRUE(Contains(list, "Standalone"));
}

TEST_F(ClassDBTest, ClassExistsReturnsTrueForRegistered) {
    EXPECT_TRUE(ClassDB::class_exists("Animal"));
    EXPECT_TRUE(ClassDB::class_exists("Dog"));
    EXPECT_FALSE(ClassDB::class_exists("Nonexistent"));
}

TEST_F(ClassDBTest, GetClassInfoReturnsRegisteredMetadata) {
    const ClassInfo* info = ClassDB::get_class_info("Animal");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "Animal");
    EXPECT_EQ(info->parent, "Object");
    ASSERT_NE(info->create_func, nullptr);
    EXPECT_TRUE(info->registered);
}

TEST_F(ClassDBTest, GetClassInfoReturnsNullForUnknown) {
    EXPECT_EQ(ClassDB::get_class_info("Nonexistent"), nullptr);
}

// ===================== 父类链 =====================

TEST_F(ClassDBTest, ParentClassRecordedOnRegister) {
    EXPECT_EQ(ClassDB::get_parent_class("Dog"), "Animal");
    EXPECT_EQ(ClassDB::get_parent_class("Animal"), "Object");
    EXPECT_EQ(ClassDB::get_parent_class("Standalone"), "Object");
}

TEST_F(ClassDBTest, IsParentClassDirectAndTransitive) {
    EXPECT_TRUE(ClassDB::is_parent_class("Dog", "Animal"));    // 直接父
    EXPECT_TRUE(ClassDB::is_parent_class("Dog", "Object"));    // 传递祖先
    EXPECT_TRUE(ClassDB::is_parent_class("Dog", "Dog"));       // 自身
    EXPECT_FALSE(ClassDB::is_parent_class("Dog", "Standalone"));  // 无关
    EXPECT_FALSE(ClassDB::is_parent_class("Animal", "Dog"));   // 反向非父
}

TEST_F(ClassDBTest, IsParentClassWithUnknownReturnsFalse) {
    EXPECT_FALSE(ClassDB::is_parent_class("Unknown", "Object"));
    EXPECT_FALSE(ClassDB::is_parent_class("Dog", "Unknown"));
}

// ===================== instantiate =====================

TEST_F(ClassDBTest, InstantiateByClassNameReturnsCorrectClass) {
    Object* obj = ClassDB::instantiate("Dog");
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->get_class(), "Dog");
    delete obj;
}

TEST_F(ClassDBTest, InstantiateAnimalReturnsAnimal) {
    Object* obj = ClassDB::instantiate("Animal");
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->get_class(), "Animal");
    delete obj;
}

TEST_F(ClassDBTest, InstantiateUnknownReturnsNull) {
    EXPECT_EQ(ClassDB::instantiate("Nonexistent"), nullptr);
}

// ===================== call（按名调用方法） =====================

TEST_F(ClassDBTest, CallDispatchesToDerivedOverride) {
    Dog d;
    d.legs = 4;
    // Dog 覆盖 speak / area —— 应命中 Dog 类下登记的方法
    EXPECT_EQ(ClassDB::call(&d, "speak").as_string(), "woof");
    EXPECT_EQ(ClassDB::call(&d, "area").as_int(), 8);  // legs * 2
}

TEST_F(ClassDBTest, CallFallsBackToParentWhenNotOverridden) {
    // Animal::speak 返回 "?"；Dog 未独立登记 speak 时会回退到 Animal 的版本。
    // 这里 Dog 已覆盖 speak，故另用 Animal 实例验证父类路径。
    Animal a;
    EXPECT_EQ(ClassDB::call(&a, "speak").as_string(), "?");
    EXPECT_EQ(ClassDB::call(&a, "area").as_int(), 0);
}

TEST_F(ClassDBTest, CallWithArgsInvokesMethod) {
    Dog d;
    std::vector<Variant> args{Variant(int64_t(3))};
    ClassDB::call(&d, "bark_n", args);
    EXPECT_EQ(d.bark_count, 3);

    // 二次调用累加
    std::vector<Variant> more{Variant(int64_t(2))};
    ClassDB::call(&d, "bark_n", more);
    EXPECT_EQ(d.bark_count, 5);
    EXPECT_EQ(ClassDB::call(&d, "get_bark_count").as_int(), 5);
}

TEST_F(ClassDBTest, CallZeroArgOverloadWorks) {
    Dog d;
    d.bark_count = 7;
    Variant r = ClassDB::call(&d, "get_bark_count");
    EXPECT_EQ(r.as_int(), 7);
}

TEST_F(ClassDBTest, CallUnknownMethodReturnsNil) {
    Dog d;
    EXPECT_TRUE(ClassDB::call(&d, "nonexistent_method").is_nil());
}

TEST_F(ClassDBTest, CallOnNullObjectReturnsNil) {
    EXPECT_TRUE(ClassDB::call(nullptr, "speak").is_nil());
}

TEST_F(ClassDBTest, CallWithTooFewArgsReturnsNil) {
    Dog d;
    // bark_n 需要 1 个参数；空参数列表不应崩溃
    std::vector<Variant> empty;
    EXPECT_TRUE(ClassDB::call(&d, "bark_n", empty).is_nil());
    EXPECT_EQ(d.bark_count, 0);  // 未被调用
}

// ===================== 属性（set/get via ClassDB） =====================

TEST_F(ClassDBTest, BindPropertyStringRoundtrip) {
    Animal a;
    EXPECT_TRUE(ClassDB::set_property(&a, "name", Variant(std::string("rex"))));
    EXPECT_EQ(a.name, "rex");
    EXPECT_EQ(ClassDB::get_property(&a, "name").as_string(), "rex");
}

TEST_F(ClassDBTest, BindPropertyIntRoundtrip) {
    Animal a;
    EXPECT_TRUE(ClassDB::set_property(&a, "legs", Variant(int64_t(4))));
    EXPECT_EQ(a.legs, 4);
    EXPECT_EQ(ClassDB::get_property(&a, "legs").as_int(), 4);
}

TEST_F(ClassDBTest, PropertyInheritedFromParentAccessibleOnChild) {
    // Dog 没有自己登记 "legs"，但应能通过 Animal 的属性绑定访问
    Dog d;
    EXPECT_TRUE(ClassDB::set_property(&d, "legs", Variant(int64_t(6))));
    EXPECT_EQ(d.legs, 6);
    EXPECT_EQ(ClassDB::get_property(&d, "legs").as_int(), 6);
}

TEST_F(ClassDBTest, GetUnknownPropertyReturnsNil) {
    Animal a;
    EXPECT_TRUE(ClassDB::get_property(&a, "missing").is_nil());
}

TEST_F(ClassDBTest, SetUnknownPropertyReturnsFalse) {
    Animal a;
    EXPECT_FALSE(ClassDB::set_property(&a, "missing", Variant(int64_t(1))));
}

TEST_F(ClassDBTest, SetPropertyOnNullReturnsFalse) {
    EXPECT_FALSE(ClassDB::set_property(nullptr, "name", Variant(std::string("x"))));
}

// ===================== 枚举 =====================

TEST_F(ClassDBTest, PropertyListIncludesRegistered) {
    auto props = ClassDB::get_property_list("Animal");
    EXPECT_TRUE(Contains(props, "name"));
    EXPECT_TRUE(Contains(props, "legs"));
}

TEST_F(ClassDBTest, MethodListIncludesRegistered) {
    auto methods = ClassDB::get_method_list("Animal");
    EXPECT_TRUE(Contains(methods, "speak"));
    EXPECT_TRUE(Contains(methods, "area"));
}

TEST_F(ClassDBTest, MethodListOnUnknownReturnsEmpty) {
    EXPECT_TRUE(ClassDB::get_method_list("Nonexistent").empty());
}

// ===================== cast_to<T>（替代 dynamic_cast） =====================

TEST_F(ClassDBTest, CastToExactTypeSucceeds) {
    Dog* d = new Dog();
    Object* obj = d;
    Dog* back = ClassDB::cast_to<Dog>(obj);
    EXPECT_EQ(back, d);
    delete d;
}

TEST_F(ClassDBTest, CastToBaseSucceedsWhenObjectIsDerived) {
    Dog* d = new Dog();
    Object* obj = d;
    Animal* a = ClassDB::cast_to<Animal>(obj);
    EXPECT_EQ(a, static_cast<Animal*>(d));
    delete d;
}

TEST_F(ClassDBTest, CastToUnrelatedReturnsNull) {
    Dog* d = new Dog();
    Object* obj = d;
    EXPECT_EQ(ClassDB::cast_to<Standalone>(obj), nullptr);
    delete d;
}

TEST_F(ClassDBTest, CastDownFromBaseReturnsNull) {
    // 真实对象是 Animal，cast_to<Dog> 应失败
    Animal* a = new Animal();
    Object* obj = a;
    EXPECT_EQ(ClassDB::cast_to<Dog>(obj), nullptr);
    delete a;
}

TEST_F(ClassDBTest, CastToNullReturnsNull) {
    EXPECT_EQ(ClassDB::cast_to<Dog>(nullptr), nullptr);
}

// ===================== 信号占位 =====================

TEST_F(ClassDBTest, BindSignalRecordsName) {
    ClassDB::bind_signal("Animal", "changed");
    ClassDB::bind_signal("Animal", "renamed");
    auto sigs = ClassDB::get_signal_list("Animal");
    EXPECT_TRUE(Contains(sigs, "changed"));
    EXPECT_TRUE(Contains(sigs, "renamed"));
}

TEST_F(ClassDBTest, BindSignalDeduplicatesByName) {
    ClassDB::bind_signal("Animal", "changed");
    ClassDB::bind_signal("Animal", "changed");
    auto sigs = ClassDB::get_signal_list("Animal");
    EXPECT_EQ(std::count(sigs.begin(), sigs.end(), "changed"), 1);
}

// ===================== cleanup =====================

TEST_F(ClassDBTest, CleanupClearsAllClasses) {
    ClassDB::cleanup();
    EXPECT_FALSE(ClassDB::class_exists("Animal"));
    EXPECT_FALSE(ClassDB::class_exists("Dog"));
    EXPECT_TRUE(ClassDB::get_class_list().empty());
}
