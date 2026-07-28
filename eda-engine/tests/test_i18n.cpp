// i18n 翻译系统单元测试（P4 Task 8 / 04-08-01）。
//
// 覆盖：
//   1. tr() 查表命中（当前 locale 表有条目 → 返回译法）。
//   2. tr() 缺失退回英文 source（无表 / 无条目 / 译法空 → 返回 source，
//      **绝不返回 key、绝不返回空**）。
//   3. locale 切换后 tr() 输出变化（免重启切换核心机制）。
//   4. .po 加载：单数 / 复数 / 上下文（msgctxt）/ 模糊标记（fuzzy）/ 译者注释（#.）/
//      跨行字符串 / 头条目跳过。
//   5. tr_n() CLDR 简化复数分发（英文 1/other、中文无复数、无表退回源串）。
//   6. tr_ctxt() msgctxt 上下文消歧（同 source 不同 context 译法独立）。
//   7. 固有名词直通（IPC/Gerber/courtyard 等不进 tr 池）。
//   8. locale_changed 信号广播（set_locale 触发，控件据此 queue_redraw 免重启）。
//   9. PoExtractor 生成 .pot 模板（含 msgctxt / msgid_plural / 引用 / 注释）。
//
// 注意：TranslationServer 是进程级单例，状态跨测试残留——所有触及单例的测试使用
// I18nFixture 在 SetUp/TearDown 清理（已加载表 + 信号连接），保证测试独立性。

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "scene/gui/i18n/po_extractor.h"
#include "scene/gui/i18n/po_loader.h"
#include "scene/gui/i18n/translation.h"

using namespace eda;

// ---------------------------------------------------------------------------
// Fixture：每个测试前后清空单例状态。
// ---------------------------------------------------------------------------

class I18nFixture : public ::testing::Test {
protected:
    void SetUp() override {
        auto& srv = TranslationServer::get();
        // 清理上一个测试可能连接的 lambda（避免悬空捕获）
        srv.locale_changed().disconnect_all();
        // 卸载所有已知 locale 的表（防止跨测试污染）
        for (const char* loc : {"en", "zh-CN", "zh", "ja", "fr", "de", "ru"}) {
            srv.unload_translation(loc);
        }
        srv.set_locale("en");  // 默认 locale；触发信号但无监听者，no-op
    }

    void TearDown() override {
        auto& srv = TranslationServer::get();
        srv.locale_changed().disconnect_all();
        for (const char* loc : {"en", "zh-CN", "zh", "ja", "fr", "de", "ru"}) {
            srv.unload_translation(loc);
        }
    }

    // 便捷：构建一个简并 TranslationTable（无上下文，单数条目）。
    static TranslationTable make_simple_table(
        const std::vector<std::pair<std::string, std::string>>& kv) {
        TranslationTable t;
        for (const auto& [src, tr] : kv) {
            TranslationEntry e;
            e.source = src;
            e.translation = tr;
            t.add(std::move(e));
        }
        return t;
    }
};

// ===========================================================================
// 1. tr() 查表命中
// ===========================================================================

TEST_F(I18nFixture, TrHitReturnsTranslation) {
    TranslationServer::get().set_locale("zh-CN");
    TranslationServer::get().load_translation("zh-CN",
        make_simple_table({{"footprint", "封装"},
                           {"schematic", "原理图"}}));

    EXPECT_EQ(tr("footprint"), "封装");
    EXPECT_EQ(tr("schematic"), "原理图");
}

// ===========================================================================
// 2. tr() 缺失退回 source（绝不返回 key、绝不返回空）
// ===========================================================================

TEST_F(I18nFixture, TrMissReturnsSourceNotKey) {
    // 无表：直接退回 source（"key" 概念不存在——source 即 key）
    TranslationServer::get().set_locale("en");
    EXPECT_EQ(tr("footprint"), "footprint");
    EXPECT_NE(tr("footprint"), "");
}

