// CommandPalette / CommandRegistry / fuzzy_match 单元测试（P4 Task 6）。
//
// 覆盖（约 24 个用例）：
//   1. CommandRegistry（6）：register / 覆盖 / unregister / all / 内置占位 / 不覆盖业务。
//   2. fuzzy_match（8）：空查询 / 子序列命中 / 子序列失败 / 大小写不敏感 /
//      精确 > 前缀 / 连续 > 分散 / 首字符 > 内部字符。
//   3. CommandPalette set_query（4）："open" 精确高分排首 / "os" 命中 Close 而非
//      Open（子序列区分）/ 结果按 score 降序 / max_results 截断。
//   4. 键盘导航（3）：Down 推进 + clamp / Up 回退 + clamp / 新查询重置选中。
//   5. Enter 执行 handler（3）：触发 handler / 占位 handler 空跳过 / 无结果返回 false。
//   6. open / close（2）：open/close 切换可见 / open 重置 query 与选中。
//
// 所有权：所有 CommandRegistry / CommandPalette / Control 对象均为栈对象，无 parent
// 挂树，~Control 安全。CommandPalette 默认绑 CommandRegistry::get() 单例，但每个测试
// 经 set_registry 替换为本地 registry，互不污染单例状态。

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "editor/command_palette/command_palette.h"
#include "editor/command_palette/command_registry.h"

using namespace eda;

namespace {

// 测试用 registry 填充：注册 4 条样本命令（File × 3 + View × 1）。
// 不返回 by value（CommandRegistry 禁拷禁移动），改用 fill-by-reference。
void fill_sample_registry(CommandRegistry& r) {
    Command open;
    open.id = "file.open";   open.name = "Open File";   open.category = "File"; open.handler = [] {};
    Command close;
    close.id = "file.close"; close.name = "Close File"; close.category = "File"; close.handler = [] {};
    Command save;
    save.id  = "file.save";  save.name  = "Save File";  save.category = "File"; save.handler  = [] {};
    Command zoom_in;
    zoom_in.id = "view.zoom_in"; zoom_in.name = "Zoom In"; zoom_in.category = "View"; zoom_in.handler = [] {};
    r.register_command(open);
    r.register_command(close);
    r.register_command(save);
    r.register_command(zoom_in);
}

}  // namespace

// ===========================================================================
// 1. CommandRegistry
// ===========================================================================

TEST(CommandRegistryTest, RegisterAndLookup) {
    CommandRegistry r;
    Command c;
    c.id = "test.alpha";
    c.name = "Alpha";
    c.category = "Test";
    c.handler = [] {};
    r.register_command(c);

    ASSERT_NE(r.get("test.alpha"), nullptr);
    EXPECT_EQ(r.get("test.alpha")->name, "Alpha");
    EXPECT_EQ(r.size(), 1u);
    EXPECT_EQ(r.get("missing"), nullptr);
}

TEST(CommandRegistryTest, RegisterOverwritesById) {
    CommandRegistry r;
    Command a; a.id = "dup"; a.name = "First";  a.handler = [] {};
    Command b; b.id = "dup"; b.name = "Second"; b.handler = [] {};
    r.register_command(a);
    r.register_command(b);  // 同 id → 覆盖

    ASSERT_EQ(r.size(), 1u);
    ASSERT_NE(r.get("dup"), nullptr);
    EXPECT_EQ(r.get("dup")->name, "Second");
}

TEST(CommandRegistryTest, UnregisterById) {
    CommandRegistry r;
    Command c; c.id = "rm"; c.name = "Remove"; c.handler = [] {};
    r.register_command(c);
    ASSERT_EQ(r.size(), 1u);

    EXPECT_TRUE(r.unregister("rm"));
    EXPECT_FALSE(r.unregister("rm"));  // 已删除：再删返回 false
    EXPECT_EQ(r.size(), 0u);
    EXPECT_EQ(r.get("rm"), nullptr);
}

