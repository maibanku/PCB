// tests/test_command.cpp
//
// P3 命令栈 · Task 8 测试：UndoStack + SetPropertyCommand + SelectionSet。
//
// 对应 plan「几何与命令栈实现计划.md」Task 8：
//   - push/undo/redo 往返（多步、状态正确切换）
//   - macro 合并（多次 push 在 end_macro 后合并为单个 undo 单元；嵌套宏）
//   - 内存上限丢弃（超限淘汰最旧；index_/clean_index_ 正确调整）
//   - SetPropertyCommand 新旧值切换（含 ClassDB 注册属性 + 字符串属性）
//   - merge_with 连续拖动合并（同 target+key 合并；不同 key 不合并；默认关闭）
//   - clean_index 保存点跟踪
//   - SelectionSet 增删/框选/过滤/反选/选择变更命令化
//
// 集成时由主循环在 tests/CMakeLists.txt 追加：
//   add_executable(test_command test_command.cpp)
//   target_link_libraries(test_command PRIVATE eda_command gtest_main)
//   gtest_discover_tests(test_command)

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/object/variant.h"
#include "eda/command/command.h"
#include "eda/command/selection.h"
#include "eda/command/undo_stack.h"

using namespace eda;
using namespace eda::command;

namespace {

// 测试用 Object：带 ClassDB 注册属性 value / label，供 SetPropertyCommand 验证
// 通用属性变更命令的两条路径（ClassDB PropertyBind + Object::set/get 回落）。
class PropObject : public Object {
public:
    int value = 0;
    std::string label;

    int get_value() const { return value; }
    void set_value(int v) { value = v; }
    std::string get_label() const { return label; }
    void set_label(const std::string& v) { label = v; }

    static std::string get_class_static() { return "PropObject"; }
    std::string get_class() const override { return "PropObject"; }
    bool is_class(const std::string& n) const override {
        return n == "PropObject" || Object::is_class(n);
    }
};

// 测试用 Object：直接覆写 set/get（不走 ClassDB 注册），验证 SetPropertyCommand
// 的回落路径。
class MetaObject : public Object {
public:
    int64_t cached = 0;

    bool set(const std::string& key, const Variant& v) override {
        if (key == "cached") {
            cached = v.as_int();
            return true;
        }
        return Object::set(key, v);
    }
    Variant get(const std::string& key) const override {
        if (key == "cached") {
            return Variant(cached);
        }
        return Object::get(key);
    }

    static std::string get_class_static() { return "MetaObject"; }
    std::string get_class() const override { return "MetaObject"; }
    bool is_class(const std::string& n) const override {
        return n == "MetaObject" || Object::is_class(n);
    }
};

void RegisterTestClasses() {
    GDREGISTER_CLASS(PropObject);
    ClassDB::bind_property("value", &PropObject::get_value, &PropObject::set_value);
    ClassDB::bind_property("label", &PropObject::get_label, &PropObject::set_label);
}

class CommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        ClassDB::cleanup();
        RegisterTestClasses();
    }
    void TearDown() override { ClassDB::cleanup(); }
};

// 便利：构造一个设置 value 的命令。
std::unique_ptr<SetPropertyCommand> SetValue(PropObject* obj, int64_t v) {
    return std::make_unique<SetPropertyCommand>(obj, "value", Variant(v));
}

}  // namespace

// ============================================================
// SetPropertyCommand：泛用属性变更命令
// ============================================================

TEST_F(CommandTest, SetPropertyCommandAppliesNewValue) {
    PropObject obj;
    obj.value = 10;
    UndoStack stack;
    stack.push(SetValue(&obj, 42));
    EXPECT_EQ(obj.value, 42);
}

TEST_F(CommandTest, SetPropertyCommandUndoRestoresOldValue) {
    PropObject obj;
    obj.value = 10;
    UndoStack stack;
    stack.push(SetValue(&obj, 42));
    ASSERT_TRUE(stack.undo());
    EXPECT_EQ(obj.value, 10);  // 回到 execute 前的旧值
}

