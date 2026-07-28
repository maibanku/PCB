// 翻译系统核心（P4 Task 8 / 04-08-01）。
//
// 业务代码主入口（贯穿所有 GUI 控件、Inspector、命令面板、DRC 结果面板……）：
//   - eda::tr(source)              —— 单数查表：当前 locale 表里有则返回译法，否则退回
//                                     source 本身（**绝不返回 key、绝不返回空**）。
//   - eda::tr_ctxt(context, source)—— msgctxt 上下文消歧：同一英文 source 在不同 UI 语境
//                                     （如库编辑器 vs 报告列名）译法不同时使用。
//   - eda::tr_n(singular, plural, n)—— CLDR 简化复数分发（中文无复数 / 英文 1-other /
//                                      其他 locale 占位，正式版接 ICU MessageFormat）。
//
// source 即 SSOT 锚点（[[00-总览/04-术语表]] §四"英文 source"列，全小写）：
//   - 业务代码用英文 source 作 key；
//   - 译法由各 locale 的 .po 资源提供（由 PoLoader 解析为 TranslationTable）；
//   - 固有名词（IPC-2581/Gerber/courtyard/BOM/DRC/Via/Pad 等，§三清单）**不进 tr()
//     池**——代码直接字面量；本模块提供 is_inherent_noun() 供 CI 抽取工具校验。
//
// TranslationServer —— 进程级单例（Meyers singleton，与 ThemeManager 同构）：
//   - 持 locale -> TranslationTable 注册表；
//   - set_locale 切换后广播 locale_changed 信号——控件在槽里 queue_redraw 重渲染，
//     实现"切换语言免重启"（验收点：i18n 运行时切换免重启）。
//
// 设计参考：Godot TranslationServer + Object::tr()； gettext libintl 运行时语义。
// 本实现**不依赖 gettext 库**——.po 解析由 scene/gui/i18n/po_loader 手写最小实现。

#pragma once

#include "core/object/signal.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eda {

// ---------------------------------------------------------------------------
// TranslationEntry —— 单条翻译记录（对齐 gettext .po 一个条目）。
// ---------------------------------------------------------------------------
//
// 字段映射：
//   context    <- msgctxt（可选；空字符串表示"无上下文"）
//   source     <- msgid（英文 source；SSOT 锚点）
//   translation<- msgstr（单数译法；复数条目此处留空）
//   plurals    <- msgstr[N]（CLDR 复数形式译法；单数条目此处为空）
//                 plurals[0] 通常对应 CLDR "one"，plurals[1] 对应 "other"（英文）；
//                 中文仅 plurals[0]（恒使用 one/other 合并的单形式）。
//   fuzzy      <- #, fuzzy 模糊标记（译法待复审；运行时仍返回，CI 覆盖率统计单算）
//   comments   <- #. 开发者注释（译者上下文，如 "// @i18n-context: 出现在 DRC 报告中"）

struct TranslationEntry {
    std::string context;
    std::string source;
    std::string translation;
    std::vector<std::string> plurals;
    bool fuzzy = false;
    std::vector<std::string> comments;
};

// ---------------------------------------------------------------------------
// TranslationTable —— 单 locale 的翻译表（值语义，可拷贝/移动）。
// ---------------------------------------------------------------------------
//
// 内部布局：线性 entries_ 向量（保留 .po 条目顺序，便于抽取工具按序遍历）+
// (context, source) -> index 哈希索引（O(1) 查询）。
//
// key 拼接：context + "\x01" + source（\x01 作分隔符，避免 (ctx="a",src="bc") 与
// (ctx="ab",src="c") 这类合法输入产生同 key——source/context 一般不含控制字符）。

class TranslationTable {
public:
    TranslationTable() = default;
    ~TranslationTable() = default;
    TranslationTable(const TranslationTable&) = default;
    TranslationTable& operator=(const TranslationTable&) = default;
    TranslationTable(TranslationTable&&) noexcept = default;
    TranslationTable& operator=(TranslationTable&&) noexcept = default;

    // 添加一条翻译。同 (context, source) 已存在则覆盖（保留新条目的顺序位置不前移）。
    void add(TranslationEntry entry);

    // 精确查询（context 空 + source 命中 = 无上下文条目）。未命中返回 nullptr。
    const TranslationEntry* find(const std::string& context,
                                 const std::string& source) const;

    std::size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

    // 只读视图（覆盖率工具、抽取工具消费）。
    const std::vector<TranslationEntry>& entries() const { return entries_; }

    // 构建 (context, source) 的复合 key（公开供工具复用）。
    static std::string make_key(const std::string& context,
                                const std::string& source);

private:
    std::vector<TranslationEntry>                  entries_;
    std::unordered_map<std::string, std::size_t>   index_;
};