TEST(CommandRegistryTest, AllReturnsRegisteredSet) {
    CommandRegistry r;
    Command a; a.id = "a"; a.name = "A"; a.handler = [] {};
    Command b; b.id = "b"; b.name = "B"; b.handler = [] {};
    r.register_command(a);
    r.register_command(b);

    auto all = r.all();
    ASSERT_EQ(all.size(), 2u);
    // unordered_map 顺序未规定；按排序后的 id 集合验证。
    std::vector<std::string> ids;
    for (const Command* c : all) ids.push_back(c->id);
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids, (std::vector<std::string>{"a", "b"}));
}

TEST(CommandRegistryTest, RegisterBuiltinsAddsPlaceholders) {
    CommandRegistry r;
    r.register_builtins();
    EXPECT_EQ(r.size(), 11u);

    EXPECT_NE(r.get(builtin::FILE_OPEN), nullptr);
    EXPECT_NE(r.get(builtin::EDIT_UNDO), nullptr);
    EXPECT_NE(r.get(builtin::EDIT_REDO), nullptr);
    EXPECT_NE(r.get(builtin::VIEW_ZOOM_IN), nullptr);
    EXPECT_NE(r.get(builtin::VIEW_ZOOM_FIT), nullptr);

    // 占位 handler 为空（业务后续以同 id 覆盖接入真实逻辑）。
    EXPECT_FALSE(r.get(builtin::FILE_OPEN)->handler);
    EXPECT_FALSE(r.get(builtin::EDIT_UNDO)->handler);
}

TEST(CommandRegistryTest, RegisterBuiltinsDoesNotOverrideBusiness) {
    CommandRegistry r;
    Command c;
    c.id = builtin::FILE_OPEN;
    c.name = "My Open";
    c.handler = [] { /* real handler */ };
    r.register_command(c);

    r.register_builtins();  // 已有同 id → 不覆盖

    ASSERT_NE(r.get(builtin::FILE_OPEN), nullptr);
    EXPECT_EQ(r.get(builtin::FILE_OPEN)->name, "My Open");
    EXPECT_TRUE(r.get(builtin::FILE_OPEN)->handler);  // 业务 handler 保留
}

// ===========================================================================
// 2. fuzzy_match
// ===========================================================================

TEST(FuzzyMatchTest, EmptyQueryMatchesAllWithZeroScore) {
    auto m = fuzzy_match("", "anything");
    EXPECT_TRUE(m.matched);
    EXPECT_DOUBLE_EQ(m.score, 0.0);
}

TEST(FuzzyMatchTest, SubsequenceMatchSucceeds) {
    // "os" 在 "close" 中按序出现：o@2, s@3（相邻）。
    auto m = fuzzy_match("os", "close");
    EXPECT_TRUE(m.matched);
    EXPECT_GT(m.score, 0.0);
}

TEST(FuzzyMatchTest, NonSubsequenceFails) {
    // "os" 在 "open" 中无 's'：不是子序列 → matched=false。
    auto m = fuzzy_match("os", "open");
    EXPECT_FALSE(m.matched);
}

TEST(FuzzyMatchTest, CaseInsensitive) {
    auto lower_q = fuzzy_match("os", "close");
    auto upper_q = fuzzy_match("OS", "close");
    auto upper_t = fuzzy_match("os", "Close");
    ASSERT_TRUE(lower_q.matched);
    ASSERT_TRUE(upper_q.matched);
    ASSERT_TRUE(upper_t.matched);
    EXPECT_DOUBLE_EQ(lower_q.score, upper_q.score);
    EXPECT_DOUBLE_EQ(lower_q.score, upper_t.score);
}

TEST(FuzzyMatchTest, ExactMatchScoresHigherThanPrefix) {
    auto exact  = fuzzy_match("open", "open");
    auto prefix = fuzzy_match("open", "open file");
    ASSERT_TRUE(exact.matched);
    ASSERT_TRUE(prefix.matched);
    EXPECT_GT(exact.score, prefix.score);
}

TEST(FuzzyMatchTest, ConsecutiveBonusBeatsSparse) {
    // "abc" 在 "abc" 中连续命中，在 "aXbXc" 中分散命中。
    auto dense   = fuzzy_match("abc", "abc");
    auto sparse  = fuzzy_match("abc", "aXbXc");
    ASSERT_TRUE(dense.matched);
    ASSERT_TRUE(sparse.matched);
    EXPECT_GT(dense.score, sparse.score);
}