TEST_F(CommandTest, SetPropertyCommandRedoReappliesNewValue) {
    PropObject obj;
    obj.value = 10;
    UndoStack stack;
    stack.push(SetValue(&obj, 42));
    ASSERT_TRUE(stack.undo());
    ASSERT_TRUE(stack.redo());
    EXPECT_EQ(obj.value, 42);  // 重放新值
}

TEST_F(CommandTest, SetPropertyCommandStringProperty) {
    PropObject obj;
    obj.label = "old";
    UndoStack stack;
    stack.push(std::make_unique<SetPropertyCommand>(
        &obj, "label", Variant(std::string("new"))));
    EXPECT_EQ(obj.label, "new");
    stack.undo();
    EXPECT_EQ(obj.label, "old");
    stack.redo();
    EXPECT_EQ(obj.label, "new");
}

TEST_F(CommandTest, SetPropertyCommandWorksOnMetaObjectSetOverride) {
    // 未注册 ClassDB 属性、但直接覆写 set/get 的对象：回落路径。
    MetaObject obj;
    obj.cached = 1;
    UndoStack stack;
    stack.push(std::make_unique<SetPropertyCommand>(
        &obj, "cached", Variant(int64_t(99))));
    EXPECT_EQ(obj.cached, 99);
    stack.undo();
    EXPECT_EQ(obj.cached, 1);
    stack.redo();
    EXPECT_EQ(obj.cached, 99);
}

TEST_F(CommandTest, SetPropertyCommandHasAutoName) {
    PropObject obj;
    SetPropertyCommand cmd(&obj, "value", Variant(int64_t(1)));
    EXPECT_EQ(cmd.get_name(), std::string("Set value"));
}

// ============================================================
// UndoStack：push/undo/redo 往返
// ============================================================

TEST_F(CommandTest, CanUndoRedoFlagsReflectStackState) {
    PropObject obj;
    UndoStack stack;
    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());

    stack.push(SetValue(&obj, 1));
    EXPECT_TRUE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());

    stack.undo();
    EXPECT_FALSE(stack.can_undo());
    EXPECT_TRUE(stack.can_redo());

    stack.redo();
    EXPECT_TRUE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
}

TEST_F(CommandTest, PushUndoRedoRoundTripMultipleCommands) {
    PropObject obj;
    obj.value = 0;
    UndoStack stack;
    for (int i = 1; i <= 5; ++i) {
        stack.push(SetValue(&obj, i));
    }
    EXPECT_EQ(obj.value, 5);

    // 逐步 undo 回到 0
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(stack.undo());
    EXPECT_EQ(obj.value, 0);
    EXPECT_FALSE(stack.can_undo());

    // 逐步 redo 回到 5
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(stack.redo());
    EXPECT_EQ(obj.value, 5);
    EXPECT_FALSE(stack.can_redo());
}

TEST_F(CommandTest, PushAfterUndoTruncatesRedoHistory) {
    PropObject obj;
    UndoStack stack;
    stack.push(SetValue(&obj, 1));
    stack.push(SetValue(&obj, 2));
    ASSERT_TRUE(stack.undo());  // 回到 1，redo 可用
    ASSERT_TRUE(stack.can_redo());

    // 新分支入栈，原 redo 分支被截断
    stack.push(SetValue(&obj, 99));
    EXPECT_FALSE(stack.can_redo());
    EXPECT_EQ(stack.size(), 2u);  // [set(1), set(99)]
    EXPECT_EQ(obj.value, 99);
}

TEST_F(CommandTest, ClearResetsStack) {
    PropObject obj;
    UndoStack stack;
    stack.push(SetValue(&obj, 1));
    stack.push(SetValue(&obj, 2));
    stack.clear();
    EXPECT_EQ(stack.size(), 0u);
    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
    EXPECT_TRUE(stack.is_clean());  // 空栈视为干净
}