TEST_F(I18nFixture, TrMissWithTableReturnsSource) {
    // 有表但条目缺失：退回 source
    TranslationServer::get().set_locale("zh-CN");
    TranslationServer::get().load_translation("zh-CN",
        make_simple_table({{"schematic", "原理图"}}));
    EXPECT_EQ(tr("schematic"), "原理图");  // 命中
    EXPECT_EQ(tr("footprint"), "footprint");  // 缺失：退回 source
}

TEST_F(I18nFixture, TrNullSourceReturnsEmpty) {
    // nullptr 安全：返回空串，不崩溃
    EXPECT_EQ(tr(static_cast<const char*>(nullptr)), "");
}

// ===========================================================================
// 3. locale 切换后 tr() 输出变化（免重启切换）
// ===========================================================================

TEST_F(I18nFixture, LocaleSwitchChangesTrOutput) {
    TranslationServer::get().load_translation("zh-CN",
        make_simple_table({{"footprint", "封装"}}));
    // 英文有特殊形式（验证不同 locale 表互不干扰）
    TranslationServer::get().load_translation("en",
        make_simple_table({{"footprint", "Footprint (en)"}}));

    TranslationServer::get().set_locale("zh-CN");
    EXPECT_EQ(tr("footprint"), "封装");

    TranslationServer::get().set_locale("en");
    EXPECT_EQ(tr("footprint"), "Footprint (en)");

    // 切回 zh-CN：免重启再次取到中文
    TranslationServer::get().set_locale("zh-CN");
    EXPECT_EQ(tr("footprint"), "封装");
}

// ===========================================================================
// 4. .po 加载（单数 / 复数 / 上下文 / 模糊 / 注释 / 跨行 / 头跳过）
// ===========================================================================

TEST(PoLoaderTest, ParsesSingularEntries) {
    const std::string po =
        "msgid \"footprint\"\n"
        "msgstr \"封装\"\n"
        "\n"
        "msgid \"schematic\"\n"
        "msgstr \"原理图\"\n";
    const TranslationTable t = PoLoader::parse(po);
    EXPECT_EQ(t.size(), 2u);

    const auto* e = t.find("", "footprint");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->translation, "封装");
    EXPECT_FALSE(e->fuzzy);
}

TEST(PoLoaderTest, ParsesPluralEntries) {
    const std::string po =
        "msgid \"pad\"\n"
        "msgid_plural \"pads\"\n"
        "msgstr[0] \"焊盘\"\n"
        "msgstr[1] \"焊盘（复）\"\n";
    const TranslationTable t = PoLoader::parse(po);
    ASSERT_EQ(t.size(), 1u);

    const auto* e = t.find("", "pad");
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(e->plurals.size(), 2u);
    EXPECT_EQ(e->plurals[0], "焊盘");
    EXPECT_EQ(e->plurals[1], "焊盘（复）");
    // 单数译法在复数条目中留空
    EXPECT_TRUE(e->translation.empty());
}

TEST(PoLoaderTest, ParsesContextDisambiguation) {
    const std::string po =
        "msgid \"pad\"\n"
        "msgstr \"焊盘\"\n"
        "\n"
        "msgctxt \"report\"\n"
        "msgid \"pad\"\n"
        "msgstr \"Pad\"\n";
    const TranslationTable t = PoLoader::parse(po);
    EXPECT_EQ(t.size(), 2u);

    const auto* def = t.find("", "pad");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->translation, "焊盘");

    const auto* rep = t.find("report", "pad");
    ASSERT_NE(rep, nullptr);
    EXPECT_EQ(rep->translation, "Pad");
    EXPECT_EQ(rep->context, "report");
}

TEST(PoLoaderTest, ParsesFuzzyFlagAndComments) {
    const std::string po =
        "#. Appears in DRC report referring to physical pad\n"
        "#, fuzzy\n"
        "msgctxt \"report\"\n"
        "msgid \"pad\"\n"
        "msgstr \"焊盘（待复审）\"\n";
    const TranslationTable t = PoLoader::parse(po);
    ASSERT_EQ(t.size(), 1u);

    const auto* e = t.find("report", "pad");
    ASSERT_NE(e, nullptr);
    EXPECT_TRUE(e->fuzzy);
    ASSERT_EQ(e->comments.size(), 1u);
    EXPECT_EQ(e->comments[0], "Appears in DRC report referring to physical pad");
    // 译法仍被加载（fuzzy 是 metadata，运行时不阻断）
    EXPECT_EQ(e->translation, "焊盘（待复审）");
}

