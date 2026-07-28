// 翻译系统核心实现（P4 Task 8 / 04-08-01）。
//
// 含：
//   - TranslationTable 查表（add/find/make_key）。
//   - 固有名词清单（IPC-2581/Gerber/courtyard/BOM/DRC/Via/Pad 等）+ 大小写不敏感匹配。
//   - CLDR 简化复数规则（中文族无复数 / 英文族 1-other / 其他占位）。
//   - TranslationServer 单例（Meyers singleton，与 ThemeManager 同构）：
//       * load/unload_translation 维护 locale->table 注册表；
//       * set_locale 广播 locale_changed（控件 queue_redraw 刷新，免重启）；
//       * tr/tr_ctxt/tr_n 查表，缺失退回 source（绝不返回 key）。
//   - 全局便捷函数 eda::tr / eda::tr_ctxt / eda::tr_n 转发到单例。

#include "scene/gui/i18n/translation.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace eda {

// ===========================================================================
// TranslationTable
// ===========================================================================

std::string TranslationTable::make_key(const std::string& context,
                                       const std::string& source) {
    // \x01 作分隔符：source 与 context 一般为可打印文本，不会含 SOH 控制字符，
    // 因此 (ctx="a", src="b") 与 (ctx="", src="a\x01b") 不会冲突。
    return context + "\x01" + source;
}

void TranslationTable::add(TranslationEntry entry) {
    const std::string key = make_key(entry.context, entry.source);
    auto it = index_.find(key);
    if (it == index_.end()) {
        index_[key] = entries_.size();
        entries_.push_back(std::move(entry));
    } else {
        // 同 key 已存在：原位覆盖（不前移，保留 .po 中的原始顺序）。
        entries_[it->second] = std::move(entry);
    }
}

const TranslationEntry* TranslationTable::find(const std::string& context,
                                                const std::string& source) const {
    auto it = index_.find(make_key(context, source));
    if (it == index_.end()) return nullptr;
    return &entries_[it->second];
}

// ===========================================================================
// 固有名词清单
// ===========================================================================
//
// 来自 [[00-总览/04-术语表]] §三"固有名词"清单——EDA 行业专有术语 / 标准代号，
// **所有 locale 都不翻译**。代码直接字面量，不应进 tr() 池。
//
// 清单为最小常用集（IPC 标准 / Gerber / courtyard / 通用缩写 / 常见封装族），
// 后续可由术语表 SSOT 自动同步。维护时保持小写归一前的"规范书写"形式（如 "IPC-2581"
// 含连字符）；匹配走 to_lower 容错。

const std::vector<std::string>& inherent_nouns() {
    static const std::vector<std::string> nouns = {
        // IPC 标准
        "IPC-2581", "IPC-2221", "IPC-7351", "IPC-2152",
        // 行业专有术语
        "Gerber", "courtyard", "ODB++",
        // 通用缩写（报告/检查类）
        "BOM", "DRC", "DFF", "DFM", "DFA", "DFT", "ETC",
        // 物理元素（部分作为固有名词保留原词）
        "Via", "Pad", "Pin", "Net",
        // 焊盘工艺
        "SMD", "NSMD",
        // 常见封装族代号
        "QFP", "BGA", "QFN", "SOP", "SOIC", "TSOP", "TSSOP", "PLCC",
        // 其他
        "NC",  // No Connect
    };
    return nouns;
}

namespace {

std::string to_lower_ascii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

}  // namespace

bool is_inherent_noun(const std::string& source) {
    if (source.empty()) return false;
    const std::string needle = to_lower_ascii(source);
    const auto& nouns = inherent_nouns();
    for (const auto& n : nouns) {
        if (to_lower_ascii(n) == needle) return true;
    }
    return false;
}

// ===========================================================================
// CLDR 复数规则（简化版）
// ===========================================================================
//
// 完整 CLDR Plural Rules（one/two/few/many/other + zero）按语言分支几十条规则；
// 正式版应接 ICU MessageFormat + PluralRules。本实现按 [[04-08-01]] §"复数形式"要求
// 简化为：
//   1) 中文族 + 东南亚（无复数形式）：恒返回 0
//   2) 默认（英文族及占位）：n == 1 ? 0 : 1
//
// 占位说明：俄/波兰/阿拉伯等多类复数语言按"1/other"占位，可能不完全贴合 gettext
// .po 中 msgstr[N] 的下标约定；正式版接 ICU 后这些 locale 改用完整 CLDR 规则。

std::string locale_language(const std::string& locale) {
    // 取 "-" 或 "_" 之前部分（容错两种分隔），并小写化。
    std::size_t sep = std::string::npos;
    for (std::size_t i = 0; i < locale.size(); ++i) {
        if (locale[i] == '-' || locale[i] == '_') {
            sep = i;
            break;
        }
    }
    std::string lang = (sep == std::string::npos) ? locale : locale.substr(0, sep);
    return to_lower_ascii(lang);
}

namespace {

bool is_no_plural_language(const std::string& lang) {
    // CLDR "other"-only 语言（无复数形式区分）——本简化版只覆盖中文族 + 东南亚主要语种，
    // 其他语言（俄/阿/波等多类复数）一律按英文 "1/other" 占位；正式版接 ICU 后改用
    // 完整 CLDR Plural Rules。
    //   见 https://unicode-org.github.io/cldr-staging/charts/latest/supplemental/language_plural_rules.html
    static const std::vector<std::string> no_plural = {
        "zh", "ja", "ko", "vi", "th", "id", "ms",
    };
    return std::find(no_plural.begin(), no_plural.end(), lang) != no_plural.end();
}

}  // namespace