TEST(FuzzyMatchTest, FirstLetterBonusBeatsInnerMatch) {
    // "o" 命中 "open" 首字符（+词边界 +前缀），命中 "loop" 第 2 字符（无奖励）。
    auto first = fuzzy_match("o", "open");
    auto inner = fuzzy_match("o", "loop");
    ASSERT_TRUE(first.matched);
    ASSERT_TRUE(inner.matched);
    EXPECT_GT(first.score, inner.score);
}

TEST(FuzzyMatchTest, WordBoundaryBonus) {
    // "f" 命中 "open file" 的词首 'f'（前导空格），高于命中 "find" 的首字符之后场景。
    // 这里比较：词边界命中 vs 非词边界命中（"o" 在 "code" 的位置 1，非词首）。
    auto wb = fuzzy_match("f", "open file");   // f 在词首（前导空格）
    auto no_wb = fuzzy_match("o", "code");      // o 在 pos 1，前导 'c' 非分隔符
    ASSERT_TRUE(wb.matched);
    ASSERT_TRUE(no_wb.matched);
    EXPECT_GT(wb.score, no_wb.score);
}

// ===========================================================================
// 3. CommandPalette set_query
// ===========================================================================

TEST(CommandPaletteQueryTest, QueryOpenRanksOpenFileFirst) {
    CommandRegistry reg;
    fill_sample_registry(reg);
    CommandPalette p;
    p.set_registry(&reg);

    p.set_query("open");  // 精确命中 "Open File"（含前缀 / 词边界 / 连续加分）

    ASSERT_GE(p.get_match_count(), 1u);
    EXPECT_EQ(p.get_matches()[0].command->name, "Open File");
}

TEST(CommandPaletteQueryTest, QueryOsMatchesCloseNotOpen) {
    // 关键回归点：subsequence 区分相似命令。
    // "os" 子序列命中 "Close File"（o@2, s@3），但 "Open File" 无 's' → 排除。
    CommandRegistry reg;
    fill_sample_registry(reg);
    CommandPalette p;
    p.set_registry(&reg);
    p.set_query("os");

    bool found_close = false;
    bool found_open  = false;
    for (const auto& m : p.get_matches()) {
        if (m.command->name == "Close File") found_close = true;
        if (m.command->name == "Open File")  found_open  = true;
    }
    EXPECT_TRUE(found_close);
    EXPECT_FALSE(found_open);
}

TEST(CommandPaletteQueryTest, ResultsSortedByScoreDescending) {
    CommandRegistry reg;
    fill_sample_registry(reg);
    CommandPalette p;
    p.set_registry(&reg);
    p.set_query("file");  // 命中所有 *File 命令

    ASSERT_GE(p.get_match_count(), 3u);
    for (std::size_t i = 1; i < p.get_match_count(); ++i) {
        EXPECT_GE(p.get_matches()[i - 1].score, p.get_matches()[i].score)
            << "结果应在 index " << i - 1 << " 与 " << i << " 间按 score 降序";
    }
}

TEST(CommandPaletteQueryTest, MaxResultsTruncatesList) {
    CommandRegistry reg;
    fill_sample_registry(reg);
    CommandPalette p;
    p.set_registry(&reg);
    p.set_max_results(2);
    p.set_query("file");  // 命中 3 条但截断为 2

    EXPECT_EQ(p.get_match_count(), 2u);
}

TEST(CommandPaletteQueryTest, EmptyQueryMatchesAll) {
    CommandRegistry reg;
    fill_sample_registry(reg);
    CommandPalette p;
    p.set_registry(&reg);
    p.set_query("");  // 空查询：展示全部命令

    EXPECT_EQ(p.get_match_count(), 4u);
}

// ===========================================================================
// 4. 键盘导航（Up / Down）
// ===========================================================================

TEST(CommandPaletteNavTest, DownAdvancesAndClampsAtLast) {
    CommandRegistry reg;
    fill_sample_registry(reg);
    CommandPalette p;
    p.set_registry(&reg);
    p.set_query("file");  // 3 条
    ASSERT_EQ(p.get_match_count(), 3u);
    EXPECT_EQ(p.get_selected_index(), 0);

    EXPECT_EQ(p.move_selection_down(), 1);
    EXPECT_EQ(p.move_selection_down(), 2);
    EXPECT_EQ(p.move_selection_down(), 2);  // 已末项：clamp 不回绕
}