TEST(PoLoaderTest, SkipsHeaderAndHandlesMultiline) {
    const std::string po =
        "msgid \"\"\n"
        "msgstr \"\"\n"
        "\"Project-Id-Version: test\\n\"\n"
        "\"Content-Type: text/plain; charset=UTF-8\\n\"\n"
        "\n"
        "msgid \"schematic\"\n"
        "msgstr \"\"\n"
        "\"原理\"\n"
        "\"图\"\n";
    const TranslationTable t = PoLoader::parse(po);
    // 头条目（msgid ""）被跳过，仅 1 条业务条目
    EXPECT_EQ(t.size(), 1u);

    const auto* e = t.find("", "schematic");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->translation, "原理图");  // 跨行拼接
}

TEST(PoLoaderTest, HandlesEscapeSequences) {
    const std::string po =
        "msgid \"multi\"\n"
        "msgstr \"line1\\nline2\\ttabbed\\\"q\\\\\\n\"\n";
    const TranslationTable t = PoLoader::parse(po);
    const auto* e = t.find("", "multi");
    ASSERT_NE(e, nullptr);
    // 解码：\n->LF / \t->TAB / \"->" / \\->\ / 末尾 \n->LF
    EXPECT_EQ(e->translation, "line1\nline2\ttabbed\"q\\\n");
}

TEST(PoLoaderTest, IgnoresReferencesAndTranslatorComments) {
    const std::string po =
        "# Translator's note\n"
        "#: ui/button.cpp:42\n"
        "#| msgid \"old\"\n"
        "msgid \"button\"\n"
        "msgstr \"按钮\"\n";
    const TranslationTable t = PoLoader::parse(po);
    ASSERT_EQ(t.size(), 1u);

    const auto* e = t.find("", "button");
    ASSERT_NE(e, nullptr);
    // # translator's note / #: reference / #| previous 都不进 developer comments
    EXPECT_EQ(e->comments.size(), 0u);
    EXPECT_EQ(e->translation, "按钮");
}

// ===========================================================================
// 5. tr_n() CLDR 简化复数分发
// ===========================================================================

TEST_F(I18nFixture, TrnEnglishPluralDispatch) {
    // 英文：n == 1 取 one（plurals[0]），其余取 other（plurals[1]）
    TranslationServer::get().set_locale("en");
    TranslationEntry e;
    e.source = "pad";
    e.plurals = {"pad (one)", "pads (other)"};
    TranslationTable t;
    t.add(std::move(e));
    TranslationServer::get().load_translation("en", std::move(t));

    EXPECT_EQ(tr_n("pad", "pads", 1), "pad (one)");
    EXPECT_EQ(tr_n("pad", "pads", 0), "pads (other)");
    EXPECT_EQ(tr_n("pad", "pads", 2), "pads (other)");
    EXPECT_EQ(tr_n("pad", "pads", 100), "pads (other)");
}

TEST_F(I18nFixture, TrnChineseNoPlural) {
    // 中文族：恒首形式（plurals[0]），不论 count
    TranslationServer::get().set_locale("zh-CN");
    TranslationEntry e;
    e.source = "pad";
    e.plurals = {"焊盘"};  // 中文仅一种形式
    TranslationTable t;
    t.add(std::move(e));
    TranslationServer::get().load_translation("zh-CN", std::move(t));

    EXPECT_EQ(tr_n("pad", "pads", 1), "焊盘");
    EXPECT_EQ(tr_n("pad", "pads", 0), "焊盘");
    EXPECT_EQ(tr_n("pad", "pads", 100), "焊盘");
}