TEST_F(CommandTest, UndoRedoRefusedInsideMacro) {
    PropObject obj;
    UndoStack stack;
    stack.push(SetValue(&obj, 1));
    stack.begin_macro("m");
    EXPECT_FALSE(stack.undo());  // macro 中禁止半途 undo
    EXPECT_FALSE(stack.redo());
    stack.end_macro();
}

// ============================================================
// 内存上限：超限淘汰最旧
// ============================================================

TEST_F(CommandTest, MemoryLimitDropsOldestCommands) {
    PropObject obj;
    obj.value = 0;
    UndoStack stack;
    stack.set_memory_limit(3);
    for (int i = 1; i <= 5; ++i) {
        stack.push(SetValue(&obj, i));
    }
    // 只保留最后 3 条
    EXPECT_EQ(stack.size(), 3u);

    // 只能 undo 3 步
    ASSERT_TRUE(stack.undo());
    ASSERT_TRUE(stack.undo());
    ASSERT_TRUE(stack.undo());
    EXPECT_FALSE(stack.can_undo());
    // 最早可回退到的状态：value == 2（cmd1、cmd2 已永久落地、不可撤销）
    EXPECT_EQ(obj.value, 2);
}

TEST_F(CommandTest, MemoryLimitZeroMeansUnlimited) {
    PropObject obj;
    UndoStack stack;
    stack.set_memory_limit(0);
    for (int i = 1; i <= 200; ++i) {
        stack.push(SetValue(&obj, i));
    }
    EXPECT_EQ(stack.size(), 200u);
}

TEST_F(CommandTest, MemoryLimitShrinkAfterPush) {
    PropObject obj;
    UndoStack stack;
    for (int i = 1; i <= 10; ++i) stack.push(SetValue(&obj, i));
    EXPECT_EQ(stack.size(), 10u);
    stack.set_memory_limit(4);  // 立即淘汰到 4
    EXPECT_EQ(stack.size(), 4u);
}

// ============================================================
// Macro：事务化合并
// ============================================================

TEST_F(CommandTest, MacroGroupsMultiplePushesIntoOneUndo) {
    PropObject obj;
    obj.value = 0;
    UndoStack stack;
    stack.begin_macro("batch");
    stack.push(SetValue(&obj, 1));
    stack.push(SetValue(&obj, 2));
    stack.push(SetValue(&obj, 3));
    stack.end_macro();

    EXPECT_EQ(obj.value, 3);
    EXPECT_EQ(stack.size(), 1u);  // 单个 macro 在主栈

    // 一次 undo 回退全部
    ASSERT_TRUE(stack.undo());
    EXPECT_EQ(obj.value, 0);
    EXPECT_EQ(stack.size(), 1u);

    // 一次 redo 重放全部
    ASSERT_TRUE(stack.redo());
    EXPECT_EQ(obj.value, 3);
}

TEST_F(CommandTest, NestedMacro) {
    PropObject obj;
    obj.value = 0;
    UndoStack stack;
    stack.begin_macro("outer");
    stack.push(SetValue(&obj, 1));
    stack.begin_macro("inner");
    stack.push(SetValue(&obj, 2));
    stack.push(SetValue(&obj, 3));
    stack.end_macro();  // inner 关闭，作为子命令并入 outer
    stack.push(SetValue(&obj, 4));
    stack.end_macro();  // outer 关闭，压入主栈

    EXPECT_EQ(obj.value, 4);
    EXPECT_EQ(stack.size(), 1u);
    ASSERT_TRUE(stack.undo());
    EXPECT_EQ(obj.value, 0);
    ASSERT_TRUE(stack.redo());
    EXPECT_EQ(obj.value, 4);
}

TEST_F(CommandTest, EmptyMacroNotPushed) {
    UndoStack stack;
    stack.begin_macro("empty");
    stack.end_macro();
    EXPECT_EQ(stack.size(), 0u);
}