TEST(CommandPaletteNavTest, UpGoesBackAndClampsAtFirst) {
    CommandRegistry reg;
    fill_sample_registry(reg);
    CommandPalette p;
    p.set_registry(&reg);
    p.set_query("file");
    p.move_selection_down();
    p.move_selection_down();
    ASSERT_EQ(p.get_selected_index(), 2);

    EXPECT_EQ(p.move_selection_up(), 1);
    EXPECT_EQ(p.move_selection_up(), 0);
    EXPECT_EQ(p.move_selection_up(), 0);  // 已首项：clamp 不回绕
}

TEST(CommandPaletteNavTest, NewQueryResetsSelectionToFirst) {
    CommandRegistry reg;
    fill_sample_registry(reg);
    CommandPalette p;
    p.set_registry(&reg);
    p.set_query("file");
    p.move_selection_down();
    ASSERT_EQ(p.get_selected_index(), 1);

    p.set_query("open");
    EXPECT_EQ(p.get_selected_index(), 0);
}

TEST(CommandPaletteNavTest, NavigationOnEmptyResultsIsSafe) {
    CommandRegistry reg;
    fill_sample_registry(reg);
    CommandPalette p;
    p.set_registry(&reg);
    p.set_query("zzzznomatch");
    ASSERT_EQ(p.get_match_count(), 0u);

    EXPECT_EQ(p.move_selection_down(), 0);
    EXPECT_EQ(p.move_selection_up(), 0);
    EXPECT_EQ(p.get_selected_command(), nullptr);
}

// ===========================================================================
// 5. Enter 执行 handler
// ===========================================================================

TEST(CommandPaletteInvokeTest, EnterExecutesSelectedHandler) {
    CommandRegistry reg;
    int fired = 0;
    Command c;
    c.id = "test.fire";
    c.name = "Fire";
    c.handler = [&fired] { ++fired; };
    reg.register_command(c);

    CommandPalette p;
    p.set_registry(&reg);
    p.set_query("fire");
    ASSERT_EQ(p.get_match_count(), 1u);

    EXPECT_TRUE(p.invoke_selected());
    EXPECT_EQ(fired, 1);
}

TEST(CommandPaletteInvokeTest, EnterOnPlaceholderSkipsEmptyHandler) {
    // 占位命令 handler 为空：invoke_selected 返回 false，不视为错误。
    CommandRegistry reg;
    reg.register_builtins();
    CommandPalette p;
    p.set_registry(&reg);
    p.set_query("undo");
    ASSERT_EQ(p.get_match_count(), 1u);

    EXPECT_FALSE(p.invoke_selected());
}

TEST(CommandPaletteInvokeTest, EnterWithNoResultsReturnsFalse) {
    CommandRegistry reg;
    fill_sample_registry(reg);
    CommandPalette p;
    p.set_registry(&reg);
    p.set_query("zzzznomatch");
    ASSERT_EQ(p.get_match_count(), 0u);

    EXPECT_FALSE(p.invoke_selected());
}

// ===========================================================================
// 6. open / close（Esc 关闭）
// ===========================================================================

TEST(CommandPaletteOpenTest, OpenThenCloseTogglesVisibility) {
    CommandPalette p;
    EXPECT_FALSE(p.is_open());

    p.open();
    EXPECT_TRUE(p.is_open());

    p.close();  // Esc
    EXPECT_FALSE(p.is_open());
}

TEST(CommandPaletteOpenTest, OpenResetsQueryAndSelection) {
    CommandRegistry reg;
    fill_sample_registry(reg);
    CommandPalette p;
    p.set_registry(&reg);
    p.set_query("file");
    p.move_selection_down();
    ASSERT_EQ(p.get_selected_index(), 1);
    ASSERT_FALSE(p.get_query().empty());

    p.open();
    EXPECT_TRUE(p.get_query().empty());  // 查询清空
    EXPECT_EQ(p.get_selected_index(), 0);  // 选中重置为首项
    EXPECT_TRUE(p.is_open());
}