TEST_F(I18nFixture, TrnNoTableFallsBackToSourceByLocale) {
    // 无表：按 locale 规则在源串 singular/plural 间选
    TranslationServer::get().set_locale("fr");  // 无 fr 表
    EXPECT_EQ(tr_n("pad", "pads", 1), "pad");
    EXPECT_EQ(tr_n("pad", "pads", 2), "pads");

    TranslationServer::get().set_locale("zh-CN");  // 无 zh-CN 表
    EXPECT_EQ(tr_n("pad", "pads", 1), "pad");
    EXPECT_EQ(tr_n("pad", "pads", 5), "pad");  // 中文规则恒首形式
}

TEST_F(I18nFixture, TrnPluralIndexOutOfBoundsFallsBack) {
    // 条目只有 plurals[0] 但 locale 规则请求 idx=1：退回首可用形式
    TranslationServer::get().set_locale("en");
    TranslationEntry e;
    e.source = "via";
    e.plurals = {"via-only"};  // 仅一种形式
    TranslationTable t;
    t.add(std::move(e));
    TranslationServer::get().load_translation("en", std::move(t));

    EXPECT_EQ(tr_n("via", "vias", 1), "via-only");
    EXPECT_EQ(tr_n("via", "vias", 5), "via-only");  // 越界退回首形式
}

// ===========================================================================
// 6. tr_ctxt() msgctxt 上下文消歧
// ===========================================================================

TEST_F(I18nFixture, TrCtxtDisambiguatesByContext) {
    TranslationServer::get().set_locale("zh-CN");
    TranslationTable t;
    TranslationEntry def;
    def.source = "pad";
    def.translation = "焊盘";
    TranslationEntry rep;
    rep.context = "report";
    rep.source = "pad";
    rep.translation = "Pad";
    t.add(std::move(def));
    t.add(std::move(rep));
    TranslationServer::get().load_translation("zh-CN", std::move(t));

    // 默认 context="" 取焊盘
    EXPECT_EQ(tr("pad"), "焊盘");
    // tr_ctxt 指定 report 上下文取列名
    EXPECT_EQ(tr_ctxt("report", "pad"), "Pad");
    // tr_ctxt 用未注册的 context → 退回 source（绝不返回 key）
    EXPECT_EQ(tr_ctxt("nonexistent", "pad"), "pad");
}

TEST_F(I18nFixture, TrCtxtMissReturnsSource) {
    TranslationServer::get().set_locale("zh-CN");
    TranslationServer::get().load_translation("zh-CN",
        make_simple_table({{"footprint", "封装"}}));
    // context 命中但 context 错配：退回 source
    EXPECT_EQ(tr_ctxt("report", "footprint"), "footprint");
}

// ===========================================================================
// 7. 固有名词直通
// ===========================================================================

TEST(InherentNounTest, DetectsKnownNounsCaseInsensitive) {
    EXPECT_TRUE(is_inherent_noun("Gerber"));
    EXPECT_TRUE(is_inherent_noun("gerber"));
    EXPECT_TRUE(is_inherent_noun("GERBER"));
    EXPECT_TRUE(is_inherent_noun("courtyard"));
    EXPECT_TRUE(is_inherent_noun("IPC-2581"));
    EXPECT_TRUE(is_inherent_noun("DRC"));
    EXPECT_TRUE(is_inherent_noun("BOM"));
    EXPECT_TRUE(is_inherent_noun("Via"));
    EXPECT_TRUE(is_inherent_noun("Pad"));
}

TEST(InherentNounTest, RejectsTranslatableTerms) {
    EXPECT_FALSE(is_inherent_noun("footprint"));
    EXPECT_FALSE(is_inherent_noun("schematic"));
    EXPECT_FALSE(is_inherent_noun("pad count"));  // 非清单项
    EXPECT_FALSE(is_inherent_noun(""));
}

TEST_F(I18nFixture, InherentNounPassthroughViaTr) {
    // 即便 zh-CN 表存在，固有名词不进 tr() 池——代码直接字面量
    // 即便误传入 tr()，因表里不会有条目，缺失退回 source 等于直通
    TranslationServer::get().set_locale("zh-CN");
    TranslationServer::get().load_translation("zh-CN",
        make_simple_table({{"footprint", "封装"}}));
    EXPECT_EQ(tr("Gerber"), "Gerber");      // 直通
    EXPECT_EQ(tr("footprint"), "封装");      // 正常翻译
}