TEST_F(CommandTest, MacroChildrenAppearInCommandTree) {
    PropObject obj;
    UndoStack stack;
    stack.begin_macro("m");
    stack.push(SetValue(&obj, 1));
    stack.push(SetValue(&obj, 2));
    stack.end_macro();
    const Command* top = stack.at(0);
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->get_class(), std::string("MacroCommand"));
    const auto* macro = static_cast<const MacroCommand*>(top);
    EXPECT_EQ(macro->child_count(), 2u);
}

// ============================================================
// Merge：连续同类命令合并（连续拖动场景）
// ============================================================

TEST_F(CommandTest, MergeDisabledByDefault) {
    PropObject obj;
    obj.value = 0;
    UndoStack stack;
    stack.push(SetValue(&obj, 1));
    stack.push(SetValue(&obj, 2));
    EXPECT_EQ(stack.size(), 2u);  // 默认不合并
}

TEST_F(CommandTest, MergeConsecutiveSameKeyWhenEnabled) {
    PropObject obj;
    obj.value = 0;
    UndoStack stack;
    stack.set_merge_enabled(true);
    stack.push(SetValue(&obj, 1));
    stack.push(SetValue(&obj, 2));
    stack.push(SetValue(&obj, 3));
    // 三次连续同 target+key 合并为一条
    EXPECT_EQ(stack.size(), 1u);
    EXPECT_EQ(obj.value, 3);

    // 一次 undo 回到最初（旧值保留最早）
    ASSERT_TRUE(stack.undo());
    EXPECT_EQ(obj.value, 0);
    ASSERT_TRUE(stack.redo());
    EXPECT_EQ(obj.value, 3);
}

TEST_F(CommandTest, MergeStopsOnDifferentKey) {
    PropObject obj;
    obj.value = 0;
    obj.label = "a";
    UndoStack stack;
    stack.set_merge_enabled(true);
    stack.push(SetValue(&obj, 1));
    stack.push(SetValue(&obj, 2));
    stack.push(std::make_unique<SetPropertyCommand>(
        &obj, "label", Variant(std::string("b"))));
    // value 合并为一条 + label 一条 = 2 条
    EXPECT_EQ(stack.size(), 2u);
    EXPECT_EQ(obj.value, 2);
    EXPECT_EQ(obj.label, "b");
}

TEST_F(CommandTest, MergeDoesNotCrossUndoBoundary) {
    PropObject obj;
    obj.value = 0;
    UndoStack stack;
    stack.set_merge_enabled(true);
    stack.push(SetValue(&obj, 1));
    stack.push(SetValue(&obj, 2));
    ASSERT_TRUE(stack.undo());  // 回到 0，index_ != size
    stack.push(SetValue(&obj, 9));  // 新分支，不应与已 undo 的栈顶合并
    EXPECT_EQ(stack.size(), 1u);  // 旧分支被截断，仅留新命令
    EXPECT_EQ(obj.value, 9);
}

// ============================================================
// Clean state（保存点跟踪）
// ============================================================

TEST_F(CommandTest, CleanStateTracking) {
    PropObject obj;
    obj.value = 0;
    UndoStack stack;
    EXPECT_TRUE(stack.is_clean());  // 初始空栈视为干净

    stack.push(SetValue(&obj, 1));
    EXPECT_FALSE(stack.is_clean());

    stack.set_clean();  // 标记保存点
    EXPECT_TRUE(stack.is_clean());

    stack.push(SetValue(&obj, 2));
    EXPECT_FALSE(stack.is_clean());

    ASSERT_TRUE(stack.undo());  // 回到保存点
    EXPECT_TRUE(stack.is_clean());

    ASSERT_TRUE(stack.redo());
    EXPECT_FALSE(stack.is_clean());
}

TEST_F(CommandTest, CleanIndexLostWhenTruncatedByBranch) {
    PropObject obj;
    obj.value = 0;
    UndoStack stack;
    stack.push(SetValue(&obj, 1));
    stack.push(SetValue(&obj, 2));
    stack.undo();  // index=1
    stack.set_clean();  // clean 在 index=1
    stack.undo();  // index=0
    stack.push(SetValue(&obj, 99));  // 截断 [1, end)，clean_index=1 落在截断区
    EXPECT_FALSE(stack.is_clean());  // clean 丢失
}