std::size_t plural_form_index(const std::string& locale, int64_t count) {
    const std::string lang = locale_language(locale);
    if (is_no_plural_language(lang)) {
        return 0;  // 无复数：恒首形式
    }
    // 英文族及占位：n == 1 取 one（索引 0），其余取 other（索引 1）。
    // 注意 count 可能为负（理论上不应出现）；保守当作 other 处理。
    return (count == 1) ? 0 : 1;
}

// ===========================================================================
// TranslationServer
// ===========================================================================

TranslationServer::TranslationServer() {
    // 默认 locale "en"——无 en 表时 tr 直接退回 source（验收点：缺失退回英文）。
    // 构造期不 emit locale_changed：首批控件尚未 connect，emit 也是空跑（同 ThemeManager）。
}

TranslationServer& TranslationServer::get() {
    // C++11 函数内 static：线程安全初始化（Meyers singleton）。
    static TranslationServer instance;
    return instance;
}

void TranslationServer::set_locale(const std::string& locale) {
    current_locale_ = locale;
    // 广播：控件在槽里 queue_redraw 刷新所有 tr() 文本——免重启切换。
    locale_changed_.emit();
}

bool TranslationServer::load_translation(const std::string& locale,
                                         TranslationTable table) {
    bool existed = tables_.find(locale) != tables_.end();
    tables_[locale] = std::move(table);
    return existed;
}

bool TranslationServer::unload_translation(const std::string& locale) {
    auto it = tables_.find(locale);
    if (it == tables_.end()) return false;
    tables_.erase(it);
    return true;
}

const TranslationTable* TranslationServer::get_table(const std::string& locale) const {
    auto it = tables_.find(locale);
    return (it == tables_.end()) ? nullptr : &it->second;
}

std::string TranslationServer::tr(const std::string& source) const {
    // 缺失层层退回 source（绝不返回 key、绝不返回空串）：
    //   1) 当前 locale 无表 → source
    //   2) 表中无 (context="", source) 条目 → source
    //   3) 条目 translation 为空 → source
    //   4) 否则 → translation（fuzzy 不影响运行时返回，仅作 CI 覆盖率标记）
    if (const TranslationTable* table = get_table(current_locale_)) {
        if (const TranslationEntry* e = table->find("", source)) {
            if (!e->translation.empty()) {
                return e->translation;
            }
        }
    }
    return source;
}

std::string TranslationServer::tr_ctxt(const std::string& context,
                                        const std::string& source) const {
    // 与 tr 同构，仅多一个 msgctxt 精确匹配维度。
    if (const TranslationTable* table = get_table(current_locale_)) {
        if (const TranslationEntry* e = table->find(context, source)) {
            if (!e->translation.empty()) {
                return e->translation;
            }
        }
    }
    return source;
}

std::string TranslationServer::tr_n(const std::string& singular,
                                    const std::string& plural,
                                    int64_t count) const {
    // 1) 表中查 (context="", singular) 的 plurals 形式
    if (const TranslationTable* table = get_table(current_locale_)) {
        if (const TranslationEntry* e = table->find("", singular)) {
            if (!e->plurals.empty()) {
                // 按 CLDR 简化规则选下标
                std::size_t idx = plural_form_index(current_locale_, count);
                if (idx < e->plurals.size() && !e->plurals[idx].empty()) {
                    return e->plurals[idx];
                }
                // 下标越界或空：退回首可用形式
                for (const auto& form : e->plurals) {
                    if (!form.empty()) return form;
                }
                // 全空：落空退回源串（下面统一处理）
            }
        }
    }

    // 2) 无表 / 无条目 / 全空：按 locale 规则在源串 singular/plural 间选
    std::size_t idx = plural_form_index(current_locale_, count);
    return (idx == 0) ? singular : plural;
}

// ===========================================================================
// 全局便捷函数（业务代码主入口）
// ===========================================================================
//
// 直接转发到 TranslationServer::get() 单例。const char* 重载匹配字面量调用约定；
// std::string 重载避免临时构造（虽小，tr() 是热路径）。

std::string tr(const char* source) {
    if (source == nullptr) return std::string{};
    return TranslationServer::get().tr(std::string(source));
}

std::string tr(const std::string& source) {
    return TranslationServer::get().tr(source);
}

std::string tr_ctxt(const char* context, const char* source) {
    if (source == nullptr) return std::string{};
    std::string ctx = (context == nullptr) ? std::string{} : std::string(context);
    return TranslationServer::get().tr_ctxt(ctx, std::string(source));
}

std::string tr_ctxt(const std::string& context, const std::string& source) {
    return TranslationServer::get().tr_ctxt(context, source);
}

std::string tr_n(const char* singular, const char* plural, int64_t count) {
    if (singular == nullptr) return std::string{};
    std::string p = (plural == nullptr) ? std::string(singular) : std::string(plural);
    return TranslationServer::get().tr_n(std::string(singular), p, count);
}

std::string tr_n(const std::string& singular,
                 const std::string& plural,
                 int64_t count) {
    return TranslationServer::get().tr_n(singular, plural, count);
}

}  // namespace eda