// ===========================================================================
// 8. locale_changed 信号广播
// ===========================================================================

TEST_F(I18nFixture, LocaleChangedSignalFiresOnSetLocale) {
    int fired = 0;
    std::string last_seen_locale;
    TranslationServer::get().locale_changed().connect(
        [&fired, &last_seen_locale]() {
            ++fired;
            last_seen_locale = TranslationServer::get().get_locale();
        });

    TranslationServer::get().set_locale("zh-CN");
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(last_seen_locale, "zh-CN");

    TranslationServer::get().set_locale("en");
    EXPECT_EQ(fired, 2);
    EXPECT_EQ(last_seen_locale, "en");
}

TEST_F(I18nFixture, LocaleChangedSignalMultipleListeners) {
    int a = 0, b = 0;
    TranslationServer::get().locale_changed().connect([&a]() { ++a; });
    TranslationServer::get().locale_changed().connect([&b]() { ++b; });

    TranslationServer::get().set_locale("ja");
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

// ===========================================================================
// 9. PoExtractor 生成 .pot 模板
// ===========================================================================

TEST(PoExtractorTest, GenerateSimpleProducesValidPot) {
    const std::string pot = PoExtractor::generate_simple({"footprint", "schematic"});
    // 头部 metadata
    EXPECT_NE(pot.find("msgid \"\""), std::string::npos);
    EXPECT_NE(pot.find("Content-Type: text/plain; charset=UTF-8"), std::string::npos);
    // 业务条目
    EXPECT_NE(pot.find("msgid \"footprint\""), std::string::npos);
    EXPECT_NE(pot.find("msgid \"schematic\""), std::string::npos);
    EXPECT_NE(pot.find("msgstr \"\""), std::string::npos);

    // Round-trip：解析生成的 .pot 应得到 2 条业务条目（header 跳过）
    const TranslationTable t = PoLoader::parse(pot);
    EXPECT_EQ(t.size(), 2u);
    EXPECT_NE(t.find("", "footprint"), nullptr);
    EXPECT_NE(t.find("", "schematic"), nullptr);
}

TEST(PoExtractorTest, GenerateWithContextPluralAndReferences) {
    std::vector<PoExtractor::SourceEntry> entries;
    PoExtractor::SourceEntry e1;
    e1.source = "pad";
    e1.context = "report";
    e1.comment = "DRC report column header";
    e1.reference = "ui/report.cpp:42";
    entries.push_back(e1);

    PoExtractor::SourceEntry e2;
    e2.source = "pad";
    e2.plural = "pads";
    e2.reference = "ui/library.cpp:10";
    entries.push_back(e2);

    const std::string pot = PoExtractor::generate(entries);

    // msgctxt 输出
    EXPECT_NE(pot.find("msgctxt \"report\""), std::string::npos);
    // msgid_plural + msgstr[0..1]
    EXPECT_NE(pot.find("msgid_plural \"pads\""), std::string::npos);
    EXPECT_NE(pot.find("msgstr[0] \"\""), std::string::npos);
    EXPECT_NE(pot.find("msgstr[1] \"\""), std::string::npos);
    // #. developer comment
    EXPECT_NE(pot.find("#. DRC report column header"), std::string::npos);
    // #: reference
    EXPECT_NE(pot.find("#: ui/report.cpp:42"), std::string::npos);
    EXPECT_NE(pot.find("#: ui/library.cpp:10"), std::string::npos);
}

TEST(PoExtractorTest, MergesDuplicateSources) {
    // 同 source 多处引用：合并到一条 msgid，多条 #: 引用行
    std::vector<PoExtractor::SourceEntry> entries;
    PoExtractor::SourceEntry a;
    a.source = "pad";
    a.reference = "ui/a.cpp:1";
    entries.push_back(a);
    PoExtractor::SourceEntry b;
    b.source = "pad";
    b.reference = "ui/b.cpp:2";
    entries.push_back(b);
    PoExtractor::SourceEntry c;
    c.source = "pad";
    c.reference = "ui/a.cpp:1";  // 重复引用——应去重
    entries.push_back(c);

    const std::string pot = PoExtractor::generate(entries);

    // 仅一条 msgid "pad"（header 的 msgid "" 不算）
    const std::size_t first = pot.find("msgid \"pad\"");
    EXPECT_NE(first, std::string::npos);
    EXPECT_EQ(pot.find("msgid \"pad\"", first + 1), std::string::npos);

    // 两条引用（去重后）
    EXPECT_NE(pot.find("#: ui/a.cpp:1"), std::string::npos);
    EXPECT_NE(pot.find("#: ui/b.cpp:2"), std::string::npos);
}

// ===========================================================================
// 10. 纯函数：plural_form_index / locale_language / TranslationTable.add 覆盖
// ===========================================================================

TEST(PluralRuleTest, ChineseAlwaysZero) {
    EXPECT_EQ(plural_form_index("zh-CN", 0), 0u);
    EXPECT_EQ(plural_form_index("zh-CN", 1), 0u);
    EXPECT_EQ(plural_form_index("zh-CN", 100), 0u);
    EXPECT_EQ(plural_form_index("zh", 5), 0u);
    EXPECT_EQ(plural_form_index("ja", 5), 0u);
    EXPECT_EQ(plural_form_index("ko", 5), 0u);
    EXPECT_EQ(plural_form_index("vi", 5), 0u);
}

TEST(PluralRuleTest, EnglishOneOther) {
    EXPECT_EQ(plural_form_index("en", 1), 0u);
    EXPECT_EQ(plural_form_index("en", 0), 1u);
    EXPECT_EQ(plural_form_index("en", 2), 1u);
    EXPECT_EQ(plural_form_index("en", 100), 1u);
    // 占位：俄/法/德等暂按英文规则
    EXPECT_EQ(plural_form_index("ru", 1), 0u);
    EXPECT_EQ(plural_form_index("ru", 5), 1u);
    EXPECT_EQ(plural_form_index("fr", 0), 1u);
}

TEST(LocaleLanguageTest, ExtractsPrimaryTag) {
    EXPECT_EQ(locale_language("zh-CN"), "zh");
    EXPECT_EQ(locale_language("en_US"), "en");
    EXPECT_EQ(locale_language("en"), "en");
    EXPECT_EQ(locale_language("ja-JP"), "ja");
    EXPECT_EQ(locale_language("ZH-cn"), "zh");  // 大小写归一
}

TEST(TranslationTableTest, AddOverwritesSameKey) {
    TranslationTable t;
    TranslationEntry a;
    a.source = "pad";
    a.translation = "焊盘v1";
    t.add(std::move(a));
    TranslationEntry b;
    b.source = "pad";
    b.translation = "焊盘v2";
    t.add(std::move(b));

    EXPECT_EQ(t.size(), 1u);
    const auto* e = t.find("", "pad");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->translation, "焊盘v2");  // 覆盖后取新值
}

TEST(TranslationTableTest, FindMissReturnsNull) {
    TranslationTable t;
    EXPECT_EQ(t.find("", "anything"), nullptr);
    EXPECT_EQ(t.find("ctx", "anything"), nullptr);
}

TEST(TranslationTableTest, ContextSourceKeyCollisionAvoided) {
    // (ctx="a", src="b") 与 (ctx="ab", src="") 不会冲突
    TranslationTable t;
    TranslationEntry e1;
    e1.context = "a";
    e1.source = "b";
    e1.translation = "T1";
    t.add(std::move(e1));
    TranslationEntry e2;
    e2.context = "ab";
    e2.source = "";
    e2.translation = "T2";
    t.add(std::move(e2));

    EXPECT_EQ(t.size(), 2u);
    EXPECT_NE(t.find("a", "b"), nullptr);
    EXPECT_NE(t.find("ab", ""), nullptr);
}