// ============================================================
// changed 信号
// ============================================================

TEST_F(CommandTest, ChangedSignalEmittedOnPushUndoRedo) {
    PropObject obj;
    UndoStack stack;
    int count = 0;
    stack.changed().connect([&count] { ++count; });
    stack.push(SetValue(&obj, 1));
    stack.undo();
    stack.redo();
    EXPECT_EQ(count, 3);
}

// ============================================================
// SelectionSet<T>
// ============================================================

TEST(SelectionSetTest, AddRemoveContainsSize) {
    SelectionSet<int> sel;
    EXPECT_TRUE(sel.empty());
    sel.add(1);
    sel.add(2);
    EXPECT_EQ(sel.size(), 2u);
    EXPECT_TRUE(sel.contains(1));
    EXPECT_FALSE(sel.contains(99));
    sel.remove(1);
    EXPECT_FALSE(sel.contains(1));
    EXPECT_EQ(sel.size(), 1u);
}

TEST(SelectionSetTest, ToggleFlipsPresence) {
    SelectionSet<int> sel;
    sel.toggle(1);
    EXPECT_TRUE(sel.contains(1));
    sel.toggle(1);
    EXPECT_FALSE(sel.contains(1));
}

TEST(SelectionSetTest, ClearEmptiesSet) {
    SelectionSet<int> sel;
    sel.add(1);
    sel.add(2);
    sel.clear();
    EXPECT_TRUE(sel.empty());
}

TEST(SelectionSetTest, SelectModesReplaceAddRemoveToggle) {
    SelectionSet<int> sel;
    sel.select({1, 2}, SelectMode::REPLACE);
    EXPECT_EQ(sel.size(), 2u);

    sel.select({2, 3}, SelectMode::REPLACE);
    EXPECT_EQ(sel.size(), 2u);
    EXPECT_FALSE(sel.contains(1));
    EXPECT_TRUE(sel.contains(3));

    sel.select({4, 5}, SelectMode::ADD);
    EXPECT_EQ(sel.size(), 4u);

    sel.select({2, 4}, SelectMode::REMOVE);
    EXPECT_FALSE(sel.contains(2));
    EXPECT_FALSE(sel.contains(4));

    sel.select({3, 5, 9}, SelectMode::TOGGLE);
    EXPECT_FALSE(sel.contains(3));
    EXPECT_FALSE(sel.contains(5));
    EXPECT_TRUE(sel.contains(9));
}

TEST(SelectionSetTest, BoxSelectWithCandidates) {
    // 模拟框选：调用方先用 R-tree 范围查询得到候选 id，SelectionSet 不感知 Box 类型。
    SelectionSet<int> sel;
    std::vector<int> candidates = {10, 20, 30};
    sel.box_select(candidates, SelectMode::REPLACE);
    EXPECT_EQ(sel.size(), 3u);
    EXPECT_TRUE(sel.contains(10));
    EXPECT_TRUE(sel.contains(30));
}

TEST(SelectionSetTest, BoxSelectTemplatedProvider) {
    // 模板重载：Box 类型由调用方决定，provider 把 Box 映射为候选 vector。
    SelectionSet<int> sel;
    struct Box { int lo, hi; };
    auto provider = [](const Box& b) {
        std::vector<int> out;
        for (int i = b.lo; i <= b.hi; ++i) out.push_back(i);
        return out;
    };
    sel.box_select(Box{1, 5}, provider, SelectMode::REPLACE);
    EXPECT_EQ(sel.size(), 5u);
}

TEST(SelectionSetTest, SelectAllAndInvert) {
    SelectionSet<int> sel;
    sel.add(1);
    sel.add(3);
    std::vector<int> universe = {1, 2, 3, 4, 5};

    sel.select_all(universe);
    EXPECT_EQ(sel.size(), 5u);

    sel.invert(universe);
    EXPECT_TRUE(sel.empty());  // 全选 → 反选 → 全空
}