// ---------------------------------------------------------------------------
// 固有名词（不进 tr() 池）。
// ---------------------------------------------------------------------------
//
// 来自 [[00-总览/04-术语表]] §三清单：IPC-2581 / Gerber / courtyard / BOM / DRC /
// Via / Pad / ODB++ / DFM / DFT / DFA / SMD / NSMD / BGA / QFP / QFN / SOIC / SOP ……
//
// 这些是 EDA 行业专有术语或标准代号，**所有 locale 都不翻译**，代码直接用字面量。
// 即便误传入 tr()，由于表里不会有对应条目，缺失退回 source 也等于直通——
// is_inherent_noun() 主要给 CI 抽取工具用（阻断把这些词包进 tr() 的 PR）。
//
// 匹配大小写不敏感（"Gerber"/"gerber"/"GERBER" 均命中）。
bool is_inherent_noun(const std::string& source);

// 固有名词清单（只读视图，供抽取工具枚举）。
const std::vector<std::string>& inherent_nouns();

// ---------------------------------------------------------------------------
// CLDR 复数规则（简化版）。
// ---------------------------------------------------------------------------
//
// 返回 count 在 locale 下应使用的复数形式索引（即 plurals[] 的下标）：
//   - 中文族 + 东南亚（zh/ja/ko/vi/th/id/ms）：无复数，恒 0
//   - 英文族及默认（en/de/fr/es/it/nl/pt/ru/pl/...）：n == 1 ? 0 : 1
//
// 简化边界：俄/阿/波等多类复数语言暂按"1/other"占位；正式版接入 ICU
// MessageFormat 后用完整 CLDR plural rules（one/few/many/other…）。
//
// locale 取主语言码：截断 "-" 或 "_" 之前部分（"zh-CN" -> "zh" / "en_US" -> "en"）。
std::size_t plural_form_index(const std::string& locale, int64_t count);

// 从 locale 标签提取主语言码（小写）。如 "zh-CN" -> "zh" / "en_US" -> "en"。
std::string locale_language(const std::string& locale);

// ---------------------------------------------------------------------------
// TranslationServer —— 进程级单例（locale 管理 + 翻译查询 + 切换广播）。
// ---------------------------------------------------------------------------

class TranslationServer {
public:
    static TranslationServer& get();

    TranslationServer(const TranslationServer&) = delete;
    TranslationServer& operator=(const TranslationServer&) = delete;
    TranslationServer(TranslationServer&&) = delete;
    TranslationServer& operator=(TranslationServer&&) = delete;

    // —— locale 管理 ——
    //
    // 切换当前 locale 并广播 locale_changed（即便新旧 locale 相同也广播，调用方自行
    // 去抖；典型用法：用户在偏好里切换后，所有控件 queue_redraw 重渲染，**免重启**）。
    void            set_locale(const std::string& locale);
    const std::string& get_locale() const { return current_locale_; }

    // —— 翻译资源注册 ——
    //
    // 注册某 locale 的翻译表（覆盖同 locale 已有表）。返回之前是否有该 locale。
    bool load_translation(const std::string& locale, TranslationTable table);
    // 移除某 locale 的翻译表（测试/卸载语言包用）。不存在则 no-op 返回 false。
    bool unload_translation(const std::string& locale);
    // 查询某 locale 的翻译表（不存在返回 nullptr）。
    const TranslationTable* get_table(const std::string& locale) const;

    // —— 翻译查询（不抛异常；任何缺失都退回 source）——
    //
    // 单数：当前 locale 表有 (context="", source) 且 translation 非空 → 返回译法；
    //       否则退回 source（**绝不返回 key、绝不返回空串**）。
    std::string tr(const std::string& source) const;

    // 上下文消歧（msgctxt）。无译法退回 source。
    std::string tr_ctxt(const std::string& context,
                        const std::string& source) const;

    // 复数（CLDR 简化分发）：
    //   1) 当前 locale 表有该 source 的 plurals[] → 用 plural_form_index 选下标；
    //   2) 下标越界或对应形式空 → 退回首可用形式，再退回源串 singular/plural；
    //   3) 无表/无条目 → 按 locale 规则在源串 singular/plural 间选。
    std::string tr_n(const std::string& singular,
                     const std::string& plural,
                     int64_t count) const;

    // 切换信号：set_locale 后广播。控件典型在槽里 queue_redraw 刷新所有 tr() 文本。
    //  srv.locale_changed().connect([this]() { queue_redraw(); });
    Signal<>& locale_changed() { return locale_changed_; }

private:
    TranslationServer();

    std::unordered_map<std::string, TranslationTable> tables_;
    std::string current_locale_ = "en";  // 默认英文（无表时 tr 直接退回 source）
    Signal<>    locale_changed_;
};

// ---------------------------------------------------------------------------
// 全局便捷函数（业务代码主入口）。
// ---------------------------------------------------------------------------
//
// 直接转发到 TranslationServer::get() 的当前 locale。参数 const char* 以匹配
// `tr("footprint")` 这种字面量调用约定（[[00-总览/04-术语表]] §四 全小写 source）。
//
// nullptr 安全：传入 nullptr 视作空串（退回空串，不崩溃）。

std::string tr(const char* source);
std::string tr(const std::string& source);  // 重载：避免临时 std::string 构造

std::string tr_ctxt(const char* context, const char* source);
std::string tr_ctxt(const std::string& context, const std::string& source);

std::string tr_n(const char* singular, const char* plural, int64_t count);
std::string tr_n(const std::string& singular,
                 const std::string& plural,
                 int64_t count);

}  // namespace eda