TEST(SelectionSetTest, InvertPartialSelection) {
    SelectionSet<int> sel;
    sel.add(2);
    sel.add(4);
    std::vector<int> universe = {1, 2, 3, 4, 5};
    sel.invert(universe);
    EXPECT_EQ(sel.size(), 3u);  // {1, 3, 5}
    EXPECT_TRUE(sel.contains(1));
    EXPECT_TRUE(sel.contains(3));
    EXPECT_TRUE(sel.contains(5));
    EXPECT_FALSE(sel.contains(2));
}

TEST(SelectionSetTest, FilterKeepsMatching) {
    SelectionSet<int> sel;
    for (int i = 1; i <= 10; ++i) sel.add(i);
    sel.filter([](int v) { return v % 2 == 0; });
    EXPECT_EQ(sel.size(), 5u);
    EXPECT_TRUE(sel.contains(2));
    EXPECT_FALSE(sel.contains(1));
}

TEST(SelectionSetTest, FilterByAttribute) {
    SelectionSet<int> sel;
    sel.add(1);
    sel.add(2);
    sel.add(3);
    sel.add(4);
    // 属性映射：偶数 id → layer 2；奇数 id → layer 1
    auto layer_of = [](int id) { return id % 2 == 0 ? 2 : 1; };
    sel.filter_by(layer_of, 2);  // 仅保留 layer==2 的（偶数 id）
    EXPECT_EQ(sel.size(), 2u);
    EXPECT_TRUE(sel.contains(2));
    EXPECT_TRUE(sel.contains(4));
    EXPECT_FALSE(sel.contains(1));
}

TEST(SelectionSetTest, OnChangedSignalOnlyOnActualChange) {
    SelectionSet<int> sel;
    int count = 0;
    sel.on_changed().connect([&count] { ++count; });
    sel.add(1);   // 变化 → 触发
    sel.add(1);   // 已存在 → 不触发
    sel.add(2);   // 变化 → 触发
    sel.remove(99);  // 不存在 → 不触发
    EXPECT_EQ(count, 2);
}

// ============================================================
// SelectionChangeCommand<T>：选择变更命令化
// ============================================================

TEST(SelectionSetTest, SelectionChangeCommandRoundTrip) {
    SelectionSet<int> sel;
    sel.add(1);
    sel.add(2);

    UndoStack stack;
    stack.push(std::make_unique<SelectionChangeCommand<int>>(
        &sel, std::vector<int>{3, 4, 5}));
    EXPECT_EQ(sel.size(), 3u);
    EXPECT_TRUE(sel.contains(3));
    EXPECT_FALSE(sel.contains(1));  // 旧选择被替换

    ASSERT_TRUE(stack.undo());
    EXPECT_EQ(sel.size(), 2u);
    EXPECT_TRUE(sel.contains(1));
    EXPECT_TRUE(sel.contains(2));
    EXPECT_FALSE(sel.contains(3));

    ASSERT_TRUE(stack.redo());
    EXPECT_EQ(sel.size(), 3u);
    EXPECT_TRUE(sel.contains(5));
}

TEST(SelectionSetTest, SelectionChangeCommandInMacro) {
    SelectionSet<int> sel;
    sel.add(1);

    UndoStack stack;
    stack.begin_macro("batch select");
    stack.push(std::make_unique<SelectionChangeCommand<int>>(
        &sel, std::vector<int>{2, 3}));
    stack.push(std::make_unique<SelectionChangeCommand<int>>(
        &sel, std::vector<int>{4, 5}));
    stack.end_macro();

    EXPECT_EQ(sel.size(), 2u);  // 最终 {4, 5}
    EXPECT_TRUE(sel.contains(4));

    // 一次 undo 回到 {1}
    ASSERT_TRUE(stack.undo());
    EXPECT_EQ(sel.size(), 1u);
    EXPECT_TRUE(sel.contains(1));
}
